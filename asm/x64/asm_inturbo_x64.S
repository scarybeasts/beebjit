#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "../asm_jit_defs.h"
#include "asm_calling_convention_x64.h"
#include "asm_defs_registers_x64.h"

.file "asm/x64/asm_inturbo_x64.S"
.intel_syntax noprefix
.section rodata
.text


.globl asm_inturbo_save_countdown
.globl asm_inturbo_save_countdown_END
asm_inturbo_save_countdown:

  mov REG_SCRATCH4, REG_COUNTDOWN

asm_inturbo_save_countdown_END:
  ret


.globl asm_inturbo_JMP_scratch_plus_1_interp
.globl asm_inturbo_JMP_scratch_plus_1_interp_END
asm_inturbo_JMP_scratch_plus_1_interp:

  lea REG_6502_PC_32, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR + 1]

asm_inturbo_JMP_scratch_plus_1_interp_END:
  ret


.globl asm_inturbo_load_pc_from_pc
.globl asm_inturbo_load_pc_from_pc_END
asm_inturbo_load_pc_from_pc:

  movzx REG_6502_PC_32, WORD PTR [REG_6502_PC + 1]
  lea REG_6502_PC_32, [REG_6502_PC + K_BBC_MEM_READ_FULL_ADDR]

asm_inturbo_load_pc_from_pc_END:
  ret


.globl asm_inturbo_check_special_address
.globl asm_inturbo_check_special_address_END
.globl asm_inturbo_check_special_address_lea_patch
.globl asm_inturbo_check_special_address_jb_patch
asm_inturbo_check_special_address:

  lea REG_SCRATCH3_32, [REG_SCRATCH1 + 0xFFFF]
asm_inturbo_check_special_address_lea_patch:
  bt REG_SCRATCH3_32, 16
  jb asm_unpatched_branch_target
asm_inturbo_check_special_address_jb_patch:

asm_inturbo_check_special_address_END:
  ret


.globl asm_inturbo_check_countdown
.globl asm_inturbo_check_countdown_END
.globl asm_inturbo_check_countdown_lea_patch
.globl asm_inturbo_check_countdown_jb_patch
asm_inturbo_check_countdown:

  lea REG_COUNTDOWN, [REG_COUNTDOWN - 1]
asm_inturbo_check_countdown_lea_patch:
  bt REG_COUNTDOWN, 63
  jb asm_unpatched_branch_target
asm_inturbo_check_countdown_jb_patch:

asm_inturbo_check_countdown_END:
  ret


.globl asm_inturbo_commit_branch
.globl asm_inturbo_commit_branch_END
asm_inturbo_commit_branch:

  mov REG_6502_PC, REG_SCRATCH1

asm_inturbo_commit_branch_END:
  ret


.globl asm_inturbo_check_decimal
.globl asm_inturbo_check_decimal_END
.globl asm_inturbo_check_decimal_jb_patch
asm_inturbo_check_decimal:

  bt REG_6502_ID_F_64, 3
  jb asm_unpatched_branch_target
asm_inturbo_check_decimal_jb_patch:

asm_inturbo_check_decimal_END:
  ret


.globl asm_inturbo_check_interrupt
.globl asm_inturbo_check_interrupt_END
.globl asm_inturbo_check_interrupt_jae_patch
asm_inturbo_check_interrupt:

  mov REG_SCRATCH1, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  mov REG_SCRATCH1_32, \
      DWORD PTR [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_IRQ_FIRE]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 - 1]
  bt REG_SCRATCH1_32, 31
  jae asm_unpatched_branch_target
asm_inturbo_check_interrupt_jae_patch:

asm_inturbo_check_interrupt_END:
  ret


.globl asm_inturbo_load_opcode
.globl asm_inturbo_load_opcode_END
.globl asm_inturbo_load_opcode_mov_patch
asm_inturbo_load_opcode:

  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC + 1]
asm_inturbo_load_opcode_mov_patch:

asm_inturbo_load_opcode_END:
  ret


