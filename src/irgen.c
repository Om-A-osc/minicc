#include "irgen.h"

#include <stdint.h>
#include <string.h>

#include "sema.h"

typedef struct {
    IrModule *m;
    IrFunc *f;
    IrBlock *cur;
    FuncDecl *fn;
    int cur_line;
    IrBlock *break_target;
    IrBlock *continue_target;
} Gen;

/* ------------------------------------------------------------ helpers */

static IrType lower_type(const Type *t) {
    switch (t->kind) {
    case TY_INT: return IR_I32;
    case TY_BOOL: return IR_I1;
    case TY_DOUBLE: return IR_F64;
    case TY_VOID: return IR_VOID;
    default: return IR_PTR;
    }
}

static Operand op_zero(IrType t) {
    switch (t) {
    case IR_F64: return op_fimm(0.0);
    case IR_PTR: return op_null();
    default: return op_imm(0, t);
    }
}

static const char *vtable_name(const ClassDecl *c) { return xstrfmt("mccvt_%s", c->name); }
static const char *classinfo_name(const ClassDecl *c) { return xstrfmt("mccci_%s", c->name); }
static const char *global_name(const VarDecl *v) { return xstrfmt("mccg_%s", v->name); }

static void set_block(Gen *g, IrBlock *b) {
    g->cur = b;
}

static IrInstr *emit(Gen *g, IrInstr *i) {
    if (ir_block_terminated(g->cur)) {
        /* code after return/break/continue: park it in a fresh block that
         * ir_func_cleanup will remove as unreachable */
        set_block(g, ir_block_new(g->f));
    }
    ir_block_append(g->cur, i);
    return i;
}

static IrInstr *instr(Gen *g, IrOp op) {
    return ir_instr_new(op, g->cur_line);
}

static Operand emit_bin(Gen *g, IrBinOp bop, Operand a, Operand b, IrType result) {
    IrInstr *i = instr(g, IR_BIN);
    i->bop = bop;
    i->a = a;
    i->b = b;
    i->type = result;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, result);
}

static Operand emit_un(Gen *g, IrUnOp uop, Operand a) {
    IrInstr *i = instr(g, IR_UN);
    i->uop = uop;
    i->a = a;
    i->type = a.type;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, a.type);
}

static Operand emit_sitofp(Gen *g, Operand a) {
    IrInstr *i = instr(g, IR_SITOFP);
    i->a = a;
    i->type = IR_F64;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, IR_F64);
}

static Operand emit_zext(Gen *g, Operand a) {
    IrInstr *i = instr(g, IR_ZEXT);
    i->a = a;
    i->type = IR_I32;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, IR_I32);
}

static Operand emit_loadl(Gen *g, int slot, IrType t) {
    IrInstr *i = instr(g, IR_LOADL);
    i->slot = slot;
    i->type = t;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, t);
}

static void emit_storel(Gen *g, int slot, Operand v, IrType t) {
    IrInstr *i = instr(g, IR_STOREL);
    i->slot = slot;
    i->a = v;
    i->type = t;
    emit(g, i);
}

static Operand emit_loadg(Gen *g, const char *name, IrType t) {
    IrInstr *i = instr(g, IR_LOADG);
    i->name = name;
    i->type = t;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, t);
}

static void emit_storeg(Gen *g, const char *name, Operand v, IrType t) {
    IrInstr *i = instr(g, IR_STOREG);
    i->name = name;
    i->a = v;
    i->type = t;
    emit(g, i);
}

static Operand emit_loadf(Gen *g, Operand ptr, int offset, IrType t) {
    IrInstr *i = instr(g, IR_LOADF);
    i->a = ptr;
    i->offset = offset;
    i->type = t;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, t);
}

static void emit_storef(Gen *g, Operand ptr, int offset, Operand v, IrType t) {
    IrInstr *i = instr(g, IR_STOREF);
    i->a = ptr;
    i->offset = offset;
    i->b = v;
    i->type = t;
    emit(g, i);
}

