/* emit_x86.c - x86-64 code generator.
 *
 * A straightforward "everything lives in the stack frame" code generator:
 * every IR slot and temp gets an 8-byte location below %rbp, and each IR
 * instruction loads its operands into scratch registers, computes, and
 * stores the result back. There is no register allocation, which keeps the
 * generator small and obviously correct; the IR-level optimiser removes the
 * bulk of the redundant loads before we get here.
 *
 * Supports the System V (Linux) and Microsoft x64 (Windows) calling
 * conventions, emits DWARF line-number directives (.file/.loc) so gdb can
 * step through MiniCC source, and CFI unwind directives on ELF targets. */
#include "emit_x86.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

enum { RAX, RCX, RDX, RDI, RSI, R8, R9, R10, R11, NREG };

static const char *gpr64[NREG] = { "%rax", "%rcx", "%rdx", "%rdi", "%rsi", "%r8", "%r9", "%r10", "%r11" };
static const char *gpr32[NREG] = { "%eax", "%ecx", "%edx", "%edi", "%esi", "%r8d", "%r9d", "%r10d", "%r11d" };
static const char *gpr8[NREG]  = { "%al", "%cl", "%dl", "%dil", "%sil", "%r8b", "%r9b", "%r10b", "%r11b" };

static const int sysv_int_args[6] = { RDI, RSI, RDX, RCX, R8, R9 };
static const int win_int_args[4]  = { RCX, RDX, R8, R9 };

typedef struct {
    FILE *out;
    X86Abi abi;
    int debug;
    const IrFunc *f;
    int nslots;
    int last_line;
    Vec *double_pool;       /* uint64_t* bit patterns */
    /* Peephole: the temp whose value is currently held in rax / xmm0 (or -1).
     * store_temp sets it; o() clears it whenever an emitted instruction
     * writes the register; a following load of the same temp is elided. */
    int cache_gpr_temp;
    int cache_xmm_temp;
} X;

X86Abi x86_host_abi(void) {
#ifdef _WIN32
    return ABI_WIN64;
#else
    return ABI_SYSV;
#endif
}

static void o(X *x, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(x->out, "    %s\n", buf);

    /* track clobbers of the cached result registers (AT&T: destination last) */
    if (!strncmp(buf, "call", 4) || !strncmp(buf, "idivl", 5) || !strncmp(buf, "ud2", 3)) {
        x->cache_gpr_temp = x->cache_xmm_temp = -1;
        return;
    }
    const char *comma = strrchr(buf, ',');
    const char *dst = comma ? comma + 1 : strchr(buf, ' ');
    if (!dst) return;
    if (strstr(dst, "%eax") || strstr(dst, "%rax") || strstr(dst, "%al")) x->cache_gpr_temp = -1;
    if (strstr(dst, "%xmm0")) x->cache_xmm_temp = -1;
}

static int slot_off(X *x, int slot) { return -8 * (slot + 1); }
static int temp_off(X *x, int temp) { return -8 * (x->nslots + temp + 1); }

static int is_elf(X *x) { return x->abi == ABI_SYSV; }

static int double_const(X *x, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    for (int i = 0; i < x->double_pool->len; i++)
        if (*(uint64_t *)x->double_pool->items[i] == bits) return i;
    uint64_t *p = xmalloc(sizeof *p);
    *p = bits;
    vec_push(x->double_pool, p);
    return x->double_pool->len - 1;
}

/* ---------------------------------------------------- operand moves */

