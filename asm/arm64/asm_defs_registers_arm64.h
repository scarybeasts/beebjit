#ifndef BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H
#define BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H

/* Caller save. */
#define REG_COUNTDOWN      x0
#define REG_6502_A         x1
#define REG_6502_A_32      w1
#define REG_6502_X         x2
#define REG_6502_X_32      w2
#define REG_6502_Y         x3
#define REG_6502_Y_32      w3
#define REG_6502_S         x4
#define REG_6502_S_32      w4
#define REG_6502_PC        x5
#define REG_6502_PC_32     w5
#define REG_SCRATCH1       x13
#define REG_SCRATCH1_32    w13
#define REG_SCRATCH2       x14
#define REG_SCRATCH2_32    w14
#define REG_SCRATCH3       x15
#define REG_SCRATCH3_32    w15
/* Callee save. */
#define REG_CONTEXT        x19
#define REG_INTURBO_CODE   x20
#define REG_DEBUG_FUNC     x21
#define REG_MEM_READ       x22
#define REG_MEM_WRITE      x23

#endif /* BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H */