.globl asm_inturbo_advance_pc
.globl asm_inturbo_advance_pc_END
.globl asm_inturbo_advance_pc_lea_patch
asm_inturbo_advance_pc:

  lea REG_6502_PC_32, [REG_6502_PC + 1]
asm_inturbo_advance_pc_lea_patch:

asm_inturbo_advance_pc_END:
  ret


.globl asm_inturbo_jump_opcode
.globl asm_inturbo_jump_opcode_END
asm_inturbo_jump_opcode:

  lahf
  shl REG_SCRATCH1_32, K_INTURBO_OPCODES_SHIFT
  sahf
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_INTURBO_OPCODES]
  jmp REG_SCRATCH1

asm_inturbo_jump_opcode_END:
  ret


.globl asm_inturbo_ret
.globl asm_inturbo_ret_END
asm_inturbo_ret:

  ret

asm_inturbo_ret_END:
  ret


.globl asm_inturbo_enter_debug
.globl asm_inturbo_enter_debug_END
asm_inturbo_enter_debug:

  call asm_unpatched_branch_target
asm_inturbo_enter_debug_END:
  ret


.globl asm_inturbo_jump_call_interp
.globl asm_inturbo_jump_call_interp_END
.globl asm_inturbo_jump_call_interp_jmp_patch
asm_inturbo_jump_call_interp:

  jmp asm_unpatched_branch_target
asm_inturbo_jump_call_interp_jmp_patch:

asm_inturbo_jump_call_interp_END:
  ret


.globl asm_inturbo_do_write_invalidation
.globl asm_inturbo_do_write_invalidation_END
asm_inturbo_do_write_invalidation:

  # The pointer to the code blocks is stored in the private pointer space of
  # our context object.
  mov REG_SCRATCH2, [REG_CONTEXT]
  mov REG_SCRATCH1_32, [REG_SCRATCH2 + REG_SCRATCH1 * 4]
  mov WORD PTR [REG_SCRATCH1], 0x17ff

asm_inturbo_do_write_invalidation_END:
  ret


.globl asm_inturbo_pc_plus_2_to_scratch
.globl asm_inturbo_pc_plus_2_to_scratch_END
asm_inturbo_pc_plus_2_to_scratch:

  lea REG_SCRATCH1_32, [REG_6502_PC - K_BBC_MEM_READ_FULL_ADDR + 2]

asm_inturbo_pc_plus_2_to_scratch_END:
  ret


.globl asm_inturbo_interrupt_vector
.globl asm_inturbo_interrupt_vector_END
asm_inturbo_interrupt_vector:

  movzx REG_6502_PC_32, WORD PTR [K_BBC_MEM_READ_FULL_ADDR + K_6502_VECTOR_IRQ]
  lea REG_6502_PC_32, [REG_6502_PC + K_BBC_MEM_READ_FULL_ADDR]

asm_inturbo_interrupt_vector_END:
  ret


.globl asm_inturbo_call_interp
asm_inturbo_call_interp:

  mov REG_COUNTDOWN, REG_SCRATCH4
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_save_AXYS_PC_flags

  # Save REG_CONTEXT because it's currently the same as REG_PARAM1 in the
  # AMD64 calling convention, which is overwritten below.
  mov REG_SCRATCH3, REG_CONTEXT

  # Double push, including a dummy push, to keep stack correctly aligned.
  push REG_CONTEXT
  push REG_CONTEXT
  # param1 is interp object.
  mov REG_PARAM1, [REG_SCRATCH3 + K_CONTEXT_OFFSET_INTERP_OBJECT]
  # param2 is stack storage for 2x int64 return values.
  lea REG_PARAM2, [rsp - 16]
  # param3 is current countdown value.
  mov REG_PARAM3, REG_COUNTDOWN

  # Return value space and Win x64 shadow space convention.
  sub rsp, 16 + 32
  call [REG_SCRATCH3 + K_CONTEXT_OFFSET_INTERP_CALLBACK]
  add rsp, 16 + 32
  mov REG_COUNTDOWN, [rsp - 16]
  mov REG_RETURN, [rsp - 8]
  pop REG_CONTEXT
  pop REG_CONTEXT

  test REG_RETURN, REG_RETURN
  je not_exiting
  ret

