#include "asm_x64_defs.h"

.file "asm_x64_common.S"
.intel_syntax noprefix
.section rodata
.text


.globl asm_x64_instruction_CRASH
.globl asm_x64_instruction_CRASH_END
asm_x64_instruction_CRASH:

  mov al, [0x0000dead]

asm_x64_instruction_CRASH_END:
  ret


.globl asm_x64_instruction_REAL_NOP
.globl asm_x64_instruction_REAL_NOP_END
asm_x64_instruction_REAL_NOP:

  nop

asm_x64_instruction_REAL_NOP_END:
  ret


.globl asm_x64_instruction_TRAP
.globl asm_x64_instruction_TRAP_END
asm_x64_instruction_TRAP:

  int 3

asm_x64_instruction_TRAP_END:
  ret


.globl asm_x64_instruction_ILLEGAL
.globl asm_x64_instruction_ILLEGAL_END
asm_x64_instruction_ILLEGAL:

  ud2

asm_x64_instruction_ILLEGAL_END:
  ret


.globl asm_x64_instruction_BIT_common
.globl asm_x64_instruction_BIT_common_END
asm_x64_instruction_BIT_common:

  # NOTE: tried alternatives here with pdep, bextr, cmov, but this turned out
  # to be fastest.
  # Load OF.
  bt REG_SCRATCH1_32, 6
  setb REG_6502_OF
  # Load ZF.
  test REG_SCRATCH1_8, REG_6502_A
  sete REG_SCRATCH2_8
  shl REG_SCRATCH2_8, 6
  # Load NF.
  and REG_SCRATCH1_8, 0x80
  # Put ZF / NF together.
  or REG_SCRATCH1_8, REG_SCRATCH2_8
  mov ah, REG_SCRATCH1_8
  sahf

asm_x64_instruction_BIT_common_END:
  ret


.globl asm_x64_instruction_CLC
.globl asm_x64_instruction_CLC_END
asm_x64_instruction_CLC:

  mov REG_6502_CF, 0

asm_x64_instruction_CLC_END:
  ret


.globl asm_x64_instruction_CLD
.globl asm_x64_instruction_CLD_END
asm_x64_instruction_CLD:

  btr REG_6502_ID_F_32, 3

asm_x64_instruction_CLD_END:
  ret


.globl asm_x64_instruction_CLI
.globl asm_x64_instruction_CLI_END
asm_x64_instruction_CLI:

  btr REG_6502_ID_F_32, 2

asm_x64_instruction_CLI_END:
  ret


.globl asm_x64_instruction_CLV
.globl asm_x64_instruction_CLV_END
asm_x64_instruction_CLV:

  mov REG_6502_OF, 0

asm_x64_instruction_CLV_END:
  ret


.globl asm_x64_instruction_DEX
.globl asm_x64_instruction_DEX_END
asm_x64_instruction_DEX:

  dec REG_6502_X

asm_x64_instruction_DEX_END:
  ret


.globl asm_x64_instruction_DEY
.globl asm_x64_instruction_DEY_END
asm_x64_instruction_DEY:

  dec REG_6502_Y

asm_x64_instruction_DEY_END:
  ret


.globl asm_x64_instruction_INX
.globl asm_x64_instruction_INX_END
asm_x64_instruction_INX:

  inc REG_6502_X

asm_x64_instruction_INX_END:
  ret


.globl asm_x64_instruction_INY
.globl asm_x64_instruction_INY_END
asm_x64_instruction_INY:

  inc REG_6502_Y

asm_x64_instruction_INY_END:
  ret


.globl asm_x64_instruction_PHA
.globl asm_x64_instruction_PHA_END
asm_x64_instruction_PHA:

  mov [REG_6502_S_64], REG_6502_A
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

asm_x64_instruction_PHA_END:
  ret


.globl asm_x64_instruction_PLA
.globl asm_x64_instruction_PLA_END
asm_x64_instruction_PLA:

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  mov REG_6502_A, BYTE PTR [REG_6502_S_64]

asm_x64_instruction_PLA_END:
  ret


.globl asm_x64_instruction_SEC
.globl asm_x64_instruction_SEC_END
asm_x64_instruction_SEC:

  mov REG_6502_CF, 1

asm_x64_instruction_SEC_END:
  ret


.globl asm_x64_instruction_SED
.globl asm_x64_instruction_SED_END
asm_x64_instruction_SED:

  bts REG_6502_ID_F_32, 3

asm_x64_instruction_SED_END:
  ret


.globl asm_x64_instruction_SEI
.globl asm_x64_instruction_SEI_END
asm_x64_instruction_SEI:

  bts REG_6502_ID_F_32, 2

