#include "ast.h"

#include <string.h>

/* ------------------------------------------------------------------ types */

static Type prim_types[] = {
    { TY_INT, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_DOUBLE, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_BOOL, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_STRING, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_VOID, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_NULL, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_NAMED, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_ARRAY, NULL, NULL, NULL, NULL, 0, 0 },
    { TY_ERROR, NULL, NULL, NULL, NULL, 0, 0 },
};

Type *type_prim(TypeKind kind) {
    return &prim_types[kind];
}

Type *type_new(TypeKind kind, int line, int col) {
    Type *t = xcalloc(1, sizeof(Type));
    t->kind = kind;
    t->line = line;
    t->col = col;
    return t;
}

Type *type_named(char *name, int line, int col) {
    Type *t = type_new(TY_NAMED, line, col);
    t->name = name;
    return t;
}

Type *type_array(Type *elem, int line, int col) {
    Type *t = type_new(TY_ARRAY, line, col);
    t->elem = elem;
    return t;
}

int type_equal(const Type *a, const Type *b) {
    if (a == b) return 1;
    if (!a || !b || a->kind != b->kind) return 0;
    switch (a->kind) {
    case TY_NAMED: return strcmp(a->name, b->name) == 0;
    case TY_ARRAY: return type_equal(a->elem, b->elem);
    default: return 1;
    }
}

const char *type_str(const Type *t) {
    if (!t) return "<none>";
    switch (t->kind) {
    case TY_INT: return "int";
    case TY_DOUBLE: return "double";
    case TY_BOOL: return "bool";
    case TY_STRING: return "string";
    case TY_VOID: return "void";
    case TY_NULL: return "null";
    case TY_NAMED: return t->name;
    case TY_ARRAY: return xstrfmt("%s[]", type_str(t->elem));
    case TY_ERROR: return "<error>";
    }
    return "?";
}

/* ------------------------------------------------------------ expressions */