not_exiting:
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_restore_AXYS_PC_flags

  movzx REG_SCRATCH3_32, BYTE PTR [REG_6502_PC]
  lahf
  shl REG_SCRATCH3_32, K_INTURBO_OPCODES_SHIFT
  sahf
  lea REG_SCRATCH3_32, [REG_SCRATCH3 + K_INTURBO_OPCODES]
  jmp REG_SCRATCH3


.globl asm_inturbo_mode_rel
.globl asm_inturbo_mode_rel_END
asm_inturbo_mode_rel:

  movsx REG_SCRATCH2, BYTE PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1, [REG_6502_PC + 2]
  lea REG_SCRATCH2, [REG_SCRATCH1 + REG_SCRATCH2]

asm_inturbo_mode_rel_END:
  ret


.globl asm_inturbo_mode_zpg
.globl asm_inturbo_mode_zpg_END
asm_inturbo_mode_zpg:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]

asm_inturbo_mode_zpg_END:
  ret


.globl asm_inturbo_mode_abs
.globl asm_inturbo_mode_abs_END
asm_inturbo_mode_abs:

  movzx REG_SCRATCH1, WORD PTR [REG_6502_PC + 1]

asm_inturbo_mode_abs_END:
  ret


.globl asm_inturbo_mode_abx
.globl asm_inturbo_mode_abx_END
asm_inturbo_mode_abx:

  movzx REG_SCRATCH2_32, WORD PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_32, [REG_SCRATCH2 + REG_6502_X_64]

asm_inturbo_mode_abx_END:
  ret


.globl asm_inturbo_mode_abx_check_page_crossing
.globl asm_inturbo_mode_abx_check_page_crossing_END
asm_inturbo_mode_abx_check_page_crossing:

  movzx REG_SCRATCH2, REG_SCRATCH2_8
  lea REG_SCRATCH2, [REG_SCRATCH2 + REG_6502_X_64]
  bt REG_SCRATCH2_32, 8
  jnc asm_inturbo_mode_abx_check_page_crossing_END
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 1]

asm_inturbo_mode_abx_check_page_crossing_END:
  ret


.globl asm_inturbo_mode_aby
.globl asm_inturbo_mode_aby_END
asm_inturbo_mode_aby:

  movzx REG_SCRATCH2_32, WORD PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_32, [REG_SCRATCH2 + REG_6502_Y_64]

asm_inturbo_mode_aby_END:
  ret


.globl asm_inturbo_mode_aby_check_page_crossing
.globl asm_inturbo_mode_aby_check_page_crossing_END
asm_inturbo_mode_aby_check_page_crossing:

  movzx REG_SCRATCH2, REG_SCRATCH2_8
  lea REG_SCRATCH2, [REG_SCRATCH2 + REG_6502_Y_64]
  bt REG_SCRATCH2_32, 8
  jnc asm_inturbo_mode_aby_check_page_crossing_END
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 1]

asm_inturbo_mode_aby_check_page_crossing_END:
  ret


.globl asm_inturbo_mode_zpx
.globl asm_inturbo_mode_zpx_END
asm_inturbo_mode_zpx:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_6502_X_64]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_inturbo_mode_zpx_END:
  ret


.globl asm_inturbo_mode_zpy
.globl asm_inturbo_mode_zpy_END
asm_inturbo_mode_zpy:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_6502_Y_64]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_inturbo_mode_zpy_END:
  ret


.globl asm_inturbo_mode_idx
.globl asm_inturbo_mode_idx_jump_patch
.globl asm_inturbo_mode_idx_END
asm_inturbo_mode_idx:

  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC + 1]

  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_6502_X_64]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

  lea REG_SCRATCH2_32, [REG_SCRATCH1 + 1]
  movzx REG_SCRATCH1_32, WORD PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  # Handle special case of 0xFF via the interpreter.
  bt REG_SCRATCH2_32, 8
  jb asm_unpatched_branch_target
