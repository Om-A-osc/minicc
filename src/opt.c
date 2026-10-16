#include "opt.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------ helpers */

static int has_dst(const IrInstr *i) {
    return i->dst >= 0;
}

/* Instructions whose only effect is defining their result. Loads are pure
 * because every pointer is null- and bounds-checked before it is used. */
static int is_pure(const IrInstr *i) {
    switch (i->op) {
    case IR_BIN: case IR_UN: case IR_SITOFP: case IR_ZEXT:
    case IR_LOADL: case IR_LOADG: case IR_LOADF: case IR_LOADX:
        return 1;
    default:
        return 0;
    }
}

typedef void (*OperandFn)(Operand *o, void *ctx);

static void for_each_operand(IrInstr *i, OperandFn fn, void *ctx) {
    fn(&i->a, ctx);
    fn(&i->b, ctx);
    fn(&i->c, ctx);
    for (int k = 0; k < i->nargs; k++) fn(&i->args[k], ctx);
}

/* Replace temps according to a substitution map (indexed by temp id). */
static void subst_fn(Operand *o, void *ctx) {
    Operand *map = ctx;
    if (o->kind == OPK_TEMP && map[o->temp].kind != OPK_NONE) *o = map[o->temp];
}

static void substitute(IrFunc *f, Operand *map) {
    VEC_FOREACH(IrBlock *, b, f->blocks)
        VEC_FOREACH(IrInstr *, i, b->instrs) for_each_operand(i, subst_fn, map);
}

static Operand *new_map(const IrFunc *f) {
    Operand *map = xcalloc((size_t)(f->ntemps ? f->ntemps : 1), sizeof(Operand));
    for (int i = 0; i < f->ntemps; i++) map[i] = op_none();
    return map;
}

/* ------------------------------------------------- constant folding */

static int32_t wrap32(int64_t v) {
    return (int32_t)(uint32_t)(uint64_t)v;
}

static int fold_bin(const IrInstr *i, Operand *out) {
    Operand a = i->a, b = i->b;
    if (!op_is_const(a) || !op_is_const(b)) return 0;
    if (a.type == IR_I32 || a.type == IR_I1) {
        int64_t x = a.imm, y = b.imm;
        int64_t r;
        switch (i->bop) {
        case BOP_ADD: r = wrap32(x + y); break;
        case BOP_SUB: r = wrap32(x - y); break;
        case BOP_MUL: r = wrap32(x * y); break;
        case BOP_DIV:
            if (y == 0 || (x == INT32_MIN && y == -1)) return 0;
            r = x / y; break;
        case BOP_REM:
            if (y == 0 || (x == INT32_MIN && y == -1)) return 0;
            r = x % y; break;
        case BOP_LT: r = x < y; break;
        case BOP_LE: r = x <= y; break;
        case BOP_GT: r = x > y; break;
        case BOP_GE: r = x >= y; break;
        case BOP_EQ: r = x == y; break;
        case BOP_NE: r = x != y; break;
        case BOP_ULT: r = (uint32_t)x < (uint32_t)y; break;
        case BOP_AND: r = x & y; break;
        case BOP_OR: r = x | y; break;
        case BOP_XOR: r = x ^ y; break;
        default: return 0;
        }
        *out = op_imm(r, i->type);
        return 1;
    }
    if (a.type == IR_F64) {
        double x = a.fimm, y = b.fimm;
        switch (i->bop) {
        case BOP_ADD: *out = op_fimm(x + y); return 1;
        case BOP_SUB: *out = op_fimm(x - y); return 1;
        case BOP_MUL: *out = op_fimm(x * y); return 1;
        case BOP_DIV: *out = op_fimm(x / y); return 1;
        case BOP_LT: *out = op_imm(x < y, IR_I1); return 1;
        case BOP_LE: *out = op_imm(x <= y, IR_I1); return 1;
        case BOP_GT: *out = op_imm(x > y, IR_I1); return 1;
        case BOP_GE: *out = op_imm(x >= y, IR_I1); return 1;
        case BOP_EQ: *out = op_imm(x == y, IR_I1); return 1;
        case BOP_NE: *out = op_imm(x != y, IR_I1); return 1;
        default: return 0;
        }
    }
    if (a.type == IR_PTR) {     /* both null */
        if (i->bop == BOP_EQ) { *out = op_imm(1, IR_I1); return 1; }
        if (i->bop == BOP_NE) { *out = op_imm(0, IR_I1); return 1; }
    }
    return 0;
}

