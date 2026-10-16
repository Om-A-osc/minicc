/* emit_llvm.c - prints the IR as textual LLVM IR (opaque pointers, LLVM >= 15).
 *
 * Slots become allocas in a dedicated entry block, temps become SSA values,
 * and typed field/element accesses become byte-offset GEPs. The result is
 * consumed by clang (native code) or llc (any LLVM target, including NVPTX). */
#include "emit_llvm.h"

#include <stdint.h>
#include <string.h>

static const char *lty(IrType t) {
    switch (t) {
    case IR_VOID: return "void";
    case IR_I1: return "i1";
    case IR_I32: return "i32";
    case IR_F64: return "double";
    case IR_PTR: return "ptr";
    }
    return "?";
}

static void operand(Operand o, FILE *out) {
    switch (o.kind) {
    case OPK_NONE: fputs("<none>", out); break;
    case OPK_TEMP: fprintf(out, "%%t%d", o.temp); break;
    case OPK_IMM: fprintf(out, "%lld", o.imm); break;
    case OPK_FIMM: {
        uint64_t bits;
        memcpy(&bits, &o.fimm, sizeof bits);
        fprintf(out, "0x%016llX", (unsigned long long)bits);
        break;
    }
    case OPK_NULL: fputs("null", out); break;
    case OPK_STR: fprintf(out, "@.str%d", o.str); break;
    case OPK_SYM: fprintf(out, "@%s", o.sym); break;
    }
}

static void typed_operand(Operand o, FILE *out) {
    fprintf(out, "%s ", lty(o.type));
    operand(o, out);
}

static void emit_string(const IrString *s, int idx, FILE *out) {
    fprintf(out, "@.str%d = private unnamed_addr constant [%d x i8] c\"", idx, s->len + 1);
    for (int i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 32 && c < 127 && c != '"' && c != '\\') fputc(c, out);
        else fprintf(out, "\\%02X", c);
    }
    fputs("\\00\"\n", out);
}

static void emit_data(const IrData *d, FILE *out) {
    fprintf(out, "@%s = global { ", d->name);
    for (int i = 0; i < d->words->len; i++) {
        DataWord *w = d->words->items[i];
        fprintf(out, "%s%s", i ? ", " : "", w->kind == DW_INT ? "i64" : "ptr");
    }
    fputs(" } { ", out);
    for (int i = 0; i < d->words->len; i++) {
        DataWord *w = d->words->items[i];
        if (i) fputs(", ", out);
        if (w->kind == DW_INT) fprintf(out, "i64 %lld", w->ival);
        else fprintf(out, "ptr @%s", w->sym);
    }
    fprintf(out, " }%s%s\n", d->comment ? "   ; " : "", d->comment ? d->comment : "");
}

static const char *icmp_pred(IrBinOp op) {
    switch (op) {
    case BOP_LT: return "slt";
    case BOP_LE: return "sle";
    case BOP_GT: return "sgt";
    case BOP_GE: return "sge";
    case BOP_EQ: return "eq";
    case BOP_NE: return "ne";
    case BOP_ULT: return "ult";
    default: return "?";
    }
}

static const char *fcmp_pred(IrBinOp op) {
    switch (op) {
    case BOP_LT: return "olt";
    case BOP_LE: return "ole";
    case BOP_GT: return "ogt";
    case BOP_GE: return "oge";
    case BOP_EQ: return "oeq";
    case BOP_NE: return "une";
    default: return "?";
    }
}

static void emit_bin(const IrInstr *i, FILE *out) {
    IrType t = i->a.type;
    fprintf(out, "  %%t%d = ", i->dst);
    if (t == IR_F64) {
        switch (i->bop) {
        case BOP_ADD: fputs("fadd double ", out); break;
        case BOP_SUB: fputs("fsub double ", out); break;
        case BOP_MUL: fputs("fmul double ", out); break;
        case BOP_DIV: fputs("fdiv double ", out); break;
        default: fprintf(out, "fcmp %s double ", fcmp_pred(i->bop)); break;
        }
    } else {
        switch (i->bop) {
        case BOP_ADD: fprintf(out, "add %s ", lty(t)); break;
        case BOP_SUB: fprintf(out, "sub %s ", lty(t)); break;
        case BOP_MUL: fprintf(out, "mul %s ", lty(t)); break;
        case BOP_DIV: fprintf(out, "sdiv %s ", lty(t)); break;
        case BOP_REM: fprintf(out, "srem %s ", lty(t)); break;
        case BOP_AND: fprintf(out, "and %s ", lty(t)); break;
        case BOP_OR: fprintf(out, "or %s ", lty(t)); break;
        case BOP_XOR: fprintf(out, "xor %s ", lty(t)); break;
        default: fprintf(out, "icmp %s %s ", icmp_pred(i->bop), lty(t)); break;
        }
    }
    operand(i->a, out);
    fputs(", ", out);
    operand(i->b, out);
    fputc('\n', out);
}