asm_inturbo_mode_idx_jump_patch:

asm_inturbo_mode_idx_END:
  ret


.globl asm_inturbo_mode_idy
.globl asm_inturbo_mode_idy_jump_patch
.globl asm_inturbo_mode_idy_END
asm_inturbo_mode_idy:

  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC + 1]

  lea REG_SCRATCH3_32, [REG_SCRATCH1 + 1]

  movzx REG_SCRATCH2_32, WORD PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  lea REG_SCRATCH1_32, [REG_SCRATCH2 + REG_6502_Y_64]

  # Handle special case of 0xFF via the interpreter.
  bt REG_SCRATCH3_32, 8
  jc asm_unpatched_branch_target
asm_inturbo_mode_idy_jump_patch:

asm_inturbo_mode_idy_END:
  ret


.globl asm_inturbo_mode_idy_check_page_crossing
.globl asm_inturbo_mode_idy_check_page_crossing_END
asm_inturbo_mode_idy_check_page_crossing:

  movzx REG_SCRATCH2, REG_SCRATCH2_8
  lea REG_SCRATCH2, [REG_SCRATCH2 + REG_6502_Y_64]
  bt REG_SCRATCH2_32, 8
  jnc asm_inturbo_mode_idy_check_page_crossing_END
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 1]

asm_inturbo_mode_idy_check_page_crossing_END:
  ret


.globl asm_inturbo_mode_ind
.globl asm_inturbo_mode_ind_END
asm_inturbo_mode_ind:

  # NOTE: this does handle page crossings, i.e. JMP (&2DFF).
  movzx REG_SCRATCH1, WORD PTR [REG_6502_PC + 1]

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  lea REG_SCRATCH3_32, [REG_SCRATCH1 + 1]
  mov REG_SCRATCH1_8, REG_SCRATCH3_8
  mov REG_SCRATCH1_8_HI, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  mov REG_SCRATCH1_8, REG_SCRATCH2_8

asm_inturbo_mode_ind_END:
  ret


.globl asm_instruction_Bxx_interp_accurate
.globl asm_instruction_Bxx_interp_accurate_END
.globl asm_instruction_Bxx_interp_accurate_jb_patch
.globl asm_instruction_Bxx_interp_accurate_not_taken_target
asm_instruction_Bxx_interp_accurate:

  lahf
  xor REG_SCRATCH1_32, REG_SCRATCH2_32
  sahf
  bt REG_SCRATCH1_32, 8
  mov REG_SCRATCH1_32, 0
  setnc REG_SCRATCH1_8

  lea REG_COUNTDOWN, [REG_COUNTDOWN - 2]
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH1]
  mov REG_SCRATCH1, REG_SCRATCH2
asm_instruction_Bxx_interp_accurate_not_taken_target:

asm_instruction_Bxx_interp_accurate_END:
  ret


.globl asm_instruction_ADC_imm_interp
.globl asm_instruction_ADC_imm_interp_END
asm_instruction_ADC_imm_interp:

  shr REG_6502_CF_64, 1
  adc REG_6502_A, [REG_6502_PC + 1]
  setb REG_6502_CF
  seto REG_6502_OF

asm_instruction_ADC_imm_interp_END:
  ret


.globl asm_instruction_ADC_scratch_interp
.globl asm_instruction_ADC_scratch_interp_END
asm_instruction_ADC_scratch_interp:

  shr REG_6502_CF_64, 1
  adc REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  setb REG_6502_CF
  seto REG_6502_OF

asm_instruction_ADC_scratch_interp_END:
  ret


.globl asm_instruction_ALR_imm_interp
.globl asm_instruction_ALR_imm_interp_END
asm_instruction_ALR_imm_interp:

  and REG_6502_A, [REG_6502_PC + 1]
  shr REG_6502_A, 1
  setb REG_6502_CF