static Operand emit_loadx(Gen *g, Operand ptr, Operand idx, IrType t) {
    IrInstr *i = instr(g, IR_LOADX);
    i->a = ptr;
    i->b = idx;
    i->type = t;
    i->dst = ir_func_temp(g->f);
    emit(g, i);
    return op_temp(i->dst, t);
}

static void emit_storex(Gen *g, Operand ptr, Operand idx, Operand v, IrType t) {
    IrInstr *i = instr(g, IR_STOREX);
    i->a = ptr;
    i->b = idx;
    i->c = v;
    i->type = t;
    emit(g, i);
}

static Operand emit_call(Gen *g, const char *name, IrType ret, Operand *args, int nargs) {
    IrInstr *i = instr(g, IR_CALL);
    i->name = name;
    i->ret_type = ret;
    i->type = ret;
    i->args = xcalloc((size_t)(nargs ? nargs : 1), sizeof(Operand));
    memcpy(i->args, args, sizeof(Operand) * (size_t)nargs);
    i->nargs = nargs;
    if (ret != IR_VOID) i->dst = ir_func_temp(g->f);
    if (runtime_fn_lookup(name)) strmap_put(g->m->used_runtime, name, (void *)1);
    emit(g, i);
    return ret == IR_VOID ? op_none() : op_temp(i->dst, ret);
}

static Operand emit_callind(Gen *g, Operand fn, IrType ret, Operand *args, int nargs) {
    IrInstr *i = instr(g, IR_CALLIND);
    i->a = fn;
    i->ret_type = ret;
    i->type = ret;
    i->args = xcalloc((size_t)(nargs ? nargs : 1), sizeof(Operand));
    memcpy(i->args, args, sizeof(Operand) * (size_t)nargs);
    i->nargs = nargs;
    if (ret != IR_VOID) i->dst = ir_func_temp(g->f);
    emit(g, i);
    return ret == IR_VOID ? op_none() : op_temp(i->dst, ret);
}

/* Call a runtime function with up to three arguments. */
static Operand emit_rt(Gen *g, const char *name, int n, Operand a0, Operand a1, Operand a2) {
    const RuntimeFn *rt = runtime_fn_lookup(name);
    Operand args[3] = { a0, a1, a2 };
    return emit_call(g, name, rt->ret, args, n);
}

static void emit_br(Gen *g, IrBlock *target) {
    IrInstr *i = instr(g, IR_BR);
    i->target = target;
    emit(g, i);
}

static void emit_cbr(Gen *g, Operand cond, IrBlock *t, IrBlock *f) {
    IrInstr *i = instr(g, IR_CBR);
    i->a = cond;
    i->target = t;
    i->target2 = f;
    emit(g, i);
}

static void emit_ret(Gen *g, Operand v) {
    IrInstr *i = instr(g, IR_RET);
    i->a = v;
    emit(g, i);
}

static void emit_unreachable(Gen *g) {
    emit(g, instr(g, IR_UNREACHABLE));
}

/* -------------------------------------------------- runtime checks */

static void null_check(Gen *g, Operand ptr) {
    IrBlock *fail = ir_block_new(g->f), *ok = ir_block_new(g->f);
    Operand not_null = emit_bin(g, BOP_NE, ptr, op_null(), IR_I1);
    emit_cbr(g, not_null, ok, fail);
    set_block(g, fail);
    emit_rt(g, "__mcc_null_fail", 1, op_imm(g->cur_line, IR_I32), op_none(), op_none());
    emit_unreachable(g);
    set_block(g, ok);
}

static void bounds_check(Gen *g, Operand arr, Operand idx) {
    IrBlock *fail = ir_block_new(g->f), *ok = ir_block_new(g->f);
    Operand len = emit_loadf(g, arr, 0, IR_I32);
    Operand in_range = emit_bin(g, BOP_ULT, idx, len, IR_I1);   /* unsigned: catches negatives too */
    emit_cbr(g, in_range, ok, fail);
    set_block(g, fail);
    emit_rt(g, "__mcc_bounds_fail", 3, idx, len, op_imm(g->cur_line, IR_I32));
    emit_unreachable(g);
    set_block(g, ok);
}

