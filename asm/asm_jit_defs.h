#ifndef BEEBJIT_ASM_JIT_DEFS_H
#define BEEBJIT_ASM_JIT_DEFS_H

/* NOTE: this affects performance significantly.
 * Smaller is generally faster, which I believe is an L1 icache effect.
 * Going smaller than 7 is currently not feasible due to some opcodes not
 * fitting in 64 bytes.
 */
#define K_BBC_JIT_BYTES_SHIFT              7
#define K_BBC_JIT_BYTES_PER_BYTE           (1 << K_BBC_JIT_BYTES_SHIFT)
#define K_BBC_JIT_ADDR                     0x20000000
#define K_BBC_JIT_TRAMPOLINE_BYTES         16
#define K_BBC_JIT_TRAMPOLINES_ADDR         0x31000000
#define K_JIT_CONTEXT_OFFSET_JIT_CALLBACK  (K_CONTEXT_OFFSET_DRIVER_END + 0)
#define K_JIT_CONTEXT_OFFSET_INTURBO       (K_CONTEXT_OFFSET_DRIVER_END + 8)
#define K_JIT_CONTEXT_OFFSET_JIT_PTRS      (K_CONTEXT_OFFSET_DRIVER_END + 16)

#endif /* BEEBJIT_ASM_JIT_DEFS_H */

