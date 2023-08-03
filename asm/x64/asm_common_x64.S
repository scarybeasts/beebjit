#include "../asm_defs_host.h"
#include "../asm_tables_defs.h"
#include "asm_calling_convention_x64.h"
#include "asm_defs_registers_x64.h"

.file "asm/x64/asm_common_x64.S"
.intel_syntax noprefix
.text


.globl ASM_SYM(asm_enter_common)
.globl ASM_SYM(asm_enter_common_END)
ASM_SYM(asm_enter_common):

  # param1, rdi/rcx == context
  # param2, rsi/rdx == x64 start address
  # param3, rdx/r8  == countdown
  # param4, rcx/r9  == mem base

  push rbp
  # At this point: stack aligned to 16 bytes.
  mov rbp, rsp
  push rbx
  push r12
  push r13
  push r14
  push r15
  # Extra callee save registers in the Windows x64 calling convention.
  push rdi
  push rsi

  mov REG_MEM, REG_PARAM4

  # PARAM2 is start address, in either rsi or rdx. Either way it needs saving
  # as it will be overwritten.
  push REG_PARAM2
  mov REG_CONTEXT, REG_PARAM1
  mov REG_COUNTDOWN, REG_PARAM3

  mov REG_SCRATCH1, 0
  mov REG_SCRATCH2, 0
  mov REG_SCRATCH3, 0

  # 6502 A, X, Y, S, PC.
  mov REG_6502_A_64, 0
  mov REG_6502_X_64, 0
  mov REG_6502_Y_64, 0
  mov REG_6502_S_64, 0
  mov REG_6502_PC, 0
  # 6502 flags.
  mov REG_6502_OF_64, 0
  mov REG_6502_CF_64, 0
  mov REG_6502_ID_F_64, 0

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  # In the x64 JIT model, the 6502 S register is stored as a register
  # value that can also be used as an x64 pointers to where it indexes.
  or [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_S], REG_MEM_32
  or DWORD PTR [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_S], 0x100
  call ASM_SYM(asm_restore_AXYS_PC_flags)

  # This register points to an offset (0x80) in the zero page. This enables us
  # to use a signed 8-bit addressing mode to hit all of 0x00 - 0xFF.
  lea REG_MEM, [REG_MEM + REG_MEM_OFFSET]

  pop REG_SCRATCH1
  # At this point: stack aligned to 8 bytes, not 16.
  # The target is raw JIT code so we let the call's push re-align to a 16-byte
  # stack alignment.
  call REG_SCRATCH1

  pop rsi
  pop rdi
  pop r15
  pop r14
  pop r13
  pop r12
  pop rbx
  pop rbp
  ret

ASM_SYM(asm_enter_common_END):
  ret


.globl ASM_SYM(asm_instruction_REAL_NOP)
.globl ASM_SYM(asm_instruction_REAL_NOP_END)
ASM_SYM(asm_instruction_REAL_NOP):

  nop

ASM_SYM(asm_instruction_REAL_NOP_END):
  ret


.globl ASM_SYM(asm_instruction_TRAP)
.globl ASM_SYM(asm_instruction_TRAP_END)
ASM_SYM(asm_instruction_TRAP):

  int 3

ASM_SYM(asm_instruction_TRAP_END):
  ret


.globl ASM_SYM(asm_instruction_ILLEGAL)
.globl ASM_SYM(asm_instruction_ILLEGAL_END)
ASM_SYM(asm_instruction_ILLEGAL):

  ud2

ASM_SYM(asm_instruction_ILLEGAL_END):
  ret


.globl ASM_SYM(asm_instruction_BIT_value)
.globl ASM_SYM(asm_instruction_BIT_value_END)
ASM_SYM(asm_instruction_BIT_value):

  # NOTE: tried alternatives here with pdep, bextr, cmov, but this turned out
  # to be fastest.
  # Load OF.
  bt REG_SCRATCH2_32, 6
  setb REG_6502_OF
  # Load ZF.
  test REG_SCRATCH2_8, REG_6502_A
  sete REG_SCRATCH1_8
  shl REG_SCRATCH1_8, 6
  # Load NF.
  and REG_SCRATCH2_8, 0x80
  # Put ZF / NF together.
  or REG_SCRATCH1_8, REG_SCRATCH2_8
  mov ah, REG_SCRATCH1_8
  sahf

ASM_SYM(asm_instruction_BIT_value_END):
  ret