static int fold_instr(const IrInstr *i, Operand *out) {
    switch (i->op) {
    case IR_BIN:
        return fold_bin(i, out);
    case IR_UN:
        if (!op_is_const(i->a)) return 0;
        if (i->uop == UOP_NOT) { *out = op_imm(!i->a.imm, IR_I1); return 1; }
        if (i->a.type == IR_F64) { *out = op_fimm(-i->a.fimm); return 1; }
        *out = op_imm(wrap32(-i->a.imm), IR_I32);
        return 1;
    case IR_SITOFP:
        if (!op_is_const(i->a)) return 0;
        *out = op_fimm((double)i->a.imm);
        return 1;
    case IR_ZEXT:
        if (!op_is_const(i->a)) return 0;
        *out = op_imm(i->a.imm, IR_I32);
        return 1;
    default:
        return 0;
    }
}

static int constant_fold(IrFunc *f, OptStats *st) {
    int changed = 0;
    Operand *map = new_map(f);
    VEC_FOREACH(IrBlock *, b, f->blocks) {
        for (int k = 0; k < b->instrs->len; k++) {
            IrInstr *i = b->instrs->items[k];
            for_each_operand(i, subst_fn, map);
            Operand c;
            if (has_dst(i) && fold_instr(i, &c)) {
                map[i->dst] = c;
                vec_remove(b->instrs, k--);
                changed = 1;
                st->folded++;
            } else if (i->op == IR_CBR && op_is_const(i->a)) {
                IrBlock *taken = i->a.imm ? i->target : i->target2;
                i->op = IR_BR;
                i->a = op_none();
                i->target = taken;
                i->target2 = NULL;
                changed = 1;
                st->branches++;
            }
        }
    }
    if (changed) substitute(f, map);
    return changed;
}

/* ----------------------------------------------------- local CSE */

typedef struct {
    IrInstr *instr;     /* the expression pattern (op, operands, slot...) */
    Operand value;      /* what a matching instruction can be replaced with */
    int valid;
} AvailExpr;

static void avail_add(Vec *avail, IrInstr *pattern, Operand value) {
    AvailExpr *e = xcalloc(1, sizeof(AvailExpr));
    e->instr = pattern;
    e->value = value;
    e->valid = 1;
    vec_push(avail, e);
}

/* After `store slot <- v` a load of the same slot yields v: record a synthetic
 * LOADL pattern whose value is the stored operand (same for globals). */
static void avail_add_store(Vec *avail, const IrInstr *store) {
    IrInstr *pat = ir_instr_new(store->op == IR_STOREL ? IR_LOADL : IR_LOADG, store->line);
    pat->slot = store->slot;
    pat->name = store->name;
    pat->type = store->type;
    avail_add(avail, pat, store->a);
}

static int same_expr(const IrInstr *x, const IrInstr *y) {
    if (x->op != y->op || x->type != y->type) return 0;
    switch (x->op) {
    case IR_BIN: return x->bop == y->bop && op_equal(x->a, y->a) && op_equal(x->b, y->b);
    case IR_UN: return x->uop == y->uop && op_equal(x->a, y->a);
    case IR_SITOFP: case IR_ZEXT: return op_equal(x->a, y->a);
    case IR_LOADL: return x->slot == y->slot;
    case IR_LOADG: return strcmp(x->name, y->name) == 0;
    case IR_LOADF: return op_equal(x->a, y->a) && x->offset == y->offset;
    case IR_LOADX: return op_equal(x->a, y->a) && op_equal(x->b, y->b);
    default: return 0;
    }
}

static int local_cse(IrFunc *f, OptStats *st) {
    int changed = 0;
    Operand *map = new_map(f);
    Vec *avail = vec_new();
    VEC_FOREACH(IrBlock *, b, f->blocks) {
        avail->len = 0;
        for (int k = 0; k < b->instrs->len; k++) {
            IrInstr *i = b->instrs->items[k];
            for_each_operand(i, subst_fn, map);
            /* invalidation */
            switch (i->op) {
            case IR_STOREL:
                VEC_FOREACH(AvailExpr *, e, avail)
                    if (e->instr->op == IR_LOADL && e->instr->slot == i->slot) e->valid = 0;
                avail_add_store(avail, i);
                break;
            case IR_STOREG:
                VEC_FOREACH(AvailExpr *, e, avail)
                    if (e->instr->op == IR_LOADG && strcmp(e->instr->name, i->name) == 0) e->valid = 0;
                avail_add_store(avail, i);
                break;
            case IR_STOREF: case IR_STOREX:
                VEC_FOREACH(AvailExpr *, e, avail)
                    if (e->instr->op == IR_LOADF || e->instr->op == IR_LOADX) e->valid = 0;
                break;
            case IR_CALL: case IR_CALLIND:
                VEC_FOREACH(AvailExpr *, e, avail)
                    if (e->instr->op == IR_LOADF || e->instr->op == IR_LOADX || e->instr->op == IR_LOADG)
                        e->valid = 0;
                break;
            default:
                break;
            }
            if (!has_dst(i) || !is_pure(i)) continue;
            AvailExpr *found = NULL;
            VEC_FOREACH(AvailExpr *, e, avail)
                if (e->valid && same_expr(e->instr, i)) { found = e; break; }
            if (found) {
                map[i->dst] = found->value;
                vec_remove(b->instrs, k--);
                changed = 1;
                st->cse++;
            } else {
                avail_add(avail, i, op_temp(i->dst, i->type));
            }
        }
    }
    if (changed) substitute(f, map);
    return changed;
}

