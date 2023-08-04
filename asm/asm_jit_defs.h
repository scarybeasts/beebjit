#ifndef BEEBJIT_ASM_JIT_DEFS_H
#define BEEBJIT_ASM_JIT_DEFS_H

#include "asm_platform.h"

#define K_JIT_BYTES_PER_BYTE               (1 << K_JIT_BYTES_SHIFT)
#define K_JIT_SIZE                         (65536 * K_JIT_BYTES_PER_BYTE)
/* NOTE: K_JIT_ADDR varies betweeen platforms, and is in asm_platform.h */
#define K_JIT_ADDR_END                     (K_JIT_ADDR + K_JIT_SIZE)
#define K_JIT_NO_CODE_JIT_PTR_PAGE         (K_JIT_ADDR_END + 0)
#define K_JIT_CONTEXT_OFFSET_JIT_CALLBACK  (K_CONTEXT_OFFSET_DRIVER_END + 0)
#define K_JIT_CONTEXT_OFFSET_INTURBO       (K_CONTEXT_OFFSET_DRIVER_END + 8)
#define K_JIT_CONTEXT_OFFSET_JIT_PTRS      (K_CONTEXT_OFFSET_DRIVER_END + 16)

#endif /* BEEBJIT_ASM_JIT_DEFS_H */

