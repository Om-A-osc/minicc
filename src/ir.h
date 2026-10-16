/* ir.h - MiniCC intermediate representation.
 *
 * A typed three-address code over basic blocks. Values live in two places:
 *
 *   temps  (%tN)  are assigned exactly once (SSA-style) and hold the results
 *                 of arithmetic, loads and calls;
 *   slots  (%sN)  are function-local memory cells for parameters and local
 *                 variables, accessed only through LOADL / STOREL.
 *
 * Keeping variables in slots instead of temps means no phi nodes are needed;
 * the LLVM backend maps slots to allocas (and lets mem2reg promote them) and
 * the x86 backend maps them to stack frame locations. */
#ifndef MINICC_IR_H
#define MINICC_IR_H

#include <stdio.h>
#include "util.h"

typedef enum { IR_VOID, IR_I1, IR_I32, IR_F64, IR_PTR } IrType;

const char *irtype_str(IrType t);
int irtype_size(IrType t);

/* ------------------------------------------------------------ operands */

typedef enum {
    OPK_NONE,
    OPK_TEMP,       /* %tN */
    OPK_IMM,        /* integer immediate (i1 or i32) */
    OPK_FIMM,       /* double immediate */
    OPK_NULL,       /* null pointer */
    OPK_STR,        /* address of string literal #str */
    OPK_SYM         /* address of a module-level data symbol (vtable, global) */
} OpKind;

typedef struct {
    OpKind kind;
    IrType type;
    int temp;
    long long imm;
    double fimm;
    int str;
    const char *sym;
} Operand;

Operand op_none(void);
Operand op_temp(int t, IrType ty);
Operand op_imm(long long v, IrType ty);
Operand op_fimm(double v);
Operand op_null(void);
Operand op_str(int idx);
Operand op_sym(const char *name);
int op_is_const(Operand o);     /* IMM / FIMM / NULL */
int op_equal(Operand a, Operand b);

/* -------------------------------------------------------- instructions */

typedef enum {
    IR_BIN,         /* dst = a bop b */
    IR_UN,          /* dst = uop a */
    IR_SITOFP,      /* dst = (double) a */
    IR_ZEXT,        /* dst = (i32) a  (a is i1) */
    IR_LOADL,       /* dst = slot */
    IR_STOREL,      /* slot = a */
    IR_LOADG,       /* dst = @name */
    IR_STOREG,      /* @name = a */
    IR_LOADF,       /* dst = *(type *)(a + offset) */
    IR_STOREF,      /* *(type *)(a + offset) = b */
    IR_LOADX,       /* dst = ((type *)(a + ARRAY_HEADER))[b] */
    IR_STOREX,      /* ((type *)(a + ARRAY_HEADER))[b] = c */
    IR_CALL,        /* dst = name(args) */
    IR_CALLIND,     /* dst = (*a)(args) */
    IR_BR,          /* goto target */
    IR_CBR,         /* if a goto target else target2 */
    IR_RET,         /* return [a] */
    IR_UNREACHABLE
} IrOp;

typedef enum {
    BOP_ADD, BOP_SUB, BOP_MUL, BOP_DIV, BOP_REM,
    BOP_LT, BOP_LE, BOP_GT, BOP_GE, BOP_EQ, BOP_NE,
    BOP_ULT,        /* unsigned <, used for array bounds checks */
    BOP_AND, BOP_OR, BOP_XOR
} IrBinOp;

typedef enum { UOP_NEG, UOP_NOT } IrUnOp;

struct IrBlock;

typedef struct IrInstr {
    IrOp op;
    int line;                   /* source line, for debug info */
    int dst;                    /* result temp, or -1 */
    IrType type;                /* type of dst, or of the value loaded / stored */
    IrBinOp bop;
    IrUnOp uop;
    Operand a, b, c;
    int slot;                   /* LOADL / STOREL */
    const char *name;           /* LOADG / STOREG / CALL */
    int offset;                 /* LOADF / STOREF */
    IrType ret_type;            /* CALL / CALLIND result type (IR_VOID if none) */
    Operand *args;
    int nargs;
    struct IrBlock *target, *target2;
} IrInstr;

typedef struct IrBlock {
    int id;
    Vec *instrs;                /* IrInstr*, last one is the terminator */
    Vec *preds;                 /* IrBlock*, computed by ir_compute_preds */
} IrBlock;

typedef struct IrFunc {
    const char *name;
    IrType ret;
    int nparams;                /* parameters occupy slots 0 .. nparams-1 */
    Vec *slot_types;            /* IrType per slot (stored as intptr) */
    Vec *slot_names;            /* const char* per slot, for the IR dump */
    int ntemps;
    Vec *blocks;                /* IrBlock*, blocks[0] is the entry */
    int line;
} IrFunc;

typedef struct {
    const char *data;
    int len;
} IrString;

typedef struct {
    const char *name;
    IrType type;
} IrGlobal;

typedef enum { DW_INT, DW_SYM } DataWordKind;

typedef struct {
    DataWordKind kind;
    long long ival;
    const char *sym;
} DataWord;

typedef struct {
    const char *name;
    Vec *words;                 /* DataWord*, each 8 bytes */
    const char *comment;
} IrData;

typedef struct {
    const char *source_path;
    Vec *funcs;                 /* IrFunc* */
    Vec *globals;               /* IrGlobal* */
    Vec *strings;               /* IrString* */
    Vec *data;                  /* IrData*: vtables and class method tables */
    StrMap *string_index;       /* literal -> index+1, for de-duplication */
    StrMap *used_runtime;       /* runtime functions referenced by the module */
} IrModule;

/* ----------------------------------------------------------- building */

IrModule *ir_module_new(const char *source_path);
int       ir_module_string(IrModule *m, const char *s, int len);
void      ir_module_global(IrModule *m, const char *name, IrType t);
IrData   *ir_module_data(IrModule *m, const char *name, const char *comment);
void      ir_data_int(IrData *d, long long v);
void      ir_data_sym(IrData *d, const char *sym);

IrFunc  *ir_func_new(IrModule *m, const char *name, IrType ret, int line);
int      ir_func_slot(IrFunc *f, IrType t, const char *name);
int      ir_func_temp(IrFunc *f);
IrBlock *ir_block_new(IrFunc *f);
int      ir_block_terminated(const IrBlock *b);
IrInstr *ir_instr_new(IrOp op, int line);
void     ir_block_append(IrBlock *b, IrInstr *i);
IrInstr *ir_block_terminator(const IrBlock *b);

/* Recompute predecessor lists and drop blocks unreachable from the entry. */
void ir_func_cleanup(IrFunc *f);
void ir_compute_preds(IrFunc *f);

/* ---------------------------------------------------------- runtime */

typedef struct {
    const char *name;
    IrType ret;
    int nparams;
    IrType params[4];
} RuntimeFn;

extern const RuntimeFn runtime_fns[];
extern const int runtime_fn_count;
const RuntimeFn *runtime_fn_lookup(const char *name);

/* -------------------------------------------------------------- dump */

void ir_dump(const IrModule *m, FILE *out);
void ir_dump_func(const IrFunc *f, FILE *out);

#endif
