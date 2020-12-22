#ifndef BEEBJIT_ASM_DEFS_REGISTERS_X64_H
#define BEEBJIT_ASM_DEFS_REGISTERS_X64_H

#define REG_6502_A         al
#define REG_6502_A_32      eax
#define REG_6502_A_64      rax
#define REG_6502_X         bl
#define REG_6502_X_32      ebx
#define REG_6502_X_64      rbx
#define REG_6502_Y         cl
#define REG_6502_Y_32      ecx
#define REG_6502_Y_64      rcx
#define REG_6502_S         sil
#define REG_6502_S_32      esi
#define REG_6502_S_64      rsi
#define REG_6502_PC        r10
#define REG_6502_PC_16     r10w
#define REG_6502_PC_32     r10d
#define REG_6502_OF        r12b
#define REG_6502_OF_64     r12
#define REG_6502_CF        r14b
#define REG_6502_CF_64     r14
#define REG_6502_ID_F      r13b
#define REG_6502_ID_F_32   r13d
#define REG_6502_ID_F_64   r13
#define REG_COUNTDOWN      r15

#define REG_CONTEXT        rdi
#define REG_MEM            rbp
#define REG_MEM_OFFSET     0x80

#define REG_SCRATCH1       rdx
#define REG_SCRATCH1_8     dl
#define REG_SCRATCH1_8_HI  dh
#define REG_SCRATCH1_16    dx
#define REG_SCRATCH1_32    edx
#define REG_SCRATCH2       r8
#define REG_SCRATCH2_8     r8b
#define REG_SCRATCH2_16    r8w
#define REG_SCRATCH2_32    r8d
#define REG_SCRATCH3       r9
#define REG_SCRATCH3_8     r9b
#define REG_SCRATCH3_16    r9w
#define REG_SCRATCH3_32    r9d
#define REG_SCRATCH4       r11
#define REG_SCRATCH4_8     r11b
#define REG_SCRATCH4_32    r11d

#endif /* BEEBJIT_ASM_DEFS_REGISTERS_X64_H */