static void emit_call_args(const IrInstr *i, FILE *out) {
    fputc('(', out);
    for (int k = 0; k < i->nargs; k++) {
        if (k) fputs(", ", out);
        typed_operand(i->args[k], out);
    }
    fputs(")\n", out);
}

/* Address of a + offset into a fresh %tN.p value; returns the name used. */
static void emit_field_addr(const IrInstr *i, int dst, FILE *out, char *buf, size_t n) {
    if (i->offset == 0) {
        snprintf(buf, n, "%%t%d", i->a.temp);
        if (i->a.kind != OPK_TEMP) {   /* constant base pointer: only null is possible, keep it valid */
            snprintf(buf, n, "null");
        }
        return;
    }
    fprintf(out, "  %%t%d.p = getelementptr i8, ptr ", dst);
    operand(i->a, out);
    fprintf(out, ", i64 %d\n", i->offset);
    snprintf(buf, n, "%%t%d.p", dst);
}

static void emit_elem_addr(const IrInstr *i, int dst, FILE *out, char *buf, size_t n) {
    fprintf(out, "  %%t%d.i = sext i32 ", dst);
    operand(i->b, out);
    fputs(" to i64\n", out);
    fprintf(out, "  %%t%d.b = getelementptr i8, ptr ", dst);
    operand(i->a, out);
    fputs(", i64 8\n", out);
    fprintf(out, "  %%t%d.p = getelementptr %s, ptr %%t%d.b, i64 %%t%d.i\n", dst, lty(i->type), dst, dst);
    snprintf(buf, n, "%%t%d.p", dst);
}

static int store_counter = 0;   /* unique names for address temps of stores */

static void emit_instr(const IrInstr *i, FILE *out) {
    char addr[64];
    switch (i->op) {
    case IR_BIN:
        emit_bin(i, out);
        break;
    case IR_UN:
        if (i->uop == UOP_NOT) {
            fprintf(out, "  %%t%d = xor i1 ", i->dst); operand(i->a, out); fputs(", true\n", out);
        } else if (i->a.type == IR_F64) {
            fprintf(out, "  %%t%d = fneg double ", i->dst); operand(i->a, out); fputc('\n', out);
        } else {
            fprintf(out, "  %%t%d = sub i32 0, ", i->dst); operand(i->a, out); fputc('\n', out);
        }
        break;
    case IR_SITOFP:
        fprintf(out, "  %%t%d = sitofp i32 ", i->dst); operand(i->a, out); fputs(" to double\n", out);
        break;
    case IR_ZEXT:
        fprintf(out, "  %%t%d = zext i1 ", i->dst); operand(i->a, out); fputs(" to i32\n", out);
        break;
    case IR_LOADL:
        fprintf(out, "  %%t%d = load %s, ptr %%s%d\n", i->dst, lty(i->type), i->slot);
        break;
    case IR_STOREL:
        fputs("  store ", out); typed_operand(i->a, out); fprintf(out, ", ptr %%s%d\n", i->slot);
        break;
    case IR_LOADG:
        fprintf(out, "  %%t%d = load %s, ptr @%s\n", i->dst, lty(i->type), i->name);
        break;
    case IR_STOREG:
        fputs("  store ", out); typed_operand(i->a, out); fprintf(out, ", ptr @%s\n", i->name);
        break;
    case IR_LOADF:
        emit_field_addr(i, i->dst, out, addr, sizeof addr);
        fprintf(out, "  %%t%d = load %s, ptr %s\n", i->dst, lty(i->type), addr);
        break;
    case IR_STOREF: {
        int id = 100000 + store_counter++;
        emit_field_addr(i, id, out, addr, sizeof addr);
        fputs("  store ", out); typed_operand(i->b, out); fprintf(out, ", ptr %s\n", addr);
        break;
    }
    case IR_LOADX:
        emit_elem_addr(i, i->dst, out, addr, sizeof addr);
        fprintf(out, "  %%t%d = load %s, ptr %s\n", i->dst, lty(i->type), addr);
        break;
    case IR_STOREX: {
        int id = 100000 + store_counter++;
        emit_elem_addr(i, id, out, addr, sizeof addr);
        fputs("  store ", out); typed_operand(i->c, out); fprintf(out, ", ptr %s\n", addr);
        break;
    }
    case IR_CALL:
        fputs("  ", out);
        if (i->dst >= 0) fprintf(out, "%%t%d = ", i->dst);
        fprintf(out, "call %s @%s", lty(i->ret_type), i->name);
        emit_call_args(i, out);
        break;
    case IR_CALLIND:
        fputs("  ", out);
        if (i->dst >= 0) fprintf(out, "%%t%d = ", i->dst);
        fprintf(out, "call %s ", lty(i->ret_type));
        operand(i->a, out);
        emit_call_args(i, out);
        break;
    case IR_BR:
        fprintf(out, "  br label %%L%d\n", i->target->id);
        break;
    case IR_CBR:
        fputs("  br i1 ", out); operand(i->a, out);
        fprintf(out, ", label %%L%d, label %%L%d\n", i->target->id, i->target2->id);
        break;
    case IR_RET:
        if (i->a.kind == OPK_NONE) fputs("  ret void\n", out);
        else { fputs("  ret ", out); typed_operand(i->a, out); fputc('\n', out); }
        break;
    case IR_UNREACHABLE:
        fputs("  unreachable\n", out);
        break;
    }
}

