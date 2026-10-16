#include "sema.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------ scopes */

typedef struct Scope {
    StrMap *syms;
    struct Scope *parent;
} Scope;

typedef struct {
    Program *prog;
    Scope *global;
    Scope *cur;
    ClassDecl *cur_class;
    FuncDecl *cur_func;
    int loop_depth;
    StrMap *method_ids;     /* method name -> (id + 1) */
    int next_method_id;
} Sema;

static Scope *scope_push(Sema *s) {
    Scope *sc = xcalloc(1, sizeof(Scope));
    sc->syms = strmap_new();
    sc->parent = s->cur;
    s->cur = sc;
    return sc;
}

static void scope_pop(Sema *s) {
    s->cur = s->cur->parent;
}

static Symbol *sym_new(SymKind kind, const char *name) {
    Symbol *sym = xcalloc(1, sizeof(Symbol));
    sym->kind = kind;
    sym->name = (char *)name;
    return sym;
}

static const char *symkind_str(SymKind k) {
    switch (k) {
    case SYM_LOCAL: return "variable";
    case SYM_PARAM: return "parameter";
    case SYM_FIELD: return "field";
    case SYM_GLOBAL: return "variable";
    case SYM_FUNC: return "function";
    case SYM_CLASS: return "class";
    case SYM_IFACE: return "interface";
    }
    return "symbol";
}

/* Declare in the current scope; reports a redeclaration error. */
static int declare(Sema *s, Symbol *sym, int line, int col) {
    Symbol *prev = strmap_get(s->cur->syms, sym->name);
    if (prev) {
        diag_error(line, col, "redeclaration of %s '%s'", symkind_str(prev->kind), sym->name);
        return 0;
    }
    strmap_put(s->cur->syms, sym->name, sym);
    return 1;
}

/* ------------------------------------------------------------ layout */

int type_size(const Type *t) {
    switch (t->kind) {
    case TY_BOOL: return 1;
    case TY_INT: return 4;
    case TY_DOUBLE: return 8;
    default: return 8;      /* string, class, interface, array, null: pointers */
    }
}

int type_align(const Type *t) {
    return type_size(t);
}

static int align_up(int v, int a) {
    return (v + a - 1) / a * a;
}

