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


.globl asm_x64_instruction_EXIT
.globl asm_x64_instruction_EXIT_END
asm_x64_instruction_EXIT:

  ret

asm_x64_instruction_EXIT_END:


.globl asm_x64_instruction_REAL_NOP
.globl asm_x64_instruction_REAL_NOP_END
asm_x64_instruction_REAL_NOP:

  nop

asm_x64_instruction_REAL_NOP_END:


.globl asm_x64_instruction_TRAP
.globl asm_x64_instruction_TRAP_END
asm_x64_instruction_TRAP:

  int 3

asm_x64_instruction_TRAP_END:


.globl asm_x64_instruction_CLC
.globl asm_x64_instruction_CLC_END
asm_x64_instruction_CLC:

  mov REG_6502_CF, 0

asm_x64_instruction_CLC_END:


.globl asm_x64_instruction_CLD
.globl asm_x64_instruction_CLD_END
asm_x64_instruction_CLD:

  btr REG_6502_ID_F_64, 3

asm_x64_instruction_CLD_END:


.globl asm_x64_instruction_CLI
.globl asm_x64_instruction_CLI_END
asm_x64_instruction_CLI:

  btr REG_6502_ID_F_64, 2

asm_x64_instruction_CLI_END:


.globl asm_x64_instruction_DEX
.globl asm_x64_instruction_DEX_END
asm_x64_instruction_DEX:

  dec REG_6502_X

asm_x64_instruction_DEX_END:


.globl asm_x64_instruction_DEY
.globl asm_x64_instruction_DEY_END
asm_x64_instruction_DEY:

  dec REG_6502_Y

asm_x64_instruction_DEY_END:


.globl asm_x64_instruction_INX
.globl asm_x64_instruction_INX_END
asm_x64_instruction_INX:

  inc REG_6502_X

asm_x64_instruction_INX_END:


.globl asm_x64_instruction_INY
.globl asm_x64_instruction_INY_END
asm_x64_instruction_INY:

  inc REG_6502_Y

asm_x64_instruction_INY_END:


.globl asm_x64_instruction_PHA
.globl asm_x64_instruction_PHA_END
asm_x64_instruction_PHA:

  mov [REG_6502_S_64], REG_6502_A
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

asm_x64_instruction_PHA_END:


.globl asm_x64_instruction_PLA
.globl asm_x64_instruction_PLA_END
asm_x64_instruction_PLA:

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  mov REG_6502_A, BYTE PTR [REG_6502_S_64]

asm_x64_instruction_PLA_END:


.globl asm_x64_instruction_SEC
.globl asm_x64_instruction_SEC_END
asm_x64_instruction_SEC:

  mov REG_6502_CF, 1

asm_x64_instruction_SEC_END:


.globl asm_x64_instruction_SEI
.globl asm_x64_instruction_SEI_END
asm_x64_instruction_SEI:

  bts REG_6502_ID_F_64, 2

asm_x64_instruction_SEI_END:
  ret


.globl asm_x64_instruction_TAX
.globl asm_x64_instruction_TAX_END
asm_x64_instruction_TAX:

  mov REG_6502_X, REG_6502_A

asm_x64_instruction_TAX_END:


.globl asm_x64_instruction_TAY
.globl asm_x64_instruction_TAY_END
asm_x64_instruction_TAY:

  mov REG_6502_Y, REG_6502_A

asm_x64_instruction_TAY_END:


.globl asm_x64_instruction_TSX
.globl asm_x64_instruction_TSX_END
asm_x64_instruction_TSX:

  mov REG_6502_X, REG_6502_S

asm_x64_instruction_TSX_END:


.globl asm_x64_instruction_TXS
.globl asm_x64_instruction_TXS_END
asm_x64_instruction_TXS:

  mov REG_6502_S, REG_6502_X

asm_x64_instruction_TXS_END:


.globl asm_x64_instruction_A_NZ_flags
.globl asm_x64_instruction_A_NZ_flags_END
asm_x64_instruction_A_NZ_flags:

  test REG_6502_A, REG_6502_A