.globl ASM_SYM(asm_instruction_CLC)
.globl ASM_SYM(asm_instruction_CLC_END)
ASM_SYM(asm_instruction_CLC):

  mov REG_6502_CF, 0

ASM_SYM(asm_instruction_CLC_END):
  ret


.globl ASM_SYM(asm_instruction_CLD)
.globl ASM_SYM(asm_instruction_CLD_END)
ASM_SYM(asm_instruction_CLD):

  btr REG_6502_ID_F_32, 3

ASM_SYM(asm_instruction_CLD_END):
  ret


.globl ASM_SYM(asm_instruction_CLI)
.globl ASM_SYM(asm_instruction_CLI_END)
ASM_SYM(asm_instruction_CLI):

  btr REG_6502_ID_F_32, 2

ASM_SYM(asm_instruction_CLI_END):
  ret


.globl ASM_SYM(asm_instruction_CLV)
.globl ASM_SYM(asm_instruction_CLV_END)
ASM_SYM(asm_instruction_CLV):

  mov REG_6502_OF, 0

ASM_SYM(asm_instruction_CLV_END):
  ret


.globl ASM_SYM(asm_instruction_DEX)
.globl ASM_SYM(asm_instruction_DEX_END)
ASM_SYM(asm_instruction_DEX):

  dec REG_6502_X

ASM_SYM(asm_instruction_DEX_END):
  ret


.globl ASM_SYM(asm_instruction_DEY)
.globl ASM_SYM(asm_instruction_DEY_END)
ASM_SYM(asm_instruction_DEY):

  dec REG_6502_Y

ASM_SYM(asm_instruction_DEY_END):
  ret


.globl ASM_SYM(asm_instruction_INX)
.globl ASM_SYM(asm_instruction_INX_END)
ASM_SYM(asm_instruction_INX):

  inc REG_6502_X

ASM_SYM(asm_instruction_INX_END):
  ret


.globl ASM_SYM(asm_instruction_INY)
.globl ASM_SYM(asm_instruction_INY_END)
ASM_SYM(asm_instruction_INY):

  inc REG_6502_Y

ASM_SYM(asm_instruction_INY_END):
  ret


.globl ASM_SYM(asm_instruction_PHA)
.globl ASM_SYM(asm_instruction_PHA_END)
ASM_SYM(asm_instruction_PHA):

  mov [REG_6502_S_64], REG_6502_A
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

ASM_SYM(asm_instruction_PHA_END):
  ret


.globl ASM_SYM(asm_instruction_PLA)
.globl ASM_SYM(asm_instruction_PLA_END)
ASM_SYM(asm_instruction_PLA):

  movzx REG_6502_A_32, BYTE PTR [REG_6502_S_64 + 1]
  # Preserves carry flag! Not the overflow flag.
  inc REG_6502_S
  jne asm_instruction_PLA_no_wrap
  movzx REG_6502_A_32, BYTE PTR [REG_6502_S_64]
asm_instruction_PLA_no_wrap:

ASM_SYM(asm_instruction_PLA_END):
  ret


.globl ASM_SYM(asm_instruction_SEC)
.globl ASM_SYM(asm_instruction_SEC_END)
ASM_SYM(asm_instruction_SEC):

  mov REG_6502_CF, 1

ASM_SYM(asm_instruction_SEC_END):
  ret


.globl ASM_SYM(asm_instruction_SED)
.globl ASM_SYM(asm_instruction_SED_END)
ASM_SYM(asm_instruction_SED):

  bts REG_6502_ID_F_32, 3

ASM_SYM(asm_instruction_SED_END):
  ret


.globl ASM_SYM(asm_instruction_SEI)
.globl ASM_SYM(asm_instruction_SEI_END)
ASM_SYM(asm_instruction_SEI):

  bts REG_6502_ID_F_32, 2

ASM_SYM(asm_instruction_SEI_END):
  ret


.globl ASM_SYM(asm_instruction_TAX)
.globl ASM_SYM(asm_instruction_TAX_END)
ASM_SYM(asm_instruction_TAX):

  # Must use movzx / al because ah gets used / corrupted in various places.
  movzx REG_6502_X_32, REG_6502_A

ASM_SYM(asm_instruction_TAX_END):
  ret


.globl ASM_SYM(asm_instruction_TAY)
.globl ASM_SYM(asm_instruction_TAY_END)
ASM_SYM(asm_instruction_TAY):

  # Must use movzx / al because ah gets used / corrupted in various places.
  movzx REG_6502_Y_32, REG_6502_A

