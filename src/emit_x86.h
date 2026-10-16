/* emit_x86.h - x86-64 assembly backend (GNU as, AT&T syntax). */
#ifndef MINICC_EMIT_X86_H
#define MINICC_EMIT_X86_H

#include <stdio.h>
#include "ir.h"

typedef enum { ABI_SYSV, ABI_WIN64 } X86Abi;

X86Abi x86_host_abi(void);
void emit_x86(const IrModule *m, FILE *out, X86Abi abi, int debug_info);

#endif