asm_instruction_ALR_imm_interp_END:
  ret


.globl asm_instruction_AND_imm_interp
.globl asm_instruction_AND_imm_interp_END
asm_instruction_AND_imm_interp:

  and REG_6502_A, [REG_6502_PC + 1]

asm_instruction_AND_imm_interp_END:
  ret


.globl asm_instruction_AND_scratch_interp
.globl asm_instruction_AND_scratch_interp_END
asm_instruction_AND_scratch_interp:

  and REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_AND_scratch_interp_END:
  ret


.globl asm_instruction_ASL_acc_interp
.globl asm_instruction_ASL_acc_interp_END
asm_instruction_ASL_acc_interp:

  shl REG_6502_A, 1
  setb REG_6502_CF

asm_instruction_ASL_acc_interp_END:
  ret


.globl asm_instruction_ASL_scratch_interp
.globl asm_instruction_ASL_scratch_interp_END
asm_instruction_ASL_scratch_interp:

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  shl REG_SCRATCH2_8, 1
  setb REG_6502_CF
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8

asm_instruction_ASL_scratch_interp_END:
  ret


.globl asm_instruction_BCC_interp
.globl asm_instruction_BCC_interp_END
asm_instruction_BCC_interp:

  bt REG_6502_CF_64, 0
  cmovnc REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BCC_interp_END:
  ret


.globl asm_instruction_BCC_interp_accurate
.globl asm_instruction_BCC_interp_accurate_END
.globl asm_instruction_BCC_interp_accurate_jump_patch
asm_instruction_BCC_interp_accurate:

  bt REG_6502_CF_64, 0
  jb asm_unpatched_branch_target
asm_instruction_BCC_interp_accurate_jump_patch:

asm_instruction_BCC_interp_accurate_END:
  ret


.globl asm_instruction_BCS_interp
.globl asm_instruction_BCS_interp_END
asm_instruction_BCS_interp:

  bt REG_6502_CF_64, 0
  cmovc REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BCS_interp_END:
  ret


.globl asm_instruction_BCS_interp_accurate
.globl asm_instruction_BCS_interp_accurate_END
.globl asm_instruction_BCS_interp_accurate_jump_patch
asm_instruction_BCS_interp_accurate:

  bt REG_6502_CF_64, 0
  jae asm_unpatched_branch_target
asm_instruction_BCS_interp_accurate_jump_patch:

asm_instruction_BCS_interp_accurate_END:
  ret


.globl asm_instruction_BEQ_interp
.globl asm_instruction_BEQ_interp_END
asm_instruction_BEQ_interp:

  cmove REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BEQ_interp_END:
  ret


.globl asm_instruction_BEQ_interp_accurate
.globl asm_instruction_BEQ_interp_accurate_END
.globl asm_instruction_BEQ_interp_accurate_jump_patch
asm_instruction_BEQ_interp_accurate:

  jne asm_unpatched_branch_target
asm_instruction_BEQ_interp_accurate_jump_patch:

asm_instruction_BEQ_interp_accurate_END:
  ret


.globl asm_instruction_BIT_interp
.globl asm_instruction_BIT_interp_END
asm_instruction_BIT_interp:

  movzx REG_SCRATCH1_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_BIT_interp_END:
  ret


.globl asm_instruction_BMI_interp
.globl asm_instruction_BMI_interp_END
asm_instruction_BMI_interp:

  cmovs REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BMI_interp_END:
  ret


.globl asm_instruction_BMI_interp_accurate
.globl asm_instruction_BMI_interp_accurate_END
.globl asm_instruction_BMI_interp_accurate_jump_patch
asm_instruction_BMI_interp_accurate:

  jns asm_unpatched_branch_target
asm_instruction_BMI_interp_accurate_jump_patch:

asm_instruction_BMI_interp_accurate_END:
  ret