asm_x64_instruction_SEI_END:
  ret


.globl asm_x64_instruction_TAX
.globl asm_x64_instruction_TAX_END
asm_x64_instruction_TAX:

  mov REG_6502_X, REG_6502_A

asm_x64_instruction_TAX_END:
  ret


.globl asm_x64_instruction_TAY
.globl asm_x64_instruction_TAY_END
asm_x64_instruction_TAY:

  mov REG_6502_Y, REG_6502_A

asm_x64_instruction_TAY_END:
  ret


.globl asm_x64_instruction_TSX
.globl asm_x64_instruction_TSX_END
asm_x64_instruction_TSX:

  mov REG_6502_X, REG_6502_S

asm_x64_instruction_TSX_END:
  ret


.globl asm_x64_instruction_TXA
.globl asm_x64_instruction_TXA_END
asm_x64_instruction_TXA:

  mov REG_6502_A, REG_6502_X

asm_x64_instruction_TXA_END:
  ret


.globl asm_x64_instruction_TXS
.globl asm_x64_instruction_TXS_END
asm_x64_instruction_TXS:

  mov REG_6502_S, REG_6502_X

asm_x64_instruction_TXS_END:
  ret


.globl asm_x64_instruction_TYA
.globl asm_x64_instruction_TYA_END
asm_x64_instruction_TYA:

  mov REG_6502_A, REG_6502_Y

asm_x64_instruction_TYA_END:
  ret


.globl asm_x64_instruction_A_NZ_flags
.globl asm_x64_instruction_A_NZ_flags_END
asm_x64_instruction_A_NZ_flags:

  test REG_6502_A, REG_6502_A

asm_x64_instruction_A_NZ_flags_END:
  ret


.globl asm_x64_instruction_X_NZ_flags
.globl asm_x64_instruction_X_NZ_flags_END
asm_x64_instruction_X_NZ_flags:

  test REG_6502_X, REG_6502_X

asm_x64_instruction_X_NZ_flags_END:
  ret


.globl asm_x64_instruction_Y_NZ_flags
.globl asm_x64_instruction_Y_NZ_flags_END
asm_x64_instruction_Y_NZ_flags:

  test REG_6502_Y, REG_6502_Y

asm_x64_instruction_Y_NZ_flags_END:
  ret

.globl asm_x64_save_AXYS_PC_flags
asm_x64_save_AXYS_PC_flags:
  # Save 6502 IP.
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_PC], REG_6502_PC_32
  # Save A, X, Y, S.
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_A], REG_6502_A_32
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_X], REG_6502_X_32
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_Y], REG_6502_Y_32
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_S], REG_6502_S_32

  call asm_x64_asm_emit_intel_flags_to_scratch

  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_FLAGS], REG_SCRATCH1_32

  ret


.globl asm_x64_restore_AXYS_PC_flags
asm_x64_restore_AXYS_PC_flags:

  # Restore A, X, Y, S.
  mov REG_6502_A_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_A]
  mov REG_6502_X_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_X]
  mov REG_6502_Y_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_Y]
  mov REG_6502_S_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_S]
  # Restore 6502 IP.
  mov REG_6502_PC_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_PC]
  # Restore 6502 flags.
  movzx REG_SCRATCH1, BYTE PTR [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_FLAGS]

  call asm_x64_asm_set_intel_flags_from_scratch

  ret


.globl asm_x64_asm_emit_intel_flags_to_scratch
.globl asm_x64_asm_emit_intel_flags_to_scratch_END
asm_x64_asm_emit_intel_flags_to_scratch:
  lahf
  # Add IF+DF, CF and "always set" (0x20)
  lea REG_SCRATCH3_32, [REG_6502_ID_F_64 + REG_6502_CF_64 + 0x20]
  rorx REG_6502_OF_64, REG_6502_OF_64, 58
  lea REG_SCRATCH3_32, [REG_SCRATCH3 + REG_6502_OF_64]
  rorx REG_6502_OF_64, REG_6502_OF_64, 6
  movzx REG_SCRATCH1_32, ah
  movzx REG_SCRATCH1_32, BYTE PTR [REG_SCRATCH1 + K_ASM_TABLE_X64_FLAGS_TO_6502]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_SCRATCH3]

asm_x64_asm_emit_intel_flags_to_scratch_END:
  ret


