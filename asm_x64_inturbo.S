#include "asm_x64_defs.h"

.file "asm_x64_inturbo.S"
.intel_syntax noprefix
.section rodata
.text


.globl asm_x64_inturbo_JMP_scratch_plus_1_interp
.globl asm_x64_inturbo_JMP_scratch_plus_1_interp_END
asm_x64_inturbo_JMP_scratch_plus_1_interp:

  lea REG_6502_PC_16, [REG_SCRATCH1 + 1]

asm_x64_inturbo_JMP_scratch_plus_1_interp_END:
  ret


.globl asm_x64_inturbo_load_pc_from_pc
.globl asm_x64_inturbo_load_pc_from_pc_END
asm_x64_inturbo_load_pc_from_pc:

  mov REG_6502_PC_16, [REG_6502_PC + 1]

asm_x64_inturbo_load_pc_from_pc_END:
  ret


.globl asm_x64_inturbo_check_special_address
.globl asm_x64_inturbo_check_special_address_END
.globl asm_x64_inturbo_check_special_address_lea_patch
.globl asm_x64_inturbo_check_special_address_jb_patch
asm_x64_inturbo_check_special_address:

  lea REG_SCRATCH2_32, [REG_SCRATCH1 + 0xFFFF]
asm_x64_inturbo_check_special_address_lea_patch:
  bt REG_SCRATCH2_32, 16
  jb 0
asm_x64_inturbo_check_special_address_jb_patch:

asm_x64_inturbo_check_special_address_END:
  ret


.globl asm_x64_inturbo_check_countdown
.globl asm_x64_inturbo_check_countdown_END
.globl asm_x64_inturbo_check_countdown_lea_patch
.globl asm_x64_inturbo_check_countdown_jb_patch
asm_x64_inturbo_check_countdown:
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 1]
asm_x64_inturbo_check_countdown_lea_patch:
  bt REG_COUNTDOWN, 63
  jb 0
asm_x64_inturbo_check_countdown_jb_patch:

asm_x64_inturbo_check_countdown_END:
  ret


.globl asm_x64_inturbo_load_opcode
.globl asm_x64_inturbo_load_opcode_END
.globl asm_x64_inturbo_load_opcode_mov_patch
asm_x64_inturbo_load_opcode:

  movzx REG_SCRATCH3_32, BYTE PTR [REG_6502_PC + 1]
asm_x64_inturbo_load_opcode_mov_patch:

asm_x64_inturbo_load_opcode_END:
  ret


.globl asm_x64_inturbo_advance_pc
.globl asm_x64_inturbo_advance_pc_END
.globl asm_x64_inturbo_advance_pc_lea_patch
asm_x64_inturbo_advance_pc:

  lea REG_6502_PC_32, [REG_6502_PC + 1]
asm_x64_inturbo_advance_pc_lea_patch:

asm_x64_inturbo_advance_pc_END:
  ret


.globl asm_x64_inturbo_jump_opcode
.globl asm_x64_inturbo_jump_opcode_END
asm_x64_inturbo_jump_opcode:

  jmp [REG_SCRATCH3 * 8 + 0x3f000000]

asm_x64_inturbo_jump_opcode_END:
  ret


.globl asm_x64_inturbo_enter_debug
.globl asm_x64_inturbo_enter_debug_END
asm_x64_inturbo_enter_debug:

  mov REG_SCRATCH1, REG_6502_PC
  call [REG_CONTEXT + K_CONTEXT_OFFSET_UTIL_DEBUG]

asm_x64_inturbo_enter_debug_END:
  ret


.globl asm_x64_inturbo_check_interrupt
.globl asm_x64_inturbo_check_interrupt_END
.globl asm_x64_inturbo_check_interrupt_jae_patch
asm_x64_inturbo_check_interrupt:

  mov REG_SCRATCH1, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  mov REG_SCRATCH1_32, \
      DWORD PTR [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_IRQ_FIRE]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 - 1]
  bt REG_SCRATCH1_32, 31
  jae 0
asm_x64_inturbo_check_interrupt_jae_patch:

asm_x64_inturbo_check_interrupt_END:
  ret


