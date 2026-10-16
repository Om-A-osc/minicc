/* emit_llvm.h - LLVM IR text backend. */
#ifndef MINICC_EMIT_LLVM_H
#define MINICC_EMIT_LLVM_H

#include <stdio.h>
#include "ir.h"

void emit_llvm(const IrModule *m, FILE *out);

#endif