.globl asm_x64_asm_set_intel_flags_from_scratch
.globl asm_x64_asm_set_intel_flags_from_scratch_END
asm_x64_asm_set_intel_flags_from_scratch:
  mov REG_SCRATCH2_32, 0x40
  mov REG_SCRATCH3_32, 0x01
  # NOTE: in testing, pext is faster than bextr by about 10%.
  # This is despite the Agner Fog tables as listing:
  # pext : latency 3 throughput 1, port p1
  # bextr: latency 2 thorughput 2, port 2p0156
  # Interesting that bextr is listed as faster despite using 2uops vs. 1.
  # But my guess is that because pext doesn't change flags, the mov ah / sahf
  # can execute in parallel. bextr sets ZF.
  pext REG_6502_OF_64, REG_SCRATCH1, REG_SCRATCH2
  pext REG_6502_CF_64, REG_SCRATCH1, REG_SCRATCH3
  mov REG_6502_ID_F, [K_ASM_TABLE_6502_FLAGS_TO_MASK + REG_SCRATCH1]
  mov ah, [K_ASM_TABLE_6502_FLAGS_TO_X64 + REG_SCRATCH1]
  sahf
asm_x64_asm_set_intel_flags_from_scratch_END:
  ret


.globl asm_x64_set_brk_flag_in_scratch
.globl asm_x64_set_brk_flag_in_scratch_END
asm_x64_set_brk_flag_in_scratch:

  lea REG_SCRATCH1_32, [REG_SCRATCH1 + 0x10]

asm_x64_set_brk_flag_in_scratch_END:
  ret


.globl asm_x64_push_from_scratch
.globl asm_x64_push_from_scratch_END
asm_x64_push_from_scratch:

  mov [REG_6502_S_64], REG_SCRATCH1_8
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

asm_x64_push_from_scratch_END:
  ret


.globl asm_x64_push_word_from_scratch
.globl asm_x64_push_word_from_scratch_END
asm_x64_push_word_from_scratch:

  mov [REG_6502_S_64], REG_SCRATCH1_8_HI
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

  mov [REG_6502_S_64], REG_SCRATCH1_8
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

asm_x64_push_word_from_scratch_END:
  ret


.globl asm_x64_pull_to_scratch
.globl asm_x64_pull_to_scratch_END
asm_x64_pull_to_scratch:

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_S_64]

asm_x64_pull_to_scratch_END:
  ret


.globl asm_x64_pull_word_to_scratch
.globl asm_x64_pull_word_to_scratch_END
asm_x64_pull_word_to_scratch:

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_S_64]

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  mov REG_SCRATCH1_8_HI, BYTE PTR [REG_6502_S_64]

asm_x64_pull_word_to_scratch_END:
  ret


.globl asm_x64_asm_enter
.globl asm_x64_asm_enter_END
asm_x64_asm_enter:

  # param1, rdi == context
  # param2, rsi == x64 start address
  # param3, rdx == countdown

  push rbp
  mov rbp, rsp
  push rbx
  push r12
  push r13
  push r14
  push r15

  push REG_PARAM2
  mov REG_CONTEXT, REG_PARAM1
  mov REG_COUNTDOWN, REG_PARAM3

  mov REG_SCRATCH1, 0
  mov REG_SCRATCH2, 0
  mov REG_SCRATCH3, 0

  # 6502 A, X, Y, S.
  mov REG_6502_A_64, 0
  mov REG_6502_X_64, 0
  mov REG_6502_Y_64, 0
  mov REG_6502_S_64, 0
  # 6502 flags.
  mov REG_6502_OF_64, 0
  mov REG_6502_CF_64, 0
  mov REG_6502_ID_F_64, 0

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_x64_restore_AXYS_PC_flags

  pop REG_SCRATCH1
  call REG_SCRATCH1

  pop r15
  pop r14
  pop r13
  pop r12
  pop rbx
  pop rbp
  ret

asm_x64_asm_enter_END:
  ret


.globl asm_x64_asm_debug
.globl asm_x64_asm_debug_END
asm_x64_asm_debug:

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]

  call asm_x64_save_AXYS_PC_flags

  # Save Intel IP.
  mov REG_SCRATCH1, [rsp]
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_HOST_PC], REG_SCRATCH1_32

  push REG_CONTEXT
  # NOTE: doesn't trash REG_CONTEXT because both param1 and context are
  # currently rdi.
  mov REG_PARAM1, REG_CONTEXT
  mov REG_PARAM2, 0
  call [REG_CONTEXT + K_CONTEXT_OFFSET_DEBUG_CALLBACK]
  pop REG_CONTEXT

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_x64_restore_AXYS_PC_flags
  # TODO: handle different 6502 PC!

  ret

asm_x64_asm_debug_END:
  ret