/* -------------------------------------------------------------- DCE */

static void count_fn(Operand *o, void *ctx) {
    int *uses = ctx;
    if (o->kind == OPK_TEMP) uses[o->temp]++;
}

static int dce(IrFunc *f, OptStats *st) {
    int changed = 0, again = 1;
    int nslots = f->slot_types->len;
    while (again) {
        again = 0;
        int *uses = xcalloc((size_t)(f->ntemps ? f->ntemps : 1), sizeof(int));
        char *slot_loaded = xcalloc((size_t)(nslots ? nslots : 1), 1);
        VEC_FOREACH(IrBlock *, b, f->blocks) {
            VEC_FOREACH(IrInstr *, i, b->instrs) {
                for_each_operand(i, count_fn, uses);
                if (i->op == IR_LOADL) slot_loaded[i->slot] = 1;
            }
        }
        VEC_FOREACH(IrBlock *, b, f->blocks) {
            for (int k = 0; k < b->instrs->len; k++) {
                IrInstr *i = b->instrs->items[k];
                int dead = 0;
                if (has_dst(i) && is_pure(i) && uses[i->dst] == 0) dead = 1;
                if (i->op == IR_STOREL && !slot_loaded[i->slot]) dead = 1;
                if (dead) {
                    vec_remove(b->instrs, k--);
                    changed = again = 1;
                    st->dce++;
                }
            }
        }
    }
    return changed;
}

/* ------------------------------------------------------ CFG cleanup */

static void retarget(IrFunc *f, IrBlock *from, IrBlock *to) {
    VEC_FOREACH(IrBlock *, b, f->blocks) {
        IrInstr *t = ir_block_terminator(b);
        if (!t) continue;
        if (t->target == from) t->target = to;
        if (t->target2 == from) t->target2 = to;
        if (t->op == IR_CBR && t->target == t->target2) {
            t->op = IR_BR;
            t->a = op_none();
            t->target2 = NULL;
        }
    }
}

static int simplify_cfg(IrFunc *f, OptStats *st) {
    int changed = 0;
    ir_compute_preds(f);
    /* thread empty blocks: a block holding only `br X` is replaced by X */
    for (int k = 1; k < f->blocks->len; k++) {
        IrBlock *b = f->blocks->items[k];
        if (b->instrs->len != 1) continue;
        IrInstr *t = b->instrs->items[0];
        if (t->op != IR_BR || t->target == b) continue;
        retarget(f, b, t->target);
        changed = 1;
        st->blocks++;
    }
    if (changed) ir_func_cleanup(f);
    /* merge a block into its unique predecessor when that predecessor only jumps to it */
    for (int k = 1; k < f->blocks->len; k++) {
        IrBlock *b = f->blocks->items[k];
        if (b->preds->len != 1) continue;
        IrBlock *p = b->preds->items[0];
        IrInstr *t = ir_block_terminator(p);
        if (!t || t->op != IR_BR || t->target != b || p == b) continue;
        vec_remove(p->instrs, p->instrs->len - 1);
        VEC_FOREACH(IrInstr *, i, b->instrs) vec_push(p->instrs, i);
        b->instrs->len = 0;
        retarget(f, b, p);     /* nothing else pointed at b, but keep invariants */
        vec_remove(f->blocks, k--);
        changed = 1;
        st->blocks++;
        ir_compute_preds(f);
    }
    if (changed) ir_func_cleanup(f);
    return changed;
}

/* ------------------------------------------------------------ driver */

void opt_run(IrModule *m, int level, OptStats *stats) {
    OptStats local;
    memset(&local, 0, sizeof local);
    if (!stats) stats = &local;
    VEC_FOREACH(IrFunc *, f, m->funcs) {
        ir_func_cleanup(f);
        if (level <= 0) continue;
        for (int round = 0; round < 10; round++) {
            int changed = 0;
            changed |= constant_fold(f, stats);
            ir_func_cleanup(f);
            changed |= local_cse(f, stats);
            changed |= dce(f, stats);
            changed |= simplify_cfg(f, stats);
            if (!changed) break;
        }
    }
}