static void div_zero_check(Gen *g, Operand divisor) {
    if (divisor.kind == OPK_IMM && divisor.imm != 0) return;
    IrBlock *fail = ir_block_new(g->f), *ok = ir_block_new(g->f);
    Operand nonzero = emit_bin(g, BOP_NE, divisor, op_imm(0, IR_I32), IR_I1);
    emit_cbr(g, nonzero, ok, fail);
    set_block(g, fail);
    emit_rt(g, "__mcc_div_zero_fail", 1, op_imm(g->cur_line, IR_I32), op_none(), op_none());
    emit_unreachable(g);
    set_block(g, ok);
}

/* ------------------------------------------------------- expressions */

static Operand gen_expr(Gen *g, Expr *e);
static void gen_branch(Gen *g, Expr *e, IrBlock *t, IrBlock *f);

typedef enum { LR_SLOT, LR_GLOBAL, LR_FIELD, LR_INDEX } LRefKind;

typedef struct {
    LRefKind kind;
    IrType type;
    int slot;
    const char *name;
    Operand ptr;
    int offset;
    Operand idx;
} LRef;

static Operand gen_this(Gen *g) {
    return emit_loadl(g, 0, IR_PTR);
}

static LRef gen_lref(Gen *g, Expr *e) {
    LRef r;
    memset(&r, 0, sizeof r);
    r.type = lower_type(e->type);
    switch (e->kind) {
    case EX_IDENT:
        switch (e->sym->kind) {
        case SYM_LOCAL: case SYM_PARAM:
            r.kind = LR_SLOT;
            r.slot = e->sym->var->slot;
            break;
        case SYM_GLOBAL:
            r.kind = LR_GLOBAL;
            r.name = global_name(e->sym->var);
            break;
        case SYM_FIELD:
            r.kind = LR_FIELD;
            r.ptr = gen_this(g);
            r.offset = e->sym->var->offset;
            break;
        default:
            diag_fatal("irgen: unexpected symbol kind for identifier '%s'", e->name);
        }
        break;
    case EX_FIELD:
        r.kind = LR_FIELD;
        r.ptr = gen_expr(g, e->lhs);
        if (e->lhs->kind != EX_THIS) null_check(g, r.ptr);
        r.offset = e->field->offset;
        break;
    case EX_INDEX:
        r.kind = LR_INDEX;
        r.ptr = gen_expr(g, e->lhs);
        null_check(g, r.ptr);
        r.idx = gen_expr(g, e->rhs);
        bounds_check(g, r.ptr, r.idx);
        break;
    default:
        diag_fatal("irgen: expression is not an lvalue");
    }
    return r;
}

static Operand lref_load(Gen *g, LRef r) {
    switch (r.kind) {
    case LR_SLOT: return emit_loadl(g, r.slot, r.type);
    case LR_GLOBAL: return emit_loadg(g, r.name, r.type);
    case LR_FIELD: return emit_loadf(g, r.ptr, r.offset, r.type);
    case LR_INDEX: return emit_loadx(g, r.ptr, r.idx, r.type);
    }
    return op_none();
}

static void lref_store(Gen *g, LRef r, Operand v) {
    switch (r.kind) {
    case LR_SLOT: emit_storel(g, r.slot, v, r.type); break;
    case LR_GLOBAL: emit_storeg(g, r.name, v, r.type); break;
    case LR_FIELD: emit_storef(g, r.ptr, r.offset, v, r.type); break;
    case LR_INDEX: emit_storex(g, r.ptr, r.idx, v, r.type); break;
    }
}

/* Arithmetic / comparison on already-evaluated operands, with the type rules
 * sema established (operands share a type; strings use the runtime). */
