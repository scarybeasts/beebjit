#ifndef BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H
#define BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H

/* Caller save (x0 - x15 inclusive).. */
#define REG_6502_A                x0
#define REG_6502_A_32             w0
#define REG_6502_X                x1
#define REG_6502_X_32             w1
#define REG_6502_Y                x2
#define REG_6502_Y_32             w2
#define REG_6502_S                x3
#define REG_6502_S_32             w3
#define REG_6502_PC               x4
#define REG_6502_PC_32            w4
/* REG_JIT_SCRATCH is used by JIT when the PC is implicit. */
#define REG_JIT_SCRATCH           x4
#define REG_JIT_SCRATCH_32        w4
#define REG_6502_ID_F             x5
#define REG_6502_CF               x6
#define REG_6502_OF               x7
#define REG_SCRATCH1              x8
#define REG_SCRATCH1_32           w8
#define REG_SCRATCH2              x9
#define REG_SCRATCH2_32           w9
#define REG_INTURBO_SCRATCH3      x10
#define REG_INTURBO_SCRATCH3_32   w10
/* Callee save (x19 - x29 inclusive). */
#define REG_VALUE                 x20
#define REG_VALUE_32              w20
#define REG_JIT_ADDR              x21
#define REG_JIT_ADDR_32           w21
#define REG_JIT_ADDR_BASE         x22
#define REG_JIT_ADDR_BASE_32      w22
#define REG_MEM_STACK             x23
#define REG_COUNTDOWN             x24
#define REG_CONTEXT               x25
#define REG_JIT_PTRS              x26
#define REG_MEM_READ              x27
#define REG_MEM_WRITE             x28
#define REG_JIT_COMPILE           x29

#endif /* BEEBJIT_ASM_DEFS_REGISTERS_ARM64_H */