/* Load an integer-class operand (i1 / i32 / ptr) into a GPR. */
static void load_gpr(X *x, Operand op, int reg) {
    switch (op.kind) {
    case OPK_TEMP:
        if (reg == RAX && x->cache_gpr_temp == op.temp) break;   /* already in rax */
        if (op.type == IR_PTR) o(x, "movq %d(%%rbp), %s", temp_off(x, op.temp), gpr64[reg]);
        else o(x, "movl %d(%%rbp), %s", temp_off(x, op.temp), gpr32[reg]);
        break;
    case OPK_IMM:
        if (op.imm == 0) o(x, "xorl %s, %s", gpr32[reg], gpr32[reg]);
        else o(x, "movl $%lld, %s", op.imm, gpr32[reg]);
        break;
    case OPK_NULL:
        o(x, "xorl %s, %s", gpr32[reg], gpr32[reg]);
        break;
    case OPK_STR:
        o(x, "leaq .Lstr%d(%%rip), %s", op.str, gpr64[reg]);
        break;
    case OPK_SYM:
        o(x, "leaq %s(%%rip), %s", op.sym, gpr64[reg]);
        break;
    default:
        diag_fatal("x86: cannot load operand into a general register");
    }
}

static void load_xmm(X *x, Operand op, int xmm) {
    switch (op.kind) {
    case OPK_TEMP:
        if (xmm == 0 && x->cache_xmm_temp == op.temp) break;     /* already in xmm0 */
        o(x, "movsd %d(%%rbp), %%xmm%d", temp_off(x, op.temp), xmm);
        break;
    case OPK_FIMM:
        if (op.fimm == 0.0 && !signbit(op.fimm)) o(x, "xorpd %%xmm%d, %%xmm%d", xmm, xmm);
        else o(x, "movsd .LC%d(%%rip), %%xmm%d", double_const(x, op.fimm), xmm);
        break;
    default:
        diag_fatal("x86: cannot load operand into an SSE register");
    }
}

static void load_operand(X *x, Operand op, int reg_or_xmm) {
    if (op.type == IR_F64) load_xmm(x, op, reg_or_xmm);
    else load_gpr(x, op, reg_or_xmm);
}

/* Store the value in rax / eax / xmm0 into a temp. */
static void store_temp(X *x, int temp, IrType t) {
    switch (t) {
    case IR_F64: o(x, "movsd %%xmm0, %d(%%rbp)", temp_off(x, temp)); x->cache_xmm_temp = temp; break;
    case IR_PTR: o(x, "movq %%rax, %d(%%rbp)", temp_off(x, temp)); x->cache_gpr_temp = temp; break;
    default: o(x, "movl %%eax, %d(%%rbp)", temp_off(x, temp)); x->cache_gpr_temp = temp; break;
    }
}

/* Store a value held in register `reg` (or xmm0 for doubles) to memory. */
static void store_mem(X *x, IrType t, int reg, const char *mem) {
    switch (t) {
    case IR_I1: o(x, "movb %s, %s", gpr8[reg], mem); break;
    case IR_I32: o(x, "movl %s, %s", gpr32[reg], mem); break;
    case IR_F64: o(x, "movsd %%xmm0, %s", mem); break;
    default: o(x, "movq %s, %s", gpr64[reg], mem); break;
    }
}

/* Load from memory into rax / eax / xmm0 according to the value type. */
static void load_mem(X *x, IrType t, const char *mem) {
    switch (t) {
    case IR_I1: o(x, "movzbl %s, %%eax", mem); break;
    case IR_I32: o(x, "movl %s, %%eax", mem); break;
    case IR_F64: o(x, "movsd %s, %%xmm0", mem); break;
    default: o(x, "movq %s, %%rax", mem); break;
    }
}

/* --------------------------------------------------------- calls */

typedef struct {
    int in_reg;         /* 1 if passed in a register */
    int reg;            /* GPR index or xmm number */
    int stack_index;    /* position among stack-passed arguments */
} ArgLoc;

static void classify_args(X *x, const IrType *types, int n, ArgLoc *locs, int *nstack) {
    int nint = 0, nxmm = 0, ns = 0;
    for (int i = 0; i < n; i++) {
        ArgLoc *l = &locs[i];
        memset(l, 0, sizeof *l);
        if (x->abi == ABI_WIN64) {
            if (i < 4) { l->in_reg = 1; l->reg = types[i] == IR_F64 ? i : win_int_args[i]; }
            else l->stack_index = ns++;
        } else if (types[i] == IR_F64) {
            if (nxmm < 8) { l->in_reg = 1; l->reg = nxmm++; }
            else l->stack_index = ns++;
        } else {
            if (nint < 6) { l->in_reg = 1; l->reg = sysv_int_args[nint++]; }
            else l->stack_index = ns++;
        }
    }
    *nstack = ns;
}