Expr *expr_new(ExprKind kind, int line, int col) {
    Expr *e = xcalloc(1, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    e->col = col;
    return e;
}

Expr *expr_int(int v, int line, int col) {
    Expr *e = expr_new(EX_INT_LIT, line, col);
    e->ival = v;
    return e;
}

Expr *expr_double(double v, int line, int col) {
    Expr *e = expr_new(EX_DOUBLE_LIT, line, col);
    e->dval = v;
    return e;
}

Expr *expr_bool(int v, int line, int col) {
    Expr *e = expr_new(EX_BOOL_LIT, line, col);
    e->ival = v;
    return e;
}

Expr *expr_string(char *s, int line, int col) {
    Expr *e = expr_new(EX_STRING_LIT, line, col);
    e->sval = s;
    return e;
}

Expr *expr_ident(char *name, int line, int col) {
    Expr *e = expr_new(EX_IDENT, line, col);
    e->name = name;
    return e;
}

Expr *expr_binary(BinOp op, Expr *l, Expr *r, int line, int col) {
    Expr *e = expr_new(EX_BINARY, line, col);
    e->binop = op;
    e->lhs = l;
    e->rhs = r;
    return e;
}

Expr *expr_unary(UnOp op, Expr *x, int line, int col) {
    Expr *e = expr_new(EX_UNARY, line, col);
    e->unop = op;
    e->lhs = x;
    return e;
}

Expr *expr_assign(Expr *l, Expr *r, int compound, BinOp op, int line, int col) {
    Expr *e = expr_new(EX_ASSIGN, line, col);
    e->lhs = l;
    e->rhs = r;
    e->is_compound = compound;
    e->binop = op;
    return e;
}

Expr *expr_incdec(Expr *target, int is_prefix, int is_increment, int line, int col) {
    Expr *e = expr_new(EX_INCDEC, line, col);
    e->lhs = target;
    e->is_prefix = is_prefix;
    e->is_increment = is_increment;
    return e;
}

Expr *expr_field(Expr *obj, char *name, int line, int col) {
    Expr *e = expr_new(EX_FIELD, line, col);
    e->lhs = obj;
    e->name = name;
    return e;
}

Expr *expr_index(Expr *arr, Expr *idx, int line, int col) {
    Expr *e = expr_new(EX_INDEX, line, col);
    e->lhs = arr;
    e->rhs = idx;
    return e;
}

Expr *expr_call(char *name, Vec *args, int line, int col) {
    Expr *e = expr_new(EX_CALL, line, col);
    e->name = name;
    e->args = args ? args : vec_new();
    return e;
}

Expr *expr_method_call(Expr *obj, char *name, Vec *args, int line, int col) {
    Expr *e = expr_new(EX_METHOD_CALL, line, col);
    e->lhs = obj;
    e->name = name;
    e->args = args ? args : vec_new();
    return e;
}

Expr *expr_new_obj(char *cls, int line, int col) {
    Expr *e = expr_new(EX_NEW, line, col);
    e->name = cls;
    return e;
}

Expr *expr_new_array(Expr *n, Type *elem, int line, int col) {
    Expr *e = expr_new(EX_NEW_ARRAY, line, col);
    e->lhs = n;
    e->elem_type = elem;
    return e;
}

const char *binop_str(BinOp op) {
    static const char *names[] = {
        "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "||"
    };
    return names[op];
}

/* ------------------------------------------------------------- statements */

Stmt *stmt_new(StmtKind kind, int line, int col) {
    Stmt *s = xcalloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    s->col = col;
    return s;
}

/* ----------------------------------------------------------- declarations */

VarDecl *vardecl_new(Type *type, char *name, Expr *init, int line, int col) {
    VarDecl *v = xcalloc(1, sizeof(VarDecl));
    v->type = type;
    v->name = name;
    v->init = init;
    v->line = line;
    v->col = col;
    v->slot = -1;
    return v;
}

FuncDecl *funcdecl_new(Type *ret, char *name, Vec *params, Stmt *body, int line, int col) {
    FuncDecl *f = xcalloc(1, sizeof(FuncDecl));
    f->ret = ret;
    f->name = name;
    f->params = params ? params : vec_new();
    f->body = body;
    f->line = line;
    f->col = col;
    f->vslot = -1;
    f->method_id = -1;
    return f;
}

ClassDecl *classdecl_new(char *name, char *parent, Vec *ifaces, Vec *members, int line, int col) {
    ClassDecl *c = xcalloc(1, sizeof(ClassDecl));
    c->name = name;
    c->parent_name = parent;
    c->iface_names = ifaces ? ifaces : vec_new();
    c->fields = vec_new();
    c->methods = vec_new();
    c->ifaces = vec_new();
    c->vtable = vec_new();
    c->line = line;
    c->col = col;
    if (members) {
        VEC_FOREACH(Decl *, d, members) {
            if (d->kind == DECL_VAR) {
                d->var->owner = c;
                vec_push(c->fields, d->var);
            } else if (d->kind == DECL_FUNC) {
                d->func->owner = c;
                vec_push(c->methods, d->func);
            }
        }
    }
    return c;
}

IfaceDecl *ifacedecl_new(char *name, Vec *protos, int line, int col) {
    IfaceDecl *i = xcalloc(1, sizeof(IfaceDecl));
    i->name = name;
    i->methods = protos ? protos : vec_new();
    i->line = line;
    i->col = col;
    VEC_FOREACH(FuncDecl *, f, i->methods) f->iface_owner = i;
    return i;
}

Decl *decl_var(VarDecl *v) {
    Decl *d = xcalloc(1, sizeof(Decl));
    d->kind = DECL_VAR;
    d->var = v;
    return d;
}

Decl *decl_func(FuncDecl *f) {
    Decl *d = xcalloc(1, sizeof(Decl));
    d->kind = DECL_FUNC;
    d->func = f;
    return d;
}

Decl *decl_class(ClassDecl *c) {
    Decl *d = xcalloc(1, sizeof(Decl));
    d->kind = DECL_CLASS;
    d->cls = c;
    return d;
}

Decl *decl_iface(IfaceDecl *i) {
    Decl *d = xcalloc(1, sizeof(Decl));
    d->kind = DECL_IFACE;
    d->iface = i;
    return d;
}

Program *program_new(Vec *decls) {
    Program *p = xcalloc(1, sizeof(Program));
    p->decls = decls ? decls : vec_new();
    p->globals = vec_new();
    p->funcs = vec_new();
    p->classes = vec_new();
    p->ifaces = vec_new();
    VEC_FOREACH(Decl *, d, p->decls) {
        switch (d->kind) {
        case DECL_VAR: d->var->is_global = 1; vec_push(p->globals, d->var); break;
        case DECL_FUNC: vec_push(p->funcs, d->func); break;
        case DECL_CLASS: vec_push(p->classes, d->cls); break;
        case DECL_IFACE: vec_push(p->ifaces, d->iface); break;
        }
    }
    return p;
}

/* ------------------------------------------------------------------- dump */

static void indent(FILE *out, int n) {
    for (int i = 0; i < n; i++) fputs("  ", out);
}

static void dump_string_lit(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
        case '\n': fputs("\\n", out); break;
        case '\t': fputs("\\t", out); break;
        case '\\': fputs("\\\\", out); break;
        case '"': fputs("\\\"", out); break;
        default: fputc(*s, out);
        }
    }
    fputc('"', out);
}