static Operand gen_arith(Gen *g, BinOp op, Operand a, Operand b, const Type *operand_type) {
    if (operand_type->kind == TY_STRING) {
        if (op == BIN_ADD) return emit_rt(g, "__mcc_strcat", 2, a, b, op_none());
        Operand eq = emit_rt(g, "__mcc_streq", 2, a, b, op_none());
        return emit_bin(g, op == BIN_EQ ? BOP_NE : BOP_EQ, eq, op_imm(0, IR_I32), IR_I1);
    }
    IrType t = a.type;
    switch (op) {
    case BIN_ADD: return emit_bin(g, BOP_ADD, a, b, t);
    case BIN_SUB: return emit_bin(g, BOP_SUB, a, b, t);
    case BIN_MUL: return emit_bin(g, BOP_MUL, a, b, t);
    case BIN_DIV:
        if (t == IR_I32) div_zero_check(g, b);
        return emit_bin(g, BOP_DIV, a, b, t);
    case BIN_MOD:
        div_zero_check(g, b);
        return emit_bin(g, BOP_REM, a, b, t);
    case BIN_LT: return emit_bin(g, BOP_LT, a, b, IR_I1);
    case BIN_LE: return emit_bin(g, BOP_LE, a, b, IR_I1);
    case BIN_GT: return emit_bin(g, BOP_GT, a, b, IR_I1);
    case BIN_GE: return emit_bin(g, BOP_GE, a, b, IR_I1);
    case BIN_EQ: return emit_bin(g, BOP_EQ, a, b, IR_I1);
    case BIN_NE: return emit_bin(g, BOP_NE, a, b, IR_I1);
    default: break;
    }
    diag_fatal("irgen: unexpected arithmetic operator");
    return op_none();
}

static Operand *gen_args(Gen *g, Expr *e, Operand receiver, int has_receiver, int *nargs) {
    int n = e->args->len + has_receiver;
    Operand *args = xcalloc((size_t)(n ? n : 1), sizeof(Operand));
    int k = 0;
    if (has_receiver) args[k++] = receiver;
    VEC_FOREACH(Expr *, a, e->args) args[k++] = gen_expr(g, a);
    *nargs = n;
    return args;
}

/* Virtual call: receiver already evaluated and null-checked. */
static Operand gen_virtual_call(Gen *g, Expr *e, Operand receiver) {
    FuncDecl *fn = e->fn;
    IrType ret = lower_type(fn->ret);
    int nargs;
    Operand *args = gen_args(g, e, receiver, 1, &nargs);
    Operand fnptr;
    if (e->via_interface) {
        fnptr = emit_rt(g, "__mcc_lookup", 2, receiver, op_imm(fn->method_id, IR_I32), op_none());
    } else {
        Operand vptr = emit_loadf(g, receiver, 0, IR_PTR);
        fnptr = emit_loadf(g, vptr, 8 * (1 + fn->vslot), IR_PTR);
    }
    return emit_callind(g, fnptr, ret, args, nargs);
}

/* Evaluate a boolean expression to a value by branching into a result slot. */
static Operand gen_logical_value(Gen *g, Expr *e) {
    int slot = ir_func_slot(g->f, IR_I1, "$cond");
    IrBlock *t = ir_block_new(g->f), *f = ir_block_new(g->f), *join = ir_block_new(g->f);
    gen_branch(g, e, t, f);
    set_block(g, t);
    emit_storel(g, slot, op_imm(1, IR_I1), IR_I1);
    emit_br(g, join);
    set_block(g, f);
    emit_storel(g, slot, op_imm(0, IR_I1), IR_I1);
    emit_br(g, join);
    set_block(g, join);
    return emit_loadl(g, slot, IR_I1);
}