asm_x64_instruction_A_NZ_flags_END:


.globl asm_x64_instruction_X_NZ_flags
.globl asm_x64_instruction_X_NZ_flags_END
asm_x64_instruction_X_NZ_flags:

  test REG_6502_X, REG_6502_X

asm_x64_instruction_X_NZ_flags_END:


.globl asm_x64_instruction_Y_NZ_flags
.globl asm_x64_instruction_Y_NZ_flags_END
asm_x64_instruction_Y_NZ_flags:

  test REG_6502_Y, REG_6502_Y

asm_x64_instruction_Y_NZ_flags_END:

.globl asm_x64_save_AXYS_PC_flags
asm_x64_save_AXYS_PC_flags:
  # Save 6502 IP.
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_PC], REG_SCRATCH1_32
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
  lea REG_SCRATCH3, [REG_6502_ID_F_64 + REG_6502_CF_64 + 0x20]
  rorx REG_6502_OF_64, REG_6502_OF_64, 58
  lea REG_SCRATCH3, [REG_SCRATCH3 + REG_6502_OF_64]
  rorx REG_6502_OF_64, REG_6502_OF_64, 6
  movzx REG_SCRATCH1_32, ah
  mov REG_SCRATCH1_8, [REG_SCRATCH1 + K_ASM_TABLE_X64_FLAGS_TO_6502]
  lea REG_SCRATCH1, [REG_SCRATCH1 + REG_SCRATCH3]

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


.globl asm_x64_push_from_scratch
.globl asm_x64_push_from_scratch_END
asm_x64_push_from_scratch:

  mov [REG_6502_S_64], REG_SCRATCH1_8
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

asm_x64_push_from_scratch_END:


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


.globl asm_x64_asm_enter
.globl asm_x64_asm_enter_END
asm_x64_asm_enter:

  # param1, rdi == context
  # param2, rsi == x64 start address

  push rbp
  mov rbp, rsp
  push rbx
  push r12
  push r13
  push r14
  push r15

  mov REG_SCRATCH1, 0
  mov REG_SCRATCH2, 0
  mov REG_SCRATCH3, 0

  mov REG_CONTEXT, REG_PARAM1
  push REG_PARAM2

  # 6502 A, X, Y, S.
  mov REG_6502_A_64, 0
  mov REG_6502_X_64, 0
  mov REG_6502_Y_64, 0
  mov REG_6502_S_64, 0
  # 6502 flags.
  mov REG_6502_OF_64, 0
  mov REG_6502_CF_64, 0
  mov REG_6502_ID_F_64, 0

  mov REG_SCRATCH1, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  mov REG_6502_A_32, [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_A]
  mov REG_6502_X_32, [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_X]
  mov REG_6502_Y_32, [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_Y]
  mov REG_6502_S_32, [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_S]
  mov REG_6502_PC_32, [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_PC]
  # Restore 6502 flags.
  movzx REG_SCRATCH1, BYTE PTR [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_FLAGS]

  call asm_x64_asm_set_intel_flags_from_scratch

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


.globl asm_x64_asm_debug
.globl asm_x64_asm_debug_END
asm_x64_asm_debug:

  # Expectation: Context object in REG_CONTEXT.
  # Expectation: 6502 PC in REG_SCRATCH1.

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]

  call asm_x64_save_AXYS_PC_flags

  # Save Intel IP.
  mov REG_SCRATCH1, [rsp]
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_HOST_PC], REG_SCRATCH1_32

  mov REG_SCRATCH3, REG_CONTEXT
  push REG_CONTEXT
  mov REG_PARAM1, [REG_SCRATCH3 + K_CONTEXT_OFFSET_DEBUG_OBJECT]
  call [REG_SCRATCH3 + K_CONTEXT_OFFSET_DEBUG_CALLBACK]
  pop REG_CONTEXT

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_x64_restore_AXYS_PC_flags
  # TODO: handle different 6502 PC!

  ret

asm_x64_asm_debug_END:
