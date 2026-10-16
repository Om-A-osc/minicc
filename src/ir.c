#include "ir.h"

#include <stdint.h>
#include <string.h>

const char *irtype_str(IrType t) {
    switch (t) {
    case IR_VOID: return "void";
    case IR_I1: return "i1";
    case IR_I32: return "i32";
    case IR_F64: return "f64";
    case IR_PTR: return "ptr";
    }
    return "?";
}

int irtype_size(IrType t) {
    switch (t) {
    case IR_VOID: return 0;
    case IR_I1: return 1;
    case IR_I32: return 4;
    case IR_F64: return 8;
    case IR_PTR: return 8;
    }
    return 8;
}

/* ------------------------------------------------------------ operands */

Operand op_none(void) {
    Operand o;
    memset(&o, 0, sizeof o);
    o.kind = OPK_NONE;
    o.type = IR_VOID;
    return o;
}

Operand op_temp(int t, IrType ty) {
    Operand o = op_none();
    o.kind = OPK_TEMP;
    o.temp = t;
    o.type = ty;
    return o;
}

Operand op_imm(long long v, IrType ty) {
    Operand o = op_none();
    o.kind = OPK_IMM;
    o.imm = ty == IR_I1 ? (v != 0) : (int)v;
    o.type = ty;
    return o;
}

Operand op_fimm(double v) {
    Operand o = op_none();
    o.kind = OPK_FIMM;
    o.fimm = v;
    o.type = IR_F64;
    return o;
}

Operand op_null(void) {
    Operand o = op_none();
    o.kind = OPK_NULL;
    o.type = IR_PTR;
    return o;
}

Operand op_str(int idx) {
    Operand o = op_none();
    o.kind = OPK_STR;
    o.str = idx;
    o.type = IR_PTR;
    return o;
}

Operand op_sym(const char *name) {
    Operand o = op_none();
    o.kind = OPK_SYM;
    o.sym = name;
    o.type = IR_PTR;
    return o;
}

int op_is_const(Operand o) {
    return o.kind == OPK_IMM || o.kind == OPK_FIMM || o.kind == OPK_NULL;
}

int op_equal(Operand a, Operand b) {
    if (a.kind != b.kind || a.type != b.type) return 0;
    switch (a.kind) {
    case OPK_NONE: case OPK_NULL: return 1;
    case OPK_TEMP: return a.temp == b.temp;
    case OPK_IMM: return a.imm == b.imm;
    case OPK_FIMM: return memcmp(&a.fimm, &b.fimm, sizeof(double)) == 0;
    case OPK_STR: return a.str == b.str;
    case OPK_SYM: return strcmp(a.sym, b.sym) == 0;
    }
    return 0;
}

/* ------------------------------------------------------------- module */

IrModule *ir_module_new(const char *source_path) {
    IrModule *m = xcalloc(1, sizeof(IrModule));
    m->source_path = source_path;
    m->funcs = vec_new();
    m->globals = vec_new();
    m->strings = vec_new();
    m->data = vec_new();
    m->string_index = strmap_new();
    m->used_runtime = strmap_new();
    return m;
}

int ir_module_string(IrModule *m, const char *s, int len) {
    void *v = strmap_get(m->string_index, s);
    if (v) return (int)(intptr_t)v - 1;
    IrString *str = xcalloc(1, sizeof(IrString));
    str->data = s;
    str->len = len;
    vec_push(m->strings, str);
    int idx = m->strings->len - 1;
    strmap_put(m->string_index, s, (void *)(intptr_t)(idx + 1));
    return idx;
}

void ir_module_global(IrModule *m, const char *name, IrType t) {
    IrGlobal *g = xcalloc(1, sizeof(IrGlobal));
    g->name = name;
    g->type = t;
    vec_push(m->globals, g);
}

IrData *ir_module_data(IrModule *m, const char *name, const char *comment) {
    IrData *d = xcalloc(1, sizeof(IrData));
    d->name = name;
    d->comment = comment;
    d->words = vec_new();
    vec_push(m->data, d);
    return d;
}

void ir_data_int(IrData *d, long long v) {
    DataWord *w = xcalloc(1, sizeof(DataWord));
    w->kind = DW_INT;
    w->ival = v;
    vec_push(d->words, w);
}

void ir_data_sym(IrData *d, const char *sym) {
    DataWord *w = xcalloc(1, sizeof(DataWord));
    w->kind = DW_SYM;
    w->sym = sym;
    vec_push(d->words, w);
}

/* ----------------------------------------------------------- function */

IrFunc *ir_func_new(IrModule *m, const char *name, IrType ret, int line) {
    IrFunc *f = xcalloc(1, sizeof(IrFunc));
    f->name = name;
    f->ret = ret;
    f->slot_types = vec_new();
    f->slot_names = vec_new();
    f->blocks = vec_new();
    f->line = line;
    vec_push(m->funcs, f);
    return f;
}

int ir_func_slot(IrFunc *f, IrType t, const char *name) {
    vec_push(f->slot_types, (void *)(intptr_t)t);
    vec_push(f->slot_names, (void *)name);
    return f->slot_types->len - 1;
}

int ir_func_temp(IrFunc *f) {
    return f->ntemps++;
}

IrBlock *ir_block_new(IrFunc *f) {
    IrBlock *b = xcalloc(1, sizeof(IrBlock));
    b->id = f->blocks->len;
    b->instrs = vec_new();
    b->preds = vec_new();
    vec_push(f->blocks, b);
    return b;
}

static int is_terminator(IrOp op) {
    return op == IR_BR || op == IR_CBR || op == IR_RET || op == IR_UNREACHABLE;
}

int ir_block_terminated(const IrBlock *b) {
    if (b->instrs->len == 0) return 0;
    IrInstr *last = b->instrs->items[b->instrs->len - 1];
    return is_terminator(last->op);
}

IrInstr *ir_block_terminator(const IrBlock *b) {
    if (!ir_block_terminated(b)) return NULL;
    return b->instrs->items[b->instrs->len - 1];
}

IrInstr *ir_instr_new(IrOp op, int line) {
    IrInstr *i = xcalloc(1, sizeof(IrInstr));
    i->op = op;
    i->line = line;
    i->dst = -1;
    i->a = op_none();
    i->b = op_none();
    i->c = op_none();
    return i;
}

void ir_block_append(IrBlock *b, IrInstr *i) {
    vec_push(b->instrs, i);
}

void ir_compute_preds(IrFunc *f) {
    VEC_FOREACH(IrBlock *, b, f->blocks) b->preds->len = 0;
    VEC_FOREACH(IrBlock *, b, f->blocks) {
        IrInstr *t = ir_block_terminator(b);
        if (!t) continue;
        if (t->target) vec_push(t->target->preds, b);
        if (t->target2 && t->target2 != t->target) vec_push(t->target2->preds, b);
    }
}

/* Drop unreachable blocks and lay the rest out in reverse postorder, so
 * that (back edges aside) every block comes after its predecessors and the
 * fall-through path of a conditional branch is the common one. */
static void postorder(IrBlock *b, char *seen, Vec *out) {
    seen[b->id] = 1;
    IrInstr *t = ir_block_terminator(b);
    if (t) {
        if (t->target2 && !seen[t->target2->id]) postorder(t->target2, seen, out);
        if (t->target && !seen[t->target->id]) postorder(t->target, seen, out);
    }
    vec_push(out, b);
}

void ir_func_cleanup(IrFunc *f) {
    int n = f->blocks->len;
    if (n == 0) return;
    /* passes may have removed blocks without renumbering: refresh the ids
     * that the DFS below uses as indices */
    for (int i = 0; i < n; i++) ((IrBlock *)f->blocks->items[i])->id = i;
    char *seen = xcalloc((size_t)n, 1);
    Vec *po = vec_new();
    postorder(f->blocks->items[0], seen, po);
    Vec *rpo = vec_new();
    for (int i = po->len - 1; i >= 0; i--) vec_push(rpo, po->items[i]);
    f->blocks = rpo;
    for (int i = 0; i < f->blocks->len; i++) ((IrBlock *)f->blocks->items[i])->id = i;
    ir_compute_preds(f);
}

/* ------------------------------------------------------------ runtime */

const RuntimeFn runtime_fns[] = {
    { "__mcc_alloc",         IR_PTR,  1, { IR_I32 } },
    { "__mcc_newarray",      IR_PTR,  2, { IR_I32, IR_I32 } },
    { "__mcc_lookup",        IR_PTR,  2, { IR_PTR, IR_I32 } },
    { "__mcc_print_int",     IR_VOID, 1, { IR_I32 } },
    { "__mcc_print_bool",    IR_VOID, 1, { IR_I32 } },
    { "__mcc_print_double",  IR_VOID, 1, { IR_F64 } },
    { "__mcc_print_string",  IR_VOID, 1, { IR_PTR } },
    { "__mcc_read_int",      IR_I32,  0, { IR_VOID } },
    { "__mcc_read_line",     IR_PTR,  0, { IR_VOID } },
    { "__mcc_strcat",        IR_PTR,  2, { IR_PTR, IR_PTR } },
    { "__mcc_streq",         IR_I32,  2, { IR_PTR, IR_PTR } },
    { "__mcc_null_fail",     IR_VOID, 1, { IR_I32 } },
    { "__mcc_bounds_fail",   IR_VOID, 3, { IR_I32, IR_I32, IR_I32 } },
    { "__mcc_div_zero_fail", IR_VOID, 1, { IR_I32 } },
};
const int runtime_fn_count = (int)(sizeof(runtime_fns) / sizeof(runtime_fns[0]));

const RuntimeFn *runtime_fn_lookup(const char *name) {
    for (int i = 0; i < runtime_fn_count; i++)
        if (strcmp(runtime_fns[i].name, name) == 0) return &runtime_fns[i];
    return NULL;
}

/* --------------------------------------------------------------- dump */

static void dump_operand(Operand o, FILE *out) {
    switch (o.kind) {
    case OPK_NONE: fputs("<none>", out); break;
    case OPK_TEMP: fprintf(out, "%%t%d", o.temp); break;
    case OPK_IMM: fprintf(out, "%lld", o.imm); break;
    case OPK_FIMM: fprintf(out, "%g", o.fimm); break;
    case OPK_NULL: fputs("null", out); break;
    case OPK_STR: fprintf(out, "@.str%d", o.str); break;
    case OPK_SYM: fprintf(out, "@%s", o.sym); break;
    }
}

static const char *bop_str(IrBinOp op) {
    static const char *names[] = {
        "add", "sub", "mul", "div", "rem", "lt", "le", "gt", "ge", "eq", "ne", "ult", "and", "or", "xor"
    };
    return names[op];
}

static void dump_args(const IrInstr *i, FILE *out) {
    fputc('(', out);
    for (int k = 0; k < i->nargs; k++) {
        if (k) fputs(", ", out);
        fprintf(out, "%s ", irtype_str(i->args[k].type));
        dump_operand(i->args[k], out);
    }
    fputc(')', out);
}

static void dump_instr(const IrInstr *i, FILE *out) {
    fputs("    ", out);
    if (i->dst >= 0) fprintf(out, "%%t%d = ", i->dst);
    switch (i->op) {
    case IR_BIN:
        fprintf(out, "%s %s ", bop_str(i->bop), irtype_str(i->a.type));
        dump_operand(i->a, out); fputs(", ", out); dump_operand(i->b, out);
        break;
    case IR_UN:
        fprintf(out, "%s %s ", i->uop == UOP_NEG ? "neg" : "not", irtype_str(i->a.type));
        dump_operand(i->a, out);
        break;
    case IR_SITOFP:
        fputs("sitofp i32 ", out); dump_operand(i->a, out);
        break;
    case IR_ZEXT:
        fputs("zext i1 ", out); dump_operand(i->a, out);
        break;
    case IR_LOADL:
        fprintf(out, "load %s %%s%d", irtype_str(i->type), i->slot);
        break;
    case IR_STOREL:
        fprintf(out, "store %%s%d <- %s ", i->slot, irtype_str(i->type)); dump_operand(i->a, out);
        break;
    case IR_LOADG:
        fprintf(out, "load %s @%s", irtype_str(i->type), i->name);
        break;
    case IR_STOREG:
        fprintf(out, "store @%s <- %s ", i->name, irtype_str(i->type)); dump_operand(i->a, out);
        break;
    case IR_LOADF:
        fprintf(out, "loadf %s [", irtype_str(i->type)); dump_operand(i->a, out);
        fprintf(out, " + %d]", i->offset);
        break;
    case IR_STOREF:
        fprintf(out, "storef %s [", irtype_str(i->type)); dump_operand(i->a, out);
        fprintf(out, " + %d] <- ", i->offset); dump_operand(i->b, out);
        break;
    case IR_LOADX:
        fprintf(out, "loadx %s ", irtype_str(i->type)); dump_operand(i->a, out);
        fputc('[', out); dump_operand(i->b, out); fputc(']', out);
        break;
    case IR_STOREX:
        fprintf(out, "storex %s ", irtype_str(i->type)); dump_operand(i->a, out);
        fputc('[', out); dump_operand(i->b, out); fputs("] <- ", out); dump_operand(i->c, out);
        break;
    case IR_CALL:
        fprintf(out, "call %s @%s", irtype_str(i->ret_type), i->name);
        dump_args(i, out);
        break;
    case IR_CALLIND:
        fprintf(out, "callind %s ", irtype_str(i->ret_type)); dump_operand(i->a, out);
        dump_args(i, out);
        break;
    case IR_BR:
        fprintf(out, "br L%d", i->target->id);
        break;
    case IR_CBR:
        fputs("cbr ", out); dump_operand(i->a, out);
        fprintf(out, ", L%d, L%d", i->target->id, i->target2->id);
        break;
    case IR_RET:
        fputs("ret", out);
        if (i->a.kind != OPK_NONE) { fprintf(out, " %s ", irtype_str(i->a.type)); dump_operand(i->a, out); }
        break;
    case IR_UNREACHABLE:
        fputs("unreachable", out);
        break;
    }
    fputc('\n', out);
}

void ir_dump_func(const IrFunc *f, FILE *out) {
    fprintf(out, "function %s @%s(", irtype_str(f->ret), f->name);
    for (int i = 0; i < f->nparams; i++) {
        if (i) fputs(", ", out);
        fprintf(out, "%s %%s%d", irtype_str((IrType)(intptr_t)f->slot_types->items[i]), i);
    }
    fputs(") {\n", out);
    for (int i = 0; i < f->slot_types->len; i++) {
        fprintf(out, "    ; %%s%d : %s = %s\n", i, irtype_str((IrType)(intptr_t)f->slot_types->items[i]),
                (const char *)f->slot_names->items[i]);
    }
    VEC_FOREACH(IrBlock *, b, f->blocks) {
        fprintf(out, "  L%d:", b->id);
        if (b->preds->len) {
            fputs("        ; preds:", out);
            VEC_FOREACH(IrBlock *, p, b->preds) fprintf(out, " L%d", p->id);
        }
        fputc('\n', out);
        VEC_FOREACH(IrInstr *, i, b->instrs) dump_instr(i, out);
    }
    fputs("}\n\n", out);
}

static void dump_string(const IrString *s, FILE *out) {
    fputc('"', out);
    for (int i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        if (c == '\n') fputs("\\n", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c == '"') fputs("\\\"", out);
        else if (c == '\\') fputs("\\\\", out);
        else if (c < 32 || c >= 127) fprintf(out, "\\x%02x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

void ir_dump(const IrModule *m, FILE *out) {
    fprintf(out, "; module %s\n\n", m->source_path);
    VEC_FOREACH(IrGlobal *, g, m->globals)
        fprintf(out, "global %s @%s\n", irtype_str(g->type), g->name);
    if (m->globals->len) fputc('\n', out);
    for (int i = 0; i < m->strings->len; i++) {
        fprintf(out, "string @.str%d = ", i);
        dump_string(m->strings->items[i], out);
        fputc('\n', out);
    }
    if (m->strings->len) fputc('\n', out);
    VEC_FOREACH(IrData *, d, m->data) {
        fprintf(out, "data @%s = [", d->name);
        for (int i = 0; i < d->words->len; i++) {
            DataWord *w = d->words->items[i];
            if (i) fputs(", ", out);
            if (w->kind == DW_INT) fprintf(out, "%lld", w->ival);
            else fprintf(out, "@%s", w->sym);
        }
        fprintf(out, "]%s%s\n", d->comment ? "   ; " : "", d->comment ? d->comment : "");
    }
    if (m->data->len) fputc('\n', out);
    VEC_FOREACH(IrFunc *, f, m->funcs) ir_dump_func(f, out);
}