VarDecl *class_find_field(ClassDecl *c, const char *name) {
    for (; c; c = c->parent) {
        VEC_FOREACH(VarDecl *, f, c->fields)
            if (strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

FuncDecl *class_find_method(ClassDecl *c, const char *name) {
    VEC_FOREACH(FuncDecl *, m, c->vtable)
        if (strcmp(m->name, name) == 0) return m;
    return NULL;
}

int class_is_subclass(ClassDecl *sub, ClassDecl *super) {
    for (; sub; sub = sub->parent)
        if (sub == super) return 1;
    return 0;
}

int class_implements(ClassDecl *c, IfaceDecl *i) {
    for (; c; c = c->parent) {
        VEC_FOREACH(IfaceDecl *, x, c->ifaces)
            if (x == i) return 1;
    }
    return 0;
}

static IfaceDecl *iface_find_method_decl(IfaceDecl *i, const char *name, FuncDecl **out) {
    VEC_FOREACH(FuncDecl *, m, i->methods) {
        if (strcmp(m->name, name) == 0) {
            if (out) *out = m;
            return i;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------- types */

static Type *error_type(void) {
    return type_prim(TY_ERROR);
}

static int is_error(const Type *t) {
    return !t || t->kind == TY_ERROR;
}

static int is_reference(const Type *t) {
    return t->kind == TY_NAMED || t->kind == TY_ARRAY || t->kind == TY_NULL;
}

/* Resolve named types to their declarations; returns the (possibly error) type. */
static Type *resolve_type(Sema *s, Type *t) {
    if (!t) return error_type();
    switch (t->kind) {
    case TY_NAMED: {
        if (t->cls || t->iface) return t;
        Symbol *sym = strmap_get(s->global->syms, t->name);
        if (sym && sym->kind == SYM_CLASS) {
            t->cls = sym->cls;
        } else if (sym && sym->kind == SYM_IFACE) {
            t->iface = sym->iface;
        } else {
            diag_error(t->line, t->col, "unknown type '%s'", t->name);
            return error_type();
        }
        return t;
    }
    case TY_ARRAY: {
        Type *e = resolve_type(s, t->elem);
        if (is_error(e)) return error_type();
        t->elem = e;
        return t;
    }
    default:
        return t;
    }
}

/* Can a value of type `from` be stored into a location of type `to`? */
static int assignable(const Type *to, const Type *from) {
    if (is_error(to) || is_error(from)) return 1;
    if (type_equal(to, from)) return 1;
    if (to->kind == TY_DOUBLE && from->kind == TY_INT) return 1;   /* implicit promotion */
    if (from->kind == TY_NULL) return to->kind == TY_NAMED || to->kind == TY_ARRAY;
    if (to->kind == TY_NAMED && from->kind == TY_NAMED) {
        if (to->cls && from->cls) return class_is_subclass(from->cls, to->cls);
        if (to->iface && from->cls) return class_implements(from->cls, to->iface);
    }
    return 0;
}

/* Wrap *ep in an implicit int -> double conversion when needed. Callers
 * check assignability first; this only performs the representation change. */
static void coerce(Expr **ep, const Type *to) {
    Expr *e = *ep;
    if (!e || !e->type || is_error(to) || is_error(e->type)) return;
    if (to->kind == TY_DOUBLE && e->type->kind == TY_INT) {
        Expr *c = expr_new(EX_CAST, e->line, e->col);
        c->lhs = e;
        c->type = type_prim(TY_DOUBLE);
        *ep = c;
    }
}

static int signature_equal(const FuncDecl *a, const FuncDecl *b) {
    if (!type_equal(a->ret, b->ret)) return 0;
    if (a->params->len != b->params->len) return 0;
    for (int i = 0; i < a->params->len; i++) {
        VarDecl *pa = a->params->items[i], *pb = b->params->items[i];
        if (!type_equal(pa->type, pb->type)) return 0;
    }
    return 1;
}

static int intern_method_id(Sema *s, const char *name) {
    void *v = strmap_get(s->method_ids, name);
    if (v) return (int)(intptr_t)v - 1;
    int id = s->next_method_id++;
    strmap_put(s->method_ids, name, (void *)(intptr_t)(id + 1));
    return id;
}

/* ------------------------------------------------- pass 1: globals */

static void collect_globals(Sema *s) {
    VEC_FOREACH(Decl *, d, s->prog->decls) {
        Symbol *sym = NULL;
        int line = 0, col = 0;
        switch (d->kind) {
        case DECL_VAR:
            sym = sym_new(SYM_GLOBAL, d->var->name);
            sym->var = d->var;
            line = d->var->line; col = d->var->col;
            break;
        case DECL_FUNC:
            sym = sym_new(SYM_FUNC, d->func->name);
            sym->fn = d->func;
            d->func->mangled = xstrfmt("mcc_%s", d->func->name);
            line = d->func->line; col = d->func->col;
            break;
        case DECL_CLASS:
            sym = sym_new(SYM_CLASS, d->cls->name);
            sym->cls = d->cls;
            d->cls->type = type_named(d->cls->name, d->cls->line, d->cls->col);
            d->cls->type->cls = d->cls;
            line = d->cls->line; col = d->cls->col;
            break;
        case DECL_IFACE:
            sym = sym_new(SYM_IFACE, d->iface->name);
            sym->iface = d->iface;
            d->iface->type = type_named(d->iface->name, d->iface->line, d->iface->col);
            d->iface->type->iface = d->iface;
            line = d->iface->line; col = d->iface->col;
            break;
        }
        declare(s, sym, line, col);
    }
}

/* ---------------------------------------- pass 2: class hierarchy */

static void resolve_hierarchy(Sema *s) {
    VEC_FOREACH(ClassDecl *, c, s->prog->classes) {
        if (c->parent_name) {
            Symbol *p = strmap_get(s->global->syms, c->parent_name);
            if (!p || p->kind != SYM_CLASS)
                diag_error(c->line, c->col, "class '%s' extends unknown class '%s'", c->name, c->parent_name);
            else
                c->parent = p->cls;
        }
        VEC_FOREACH(char *, iname, c->iface_names) {
            Symbol *p = strmap_get(s->global->syms, iname);
            if (!p || p->kind != SYM_IFACE)
                diag_error(c->line, c->col, "class '%s' implements unknown interface '%s'", c->name, iname);
            else
                vec_push(c->ifaces, p->iface);
        }
    }
    /* cycle detection */
    VEC_FOREACH(ClassDecl *, c, s->prog->classes) {
        ClassDecl *slow = c, *fast = c;
        while (fast && fast->parent) {
            slow = slow->parent;
            fast = fast->parent->parent;
            if (slow == fast) {
                diag_error(c->line, c->col, "inheritance cycle involving class '%s'", c->name);
                c->parent = NULL;
                break;
            }
        }
    }
}

static void resolve_signature(Sema *s, FuncDecl *f) {
    f->ret = resolve_type(s, f->ret);
    VEC_FOREACH(VarDecl *, p, f->params) p->type = resolve_type(s, p->type);
}

static void layout_class(Sema *s, ClassDecl *c) {
    if (c->layout_state == 2) return;
    if (c->layout_state == 1) return;      /* cycle already reported */
    c->layout_state = 1;

    int size = OBJECT_HEADER_SIZE;
    if (c->parent) {
        layout_class(s, c->parent);
        size = c->parent->size;
        VEC_FOREACH(FuncDecl *, m, c->parent->vtable) vec_push(c->vtable, m);
    }

    /* fields */
    VEC_FOREACH(VarDecl *, f, c->fields) {
        f->type = resolve_type(s, f->type);
        int dup = 0;
        for (int i = 0; i < c->fields->len && c->fields->items[i] != f; i++)
            if (strcmp(((VarDecl *)c->fields->items[i])->name, f->name) == 0) dup = 1;
        if (dup) {
            diag_error(f->line, f->col, "duplicate field '%s' in class '%s'", f->name, c->name);
        } else if (c->parent && class_find_field(c->parent, f->name)) {
            diag_error(f->line, f->col, "field '%s' is already declared in a superclass of '%s'", f->name, c->name);
        } else if (class_find_method(c, f->name)) {
            diag_error(f->line, f->col, "field '%s' conflicts with an inherited method", f->name);
        }
        if (is_error(f->type)) continue;
        int sz = type_size(f->type);
        size = align_up(size, type_align(f->type));
        f->offset = size;
        size += sz;
    }
    c->size = align_up(size, 8);

    /* methods */
    VEC_FOREACH(FuncDecl *, m, c->methods) {
        resolve_signature(s, m);
        m->mangled = xstrfmt("mcc_%s__%s", c->name, m->name);
        m->method_id = intern_method_id(s, m->name);
        int dup = 0;
        for (int i = 0; i < c->methods->len && c->methods->items[i] != m; i++)
            if (strcmp(((FuncDecl *)c->methods->items[i])->name, m->name) == 0) dup = 1;
        if (dup) {
            diag_error(m->line, m->col, "duplicate method '%s' in class '%s'", m->name, c->name);
            continue;
        }
        if (class_find_field(c, m->name)) {
            diag_error(m->line, m->col, "method '%s' conflicts with a field of the same name", m->name);
            continue;
        }
        FuncDecl *inherited = class_find_method(c, m->name);
        if (inherited) {
            if (!signature_equal(inherited, m))
                diag_error(m->line, m->col,
                           "method '%s' overrides '%s.%s' with a different signature",
                           m->name, inherited->owner->name, m->name);
            m->vslot = inherited->vslot;
            vec_set(c->vtable, m->vslot, m);
        } else {
            m->vslot = c->vtable->len;
            vec_push(c->vtable, m);
        }
    }

    /* interface conformance */
    VEC_FOREACH(IfaceDecl *, i, c->ifaces) {
        VEC_FOREACH(FuncDecl *, proto, i->methods) {
            FuncDecl *impl = class_find_method(c, proto->name);
            if (!impl)
                diag_error(c->line, c->col, "class '%s' does not implement method '%s' of interface '%s'",
                           c->name, proto->name, i->name);
            else if (!signature_equal(impl, proto))
                diag_error(impl->line, impl->col,
                           "method '%s' does not match the signature declared in interface '%s'",
                           impl->name, i->name);
        }
    }
    c->layout_state = 2;
}

static void layout_all(Sema *s) {
    VEC_FOREACH(IfaceDecl *, i, s->prog->ifaces) {
        VEC_FOREACH(FuncDecl *, m, i->methods) {
            resolve_signature(s, m);
            m->method_id = intern_method_id(s, m->name);
            int dup = 0;
            for (int k = 0; k < i->methods->len && i->methods->items[k] != m; k++)
                if (strcmp(((FuncDecl *)i->methods->items[k])->name, m->name) == 0) dup = 1;
            if (dup) diag_error(m->line, m->col, "duplicate method '%s' in interface '%s'", m->name, i->name);
        }
    }
    VEC_FOREACH(ClassDecl *, c, s->prog->classes) layout_class(s, c);
    VEC_FOREACH(FuncDecl *, f, s->prog->funcs) resolve_signature(s, f);
    VEC_FOREACH(VarDecl *, g, s->prog->globals) g->type = resolve_type(s, g->type);
}

/* ------------------------------------------------- pass 3: bodies */

static Type *check_expr(Sema *s, Expr *e);

static Symbol *lookup_var(Sema *s, const char *name) {
    for (Scope *sc = s->cur; sc && sc != s->global; sc = sc->parent) {
        Symbol *sym = strmap_get(sc->syms, name);
        if (sym) return sym;
    }
    if (s->cur_class) {
        VarDecl *f = class_find_field(s->cur_class, name);
        if (f) {
            Symbol *sym = sym_new(SYM_FIELD, f->name);
            sym->var = f;
            sym->type = f->type;
            return sym;
        }
    }
    return strmap_get(s->global->syms, name);
}

static int is_lvalue(const Expr *e) {
    if (e->kind == EX_IDENT)
        return e->sym && (e->sym->kind == SYM_LOCAL || e->sym->kind == SYM_PARAM ||
                          e->sym->kind == SYM_FIELD || e->sym->kind == SYM_GLOBAL);
    return e->kind == EX_FIELD || e->kind == EX_INDEX;
}

static void check_call_args(Sema *s, Expr *e, FuncDecl *fn, const char *what) {
    if (e->args->len != fn->params->len) {
        diag_error(e->line, e->col, "%s '%s' expects %d argument%s but %d %s given",
                   what, fn->name, fn->params->len, fn->params->len == 1 ? "" : "s",
                   e->args->len, e->args->len == 1 ? "was" : "were");
    }
    for (int i = 0; i < e->args->len; i++) {
        Expr *a = e->args->items[i];
        Type *at = check_expr(s, a);
        if (i < fn->params->len) {
            VarDecl *p = fn->params->items[i];
            if (!assignable(p->type, at))
                diag_error(a->line, a->col, "argument %d of '%s' has type %s, expected %s",
                           i + 1, fn->name, type_str(at), type_str(p->type));
            else
                coerce((Expr **)&e->args->items[i], p->type);
        }
    }
}

static Type *check_binary(Sema *s, Expr *e) {
    Type *l = check_expr(s, e->lhs);
    Type *r = check_expr(s, e->rhs);
    if (is_error(l) || is_error(r)) {
        switch (e->binop) {
        case BIN_LT: case BIN_LE: case BIN_GT: case BIN_GE: case BIN_EQ: case BIN_NE:
        case BIN_AND: case BIN_OR:
            return type_prim(TY_BOOL);
        default:
            return error_type();
        }
    }
    int numeric = (l->kind == TY_INT || l->kind == TY_DOUBLE) && (r->kind == TY_INT || r->kind == TY_DOUBLE);
    int mixed = numeric && l->kind != r->kind;
    switch (e->binop) {
    case BIN_ADD:
        if (l->kind == TY_STRING && r->kind == TY_STRING) return type_prim(TY_STRING);
        /* fall through */
    case BIN_SUB: case BIN_MUL: case BIN_DIV:
        if (l->kind == TY_INT && r->kind == TY_INT) return type_prim(TY_INT);
        if (mixed) { coerce(&e->lhs, type_prim(TY_DOUBLE)); coerce(&e->rhs, type_prim(TY_DOUBLE)); }
        if (numeric) return type_prim(TY_DOUBLE);
        break;
    case BIN_MOD:
        if (l->kind == TY_INT && r->kind == TY_INT) return type_prim(TY_INT);
        break;
    case BIN_LT: case BIN_LE: case BIN_GT: case BIN_GE:
        if (mixed) { coerce(&e->lhs, type_prim(TY_DOUBLE)); coerce(&e->rhs, type_prim(TY_DOUBLE)); }
        if (numeric) return type_prim(TY_BOOL);
        break;
    case BIN_EQ: case BIN_NE:
        if (mixed) { coerce(&e->lhs, type_prim(TY_DOUBLE)); coerce(&e->rhs, type_prim(TY_DOUBLE)); return type_prim(TY_BOOL); }
        if (type_equal(l, r) && l->kind != TY_VOID) return type_prim(TY_BOOL);
        if (is_reference(l) && is_reference(r) && (assignable(l, r) || assignable(r, l)))
            return type_prim(TY_BOOL);
        break;
    case BIN_AND: case BIN_OR:
        if (l->kind == TY_BOOL && r->kind == TY_BOOL) return type_prim(TY_BOOL);
        break;
    }
    diag_error(e->line, e->col, "invalid operands to binary '%s' (%s and %s)",
               binop_str(e->binop), type_str(l), type_str(r));
    switch (e->binop) {
    case BIN_LT: case BIN_LE: case BIN_GT: case BIN_GE: case BIN_EQ: case BIN_NE:
    case BIN_AND: case BIN_OR:
        return type_prim(TY_BOOL);
    default:
        return error_type();
    }
}

static Type *check_expr(Sema *s, Expr *e) {
    Type *t = error_type();
    switch (e->kind) {
    case EX_INT_LIT: t = type_prim(TY_INT); break;
    case EX_DOUBLE_LIT: t = type_prim(TY_DOUBLE); break;
    case EX_BOOL_LIT: t = type_prim(TY_BOOL); break;
    case EX_STRING_LIT: t = type_prim(TY_STRING); break;
    case EX_NULL_LIT: t = type_prim(TY_NULL); break;

    case EX_IDENT: {
        Symbol *sym = lookup_var(s, e->name);
        if (!sym) {
            diag_error(e->line, e->col, "use of undeclared identifier '%s'", e->name);
        } else if (sym->kind == SYM_FUNC || sym->kind == SYM_CLASS || sym->kind == SYM_IFACE) {
            diag_error(e->line, e->col, "%s '%s' used as a value", symkind_str(sym->kind), e->name);
        } else {
            e->sym = sym;
            e->field = sym->kind == SYM_FIELD ? sym->var : NULL;
            t = sym->var->type;
        }
        break;
    }

    case EX_THIS:
        if (!s->cur_class) diag_error(e->line, e->col, "'this' used outside of a class method");
        else t = s->cur_class->type;
        break;

    case EX_BINARY:
        t = check_binary(s, e);
        break;

    case EX_UNARY: {
        Type *o = check_expr(s, e->lhs);
        if (is_error(o)) { t = e->unop == UN_NOT ? type_prim(TY_BOOL) : o; break; }
        if (e->unop == UN_NEG && (o->kind == TY_INT || o->kind == TY_DOUBLE)) t = o;
        else if (e->unop == UN_NOT && o->kind == TY_BOOL) t = o;
        else {
            diag_error(e->line, e->col, "invalid operand to unary '%s' (%s)",
                       e->unop == UN_NEG ? "-" : "!", type_str(o));
            if (e->unop == UN_NOT) t = type_prim(TY_BOOL);
        }
        break;
    }

    case EX_ASSIGN: {
        Type *l = check_expr(s, e->lhs);
        Type *r = check_expr(s, e->rhs);
        if (!is_error(l) && !is_lvalue(e->lhs)) {
            diag_error(e->lhs->line, e->lhs->col, "expression is not assignable");
            l = error_type();
        }
        if (e->is_compound) {
            if (!is_error(l) && !is_error(r)) {
                int ok = (l->kind == TY_INT && r->kind == TY_INT) ||
                         (l->kind == TY_DOUBLE && (r->kind == TY_DOUBLE || r->kind == TY_INT) && e->binop != BIN_MOD) ||
                         (l->kind == TY_STRING && r->kind == TY_STRING && e->binop == BIN_ADD);
                if (!ok)
                    diag_error(e->line, e->col, "invalid operands to '%s=' (%s and %s)",
                               binop_str(e->binop), type_str(l), type_str(r));
                else
                    coerce(&e->rhs, l);
            }
        } else if (!assignable(l, r)) {
            diag_error(e->line, e->col, "cannot assign a value of type %s to a variable of type %s",
                       type_str(r), type_str(l));
        } else {
            coerce(&e->rhs, l);
        }
        t = l;
        break;
    }

    case EX_INCDEC: {
        Type *o = check_expr(s, e->lhs);
        if (!is_error(o)) {
            if (!is_lvalue(e->lhs))
                diag_error(e->lhs->line, e->lhs->col, "operand of '%s' is not assignable",
                           e->is_increment ? "++" : "--");
            else if (o->kind != TY_INT)
                diag_error(e->line, e->col, "operand of '%s' has type %s, expected int",
                           e->is_increment ? "++" : "--", type_str(o));
        }
        t = type_prim(TY_INT);
        break;
    }

    case EX_FIELD: {
        Type *o = check_expr(s, e->lhs);
        if (is_error(o)) break;
        if (o->kind != TY_NAMED || !o->cls) {
            diag_error(e->line, e->col, "member access on a value of type %s, which is not a class", type_str(o));
            break;
        }
        VarDecl *f = class_find_field(o->cls, e->name);
        if (!f) {
            diag_error(e->line, e->col, "class '%s' has no field named '%s'", o->cls->name, e->name);
            break;
        }
        e->field = f;
        t = f->type;
        break;
    }

    case EX_INDEX: {
        Type *a = check_expr(s, e->lhs);
        Type *i = check_expr(s, e->rhs);
        if (!is_error(i) && i->kind != TY_INT)
            diag_error(e->rhs->line, e->rhs->col, "array index has type %s, expected int", type_str(i));
        if (is_error(a)) break;
        if (a->kind != TY_ARRAY) {
            diag_error(e->line, e->col, "subscripted value of type %s is not an array", type_str(a));
            break;
        }
        t = a->elem;
        break;
    }

    case EX_CALL: {
        FuncDecl *fn = NULL;
        if (s->cur_class) fn = class_find_method(s->cur_class, e->name);   /* implicit this.m() */
        if (!fn) {
            Symbol *sym = strmap_get(s->global->syms, e->name);
            if (sym && sym->kind == SYM_FUNC) fn = sym->fn;
            else if (sym) {
                diag_error(e->line, e->col, "'%s' is a %s, not a function", e->name, symkind_str(sym->kind));
                VEC_FOREACH(Expr *, a, e->args) check_expr(s, a);
                break;
            }
        }
        if (!fn) {
            diag_error(e->line, e->col, "call to undeclared function '%s'", e->name);
            VEC_FOREACH(Expr *, a, e->args) check_expr(s, a);
            break;
        }
        e->fn = fn;
        check_call_args(s, e, fn, fn->owner ? "method" : "function");
        t = fn->ret;
        break;
    }

    case EX_METHOD_CALL: {
        Type *o = check_expr(s, e->lhs);
        if (is_error(o)) { VEC_FOREACH(Expr *, a, e->args) check_expr(s, a); break; }
        if (o->kind == TY_ARRAY) {
            if (strcmp(e->name, "length") != 0) {
                diag_error(e->line, e->col, "arrays have no method named '%s'", e->name);
            } else if (e->args->len != 0) {
                diag_error(e->line, e->col, "length() takes no arguments");
            } else {
                e->is_array_length = 1;
                t = type_prim(TY_INT);
            }
            VEC_FOREACH(Expr *, a, e->args) check_expr(s, a);
            break;
        }
        if (o->kind != TY_NAMED) {
            diag_error(e->line, e->col, "method call on a value of type %s", type_str(o));
            VEC_FOREACH(Expr *, a, e->args) check_expr(s, a);
            break;
        }
        FuncDecl *fn = NULL;
        if (o->cls) {
            fn = class_find_method(o->cls, e->name);
            if (!fn) diag_error(e->line, e->col, "class '%s' has no method named '%s'", o->cls->name, e->name);
        } else {
            iface_find_method_decl(o->iface, e->name, &fn);
            if (!fn) diag_error(e->line, e->col, "interface '%s' has no method named '%s'", o->iface->name, e->name);
            e->via_interface = 1;
        }
        if (!fn) { VEC_FOREACH(Expr *, a, e->args) check_expr(s, a); break; }
        e->fn = fn;
        check_call_args(s, e, fn, "method");
        t = fn->ret;
        break;
    }

    case EX_NEW: {
        Symbol *sym = strmap_get(s->global->syms, e->name);
        if (!sym || (sym->kind != SYM_CLASS && sym->kind != SYM_IFACE))
            diag_error(e->line, e->col, "New: unknown class '%s'", e->name);
        else if (sym->kind == SYM_IFACE)
            diag_error(e->line, e->col, "cannot instantiate interface '%s'", e->name);
        else
            t = sym->cls->type;
        break;
    }

    case EX_NEW_ARRAY: {
        Type *n = check_expr(s, e->lhs);
        if (!is_error(n) && n->kind != TY_INT)
            diag_error(e->lhs->line, e->lhs->col, "NewArray size has type %s, expected int", type_str(n));
        Type *elem = resolve_type(s, e->elem_type);
        if (is_error(elem)) break;
        e->elem_type = elem;
        t = type_array(elem, e->line, e->col);
        break;
    }

    case EX_READ_INTEGER: t = type_prim(TY_INT); break;
    case EX_READ_LINE: t = type_prim(TY_STRING); break;
    case EX_CAST: t = e->type; break;
    }
    e->type = t;
    return t;
}

static void check_condition(Sema *s, Expr *cond, const char *what) {
    Type *t = check_expr(s, cond);
    if (!is_error(t) && t->kind != TY_BOOL)
        diag_error(cond->line, cond->col, "%s condition has type %s, expected bool", what, type_str(t));
}

static void declare_local(Sema *s, VarDecl *v, SymKind kind) {
    v->type = resolve_type(s, v->type);
    if (v->init) {
        Type *it = check_expr(s, v->init);
        if (!assignable(v->type, it))
            diag_error(v->init->line, v->init->col,
                       "cannot initialize a variable of type %s with a value of type %s",
                       type_str(v->type), type_str(it));
        else
            coerce(&v->init, v->type);
    }
    Symbol *sym = sym_new(kind, v->name);
    sym->var = v;
    sym->type = v->type;
    declare(s, sym, v->line, v->col);
}

static void check_stmt(Sema *s, Stmt *st);

static void check_block(Sema *s, Stmt *b, int new_scope) {
    if (new_scope) scope_push(s);
    VEC_FOREACH(Stmt *, c, b->stmts) check_stmt(s, c);
    if (new_scope) scope_pop(s);
}

static void check_stmt(Sema *s, Stmt *st) {
    if (!st) return;
    switch (st->kind) {
    case ST_EXPR:
        check_expr(s, st->expr);
        break;
    case ST_VARDECL:
        declare_local(s, st->var, SYM_LOCAL);
        break;
    case ST_IF:
        check_condition(s, st->expr, "if");
        check_stmt(s, st->body);
        check_stmt(s, st->else_body);
        break;
    case ST_WHILE:
        check_condition(s, st->expr, "while");
        s->loop_depth++;
        check_stmt(s, st->body);
        s->loop_depth--;
        break;
    case ST_FOR:
        scope_push(s);
        check_stmt(s, st->init);
        if (st->expr) check_condition(s, st->expr, "for");
        if (st->step) check_expr(s, st->step);
        s->loop_depth++;
        check_stmt(s, st->body);
        s->loop_depth--;
        scope_pop(s);
        break;
    case ST_BREAK:
        if (s->loop_depth == 0) diag_error(st->line, st->col, "'break' is not inside a loop");
        break;
    case ST_CONTINUE:
        if (s->loop_depth == 0) diag_error(st->line, st->col, "'continue' is not inside a loop");
        break;
    case ST_RETURN: {
        Type *rt = s->cur_func->ret;
        if (st->expr) {
            Type *t = check_expr(s, st->expr);
            if (rt->kind == TY_VOID)
                diag_error(st->line, st->col, "void function '%s' should not return a value", s->cur_func->name);
            else if (!assignable(rt, t))
                diag_error(st->expr->line, st->expr->col, "returning %s from a function with return type %s",
                           type_str(t), type_str(rt));
            else
                coerce(&st->expr, rt);
        } else if (rt->kind != TY_VOID && !is_error(rt)) {
            diag_error(st->line, st->col, "non-void function '%s' should return a value", s->cur_func->name);
        }
        break;
    }
    case ST_PRINT:
        VEC_FOREACH(Expr *, a, st->args) {
            Type *t = check_expr(s, a);
            if (is_error(t)) continue;
            if (t->kind != TY_INT && t->kind != TY_BOOL && t->kind != TY_STRING && t->kind != TY_DOUBLE)
                diag_error(a->line, a->col, "Print cannot print a value of type %s", type_str(t));
        }
        break;
    case ST_BLOCK:
        check_block(s, st, 1);
        break;
    }
}

/* Conservative "does every path through this statement return?" analysis. */
static int stmt_returns(const Stmt *st) {
    if (!st) return 0;
    switch (st->kind) {
    case ST_RETURN: return 1;
    case ST_BLOCK:
        VEC_FOREACH(Stmt *, c, st->stmts) if (stmt_returns(c)) return 1;
        return 0;
    case ST_IF:
        return st->else_body && stmt_returns(st->body) && stmt_returns(st->else_body);
    default:
        return 0;
    }
}

static void check_function(Sema *s, FuncDecl *f) {
    s->cur_func = f;
    s->loop_depth = 0;
    scope_push(s);
    VEC_FOREACH(VarDecl *, p, f->params) {
        Symbol *sym = sym_new(SYM_PARAM, p->name);
        sym->var = p;
        sym->type = p->type;
        declare(s, sym, p->line, p->col);
    }
    check_block(s, f->body, 0);     /* parameters and top-level locals share a scope */
    scope_pop(s);
    if (f->ret->kind != TY_VOID && !is_error(f->ret) && !stmt_returns(f->body))
        diag_error(f->line, f->col, "control reaches end of non-void function '%s'", f->name);
    s->cur_func = NULL;
}

static void check_bodies(Sema *s) {
    VEC_FOREACH(VarDecl *, g, s->prog->globals) {
        if (g->init) {
            Type *it = check_expr(s, g->init);
            if (!assignable(g->type, it))
                diag_error(g->init->line, g->init->col,
                           "cannot initialize a variable of type %s with a value of type %s",
                           type_str(g->type), type_str(it));
            else
                coerce(&g->init, g->type);
        }
    }
    VEC_FOREACH(FuncDecl *, f, s->prog->funcs) check_function(s, f);
    VEC_FOREACH(ClassDecl *, c, s->prog->classes) {
        s->cur_class = c;
        VEC_FOREACH(FuncDecl *, m, c->methods) check_function(s, m);
        s->cur_class = NULL;
    }
}

static void check_main(Sema *s) {
    Symbol *sym = strmap_get(s->global->syms, "main");
    if (!sym || sym->kind != SYM_FUNC) {
        diag_error(0, 0, "program has no 'main' function");
        return;
    }
    FuncDecl *m = sym->fn;
    if (m->params->len != 0)
        diag_error(m->line, m->col, "'main' must not take parameters");
    if (m->ret->kind != TY_VOID && m->ret->kind != TY_INT)
        diag_error(m->line, m->col, "'main' must return void or int");
}

int sema_check(Program *prog) {
    Sema s;
    memset(&s, 0, sizeof s);
    s.prog = prog;
    s.method_ids = strmap_new();
    s.global = scope_push(&s);

    int before = g_error_count;
    collect_globals(&s);
    resolve_hierarchy(&s);
    layout_all(&s);
    check_bodies(&s);
    check_main(&s);
    return g_error_count - before;
}