static void emit_call(X *x, const IrInstr *i) {
    int n = i->nargs;
    IrType *types = xcalloc((size_t)(n ? n : 1), sizeof(IrType));
    for (int k = 0; k < n; k++) types[k] = i->args[k].type;
    ArgLoc *locs = xcalloc((size_t)(n ? n : 1), sizeof(ArgLoc));
    int nstack;
    classify_args(x, types, n, locs, &nstack);

    int shadow = x->abi == ABI_WIN64 ? 32 : 0;
    int bytes = shadow + 8 * nstack;
    bytes = (bytes + 15) / 16 * 16;
    if (bytes) o(x, "subq $%d, %%rsp", bytes);

    /* stack arguments first, staged through rax / xmm0 */
    for (int k = 0; k < n; k++) {
        if (locs[k].in_reg) continue;
        char mem[32];
        snprintf(mem, sizeof mem, "%d(%%rsp)", shadow + 8 * locs[k].stack_index);
        if (types[k] == IR_F64) { load_xmm(x, i->args[k], 0); o(x, "movsd %%xmm0, %s", mem); }
        else { load_gpr(x, i->args[k], RAX); o(x, "movq %%rax, %s", mem); }
    }
    /* then register arguments (sources are memory or immediates: no clobbering) */
    for (int k = 0; k < n; k++) {
        if (!locs[k].in_reg) continue;
        if (types[k] == IR_F64) load_xmm(x, i->args[k], locs[k].reg);
        else load_gpr(x, i->args[k], locs[k].reg);
    }
    if (i->op == IR_CALLIND) {
        load_gpr(x, i->a, R11);
        o(x, "call *%%r11");
    } else {
        o(x, "call %s", i->name);
    }
    if (bytes) o(x, "addq $%d, %%rsp", bytes);
    if (i->dst >= 0) store_temp(x, i->dst, i->ret_type);
}

/* ---------------------------------------------------- instructions */

static const char *setcc_int(IrBinOp op) {
    switch (op) {
    case BOP_LT: return "setl";
    case BOP_LE: return "setle";
    case BOP_GT: return "setg";
    case BOP_GE: return "setge";
    case BOP_EQ: return "sete";
    case BOP_NE: return "setne";
    case BOP_ULT: return "setb";
    default: return NULL;
    }
}

static void emit_bin(X *x, const IrInstr *i) {
    IrType t = i->a.type;
    if (t == IR_F64) {
        load_xmm(x, i->a, 0);
        load_xmm(x, i->b, 1);
        switch (i->bop) {
        case BOP_ADD: o(x, "addsd %%xmm1, %%xmm0"); break;
        case BOP_SUB: o(x, "subsd %%xmm1, %%xmm0"); break;
        case BOP_MUL: o(x, "mulsd %%xmm1, %%xmm0"); break;
        case BOP_DIV: o(x, "divsd %%xmm1, %%xmm0"); break;
        default:
            o(x, "ucomisd %%xmm1, %%xmm0");
            switch (i->bop) {
            case BOP_LT: o(x, "setb %%al"); break;
            case BOP_LE: o(x, "setbe %%al"); break;
            case BOP_GT: o(x, "seta %%al"); break;
            case BOP_GE: o(x, "setae %%al"); break;
            case BOP_EQ: o(x, "sete %%al"); o(x, "setnp %%cl"); o(x, "andb %%cl, %%al"); break;
            case BOP_NE: o(x, "setne %%al"); o(x, "setp %%cl"); o(x, "orb %%cl, %%al"); break;
            default: diag_fatal("x86: bad double operator");
            }
            o(x, "movzbl %%al, %%eax");
            break;
        }
        store_temp(x, i->dst, i->type);
        return;
    }
    load_gpr(x, i->a, RAX);
    load_gpr(x, i->b, RCX);
    const char *cc = setcc_int(i->bop);
    if (cc) {
        if (t == IR_PTR) o(x, "cmpq %%rcx, %%rax");
        else o(x, "cmpl %%ecx, %%eax");
        o(x, "%s %%al", cc);
        o(x, "movzbl %%al, %%eax");
    } else {
        switch (i->bop) {
        case BOP_ADD: o(x, "addl %%ecx, %%eax"); break;
        case BOP_SUB: o(x, "subl %%ecx, %%eax"); break;
        case BOP_MUL: o(x, "imull %%ecx, %%eax"); break;
        case BOP_DIV: o(x, "cltd"); o(x, "idivl %%ecx"); break;
        case BOP_REM: o(x, "cltd"); o(x, "idivl %%ecx"); o(x, "movl %%edx, %%eax"); break;
        case BOP_AND: o(x, "andl %%ecx, %%eax"); break;
        case BOP_OR: o(x, "orl %%ecx, %%eax"); break;
        case BOP_XOR: o(x, "xorl %%ecx, %%eax"); break;
        default: diag_fatal("x86: bad integer operator");
        }
    }
    store_temp(x, i->dst, i->type);
}