.globl asm_x64_inturbo_jump_call_interp
.globl asm_x64_inturbo_jump_call_interp_END
.globl asm_x64_inturbo_jump_call_interp_jmp_patch
asm_x64_inturbo_jump_call_interp:

  jmp 0
asm_x64_inturbo_jump_call_interp_jmp_patch:

asm_x64_inturbo_jump_call_interp_END:
  ret


.globl asm_x64_inturbo_pc_plus_2_to_scratch
.globl asm_x64_inturbo_pc_plus_2_to_scratch_END
asm_x64_inturbo_pc_plus_2_to_scratch:

  lea REG_SCRATCH1, [REG_6502_PC + 2]

asm_x64_inturbo_pc_plus_2_to_scratch_END:
  ret


.globl asm_x64_inturbo_interrupt_vector
.globl asm_x64_inturbo_interrupt_vector_END
asm_x64_inturbo_interrupt_vector:

  mov REG_6502_PC_16, [K_BBC_MEM_READ_ADDR + K_6502_VECTOR_IRQ]

asm_x64_inturbo_interrupt_vector_END:
  ret


.globl asm_x64_inturbo_call_interp
asm_x64_inturbo_call_interp:

  mov REG_SCRATCH1_32, REG_6502_PC_32
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_x64_save_AXYS_PC_flags

  mov REG_SCRATCH3, REG_CONTEXT
  push REG_CONTEXT
  # param1 is interp object.
  mov REG_PARAM1, [REG_SCRATCH3 + K_CONTEXT_OFFSET_INTERP_OBJECT]
  # param2 is current countdown value.
  mov REG_PARAM2, REG_COUNTDOWN

  call [REG_SCRATCH3 + K_CONTEXT_OFFSET_INTERP_CALLBACK]
  pop REG_CONTEXT

  mov REG_COUNTDOWN, REG_RETURN

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_x64_restore_AXYS_PC_flags

  movzx REG_SCRATCH3_32, BYTE PTR [REG_6502_PC]
  jmp [REG_SCRATCH3 * 8 + 0x3f000000]


.globl asm_x64_inturbo_mode_zpg
.globl asm_x64_inturbo_mode_zpg_END
asm_x64_inturbo_mode_zpg:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]

asm_x64_inturbo_mode_zpg_END:
  ret


.globl asm_x64_inturbo_mode_abs
.globl asm_x64_inturbo_mode_abs_END
asm_x64_inturbo_mode_abs:

  movzx REG_SCRATCH1, WORD PTR [REG_6502_PC + 1]

asm_x64_inturbo_mode_abs_END:
  ret


.globl asm_x64_inturbo_mode_accurate_post
.globl asm_x64_inturbo_mode_accurate_post_END
asm_x64_inturbo_mode_accurate_post:

  # Adds in the extra cycle if the 8-bit address carried.
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH3]

asm_x64_inturbo_mode_accurate_post_END:
  ret


.globl asm_x64_inturbo_mode_abx
.globl asm_x64_inturbo_mode_abx_END
asm_x64_inturbo_mode_abx:

  movzx REG_SCRATCH1_32, WORD PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_16, [REG_SCRATCH1 + REG_6502_X_64]

asm_x64_inturbo_mode_abx_END:
  ret


.globl asm_x64_inturbo_mode_abx_accurate
.globl asm_x64_inturbo_mode_abx_accurate_END
asm_x64_inturbo_mode_abx_accurate:

  lahf
  movzx REG_SCRATCH3_32, REG_6502_X
  movzx REG_SCRATCH1_32, WORD PTR [REG_6502_PC + 1]

  # Calculate if there's an 8-bit address carry.
  # This incurs an extra cycle on the 6502.
  # Leave -1 in REG_SCRATCH3 if carry, 0 otherwise.
  add REG_SCRATCH3_8, REG_SCRATCH1_8
  setae REG_SCRATCH3_8
  sahf
  lea REG_SCRATCH3, [REG_SCRATCH3 - 1]

  lea REG_SCRATCH1_16, [REG_SCRATCH1 + REG_6502_X_64]

asm_x64_inturbo_mode_abx_accurate_END:
  ret


.globl asm_x64_inturbo_mode_aby
.globl asm_x64_inturbo_mode_aby_END
asm_x64_inturbo_mode_aby:

  movzx REG_SCRATCH1_32, WORD PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_16, [REG_SCRATCH1 + REG_6502_Y_64]

