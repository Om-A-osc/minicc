/* sema.h - semantic analysis: name resolution, class layout, type checking.
 *
 * On success every Expr carries its static type, identifiers point at the
 * declaration they name, method calls point at the FuncDecl they invoke, and
 * every class has a byte layout and a vtable. Errors are reported through
 * diag_error and counted in g_error_count. */
#ifndef MINICC_SEMA_H
#define MINICC_SEMA_H

#include "ast.h"

typedef enum {
    SYM_LOCAL, SYM_PARAM, SYM_FIELD, SYM_GLOBAL,
    SYM_FUNC, SYM_CLASS, SYM_IFACE
} SymKind;

typedef struct Symbol {
    SymKind kind;
    char *name;
    Type *type;             /* variables */
    VarDecl *var;           /* SYM_LOCAL / SYM_PARAM / SYM_FIELD / SYM_GLOBAL */
    FuncDecl *fn;           /* SYM_FUNC */
    ClassDecl *cls;         /* SYM_CLASS */
    IfaceDecl *iface;       /* SYM_IFACE */
} Symbol;

/* Runs all semantic checks over the program. Returns the number of errors. */
int sema_check(Program *prog);

/* Layout helpers shared with the IR generator. */
int type_size(const Type *t);           /* storage size in bytes of a value of type t */
int type_align(const Type *t);
FuncDecl *class_find_method(ClassDecl *c, const char *name);  /* searches the vtable */
VarDecl  *class_find_field(ClassDecl *c, const char *name);   /* searches the class chain */
int class_is_subclass(ClassDecl *sub, ClassDecl *super);      /* reflexive */
int class_implements(ClassDecl *c, IfaceDecl *i);

#define OBJECT_HEADER_SIZE 8   /* the vtable pointer */
#define ARRAY_HEADER_SIZE  8   /* 32-bit length, padded to 8 */

#endif