.globl asm_instruction_BNE_interp
.globl asm_instruction_BNE_interp_END
asm_instruction_BNE_interp:

  cmovne REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BNE_interp_END:
  ret


.globl asm_instruction_BNE_interp_accurate
.globl asm_instruction_BNE_interp_accurate_END
.globl asm_instruction_BNE_interp_accurate_jump_patch
asm_instruction_BNE_interp_accurate:

  je asm_unpatched_branch_target
asm_instruction_BNE_interp_accurate_jump_patch:

asm_instruction_BNE_interp_accurate_END:
  ret


.globl asm_instruction_BPL_interp
.globl asm_instruction_BPL_interp_END
asm_instruction_BPL_interp:

  cmovns REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BPL_interp_END:
  ret


.globl asm_instruction_BPL_interp_accurate
.globl asm_instruction_BPL_interp_accurate_END
.globl asm_instruction_BPL_interp_accurate_jump_patch
asm_instruction_BPL_interp_accurate:

  js asm_unpatched_branch_target
asm_instruction_BPL_interp_accurate_jump_patch:

asm_instruction_BPL_interp_accurate_END:
  ret


.globl asm_instruction_BVC_interp
.globl asm_instruction_BVC_interp_END
asm_instruction_BVC_interp:

  bt REG_6502_OF_64, 0
  cmovnc REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BVC_interp_END:
  ret


.globl asm_instruction_BVC_interp_accurate
.globl asm_instruction_BVC_interp_accurate_END
.globl asm_instruction_BVC_interp_accurate_jump_patch
asm_instruction_BVC_interp_accurate:

  bt REG_6502_OF_64, 0
  jb asm_unpatched_branch_target
asm_instruction_BVC_interp_accurate_jump_patch:

asm_instruction_BVC_interp_accurate_END:
  ret


.globl asm_instruction_BVS_interp
.globl asm_instruction_BVS_interp_END
asm_instruction_BVS_interp:

  bt REG_6502_OF_64, 0
  cmovc REG_SCRATCH1, REG_SCRATCH2


asm_instruction_BVS_interp_END:
  ret


.globl asm_instruction_BVS_interp_accurate
.globl asm_instruction_BVS_interp_accurate_END
.globl asm_instruction_BVS_interp_accurate_jump_patch
asm_instruction_BVS_interp_accurate:

  bt REG_6502_OF_64, 0
  jae asm_unpatched_branch_target
asm_instruction_BVS_interp_accurate_jump_patch:

asm_instruction_BVS_interp_accurate_END:
  ret


.globl asm_instruction_CMP_imm_interp
.globl asm_instruction_CMP_imm_interp_END
asm_instruction_CMP_imm_interp:

  cmp REG_6502_A, [REG_6502_PC + 1]
  setae REG_6502_CF

asm_instruction_CMP_imm_interp_END:
  ret


.globl asm_instruction_CMP_scratch_interp
.globl asm_instruction_CMP_scratch_interp_END
asm_instruction_CMP_scratch_interp:

  cmp REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  setae REG_6502_CF

asm_instruction_CMP_scratch_interp_END:
  ret


.globl asm_instruction_CPX_imm_interp
.globl asm_instruction_CPX_imm_interp_END
asm_instruction_CPX_imm_interp:

  cmp REG_6502_X, [REG_6502_PC + 1]
  setae REG_6502_CF

asm_instruction_CPX_imm_interp_END:
  ret


.globl asm_instruction_CPX_scratch_interp
.globl asm_instruction_CPX_scratch_interp_END
asm_instruction_CPX_scratch_interp:

  cmp REG_6502_X, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  setae REG_6502_CF

asm_instruction_CPX_scratch_interp_END:
  ret


.globl asm_instruction_CPY_imm_interp
.globl asm_instruction_CPY_imm_interp_END
asm_instruction_CPY_imm_interp:

  cmp REG_6502_Y, [REG_6502_PC + 1]
  setae REG_6502_CF

asm_instruction_CPY_imm_interp_END:
  ret