ASM_SYM(asm_instruction_TAY_END):
  ret


.globl ASM_SYM(asm_instruction_TSX)
.globl ASM_SYM(asm_instruction_TSX_END)
ASM_SYM(asm_instruction_TSX):

  movzx REG_6502_X_32, REG_6502_S

ASM_SYM(asm_instruction_TSX_END):
  ret


.globl ASM_SYM(asm_instruction_TXA)
.globl ASM_SYM(asm_instruction_TXA_END)
ASM_SYM(asm_instruction_TXA):

  mov REG_6502_A_32, REG_6502_X_32

ASM_SYM(asm_instruction_TXA_END):
  ret


.globl ASM_SYM(asm_instruction_TXS)
.globl ASM_SYM(asm_instruction_TXS_END)
ASM_SYM(asm_instruction_TXS):

  mov REG_6502_S, REG_6502_X

ASM_SYM(asm_instruction_TXS_END):
  ret


.globl ASM_SYM(asm_instruction_TYA)
.globl ASM_SYM(asm_instruction_TYA_END)
ASM_SYM(asm_instruction_TYA):

  mov REG_6502_A_32, REG_6502_Y_32

ASM_SYM(asm_instruction_TYA_END):
  ret


.globl ASM_SYM(asm_instruction_A_NZ_flags)
.globl ASM_SYM(asm_instruction_A_NZ_flags_END)
ASM_SYM(asm_instruction_A_NZ_flags):

  test REG_6502_A, REG_6502_A

ASM_SYM(asm_instruction_A_NZ_flags_END):
  ret


.globl ASM_SYM(asm_instruction_X_NZ_flags)
.globl ASM_SYM(asm_instruction_X_NZ_flags_END)
ASM_SYM(asm_instruction_X_NZ_flags):

  test REG_6502_X, REG_6502_X

ASM_SYM(asm_instruction_X_NZ_flags_END):
  ret


.globl ASM_SYM(asm_instruction_Y_NZ_flags)
.globl ASM_SYM(asm_instruction_Y_NZ_flags_END)
ASM_SYM(asm_instruction_Y_NZ_flags):

  test REG_6502_Y, REG_6502_Y

ASM_SYM(asm_instruction_Y_NZ_flags_END):
  ret

.globl ASM_SYM(asm_save_AXYS_PC_flags)
ASM_SYM(asm_save_AXYS_PC_flags):
  # Save 6502 IP.
  # May not be used / set by every asm backend.
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_PC], REG_6502_PC_32
  # Save A, X, Y, S.
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_A], REG_6502_A_32
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_X], REG_6502_X_32
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_Y], REG_6502_Y_32
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_S], REG_6502_S_32

  call ASM_SYM(asm_asm_emit_intel_flags_to_scratch)

  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_FLAGS], REG_SCRATCH1_32

  ret


.globl ASM_SYM(asm_restore_AXYS_PC_flags)
ASM_SYM(asm_restore_AXYS_PC_flags):

  # Restore A, X, Y, S.
  mov REG_6502_A_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_A]
  mov REG_6502_X_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_X]
  mov REG_6502_Y_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_Y]
  mov REG_6502_S_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_S]
  # Restore 6502 IP.
  # May not be used / set by every asm backend.
  mov REG_6502_PC_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_PC]
  # Restore 6502 flags.
  movzx REG_SCRATCH1, BYTE PTR [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_FLAGS]

  call ASM_SYM(asm_asm_set_intel_flags_from_scratch)

  ret


.globl ASM_SYM(asm_asm_emit_intel_flags_to_scratch)
.globl ASM_SYM(asm_asm_emit_intel_flags_to_scratch_END)
ASM_SYM(asm_asm_emit_intel_flags_to_scratch):
  lahf
  # Add IF+DF, CF and "always set" (0x20).
  lea REG_SCRATCH3_32, [REG_6502_ID_F_64 + REG_6502_CF_64 + 0x20]
  # Add OF.
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_OF_64 + K_ASM_TABLE_OF_TO_6502]
  lea REG_SCRATCH3_32, [REG_SCRATCH3 + REG_SCRATCH1]

  movzx REG_SCRATCH1_32, ah
  movzx REG_SCRATCH1_32, BYTE PTR [REG_SCRATCH1 + K_ASM_TABLE_X64_FLAGS_TO_6502]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_SCRATCH3]

