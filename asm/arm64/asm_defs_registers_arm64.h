#ifndef BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H
#define BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H

/* Caller save. */
#define REG_6502_A         x0
#define REG_6502_A_32      w0
#define REG_6502_X         x1
#define REG_6502_X_32      w1
#define REG_6502_Y         x2
#define REG_6502_Y_32      w2
#define REG_6502_S         x3
#define REG_6502_S_32      w3
#define REG_6502_PC        x4
#define REG_6502_PC_32     w4
#define REG_6502_ID_F      x5
#define REG_6502_CF        x6
#define REG_6502_OF        x7
#define REG_COUNTDOWN_OLD  x12
#define REG_SCRATCH1       x13
#define REG_SCRATCH1_32    w13
#define REG_SCRATCH2       x14
#define REG_SCRATCH2_32    w14
#define REG_SCRATCH3       x15
#define REG_SCRATCH3_32    w15
/* Callee save (x19 - x29 inclusive). */
#define REG_MEM_STACK      x21
#define REG_COUNTDOWN      x22
#define REG_CONTEXT        x23
#define REG_INTURBO_CODE   x24
#define REG_INTERP_FUNC    x25
#define REG_DEBUG_FUNC     x26
#define REG_MEM_READ       x27
#define REG_MEM_WRITE      x28
#define REG_JIT_COMPILE    x29

#endif /* BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H */
