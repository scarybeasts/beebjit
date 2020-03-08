#ifndef BEEBJIT_ASM_X64_DEFS_H
#define BEEBJIT_ASM_X64_DEFS_H

#ifdef __APPLE__

#define ASM_SYMBOL(RET,NAME,PARAMETERS) RET NAME PARAMETERS asm(#NAME)

#elif defined __linux__

#define ASM_SYMBOL(RET,NAME,PARAMETERS) RET NAME PARAMETERS

#else

#error unknown platform

#endif

#define K_BBC_MEM_RAW_ADDR                      0x0f000000
#define K_BBC_MEM_READ_IND_ADDR                 0x10000000
#define K_BBC_MEM_WRITE_IND_ADDR                0x11000000
#define K_BBC_MEM_READ_FULL_ADDR                0x12000000
#define K_BBC_MEM_WRITE_FULL_ADDR               0x13000000
#define K_BBC_MEM_OFFSET_TO_WRITE_IND           0x01000000
#define K_BBC_MEM_OFFSET_TO_READ_FULL           0x02000000
#define K_BBC_MEM_OFFSET_TO_WRITE_FULL          0x03000000
#define K_BBC_MEM_OFFSET_READ_TO_WRITE          0x01000000
#define K_BBC_MEM_INACCESSIBLE_OFFSET           0xF000
#define K_BBC_MEM_INACCESSIBLE_LEN              0x1000
#define K_6502_ADDR_SPACE_SIZE                  0x10000
#define K_6502_VECTOR_IRQ                       0xFFFE
#define K_ASM_TABLE_6502_FLAGS_TO_X64           0x50000000
#define K_ASM_TABLE_6502_FLAGS_TO_MASK          0x50000100
#define K_ASM_TABLE_X64_FLAGS_TO_6502           0x50000200

#define K_CONTEXT_OFFSET_STATE_6502             8
#define K_CONTEXT_OFFSET_DEBUG_CALLBACK         16
#define K_CONTEXT_OFFSET_DEBUG_OBJECT           24
#define K_CONTEXT_OFFSET_INTERP_CALLBACK        32
#define K_CONTEXT_OFFSET_INTERP_OBJECT          40
#define K_CONTEXT_OFFSET_ABI_END                48
#define K_CONTEXT_OFFSET_DRIVER_END             (K_CONTEXT_OFFSET_ABI_END + 32)

#define K_STATE_6502_OFFSET_REG_A               0
#define K_STATE_6502_OFFSET_REG_X               4
#define K_STATE_6502_OFFSET_REG_Y               8
#define K_STATE_6502_OFFSET_REG_S               12
#define K_STATE_6502_OFFSET_REG_PC              16
#define K_STATE_6502_OFFSET_REG_FLAGS           20
#define K_STATE_6502_OFFSET_REG_IRQ_FIRE        24
#define K_STATE_6502_OFFSET_REG_HOST_PC         28
#define K_STATE_6502_OFFSET_REG_HOST_FLAGS      32

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

#define REG_RETURN         rax
#define REG_RETURN2        rdx
#define REG_PARAM1         rdi
#define REG_PARAM1_32      edi
#define REG_PARAM2         rsi
#define REG_PARAM2_32      esi
#define REG_PARAM3         rdx
#define REG_PARAM3_32      edx
#define REG_PARAM4         rcx
#define REG_PARAM4_32      ecx

#endif /* BEEBJIT_ASM_X64_DEFS_H */
