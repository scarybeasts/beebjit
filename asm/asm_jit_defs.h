#ifndef BEEBJIT_ASM_JIT_DEFS_H
#define BEEBJIT_ASM_JIT_DEFS_H

#include "asm_defs.h"

/* NOTE: this affects performance significantly.
 * 9 == -8% and 10 == -23%.
 * 7 may be a tiny shade faster (<1%), needs more tests. <= 6 is not viable.
 */
#define K_BBC_JIT_BYTES_SHIFT              8
#define K_BBC_JIT_BYTES_PER_BYTE           (1 << K_BBC_JIT_BYTES_SHIFT)
#define K_BBC_JIT_ADDR                     0x20000000
#define K_BBC_JIT_TRAMPOLINE_BYTES         16
#define K_BBC_JIT_TRAMPOLINES_ADDR         0x31000000
#define K_JIT_CONTEXT_OFFSET_JIT_CALLBACK  (K_CONTEXT_OFFSET_DRIVER_END + 0)
#define K_JIT_CONTEXT_OFFSET_JIT_PTRS      (K_CONTEXT_OFFSET_DRIVER_END + 8)

#endif /* BEEBJIT_ASM_JIT_DEFS_H */