static void emit_func(const IrFunc *f, FILE *out) {
    fprintf(out, "define %s @%s(", lty(f->ret), f->name);
    for (int i = 0; i < f->nparams; i++) {
        IrType t = (IrType)(intptr_t)f->slot_types->items[i];
        fprintf(out, "%s%s %%p%d", i ? ", " : "", lty(t), i);
    }
    fputs(") {\nentry:\n", out);
    for (int i = 0; i < f->slot_types->len; i++) {
        IrType t = (IrType)(intptr_t)f->slot_types->items[i];
        fprintf(out, "  %%s%d = alloca %s   ; %s\n", i, lty(t), (const char *)f->slot_names->items[i]);
    }
    for (int i = 0; i < f->nparams; i++) {
        IrType t = (IrType)(intptr_t)f->slot_types->items[i];
        fprintf(out, "  store %s %%p%d, ptr %%s%d\n", lty(t), i, i);
    }
    fputs("  br label %L0\n", out);
    VEC_FOREACH(IrBlock *, b, f->blocks) {
        fprintf(out, "L%d:\n", b->id);
        VEC_FOREACH(IrInstr *, i, b->instrs) emit_instr(i, out);
    }
    fputs("}\n\n", out);
}

void emit_llvm(const IrModule *m, FILE *out) {
    store_counter = 0;
    fprintf(out, "; generated by minicc from %s\n", m->source_path);
    fprintf(out, "source_filename = \"%s\"\n\n", m->source_path);

    VEC_FOREACH(IrGlobal *, g, m->globals) {
        const char *init = g->type == IR_F64 ? "0.0" : g->type == IR_PTR ? "null" : "0";
        fprintf(out, "@%s = global %s %s\n", g->name, lty(g->type), init);
    }
    for (int i = 0; i < m->strings->len; i++) emit_string(m->strings->items[i], i, out);
    VEC_FOREACH(IrData *, d, m->data) emit_data(d, out);
    fputc('\n', out);

    for (int i = 0; i < runtime_fn_count; i++) {
        const RuntimeFn *rt = &runtime_fns[i];
        if (!strmap_has(m->used_runtime, rt->name)) continue;
        fprintf(out, "declare %s @%s(", lty(rt->ret), rt->name);
        for (int k = 0; k < rt->nparams; k++) fprintf(out, "%s%s", k ? ", " : "", lty(rt->params[k]));
        fputs(")\n", out);
    }
    fputc('\n', out);

    VEC_FOREACH(IrFunc *, f, m->funcs) emit_func(f, out);
}