ASM_SYM(asm_asm_emit_intel_flags_to_scratch_END):
  ret


.globl ASM_SYM(asm_asm_set_intel_flags_from_scratch)
.globl ASM_SYM(asm_asm_set_intel_flags_from_scratch_END)
ASM_SYM(asm_asm_set_intel_flags_from_scratch):
  mov REG_6502_OF_64, REG_SCRATCH1
  and REG_6502_OF, 0x40
  shr REG_6502_OF, 6
  mov REG_6502_CF_64, REG_SCRATCH1
  and REG_6502_CF, 0x01

  mov REG_6502_ID_F, [K_ASM_TABLE_6502_FLAGS_TO_MASK + REG_SCRATCH1]
  mov ah, [K_ASM_TABLE_6502_FLAGS_TO_X64 + REG_SCRATCH1]
  sahf
ASM_SYM(asm_asm_set_intel_flags_from_scratch_END):
  ret


.globl ASM_SYM(asm_set_brk_flag_in_scratch)
.globl ASM_SYM(asm_set_brk_flag_in_scratch_END)
ASM_SYM(asm_set_brk_flag_in_scratch):

  lea REG_SCRATCH1_32, [REG_SCRATCH1 + 0x10]

ASM_SYM(asm_set_brk_flag_in_scratch_END):
  ret


.globl ASM_SYM(asm_push_from_scratch)
.globl ASM_SYM(asm_push_from_scratch_END)
ASM_SYM(asm_push_from_scratch):

  mov [REG_6502_S_64], REG_SCRATCH1_8
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

ASM_SYM(asm_push_from_scratch_END):
  ret


.globl ASM_SYM(asm_push_word_from_scratch)
.globl ASM_SYM(asm_push_word_from_scratch_END)
ASM_SYM(asm_push_word_from_scratch):

  mov REG_SCRATCH2_32, REG_SCRATCH1_32
  lahf
  shr REG_SCRATCH2_32, 8
  sahf
  mov [REG_6502_S_64], REG_SCRATCH2_8
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

  mov [REG_6502_S_64], REG_SCRATCH1_8
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

ASM_SYM(asm_push_word_from_scratch_END):
  ret


.globl ASM_SYM(asm_pull_to_scratch)
.globl ASM_SYM(asm_pull_to_scratch_END)
ASM_SYM(asm_pull_to_scratch):

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_S_64]

ASM_SYM(asm_pull_to_scratch_END):
  ret


.globl ASM_SYM(asm_pull_word_to_scratch)
.globl ASM_SYM(asm_pull_word_to_scratch_END)
ASM_SYM(asm_pull_word_to_scratch):

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_S_64]

  lea REG_SCRATCH3, [REG_6502_S_64 + 1]
  mov REG_6502_S, REG_SCRATCH3_8
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_S_64]
  lahf
  shl REG_SCRATCH1_32, 8
  or REG_SCRATCH1_32, REG_SCRATCH2_32
  sahf

ASM_SYM(asm_pull_word_to_scratch_END):
  ret


.globl ASM_SYM(asm_debug)
.globl ASM_SYM(asm_debug_END)
ASM_SYM(asm_debug):
  # Some optimizations cache values across opcodes in REG_ADDR or host flags.
  # So they must be saved.
  pushfq
  push REG_ADDR

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]

  call ASM_SYM(asm_save_AXYS_PC_flags)

  # Save Intel IP.
  mov REG_SCRATCH1, [rsp]
  mov [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_HOST_PC], REG_SCRATCH1_32

  push REG_CONTEXT
  # NOTE: doesn't trash REG_CONTEXT because both param1 and context are
  # currently rdi.
  mov REG_PARAM1, REG_CONTEXT
  mov REG_PARAM2, 0
  # Win x64 shadow space convention.
  sub rsp, 32
  call [REG_CONTEXT + K_CONTEXT_OFFSET_DEBUG_CALLBACK]
  add rsp, 32
  pop REG_CONTEXT

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call ASM_SYM(asm_restore_AXYS_PC_flags)
  # TODO: handle different 6502 PC!

  pop REG_ADDR
  popf

  ret

ASM_SYM(asm_debug_END):
  ret


# Not called in the x64 model.
.globl ASM_SYM(asm_debug_trampoline)
.globl ASM_SYM(asm_debug_trampoline_END)
ASM_SYM(asm_debug_trampoline):
  int 3
  ret

ASM_SYM(asm_debug_trampoline_END):
  ret