static void label(X *x, const IrBlock *b) {
    fprintf(x->out, ".L%s.%d:\n", x->f->name, b->id);
    x->cache_gpr_temp = x->cache_xmm_temp = -1;     /* control-flow merge point */
}

static void emit_instr(X *x, const IrInstr *i, const IrBlock *next) {
    if (x->debug && i->line > 0 && i->line != x->last_line) {
        o(x, ".loc 1 %d 0", i->line);
        x->last_line = i->line;
    }
    char mem[64];
    switch (i->op) {
    case IR_BIN:
        emit_bin(x, i);
        break;
    case IR_UN:
        if (i->a.type == IR_F64) {
            load_xmm(x, i->a, 0);
            o(x, "movq %%xmm0, %%rax");
            o(x, "btcq $63, %%rax");
            o(x, "movq %%rax, %%xmm0");
        } else {
            load_gpr(x, i->a, RAX);
            if (i->uop == UOP_NEG) o(x, "negl %%eax");
            else o(x, "xorl $1, %%eax");
        }
        store_temp(x, i->dst, i->type);
        break;
    case IR_SITOFP:
        load_gpr(x, i->a, RAX);
        o(x, "cvtsi2sdl %%eax, %%xmm0");
        store_temp(x, i->dst, IR_F64);
        break;
    case IR_ZEXT:
        load_gpr(x, i->a, RAX);
        store_temp(x, i->dst, IR_I32);
        break;
    case IR_LOADL:
        snprintf(mem, sizeof mem, "%d(%%rbp)", slot_off(x, i->slot));
        if (i->type == IR_I1) o(x, "movl %s, %%eax", mem);   /* slots hold bools as 32-bit */
        else load_mem(x, i->type, mem);
        store_temp(x, i->dst, i->type);
        break;
    case IR_STOREL:
        snprintf(mem, sizeof mem, "%d(%%rbp)", slot_off(x, i->slot));
        load_operand(x, i->a, RAX);
        store_mem(x, i->type == IR_I1 ? IR_I32 : i->type, RAX, mem);
        break;
    case IR_LOADG:
        snprintf(mem, sizeof mem, "%s(%%rip)", i->name);
        load_mem(x, i->type, mem);
        store_temp(x, i->dst, i->type);
        break;
    case IR_STOREG:
        snprintf(mem, sizeof mem, "%s(%%rip)", i->name);
        load_operand(x, i->a, RAX);
        store_mem(x, i->type, RAX, mem);
        break;
    case IR_LOADF:
        load_gpr(x, i->a, RCX);
        snprintf(mem, sizeof mem, "%d(%%rcx)", i->offset);
        load_mem(x, i->type, mem);
        store_temp(x, i->dst, i->type);
        break;
    case IR_STOREF:
        load_gpr(x, i->a, RCX);
        load_operand(x, i->b, RAX);
        snprintf(mem, sizeof mem, "%d(%%rcx)", i->offset);
        store_mem(x, i->type, RAX, mem);
        break;
    case IR_LOADX:
        load_gpr(x, i->a, RCX);
        load_gpr(x, i->b, RDX);
        o(x, "movslq %%edx, %%rdx");
        snprintf(mem, sizeof mem, "8(%%rcx,%%rdx,%d)", irtype_size(i->type));
        load_mem(x, i->type, mem);
        store_temp(x, i->dst, i->type);
        break;
    case IR_STOREX:
        load_gpr(x, i->a, RCX);
        load_gpr(x, i->b, RDX);
        o(x, "movslq %%edx, %%rdx");
        load_operand(x, i->c, RAX);
        snprintf(mem, sizeof mem, "8(%%rcx,%%rdx,%d)", irtype_size(i->type));
        store_mem(x, i->type, RAX, mem);
        break;
    case IR_CALL:
    case IR_CALLIND:
        emit_call(x, i);
        break;
    case IR_BR:
        if (i->target != next) o(x, "jmp .L%s.%d", x->f->name, i->target->id);
        break;
    case IR_CBR:
        load_gpr(x, i->a, RAX);
        o(x, "testl %%eax, %%eax");
        if (i->target2 == next) {
            o(x, "jne .L%s.%d", x->f->name, i->target->id);
        } else if (i->target == next) {
            o(x, "je .L%s.%d", x->f->name, i->target2->id);
        } else {
            o(x, "jne .L%s.%d", x->f->name, i->target->id);
            o(x, "jmp .L%s.%d", x->f->name, i->target2->id);
        }
        break;
    case IR_RET:
        if (i->a.kind != OPK_NONE) load_operand(x, i->a, RAX);
        o(x, "jmp .L%s.ret", x->f->name);
        break;
    case IR_UNREACHABLE:
        o(x, "ud2");
        break;
    }
}

