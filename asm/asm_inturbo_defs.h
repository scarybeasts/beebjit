#ifndef BEEBJIT_ASM_INTURBO_DEFS_H
#define BEEBJIT_ASM_INTURBO_DEFS_H

#define K_INTURBO_OPCODE_SHIFT             7
#define K_INTURBO_OPCODE_SIZE              (1 << K_INTURBO_OPCODE_SHIFT)
#define K_INTURBO_SIZE                     (256 * K_INTURBO_OPCODE_SIZE)
#define K_INTURBO_ADDR                     0x40000000ull
#define K_INTURBO_ADDR_END                 (K_INTURBO_ADDR + K_INTURBO_SIZE)
/* Page for the asm backends. */
#define K_INTURBO_ASM                      K_INTURBO_ADDR_END

#endif /* BEEBJIT_ASM_INTURBO_DEFS_H */