asm_x64_inturbo_mode_aby_END:
  ret


.globl asm_x64_inturbo_mode_aby_accurate
.globl asm_x64_inturbo_mode_aby_accurate_END
asm_x64_inturbo_mode_aby_accurate:

  lahf
  movzx REG_SCRATCH3_32, REG_6502_Y
  movzx REG_SCRATCH1_32, WORD PTR [REG_6502_PC + 1]

  # Calculate if there's an 8-bit address carry.
  # This incurs an extra cycle on the 6502.
  # Leave -1 in REG_SCRATCH3 if carry, 0 otherwise.
  add REG_SCRATCH3_8, REG_SCRATCH1_8
  setae REG_SCRATCH3_8
  sahf
  lea REG_SCRATCH3, [REG_SCRATCH3 - 1]

  lea REG_SCRATCH1_16, [REG_SCRATCH1 + REG_6502_Y_64]

asm_x64_inturbo_mode_aby_accurate_END:
  ret


.globl asm_x64_inturbo_mode_zpx
.globl asm_x64_inturbo_mode_zpx_END
asm_x64_inturbo_mode_zpx:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_6502_X_64]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_x64_inturbo_mode_zpx_END:
  ret


.globl asm_x64_inturbo_mode_zpy
.globl asm_x64_inturbo_mode_zpy_END
asm_x64_inturbo_mode_zpy:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_6502_Y_64]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_x64_inturbo_mode_zpy_END:
  ret


.globl asm_x64_inturbo_mode_idx
.globl asm_x64_inturbo_mode_idx_END
asm_x64_inturbo_mode_idx:

  # TODO: if two scratch registers were non-extended registers, we could
  # compute the hi and low byte better in parallel dependency chains. The
  # problem is we can't load into dh from extended registers.
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC + 1]

  lea REG_SCRATCH2_32, [REG_SCRATCH1 + REG_6502_X_64]
  movzx REG_SCRATCH2_32, REG_SCRATCH2_8

  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_6502_X_64 + 1]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

  mov REG_SCRATCH1_8_HI, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  mov REG_SCRATCH1_8, BYTE PTR [REG_SCRATCH2 + K_BBC_MEM_READ_ADDR]

asm_x64_inturbo_mode_idx_END:
  ret


.globl asm_x64_inturbo_mode_idy
.globl asm_x64_inturbo_mode_idy_END
asm_x64_inturbo_mode_idy:

  # This isn't accurate because it doesn't handle zero-page wrap around
  # for e.g. LDA ($FF),Y, or any idy mode with $FF as the operand.
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC + 1]
  movzx REG_SCRATCH1_32, WORD PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  lea REG_SCRATCH1_16, [REG_SCRATCH1 + REG_6502_Y_64]

asm_x64_inturbo_mode_idy_END:
  ret


.globl asm_x64_inturbo_mode_idy_accurate
.globl asm_x64_inturbo_mode_idy_accurate_END
asm_x64_inturbo_mode_idy_accurate:

  lahf
  movzx REG_SCRATCH3_32, REG_6502_Y

  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC + 1]

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

  # Calculate if there's an 8-bit address carry.
  # This incurs an extra cycle on the 6502.
  # Leave -1 in REG_SCRATCH3 if carry, 0 otherwise.
  add REG_SCRATCH3_8, REG_SCRATCH2_8
  setae REG_SCRATCH3_8
  sahf
  lea REG_SCRATCH3, [REG_SCRATCH3 - 1]

  lea REG_SCRATCH1_32, [REG_SCRATCH1 + 1]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8
  mov REG_SCRATCH1_8_HI, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

  mov REG_SCRATCH1_8, REG_SCRATCH2_8

  lea REG_SCRATCH1_16, [REG_SCRATCH1 + REG_6502_Y_64]

asm_x64_inturbo_mode_idy_accurate_END:
  ret


.globl asm_x64_inturbo_mode_ind
.globl asm_x64_inturbo_mode_ind_END
asm_x64_inturbo_mode_ind:

  movzx REG_SCRATCH1, WORD PTR [REG_6502_PC + 1]

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  lea REG_SCRATCH3_32, [REG_SCRATCH1 + 1]
  mov REG_SCRATCH1_8, REG_SCRATCH3_8
  mov REG_SCRATCH1_8_HI, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  mov REG_SCRATCH1_8, REG_SCRATCH2_8