static void dump_expr(const Expr *e, FILE *out, int depth) {
    indent(out, depth);
    if (!e) {
        fputs("<null-expr>\n", out);
        return;
    }
    fprintf(out, "%d:%d ", e->line, e->col);
    switch (e->kind) {
    case EX_INT_LIT: fprintf(out, "IntLit %d\n", e->ival); break;
    case EX_DOUBLE_LIT: fprintf(out, "DoubleLit %g\n", e->dval); break;
    case EX_BOOL_LIT: fprintf(out, "BoolLit %s\n", e->ival ? "true" : "false"); break;
    case EX_STRING_LIT: fputs("StringLit ", out); dump_string_lit(out, e->sval); fputc('\n', out); break;
    case EX_NULL_LIT: fputs("NullLit\n", out); break;
    case EX_IDENT: fprintf(out, "Ident %s\n", e->name); break;
    case EX_THIS: fputs("This\n", out); break;
    case EX_BINARY:
        fprintf(out, "Binary %s\n", binop_str(e->binop));
        dump_expr(e->lhs, out, depth + 1);
        dump_expr(e->rhs, out, depth + 1);
        break;
    case EX_UNARY:
        fprintf(out, "Unary %s\n", e->unop == UN_NEG ? "-" : "!");
        dump_expr(e->lhs, out, depth + 1);
        break;
    case EX_ASSIGN:
        if (e->is_compound) fprintf(out, "Assign %s=\n", binop_str(e->binop));
        else fputs("Assign\n", out);
        dump_expr(e->lhs, out, depth + 1);
        dump_expr(e->rhs, out, depth + 1);
        break;
    case EX_INCDEC:
        fprintf(out, "%s%s\n", e->is_prefix ? "Pre" : "Post", e->is_increment ? "Increment" : "Decrement");
        dump_expr(e->lhs, out, depth + 1);
        break;
    case EX_FIELD:
        fprintf(out, "Field .%s\n", e->name);
        dump_expr(e->lhs, out, depth + 1);
        break;
    case EX_INDEX:
        fputs("Index\n", out);
        dump_expr(e->lhs, out, depth + 1);
        dump_expr(e->rhs, out, depth + 1);
        break;
    case EX_CALL:
        fprintf(out, "Call %s\n", e->name);
        VEC_FOREACH(Expr *, a, e->args) dump_expr(a, out, depth + 1);
        break;
    case EX_METHOD_CALL:
        fprintf(out, "MethodCall .%s\n", e->name);
        dump_expr(e->lhs, out, depth + 1);
        VEC_FOREACH(Expr *, a, e->args) dump_expr(a, out, depth + 1);
        break;
    case EX_NEW: fprintf(out, "New %s\n", e->name); break;
    case EX_NEW_ARRAY:
        fprintf(out, "NewArray %s\n", type_str(e->elem_type));
        dump_expr(e->lhs, out, depth + 1);
        break;
    case EX_READ_INTEGER: fputs("ReadInteger\n", out); break;
    case EX_READ_LINE: fputs("ReadLine\n", out); break;
    case EX_CAST:
        fprintf(out, "Cast %s\n", type_str(e->type));
        dump_expr(e->lhs, out, depth + 1);
        break;
    }
}

static void dump_vardecl(const VarDecl *v, FILE *out, int depth) {
    indent(out, depth);
    fprintf(out, "%d:%d VarDecl %s : %s\n", v->line, v->col, v->name, type_str(v->type));
    if (v->init) dump_expr(v->init, out, depth + 1);
}