/* ------------------------------------------------------- functions */

static void emit_func(X *x, const IrFunc *f) {
    x->f = f;
    x->nslots = f->slot_types->len;
    x->last_line = 0;
    x->cache_gpr_temp = x->cache_xmm_temp = -1;
    int frame = 8 * (x->nslots + f->ntemps);
    frame = (frame + 15) / 16 * 16;

    fputs("\n", x->out);
    o(x, ".globl %s", f->name);
    if (is_elf(x)) o(x, ".type %s, @function", f->name);
    fprintf(x->out, "%s:\n", f->name);
    if (x->debug && f->line > 0) o(x, ".loc 1 %d 0", f->line);
    if (is_elf(x)) o(x, ".cfi_startproc");
    o(x, "pushq %%rbp");
    if (is_elf(x)) { o(x, ".cfi_def_cfa_offset 16"); o(x, ".cfi_offset %%rbp, -16"); }
    o(x, "movq %%rsp, %%rbp");
    if (is_elf(x)) o(x, ".cfi_def_cfa_register %%rbp");
    if (frame) o(x, "subq $%d, %%rsp", frame);

    /* spill incoming parameters into their slots */
    IrType *types = xcalloc((size_t)(f->nparams ? f->nparams : 1), sizeof(IrType));
    for (int k = 0; k < f->nparams; k++) types[k] = (IrType)(intptr_t)f->slot_types->items[k];
    ArgLoc *locs = xcalloc((size_t)(f->nparams ? f->nparams : 1), sizeof(ArgLoc));
    int nstack;
    classify_args(x, types, f->nparams, locs, &nstack);
    int incoming = 16 + (x->abi == ABI_WIN64 ? 32 : 0);
    for (int k = 0; k < f->nparams; k++) {
        char mem[32];
        snprintf(mem, sizeof mem, "%d(%%rbp)", slot_off(x, k));
        if (locs[k].in_reg) {
            if (types[k] == IR_F64) o(x, "movsd %%xmm%d, %s", locs[k].reg, mem);
            else o(x, "movq %s, %s", gpr64[locs[k].reg], mem);
        } else {
            o(x, "movq %d(%%rbp), %%rax", incoming + 8 * locs[k].stack_index);
            o(x, "movq %%rax, %s", mem);
        }
    }

    for (int k = 0; k < f->blocks->len; k++) {
        IrBlock *b = f->blocks->items[k];
        IrBlock *next = k + 1 < f->blocks->len ? f->blocks->items[k + 1] : NULL;
        label(x, b);
        VEC_FOREACH(IrInstr *, i, b->instrs) emit_instr(x, i, next);
    }

    fprintf(x->out, ".L%s.ret:\n", f->name);
    o(x, "movq %%rbp, %%rsp");
    o(x, "popq %%rbp");
    if (is_elf(x)) o(x, ".cfi_def_cfa %%rsp, 8");
    o(x, "ret");
    if (is_elf(x)) { o(x, ".cfi_endproc"); o(x, ".size %s, .-%s", f->name, f->name); }
}