asm_x64_inturbo_mode_ind_END:
  ret


.globl asm_x64_instruction_ADC_imm_interp
.globl asm_x64_instruction_ADC_imm_interp_END
.globl asm_x64_instruction_ADC_imm_interp_jb_patch
asm_x64_instruction_ADC_imm_interp:

  bt REG_6502_ID_F_64, 3
  jb 0
asm_x64_instruction_ADC_imm_interp_jb_patch:
  shr REG_6502_CF_64, 1
  adc REG_6502_A, BYTE PTR [REG_6502_PC + 1]
  setb REG_6502_CF
  seto REG_6502_OF

asm_x64_instruction_ADC_imm_interp_END:
  ret


.globl asm_x64_instruction_ADC_scratch_interp
.globl asm_x64_instruction_ADC_scratch_interp_END
.globl asm_x64_instruction_ADC_scratch_interp_jb_patch
asm_x64_instruction_ADC_scratch_interp:

  bt REG_6502_ID_F_64, 3
  jb 0
asm_x64_instruction_ADC_scratch_interp_jb_patch:
  shr REG_6502_CF_64, 1
  adc REG_6502_A, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  setb REG_6502_CF
  seto REG_6502_OF

asm_x64_instruction_ADC_scratch_interp_END:
  ret


.globl asm_x64_instruction_ALR_imm_interp
.globl asm_x64_instruction_ALR_imm_interp_END
asm_x64_instruction_ALR_imm_interp:

  and REG_6502_A, BYTE PTR [REG_6502_PC + 1]
  shr REG_6502_A, 1
  setb REG_6502_CF

asm_x64_instruction_ALR_imm_interp_END:
  ret


.globl asm_x64_instruction_AND_imm_interp
.globl asm_x64_instruction_AND_imm_interp_END
asm_x64_instruction_AND_imm_interp:

  and REG_6502_A, BYTE PTR [REG_6502_PC + 1]

asm_x64_instruction_AND_imm_interp_END:
  ret


.globl asm_x64_instruction_AND_scratch_interp
.globl asm_x64_instruction_AND_scratch_interp_END
asm_x64_instruction_AND_scratch_interp:

  and REG_6502_A, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_instruction_AND_scratch_interp_END:
  ret


.globl asm_x64_instruction_ASL_acc_interp
.globl asm_x64_instruction_ASL_acc_interp_END
asm_x64_instruction_ASL_acc_interp:

  shl REG_6502_A, 1
  setb REG_6502_CF

asm_x64_instruction_ASL_acc_interp_END:
  ret


.globl asm_x64_instruction_ASL_scratch_interp
.globl asm_x64_instruction_ASL_scratch_interp_END
asm_x64_instruction_ASL_scratch_interp:

  mov REG_SCRATCH2_8, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  shl REG_SCRATCH2_8, 1
  setb REG_6502_CF
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8

asm_x64_instruction_ASL_scratch_interp_END:
  ret


.globl asm_x64_instruction_BCC_interp
.globl asm_x64_instruction_BCC_interp_END
asm_x64_instruction_BCC_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  bt REG_6502_CF_64, 0
  cmovb REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BCC_interp_END:
  ret


.globl asm_x64_instruction_BCS_interp
.globl asm_x64_instruction_BCS_interp_END
asm_x64_instruction_BCS_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  bt REG_6502_CF_64, 0
  cmovae REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BCS_interp_END:
  ret


.globl asm_x64_instruction_BEQ_interp
.globl asm_x64_instruction_BEQ_interp_END
asm_x64_instruction_BEQ_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  cmovne REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BEQ_interp_END:
  ret


.globl asm_x64_instruction_BIT_interp
.globl asm_x64_instruction_BIT_interp_END
asm_x64_instruction_BIT_interp:

  mov ah, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  # Load OF.
  bt REG_6502_A_32, 14
  setb REG_6502_OF
  # Load ZF.
  test ah, REG_6502_A
  sete REG_SCRATCH1_8
  shl REG_SCRATCH1_8, 6
  # Load NF.
  and ah, 0x80
  # Put ZF / NF together.
  or ah, REG_SCRATCH1_8
  sahf