static void dump_stmt(const Stmt *s, FILE *out, int depth) {
    if (!s) {
        indent(out, depth);
        fputs("<null-stmt>\n", out);
        return;
    }
    switch (s->kind) {
    case ST_EXPR:
        indent(out, depth); fprintf(out, "%d:%d ExprStmt\n", s->line, s->col);
        dump_expr(s->expr, out, depth + 1);
        break;
    case ST_VARDECL:
        dump_vardecl(s->var, out, depth);
        break;
    case ST_IF:
        indent(out, depth); fprintf(out, "%d:%d If\n", s->line, s->col);
        dump_expr(s->expr, out, depth + 1);
        indent(out, depth + 1); fputs("Then\n", out);
        dump_stmt(s->body, out, depth + 2);
        if (s->else_body) {
            indent(out, depth + 1); fputs("Else\n", out);
            dump_stmt(s->else_body, out, depth + 2);
        }
        break;
    case ST_WHILE:
        indent(out, depth); fprintf(out, "%d:%d While\n", s->line, s->col);
        dump_expr(s->expr, out, depth + 1);
        dump_stmt(s->body, out, depth + 1);
        break;
    case ST_FOR:
        indent(out, depth); fprintf(out, "%d:%d For\n", s->line, s->col);
        indent(out, depth + 1); fputs("Init\n", out);
        if (s->init) dump_stmt(s->init, out, depth + 2);
        indent(out, depth + 1); fputs("Cond\n", out);
        if (s->expr) dump_expr(s->expr, out, depth + 2);
        indent(out, depth + 1); fputs("Step\n", out);
        if (s->step) dump_expr(s->step, out, depth + 2);
        indent(out, depth + 1); fputs("Body\n", out);
        dump_stmt(s->body, out, depth + 2);
        break;
    case ST_BREAK: indent(out, depth); fprintf(out, "%d:%d Break\n", s->line, s->col); break;
    case ST_CONTINUE: indent(out, depth); fprintf(out, "%d:%d Continue\n", s->line, s->col); break;
    case ST_RETURN:
        indent(out, depth); fprintf(out, "%d:%d Return\n", s->line, s->col);
        if (s->expr) dump_expr(s->expr, out, depth + 1);
        break;
    case ST_PRINT:
        indent(out, depth); fprintf(out, "%d:%d Print\n", s->line, s->col);
        VEC_FOREACH(Expr *, a, s->args) dump_expr(a, out, depth + 1);
        break;
    case ST_BLOCK:
        indent(out, depth); fprintf(out, "%d:%d Block\n", s->line, s->col);
        VEC_FOREACH(Stmt *, c, s->stmts) dump_stmt(c, out, depth + 1);
        break;
    }
}

static void dump_func(const FuncDecl *f, FILE *out, int depth) {
    indent(out, depth);
    fprintf(out, "%d:%d %s %s : %s(", f->line, f->col, f->body ? "FuncDecl" : "Prototype",
            f->name, type_str(f->ret));
    for (int i = 0; i < f->params->len; i++) {
        VarDecl *p = f->params->items[i];
        fprintf(out, "%s%s %s", i ? ", " : "", type_str(p->type), p->name);
    }
    fputs(")\n", out);
    if (f->body) dump_stmt(f->body, out, depth + 1);
}

void ast_dump(const Program *p, FILE *out) {
    fputs("Program\n", out);
    VEC_FOREACH(Decl *, d, p->decls) {
        switch (d->kind) {
        case DECL_VAR: dump_vardecl(d->var, out, 1); break;
        case DECL_FUNC: dump_func(d->func, out, 1); break;
        case DECL_CLASS: {
            ClassDecl *c = d->cls;
            indent(out, 1);
            fprintf(out, "%d:%d ClassDecl %s", c->line, c->col, c->name);
            if (c->parent_name) fprintf(out, " extends %s", c->parent_name);
            if (c->iface_names->len) {
                fputs(" implements", out);
                for (int i = 0; i < c->iface_names->len; i++)
                    fprintf(out, "%s %s", i ? "," : "", (char *)c->iface_names->items[i]);
            }
            fputc('\n', out);
            VEC_FOREACH(VarDecl *, v, c->fields) dump_vardecl(v, out, 2);
            VEC_FOREACH(FuncDecl *, f, c->methods) dump_func(f, out, 2);
            break;
        }
        case DECL_IFACE: {
            IfaceDecl *i = d->iface;
            indent(out, 1);
            fprintf(out, "%d:%d InterfaceDecl %s\n", i->line, i->col, i->name);
            VEC_FOREACH(FuncDecl *, f, i->methods) dump_func(f, out, 2);
            break;
        }
        }
    }
}