.globl asm_instruction_CPY_scratch_interp
.globl asm_instruction_CPY_scratch_interp_END
asm_instruction_CPY_scratch_interp:

  cmp REG_6502_Y, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  setae REG_6502_CF

asm_instruction_CPY_scratch_interp_END:
  ret


.globl asm_instruction_DEC_scratch_interp
.globl asm_instruction_DEC_scratch_interp_END
asm_instruction_DEC_scratch_interp:

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  dec REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8

asm_instruction_DEC_scratch_interp_END:
  ret


.globl asm_instruction_EOR_imm_interp
.globl asm_instruction_EOR_imm_interp_END
asm_instruction_EOR_imm_interp:

  xor REG_6502_A, [REG_6502_PC + 1]

asm_instruction_EOR_imm_interp_END:
  ret


.globl asm_instruction_EOR_scratch_interp
.globl asm_instruction_EOR_scratch_interp_END
asm_instruction_EOR_scratch_interp:

  xor REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_EOR_scratch_interp_END:
  ret


.globl asm_instruction_INC_scratch_interp
.globl asm_instruction_INC_scratch_interp_END
asm_instruction_INC_scratch_interp:

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  inc REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8

asm_instruction_INC_scratch_interp_END:
  ret


.globl asm_instruction_JMP_scratch_interp
.globl asm_instruction_JMP_scratch_interp_END
asm_instruction_JMP_scratch_interp:

  lea REG_6502_PC_32, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_JMP_scratch_interp_END:
  ret


.globl asm_instruction_LDA_imm_interp
.globl asm_instruction_LDA_imm_interp_END
asm_instruction_LDA_imm_interp:

  movzx REG_6502_A_32, BYTE PTR [REG_6502_PC + 1]

asm_instruction_LDA_imm_interp_END:
  ret


.globl asm_instruction_LDA_scratch_interp
.globl asm_instruction_LDA_scratch_interp_END
asm_instruction_LDA_scratch_interp:

  movzx REG_6502_A_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_LDA_scratch_interp_END:
  ret


.globl asm_instruction_LDX_imm_interp
.globl asm_instruction_LDX_imm_interp_END
asm_instruction_LDX_imm_interp:

  movzx REG_6502_X_32, BYTE PTR [REG_6502_PC + 1]

asm_instruction_LDX_imm_interp_END:
  ret


.globl asm_instruction_LDX_scratch_interp
.globl asm_instruction_LDX_scratch_interp_END
asm_instruction_LDX_scratch_interp:

  movzx REG_6502_X_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_LDX_scratch_interp_END:
  ret


.globl asm_instruction_LDY_imm_interp
.globl asm_instruction_LDY_imm_interp_END
asm_instruction_LDY_imm_interp:

  movzx REG_6502_Y_32, BYTE PTR [REG_6502_PC + 1]

asm_instruction_LDY_imm_interp_END:
  ret


.globl asm_instruction_LDY_scratch_interp
.globl asm_instruction_LDY_scratch_interp_END
asm_instruction_LDY_scratch_interp:

  movzx REG_6502_Y_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_LDY_scratch_interp_END:
  ret


.globl asm_instruction_LSR_acc_interp
.globl asm_instruction_LSR_acc_interp_END
asm_instruction_LSR_acc_interp:

  shr REG_6502_A, 1
  setb REG_6502_CF

asm_instruction_LSR_acc_interp_END:
  ret


.globl asm_instruction_LSR_scratch_interp
.globl asm_instruction_LSR_scratch_interp_END
asm_instruction_LSR_scratch_interp:

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  shr REG_SCRATCH2_8, 1
  setb REG_6502_CF
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8

asm_instruction_LSR_scratch_interp_END:
  ret


.globl asm_instruction_ORA_imm_interp
.globl asm_instruction_ORA_imm_interp_END
asm_instruction_ORA_imm_interp:

  or REG_6502_A, [REG_6502_PC + 1]

asm_instruction_ORA_imm_interp_END:
  ret