asm_x64_instruction_BIT_interp_END:
  ret


.globl asm_x64_instruction_BMI_interp
.globl asm_x64_instruction_BMI_interp_END
asm_x64_instruction_BMI_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  cmovns REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BMI_interp_END:
  ret


.globl asm_x64_instruction_BNE_interp
.globl asm_x64_instruction_BNE_interp_END
asm_x64_instruction_BNE_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  cmove REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BNE_interp_END:
  ret


.globl asm_x64_instruction_BPL_interp
.globl asm_x64_instruction_BPL_interp_END
asm_x64_instruction_BPL_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  cmovs REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BPL_interp_END:
  ret


.globl asm_x64_instruction_BVC_interp
.globl asm_x64_instruction_BVC_interp_END
asm_x64_instruction_BVC_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  bt REG_6502_OF_64, 0
  cmovb REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BVC_interp_END:
  ret


.globl asm_x64_instruction_BVS_interp
.globl asm_x64_instruction_BVS_interp_END
asm_x64_instruction_BVS_interp:

  mov REG_SCRATCH2, 0
  movsx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  bt REG_6502_OF_64, 0
  cmovae REG_SCRATCH1, REG_SCRATCH2
  lea REG_6502_PC, [REG_6502_PC + REG_SCRATCH1 + 2]

asm_x64_instruction_BVS_interp_END:
  ret


.globl asm_x64_instruction_CMP_imm_interp
.globl asm_x64_instruction_CMP_imm_interp_END
asm_x64_instruction_CMP_imm_interp:

  cmp REG_6502_A, BYTE PTR [REG_6502_PC + 1]
  setae REG_6502_CF

asm_x64_instruction_CMP_imm_interp_END:
  ret


.globl asm_x64_instruction_CMP_scratch_interp
.globl asm_x64_instruction_CMP_scratch_interp_END
asm_x64_instruction_CMP_scratch_interp:

  cmp REG_6502_A, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  setae REG_6502_CF

asm_x64_instruction_CMP_scratch_interp_END:
  ret


.globl asm_x64_instruction_CPX_imm_interp
.globl asm_x64_instruction_CPX_imm_interp_END
asm_x64_instruction_CPX_imm_interp:

  cmp REG_6502_X, BYTE PTR [REG_6502_PC + 1]
  setae REG_6502_CF

asm_x64_instruction_CPX_imm_interp_END:
  ret


.globl asm_x64_instruction_CPX_scratch_interp
.globl asm_x64_instruction_CPX_scratch_interp_END
asm_x64_instruction_CPX_scratch_interp:

  cmp REG_6502_X, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  setae REG_6502_CF

asm_x64_instruction_CPX_scratch_interp_END:
  ret


.globl asm_x64_instruction_CPY_imm_interp
.globl asm_x64_instruction_CPY_imm_interp_END
asm_x64_instruction_CPY_imm_interp:

  cmp REG_6502_Y, BYTE PTR [REG_6502_PC + 1]
  setae REG_6502_CF

asm_x64_instruction_CPY_imm_interp_END:
  ret


.globl asm_x64_instruction_CPY_scratch_interp
.globl asm_x64_instruction_CPY_scratch_interp_END
asm_x64_instruction_CPY_scratch_interp:

  cmp REG_6502_Y, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  setae REG_6502_CF

asm_x64_instruction_CPY_scratch_interp_END:
  ret


.globl asm_x64_instruction_DEC_scratch_interp
.globl asm_x64_instruction_DEC_scratch_interp_END
asm_x64_instruction_DEC_scratch_interp:

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  dec REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8

asm_x64_instruction_DEC_scratch_interp_END:
  ret


.globl asm_x64_instruction_EOR_imm_interp
.globl asm_x64_instruction_EOR_imm_interp_END
asm_x64_instruction_EOR_imm_interp:

  xor REG_6502_A, BYTE PTR [REG_6502_PC + 1]

asm_x64_instruction_EOR_imm_interp_END:
  ret