static Operand gen_expr(Gen *g, Expr *e) {
    switch (e->kind) {
    case EX_INT_LIT: return op_imm(e->ival, IR_I32);
    case EX_BOOL_LIT: return op_imm(e->ival, IR_I1);
    case EX_DOUBLE_LIT: return op_fimm(e->dval);
    case EX_NULL_LIT: return op_null();
    case EX_STRING_LIT: return op_str(ir_module_string(g->m, e->sval, (int)strlen(e->sval)));
    case EX_THIS: return gen_this(g);

    case EX_IDENT: case EX_FIELD: case EX_INDEX:
        return lref_load(g, gen_lref(g, e));

    case EX_BINARY:
        if (e->binop == BIN_AND || e->binop == BIN_OR) return gen_logical_value(g, e);
        {
            Operand a = gen_expr(g, e->lhs);
            Operand b = gen_expr(g, e->rhs);
            return gen_arith(g, e->binop, a, b, e->lhs->type);
        }

    case EX_UNARY: {
        Operand a = gen_expr(g, e->lhs);
        return emit_un(g, e->unop == UN_NEG ? UOP_NEG : UOP_NOT, a);
    }

    case EX_CAST:
        return emit_sitofp(g, gen_expr(g, e->lhs));

    case EX_ASSIGN: {
        LRef ref = gen_lref(g, e->lhs);
        Operand v = gen_expr(g, e->rhs);
        if (e->is_compound) {
            Operand cur = lref_load(g, ref);
            v = gen_arith(g, e->binop, cur, v, e->lhs->type);
        }
        lref_store(g, ref, v);
        return v;
    }

    case EX_INCDEC: {
        LRef ref = gen_lref(g, e->lhs);
        Operand old = lref_load(g, ref);
        Operand nv = emit_bin(g, e->is_increment ? BOP_ADD : BOP_SUB, old, op_imm(1, IR_I32), IR_I32);
        lref_store(g, ref, nv);
        return e->is_prefix ? nv : old;
    }

    case EX_CALL: {
        FuncDecl *fn = e->fn;
        if (fn->owner) {                        /* implicit this.method(...) */
            Operand self = gen_this(g);
            return gen_virtual_call(g, e, self);
        }
        int nargs;
        Operand *args = gen_args(g, e, op_none(), 0, &nargs);
        return emit_call(g, fn->mangled, lower_type(fn->ret), args, nargs);
    }

    case EX_METHOD_CALL: {
        Operand obj = gen_expr(g, e->lhs);
        if (e->lhs->kind != EX_THIS) null_check(g, obj);
        if (e->is_array_length) return emit_loadf(g, obj, 0, IR_I32);
        return gen_virtual_call(g, e, obj);
    }

    case EX_NEW: {
        ClassDecl *c = e->type->cls;
        Operand p = emit_rt(g, "__mcc_alloc", 1, op_imm(c->size, IR_I32), op_none(), op_none());
        emit_storef(g, p, 0, op_sym(vtable_name(c)), IR_PTR);
        return p;
    }

    case EX_NEW_ARRAY: {
        Operand n = gen_expr(g, e->lhs);
        int elem_size = type_size(e->elem_type);
        return emit_rt(g, "__mcc_newarray", 2, n, op_imm(elem_size, IR_I32), op_none());
    }

    case EX_READ_INTEGER:
        return emit_rt(g, "__mcc_read_int", 0, op_none(), op_none(), op_none());
    case EX_READ_LINE:
        return emit_rt(g, "__mcc_read_line", 0, op_none(), op_none(), op_none());
    }
    diag_fatal("irgen: unhandled expression kind %d", e->kind);
    return op_none();
}

/* Jumping code for conditions: no materialised boolean for && || !. */
static void gen_branch(Gen *g, Expr *e, IrBlock *t, IrBlock *f) {
    if (e->kind == EX_BINARY && e->binop == BIN_AND) {
        IrBlock *mid = ir_block_new(g->f);
        gen_branch(g, e->lhs, mid, f);
        set_block(g, mid);
        gen_branch(g, e->rhs, t, f);
        return;
    }
    if (e->kind == EX_BINARY && e->binop == BIN_OR) {
        IrBlock *mid = ir_block_new(g->f);
        gen_branch(g, e->lhs, t, mid);
        set_block(g, mid);
        gen_branch(g, e->rhs, t, f);
        return;
    }
    if (e->kind == EX_UNARY && e->unop == UN_NOT) {
        gen_branch(g, e->lhs, f, t);
        return;
    }
    if (e->kind == EX_BOOL_LIT) {
        emit_br(g, e->ival ? t : f);
        return;
    }
    Operand v = gen_expr(g, e);
    emit_cbr(g, v, t, f);
}