/* ------------------------------------------------------------ data */

static void emit_string(X *x, const IrString *s, int idx) {
    fprintf(x->out, ".Lstr%d:\n    .ascii \"", idx);
    for (int i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        if (c == '"' || c == '\\') fprintf(x->out, "\\%c", c);
        else if (c >= 32 && c < 127) fputc(c, x->out);
        else fprintf(x->out, "\\%03o", c);
    }
    fputs("\\000\"\n", x->out);
}

void emit_x86(const IrModule *m, FILE *out, X86Abi abi, int debug_info) {
    X x;
    memset(&x, 0, sizeof x);
    x.out = out;
    x.abi = abi;
    x.debug = debug_info;
    x.double_pool = vec_new();
    x.cache_gpr_temp = x.cache_xmm_temp = -1;

    fprintf(out, "# generated by minicc from %s (%s ABI)\n", m->source_path,
            abi == ABI_WIN64 ? "Microsoft x64" : "System V");
    if (debug_info) fprintf(out, "    .file 1 \"%s\"\n", m->source_path);

    /* globals */
    if (m->globals->len) {
        fputs("    .bss\n", out);
        VEC_FOREACH(IrGlobal *, g, m->globals) {
            o(&x, ".globl %s", g->name);
            o(&x, ".align 8");
            fprintf(out, "%s:\n", g->name);
            o(&x, ".zero 8");
        }
    }
    /* vtables and class tables */
    if (m->data->len) {
        fputs("    .data\n", out);
        VEC_FOREACH(IrData *, d, m->data) {
            o(&x, ".globl %s", d->name);
            o(&x, ".align 8");
            fprintf(out, "%s:%s%s\n", d->name, d->comment ? "   # " : "", d->comment ? d->comment : "");
            VEC_FOREACH(DataWord *, w, d->words) {
                if (w->kind == DW_INT) o(&x, ".quad %lld", w->ival);
                else o(&x, ".quad %s", w->sym);
            }
        }
    }

    fputs("    .text\n", out);
    VEC_FOREACH(IrFunc *, f, m->funcs) emit_func(&x, f);

    /* read-only data: strings and double constants (collected while emitting code) */
    if (m->strings->len || x.double_pool->len) {
        fputs(abi == ABI_WIN64 ? "    .section .rdata,\"dr\"\n" : "    .section .rodata\n", out);
        for (int i = 0; i < m->strings->len; i++) emit_string(&x, m->strings->items[i], i);
        if (x.double_pool->len) o(&x, ".align 8");
        for (int i = 0; i < x.double_pool->len; i++)
            fprintf(out, ".LC%d:\n    .quad 0x%016llx\n", i, (unsigned long long)*(uint64_t *)x.double_pool->items[i]);
    }
    if (abi == ABI_SYSV) fputs("    .section .note.GNU-stack,\"\",@progbits\n", out);
}