.globl asm_x64_instruction_EOR_scratch_interp
.globl asm_x64_instruction_EOR_scratch_interp_END
asm_x64_instruction_EOR_scratch_interp:

  xor REG_6502_A, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_instruction_EOR_scratch_interp_END:
  ret


.globl asm_x64_instruction_INC_scratch_interp
.globl asm_x64_instruction_INC_scratch_interp_END
asm_x64_instruction_INC_scratch_interp:

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  inc REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8

asm_x64_instruction_INC_scratch_interp_END:
  ret


.globl asm_x64_instruction_JMP_scratch_interp
.globl asm_x64_instruction_JMP_scratch_interp_END
asm_x64_instruction_JMP_scratch_interp:

  mov REG_6502_PC_16, REG_SCRATCH1_16

asm_x64_instruction_JMP_scratch_interp_END:
  ret


.globl asm_x64_instruction_LDA_imm_interp
.globl asm_x64_instruction_LDA_imm_interp_END
asm_x64_instruction_LDA_imm_interp:

  mov REG_6502_A, BYTE PTR [REG_6502_PC + 1]

asm_x64_instruction_LDA_imm_interp_END:
  ret


.globl asm_x64_instruction_LDA_scratch_interp
.globl asm_x64_instruction_LDA_scratch_interp_END
asm_x64_instruction_LDA_scratch_interp:

  mov REG_6502_A, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_instruction_LDA_scratch_interp_END:
  ret


.globl asm_x64_instruction_LDX_imm_interp
.globl asm_x64_instruction_LDX_imm_interp_END
asm_x64_instruction_LDX_imm_interp:

  mov REG_6502_X, BYTE PTR [REG_6502_PC + 1]

asm_x64_instruction_LDX_imm_interp_END:
  ret


.globl asm_x64_instruction_LDX_scratch_interp
.globl asm_x64_instruction_LDX_scratch_interp_END
asm_x64_instruction_LDX_scratch_interp:

  mov REG_6502_X, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_instruction_LDX_scratch_interp_END:
  ret


.globl asm_x64_instruction_LDY_imm_interp
.globl asm_x64_instruction_LDY_imm_interp_END
asm_x64_instruction_LDY_imm_interp:

  mov REG_6502_Y, BYTE PTR [REG_6502_PC + 1]

asm_x64_instruction_LDY_imm_interp_END:
  ret


.globl asm_x64_instruction_LDY_scratch_interp
.globl asm_x64_instruction_LDY_scratch_interp_END
asm_x64_instruction_LDY_scratch_interp:

  mov REG_6502_Y, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_instruction_LDY_scratch_interp_END:
  ret


.globl asm_x64_instruction_LSR_acc_interp
.globl asm_x64_instruction_LSR_acc_interp_END
asm_x64_instruction_LSR_acc_interp:

  shr REG_6502_A, 1
  setb REG_6502_CF

asm_x64_instruction_LSR_acc_interp_END:
  ret


.globl asm_x64_instruction_LSR_scratch_interp
.globl asm_x64_instruction_LSR_scratch_interp_END
asm_x64_instruction_LSR_scratch_interp:

  mov REG_SCRATCH2_8, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  shr REG_SCRATCH2_8, 1
  setb REG_6502_CF
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8

asm_x64_instruction_LSR_scratch_interp_END:
  ret


.globl asm_x64_instruction_ORA_imm_interp
.globl asm_x64_instruction_ORA_imm_interp_END
asm_x64_instruction_ORA_imm_interp:

  or REG_6502_A, BYTE PTR [REG_6502_PC + 1]

asm_x64_instruction_ORA_imm_interp_END:
  ret


.globl asm_x64_instruction_ORA_scratch_interp
.globl asm_x64_instruction_ORA_scratch_interp_END
asm_x64_instruction_ORA_scratch_interp:

  or REG_6502_A, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_instruction_ORA_scratch_interp_END:
  ret


.globl asm_x64_instruction_ROL_acc_interp
.globl asm_x64_instruction_ROL_acc_interp_END
asm_x64_instruction_ROL_acc_interp:

  shr REG_6502_CF_64, 1
  rcl REG_6502_A
  setb REG_6502_CF
  test REG_6502_A, REG_6502_A

asm_x64_instruction_ROL_acc_interp_END:
  ret


