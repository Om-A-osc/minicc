/* ast.h - abstract syntax tree for the MiniCC (Decaf-derived) language. */
#ifndef MINICC_AST_H
#define MINICC_AST_H

#include <stdio.h>
#include "util.h"

/* ------------------------------------------------------------------ types */

typedef enum {
    TY_INT, TY_DOUBLE, TY_BOOL, TY_STRING, TY_VOID,
    TY_NULL,     /* type of the literal `null` */
    TY_NAMED,    /* class or interface, resolved by sema */
    TY_ARRAY,
    TY_ERROR     /* produced after a reported error, suppresses cascades */
} TypeKind;

typedef struct Type {
    TypeKind kind;
    char *name;                 /* TY_NAMED */
    struct Type *elem;          /* TY_ARRAY */
    struct ClassDecl *cls;      /* TY_NAMED resolved to a class */
    struct IfaceDecl *iface;    /* TY_NAMED resolved to an interface */
    int line, col;
} Type;

Type *type_new(TypeKind kind, int line, int col);
Type *type_named(char *name, int line, int col);
Type *type_array(Type *elem, int line, int col);
Type *type_prim(TypeKind kind);          /* shared singletons for primitives */
int   type_equal(const Type *a, const Type *b);
const char *type_str(const Type *t);

/* ------------------------------------------------------------ expressions */

typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_LT, BIN_LE, BIN_GT, BIN_GE, BIN_EQ, BIN_NE,
    BIN_AND, BIN_OR
} BinOp;

typedef enum { UN_NEG, UN_NOT } UnOp;

typedef enum {
    EX_INT_LIT, EX_DOUBLE_LIT, EX_BOOL_LIT, EX_STRING_LIT, EX_NULL_LIT,
    EX_IDENT, EX_THIS,
    EX_BINARY, EX_UNARY,
    EX_ASSIGN,        /* lhs = rhs, or lhs op= rhs when is_compound */
    EX_INCDEC,        /* ++x, x++, --x, x-- */
    EX_FIELD,         /* lhs.name */
    EX_INDEX,         /* lhs[rhs] */
    EX_CALL,          /* name(args) */
    EX_METHOD_CALL,   /* lhs.name(args) */
    EX_NEW,           /* New(Class) */
    EX_NEW_ARRAY,     /* NewArray(n, Type) */
    EX_READ_INTEGER,  /* ReadInteger() */
    EX_READ_LINE,     /* ReadLine() */
    EX_CAST           /* implicit int -> double conversion inserted by sema */
} ExprKind;

struct Symbol;
struct FuncDecl;
struct VarDecl;

typedef struct Expr {
    ExprKind kind;
    int line, col;
    Type *type;                 /* filled in by sema */

    int ival;                   /* EX_INT_LIT, EX_BOOL_LIT */
    double dval;                /* EX_DOUBLE_LIT */
    char *sval;                 /* EX_STRING_LIT (unescaped bytes) */
    char *name;                 /* EX_IDENT, EX_FIELD, EX_CALL, EX_METHOD_CALL, EX_NEW */

    struct Expr *lhs, *rhs;     /* operands; unary/field/index/method use lhs as the object */
    Vec *args;                  /* Expr* arguments for calls */
    BinOp binop;
    UnOp unop;
    int is_compound;            /* EX_ASSIGN with binop */
    int is_prefix, is_increment;/* EX_INCDEC */
    Type *elem_type;            /* EX_NEW_ARRAY element type */

    /* resolved by sema */
    struct Symbol *sym;         /* EX_IDENT */
    struct FuncDecl *fn;        /* EX_CALL, EX_METHOD_CALL */
    struct VarDecl *field;      /* EX_FIELD (and EX_IDENT that names a field) */
    int is_array_length;        /* EX_METHOD_CALL: array.length() */
    int via_interface;          /* EX_METHOD_CALL through an interface-typed receiver */
} Expr;

Expr *expr_new(ExprKind kind, int line, int col);
Expr *expr_int(int v, int line, int col);
Expr *expr_double(double v, int line, int col);
Expr *expr_bool(int v, int line, int col);
Expr *expr_string(char *s, int line, int col);
Expr *expr_ident(char *name, int line, int col);
Expr *expr_binary(BinOp op, Expr *l, Expr *r, int line, int col);
Expr *expr_unary(UnOp op, Expr *e, int line, int col);
Expr *expr_assign(Expr *l, Expr *r, int compound, BinOp op, int line, int col);
Expr *expr_incdec(Expr *target, int is_prefix, int is_increment, int line, int col);
Expr *expr_field(Expr *obj, char *name, int line, int col);
Expr *expr_index(Expr *arr, Expr *idx, int line, int col);
Expr *expr_call(char *name, Vec *args, int line, int col);
Expr *expr_method_call(Expr *obj, char *name, Vec *args, int line, int col);
Expr *expr_new_obj(char *cls, int line, int col);
Expr *expr_new_array(Expr *n, Type *elem, int line, int col);