/* -------------------------------------------------------- statements */

static void gen_stmt(Gen *g, Stmt *s);

static void gen_vardecl(Gen *g, VarDecl *v) {
    IrType t = lower_type(v->type);
    v->slot = ir_func_slot(g->f, t, v->name);
    Operand init = v->init ? gen_expr(g, v->init) : op_zero(t);
    emit_storel(g, v->slot, init, t);
}

static void gen_stmt(Gen *g, Stmt *s) {
    if (!s) return;
    g->cur_line = s->line;
    switch (s->kind) {
    case ST_EXPR:
        gen_expr(g, s->expr);
        break;
    case ST_VARDECL:
        gen_vardecl(g, s->var);
        break;
    case ST_BLOCK:
        VEC_FOREACH(Stmt *, c, s->stmts) gen_stmt(g, c);
        break;
    case ST_IF: {
        IrBlock *then_b = ir_block_new(g->f);
        IrBlock *else_b = s->else_body ? ir_block_new(g->f) : NULL;
        IrBlock *end_b = ir_block_new(g->f);
        gen_branch(g, s->expr, then_b, else_b ? else_b : end_b);
        set_block(g, then_b);
        gen_stmt(g, s->body);
        emit_br(g, end_b);
        if (else_b) {
            set_block(g, else_b);
            gen_stmt(g, s->else_body);
            emit_br(g, end_b);
        }
        set_block(g, end_b);
        break;
    }
    case ST_WHILE: {
        IrBlock *cond_b = ir_block_new(g->f), *body_b = ir_block_new(g->f), *end_b = ir_block_new(g->f);
        emit_br(g, cond_b);
        set_block(g, cond_b);
        gen_branch(g, s->expr, body_b, end_b);
        IrBlock *saved_break = g->break_target, *saved_cont = g->continue_target;
        g->break_target = end_b;
        g->continue_target = cond_b;
        set_block(g, body_b);
        gen_stmt(g, s->body);
        emit_br(g, cond_b);
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        set_block(g, end_b);
        break;
    }
    case ST_FOR: {
        gen_stmt(g, s->init);
        g->cur_line = s->line;
        IrBlock *cond_b = ir_block_new(g->f), *body_b = ir_block_new(g->f);
        IrBlock *step_b = ir_block_new(g->f), *end_b = ir_block_new(g->f);
        emit_br(g, cond_b);
        set_block(g, cond_b);
        if (s->expr) gen_branch(g, s->expr, body_b, end_b);
        else emit_br(g, body_b);
        IrBlock *saved_break = g->break_target, *saved_cont = g->continue_target;
        g->break_target = end_b;
        g->continue_target = step_b;
        set_block(g, body_b);
        gen_stmt(g, s->body);
        emit_br(g, step_b);
        set_block(g, step_b);
        g->cur_line = s->line;
        if (s->step) gen_expr(g, s->step);
        emit_br(g, cond_b);
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        set_block(g, end_b);
        break;
    }
    case ST_BREAK:
        emit_br(g, g->break_target);
        set_block(g, ir_block_new(g->f));
        break;
    case ST_CONTINUE:
        emit_br(g, g->continue_target);
        set_block(g, ir_block_new(g->f));
        break;
    case ST_RETURN:
        if (s->expr) emit_ret(g, gen_expr(g, s->expr));
        else emit_ret(g, op_none());
        set_block(g, ir_block_new(g->f));
        break;
    case ST_PRINT:
        VEC_FOREACH(Expr *, a, s->args) {
            Operand v = gen_expr(g, a);
            switch (a->type->kind) {
            case TY_INT: emit_rt(g, "__mcc_print_int", 1, v, op_none(), op_none()); break;
            case TY_BOOL: emit_rt(g, "__mcc_print_bool", 1, emit_zext(g, v), op_none(), op_none()); break;
            case TY_DOUBLE: emit_rt(g, "__mcc_print_double", 1, v, op_none(), op_none()); break;
            default: emit_rt(g, "__mcc_print_string", 1, v, op_none(), op_none()); break;
            }
        }
        break;
    }
}