.globl asm_instruction_ORA_scratch_interp
.globl asm_instruction_ORA_scratch_interp_END
asm_instruction_ORA_scratch_interp:

  or REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]

asm_instruction_ORA_scratch_interp_END:
  ret


.globl asm_instruction_ROL_acc_interp
.globl asm_instruction_ROL_acc_interp_END
asm_instruction_ROL_acc_interp:

  shr REG_6502_CF_64, 1
  rcl REG_6502_A
  setb REG_6502_CF
  test REG_6502_A, REG_6502_A

asm_instruction_ROL_acc_interp_END:
  ret


.globl asm_instruction_ROL_scratch_interp
.globl asm_instruction_ROL_scratch_interp_END
asm_instruction_ROL_scratch_interp:

  shr REG_6502_CF_64, 1
  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  rcl REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8
  setb REG_6502_CF
  test REG_SCRATCH2_8, REG_SCRATCH2_8

asm_instruction_ROL_scratch_interp_END:
  ret


.globl asm_instruction_ROR_acc_interp
.globl asm_instruction_ROR_acc_interp_END
asm_instruction_ROR_acc_interp:

  shr REG_6502_CF_64, 1
  rcr REG_6502_A
  setb REG_6502_CF
  test REG_6502_A, REG_6502_A

asm_instruction_ROR_acc_interp_END:
  ret


.globl asm_instruction_ROR_scratch_interp
.globl asm_instruction_ROR_scratch_interp_END
asm_instruction_ROR_scratch_interp:

  shr REG_6502_CF_64, 1
  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  rcr REG_SCRATCH2_8
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8
  setb REG_6502_CF
  test REG_SCRATCH2_8, REG_SCRATCH2_8

asm_instruction_ROR_scratch_interp_END:
  ret


.globl asm_instruction_SAX_scratch_interp
.globl asm_instruction_SAX_scratch_interp_END
asm_instruction_SAX_scratch_interp:

  lahf
  movzx REG_SCRATCH2_32, REG_6502_X
  and REG_SCRATCH2_8, REG_6502_A
  sahf
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8

asm_instruction_SAX_scratch_interp_END:
  ret


.globl asm_instruction_SBC_imm_interp
.globl asm_instruction_SBC_imm_interp_END
asm_instruction_SBC_imm_interp:

  sub REG_6502_CF, 1
  sbb REG_6502_A, [REG_6502_PC + 1]
  setae REG_6502_CF
  seto REG_6502_OF

asm_instruction_SBC_imm_interp_END:
  ret


.globl asm_instruction_SBC_scratch_interp
.globl asm_instruction_SBC_scratch_interp_END
asm_instruction_SBC_scratch_interp:

  sub REG_6502_CF, 1
  sbb REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  setae REG_6502_CF
  seto REG_6502_OF

asm_instruction_SBC_scratch_interp_END:
  ret


.globl asm_instruction_SLO_scratch_interp
.globl asm_instruction_SLO_scratch_interp_END
asm_instruction_SLO_scratch_interp:

  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_FULL_ADDR]
  shl REG_SCRATCH2_8, 1
  setb REG_6502_CF
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_SCRATCH2_8
  or REG_6502_A, REG_SCRATCH2_8

asm_instruction_SLO_scratch_interp_END:
  ret


.globl asm_instruction_STA_scratch_interp
.globl asm_instruction_STA_scratch_interp_END
asm_instruction_STA_scratch_interp:

  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_6502_A

asm_instruction_STA_scratch_interp_END:
  ret


.globl asm_instruction_STX_scratch_interp
.globl asm_instruction_STX_scratch_interp_END
asm_instruction_STX_scratch_interp:

  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_6502_X

asm_instruction_STX_scratch_interp_END:
  ret


.globl asm_instruction_STY_scratch_interp
.globl asm_instruction_STY_scratch_interp_END
asm_instruction_STY_scratch_interp:

  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_FULL_ADDR], REG_6502_Y

asm_instruction_STY_scratch_interp_END:
  ret
