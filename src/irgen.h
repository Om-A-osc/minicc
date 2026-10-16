/* irgen.h - lowers a type-checked AST to the IR in ir.h. */
#ifndef MINICC_IRGEN_H
#define MINICC_IRGEN_H

#include "ast.h"
#include "ir.h"

IrModule *irgen_program(Program *prog, const char *source_path);

#endif
