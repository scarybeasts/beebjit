#ifndef BEEBJIT_ASM_JIT_DEFS_H
#define BEEBJIT_ASM_JIT_DEFS_H

#include "asm_platform.h"

/* NOTE: this affects performance significantly.
 * Smaller is generally faster, which I believe is an L1 icache effect.
 * Going smaller than 7 is currently not feasible due to some opcodes not
 * fitting in 64 bytes.
 */
#define K_JIT_BYTES_SHIFT                  7
#define K_JIT_BYTES_PER_BYTE               (1 << K_JIT_BYTES_SHIFT)
#define K_JIT_SIZE                         (65536 * K_JIT_BYTES_PER_BYTE)
/* NOTE: K_JIT_ADDR varies betweeen platforms, and is in asm_platform.h */
#define K_JIT_ADDR_END                     (K_JIT_ADDR + K_JIT_SIZE)
#define K_JIT_NO_CODE_JIT_PTR_PAGE         (K_JIT_ADDR_END + 0)
#define K_JIT_CONTEXT_OFFSET_JIT_CALLBACK  (K_CONTEXT_OFFSET_DRIVER_END + 0)
#define K_JIT_CONTEXT_OFFSET_INTURBO       (K_CONTEXT_OFFSET_DRIVER_END + 8)
#define K_JIT_CONTEXT_OFFSET_JIT_PTRS      (K_CONTEXT_OFFSET_DRIVER_END + 16)

/* TODO: these are x64 backend specific, and don't belong here. */
#define K_JIT_TRAMPOLINE_BYTES             16
#define K_JIT_TRAMPOLINES_ADDR             0x30000000

#endif /* BEEBJIT_ASM_JIT_DEFS_H */