const char *binop_str(BinOp op);

/* ------------------------------------------------------------- statements */

typedef enum {
    ST_EXPR, ST_VARDECL, ST_IF, ST_WHILE, ST_FOR,
    ST_BREAK, ST_CONTINUE, ST_RETURN, ST_PRINT, ST_BLOCK
} StmtKind;

typedef struct Stmt {
    StmtKind kind;
    int line, col;
    Expr *expr;                 /* ST_EXPR; ST_RETURN value (nullable); loop/if condition (nullable for `for`) */
    struct VarDecl *var;        /* ST_VARDECL */
    struct Stmt *body;          /* ST_IF then / ST_WHILE / ST_FOR */
    struct Stmt *else_body;     /* ST_IF (nullable) */
    struct Stmt *init;          /* ST_FOR: ST_EXPR or ST_VARDECL, nullable */
    Expr *step;                 /* ST_FOR, nullable */
    Vec *stmts;                 /* ST_BLOCK */
    Vec *args;                  /* ST_PRINT */
} Stmt;

Stmt *stmt_new(StmtKind kind, int line, int col);

/* ----------------------------------------------------------- declarations */

typedef struct VarDecl {
    char *name;
    Type *type;
    Expr *init;                 /* nullable */
    int line, col;
    /* sema */
    struct ClassDecl *owner;    /* non-NULL for fields */
    int offset;                 /* byte offset of a field inside its object */
    int is_global;
    /* irgen */
    int slot;
} VarDecl;

typedef struct FuncDecl {
    char *name;
    Type *ret;
    Vec *params;                /* VarDecl* */
    Stmt *body;                 /* ST_BLOCK; NULL for interface prototypes */
    int line, col;
    /* sema */
    struct ClassDecl *owner;    /* method owner, NULL for free functions */
    struct IfaceDecl *iface_owner;
    int vslot;                  /* vtable slot for methods */
    int method_id;              /* program-wide interned method name id */
    char *mangled;              /* symbol name emitted by the backends */
} FuncDecl;

typedef struct ClassDecl {
    char *name;
    char *parent_name;          /* nullable */
    Vec *iface_names;           /* char* */
    Vec *fields;                /* VarDecl* */
    Vec *methods;               /* FuncDecl* */
    int line, col;
    /* sema */
    struct ClassDecl *parent;
    Vec *ifaces;                /* IfaceDecl* */
    Vec *vtable;                /* FuncDecl* by slot, including inherited methods */
    int size;                   /* object size in bytes, including the vtable pointer */
    int layout_state;           /* 0 pending, 1 in progress, 2 done */
    Type *type;
} ClassDecl;

typedef struct IfaceDecl {
    char *name;
    Vec *methods;               /* FuncDecl* prototypes */
    int line, col;
    Type *type;
} IfaceDecl;

typedef enum { DECL_VAR, DECL_FUNC, DECL_CLASS, DECL_IFACE } DeclKind;

typedef struct Decl {
    DeclKind kind;
    VarDecl *var;
    FuncDecl *func;
    ClassDecl *cls;
    IfaceDecl *iface;
} Decl;

typedef struct Program {
    Vec *decls;                 /* Decl* in source order */
    /* categorized views, filled by program_new */
    Vec *globals;               /* VarDecl* */
    Vec *funcs;                 /* FuncDecl* */
    Vec *classes;               /* ClassDecl* */
    Vec *ifaces;                /* IfaceDecl* */
} Program;

VarDecl   *vardecl_new(Type *type, char *name, Expr *init, int line, int col);
FuncDecl  *funcdecl_new(Type *ret, char *name, Vec *params, Stmt *body, int line, int col);
ClassDecl *classdecl_new(char *name, char *parent, Vec *ifaces, Vec *members, int line, int col);
IfaceDecl *ifacedecl_new(char *name, Vec *protos, int line, int col);
Decl      *decl_var(VarDecl *v);
Decl      *decl_func(FuncDecl *f);
Decl      *decl_class(ClassDecl *c);
Decl      *decl_iface(IfaceDecl *i);
Program   *program_new(Vec *decls);

/* ------------------------------------------------------------------- dump */

void ast_dump(const Program *p, FILE *out);

#endif