.globl asm_x64_instruction_ROL_scratch_interp
.globl asm_x64_instruction_ROL_scratch_interp_END
asm_x64_instruction_ROL_scratch_interp:

  shr REG_6502_CF_64, 1
  mov REG_SCRATCH2_8, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  rcl REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8
  setb REG_6502_CF
  test REG_SCRATCH2_8, REG_SCRATCH2_8

asm_x64_instruction_ROL_scratch_interp_END:
  ret


.globl asm_x64_instruction_ROR_acc_interp
.globl asm_x64_instruction_ROR_acc_interp_END
asm_x64_instruction_ROR_acc_interp:

  shr REG_6502_CF_64, 1
  rcr REG_6502_A
  setb REG_6502_CF
  test REG_6502_A, REG_6502_A

asm_x64_instruction_ROR_acc_interp_END:
  ret


.globl asm_x64_instruction_ROR_scratch_interp
.globl asm_x64_instruction_ROR_scratch_interp_END
asm_x64_instruction_ROR_scratch_interp:

  shr REG_6502_CF_64, 1
  mov REG_SCRATCH2_8, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  rcr REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8
  setb REG_6502_CF
  test REG_SCRATCH2_8, REG_SCRATCH2_8

asm_x64_instruction_ROR_scratch_interp_END:
  ret


.globl asm_x64_instruction_SAX_scratch_interp
.globl asm_x64_instruction_SAX_scratch_interp_END
asm_x64_instruction_SAX_scratch_interp:

  lahf
  mov REG_SCRATCH2_8, REG_6502_X
  and REG_SCRATCH2_8, REG_6502_A
  sahf
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8

asm_x64_instruction_SAX_scratch_interp_END:
  ret


.globl asm_x64_instruction_SBC_imm_interp
.globl asm_x64_instruction_SBC_imm_interp_END
.globl asm_x64_instruction_SBC_imm_interp_jb_patch
asm_x64_instruction_SBC_imm_interp:

  bt REG_6502_ID_F_64, 3
  jb 0
asm_x64_instruction_SBC_imm_interp_jb_patch:
  sub REG_6502_CF, 1
  sbb REG_6502_A, BYTE PTR [REG_6502_PC + 1]
  setae REG_6502_CF
  seto REG_6502_OF

asm_x64_instruction_SBC_imm_interp_END:
  ret


.globl asm_x64_instruction_SBC_scratch_interp
.globl asm_x64_instruction_SBC_scratch_interp_END
.globl asm_x64_instruction_SBC_scratch_interp_jb_patch
asm_x64_instruction_SBC_scratch_interp:

  bt REG_6502_ID_F_64, 3
  jb 0
asm_x64_instruction_SBC_scratch_interp_jb_patch:
  sub REG_6502_CF, 1
  sbb REG_6502_A, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  setae REG_6502_CF
  seto REG_6502_OF

asm_x64_instruction_SBC_scratch_interp_END:
  ret


.globl asm_x64_instruction_SLO_scratch_interp
.globl asm_x64_instruction_SLO_scratch_interp_END
asm_x64_instruction_SLO_scratch_interp:

  mov REG_SCRATCH2_8, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  shl REG_SCRATCH2_8, 1
  setb REG_6502_CF
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_SCRATCH2_8
  or REG_6502_A, REG_SCRATCH2_8

asm_x64_instruction_SLO_scratch_interp_END:
  ret


.globl asm_x64_instruction_STA_scratch_interp
.globl asm_x64_instruction_STA_scratch_interp_END
asm_x64_instruction_STA_scratch_interp:

  mov BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_6502_A

asm_x64_instruction_STA_scratch_interp_END:
  ret


.globl asm_x64_instruction_STX_scratch_interp
.globl asm_x64_instruction_STX_scratch_interp_END
asm_x64_instruction_STX_scratch_interp:

  mov BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_6502_X

asm_x64_instruction_STX_scratch_interp_END:
  ret


.globl asm_x64_instruction_STY_scratch_interp
.globl asm_x64_instruction_STY_scratch_interp_END
asm_x64_instruction_STY_scratch_interp:

  mov BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_6502_Y

asm_x64_instruction_STY_scratch_interp_END:
  ret