/* --------------------------------------------------------- functions */

static void finish_function(Gen *g) {
    VEC_FOREACH(IrBlock *, b, g->f->blocks) {
        if (!ir_block_terminated(b)) {
            set_block(g, b);
            emit_ret(g, g->f->ret == IR_VOID ? op_none() : op_zero(g->f->ret));
        }
    }
    ir_func_cleanup(g->f);
}

static void gen_function(Gen *g, FuncDecl *fn) {
    g->fn = fn;
    g->cur_line = fn->line;
    g->f = ir_func_new(g->m, fn->mangled, lower_type(fn->ret), fn->line);
    if (fn->owner) ir_func_slot(g->f, IR_PTR, "this");
    VEC_FOREACH(VarDecl *, p, fn->params) p->slot = ir_func_slot(g->f, lower_type(p->type), p->name);
    g->f->nparams = g->f->slot_types->len;
    set_block(g, ir_block_new(g->f));
    gen_stmt(g, fn->body);
    finish_function(g);
}

static void gen_entry(Gen *g, Program *prog) {
    g->fn = NULL;
    g->cur_line = 0;
    g->f = ir_func_new(g->m, "__mcc_entry", IR_I32, 0);
    set_block(g, ir_block_new(g->f));
    VEC_FOREACH(VarDecl *, v, prog->globals) {
        if (!v->init) continue;
        g->cur_line = v->line;
        emit_storeg(g, global_name(v), gen_expr(g, v->init), lower_type(v->type));
    }
    FuncDecl *main_fn = NULL;
    VEC_FOREACH(FuncDecl *, f, prog->funcs) if (strcmp(f->name, "main") == 0) main_fn = f;
    Operand r = emit_call(g, main_fn->mangled, lower_type(main_fn->ret), NULL, 0);
    emit_ret(g, main_fn->ret->kind == TY_INT ? r : op_imm(0, IR_I32));
    finish_function(g);
}

static void gen_class_tables(Gen *g, ClassDecl *c) {
    IrData *vt = ir_module_data(g->m, vtable_name(c), xstrfmt("vtable for %s", c->name));
    ir_data_sym(vt, classinfo_name(c));
    VEC_FOREACH(FuncDecl *, m, c->vtable) ir_data_sym(vt, m->mangled);

    IrData *ci = ir_module_data(g->m, classinfo_name(c), xstrfmt("method table for %s (id, fn)", c->name));
    ir_data_int(ci, c->vtable->len);
    VEC_FOREACH(FuncDecl *, m, c->vtable) {
        ir_data_int(ci, m->method_id);
        ir_data_sym(ci, m->mangled);
    }
}

IrModule *irgen_program(Program *prog, const char *source_path) {
    Gen g;
    memset(&g, 0, sizeof g);
    g.m = ir_module_new(source_path);

    VEC_FOREACH(VarDecl *, v, prog->globals) ir_module_global(g.m, global_name(v), lower_type(v->type));
    VEC_FOREACH(ClassDecl *, c, prog->classes) gen_class_tables(&g, c);
    VEC_FOREACH(FuncDecl *, f, prog->funcs) gen_function(&g, f);
    VEC_FOREACH(ClassDecl *, c, prog->classes)
        VEC_FOREACH(FuncDecl *, m, c->methods) gen_function(&g, m);
    gen_entry(&g, prog);
    return g.m;
}
