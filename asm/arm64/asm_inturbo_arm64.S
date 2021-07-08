#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "asm_calling_convention_arm64.h"
#include "asm_defs_registers_arm64.h"

.file "asm/arm64/asm_inturbo_arm64.S"
.section rodata
.text


.globl asm_inturbo_enter
.globl asm_inturbo_enter_END
asm_inturbo_enter:
  movz REG_INTURBO_CODE, #(K_INTURBO_OPCODES >> 16), lsl #16
  movk REG_INTURBO_CODE, #(K_INTURBO_OPCODES & 0xFFFF)

  adr REG_INTERP_FUNC, asm_inturbo_call_interp

  br asm_enter_common

asm_inturbo_enter_END:
  ret



.globl asm_inturbo_save_countdown
.globl asm_inturbo_save_countdown_END
asm_inturbo_save_countdown:
  mov REG_COUNTDOWN_OLD, REG_COUNTDOWN

asm_inturbo_save_countdown_END:
  ret


.globl asm_inturbo_check_special_address_movz
.globl asm_inturbo_check_special_address_movz_END
asm_inturbo_check_special_address_movz:
  movz REG_SCRATCH3, #0xFFFF

asm_inturbo_check_special_address_movz_END:
  ret


.globl asm_inturbo_check_special_address_sub_and_tbz
.globl asm_inturbo_check_special_address_sub_and_tbz_END
asm_inturbo_check_special_address_sub_and_tbz:
  sub REG_SCRATCH3, REG_SCRATCH1, REG_SCRATCH3
  tbz REG_SCRATCH3, #16, asm_inturbo_check_special_address_sub_and_tbz

asm_inturbo_check_special_address_sub_and_tbz_END:
  ret


.globl asm_inturbo_check_countdown_sub
.globl asm_inturbo_check_countdown_sub_END
asm_inturbo_check_countdown_sub:
  sub REG_COUNTDOWN, REG_COUNTDOWN, #4095

asm_inturbo_check_countdown_sub_END:
  ret


.globl asm_inturbo_check_countdown_tbnz
.globl asm_inturbo_check_countdown_tbnz_END
asm_inturbo_check_countdown_tbnz:
  tbnz REG_COUNTDOWN, #63, asm_inturbo_check_countdown_tbnz

asm_inturbo_check_countdown_tbnz_END:
  ret


.globl asm_inturbo_commit_branch
.globl asm_inturbo_commit_branch_END
asm_inturbo_commit_branch:

  mov REG_6502_PC, REG_SCRATCH1

asm_inturbo_commit_branch_END:
  ret


.globl asm_inturbo_check_decimal_tbnz
.globl asm_inturbo_check_decimal_tbnz_END
asm_inturbo_check_decimal_tbnz:

  tbnz REG_6502_ID_F, #3, asm_inturbo_check_decimal_tbnz

asm_inturbo_check_decimal_tbnz_END:
  ret


.globl asm_inturbo_check_interrupt_cbnz
.globl asm_inturbo_check_interrupt_cbnz_END
asm_inturbo_check_interrupt_cbnz:

  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_STATE_6502]
  ldr REG_SCRATCH1_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_IRQ_FIRE]
  cbnz REG_SCRATCH1, asm_inturbo_check_interrupt_cbnz

asm_inturbo_check_interrupt_cbnz_END:
  ret


.globl asm_inturbo_load_and_advance_pc
.globl asm_inturbo_load_and_advance_pc_END
asm_inturbo_load_and_advance_pc:
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #-0x100]!

asm_inturbo_load_and_advance_pc_END:
  ret

.globl asm_inturbo_jump_next_opcode
.globl asm_inturbo_jump_next_opcode_END
asm_inturbo_jump_next_opcode:
  add REG_SCRATCH1, REG_INTURBO_CODE, REG_SCRATCH1, lsl #K_INTURBO_OPCODES_SHIFT
  br REG_SCRATCH1

asm_inturbo_jump_next_opcode_END:
  ret


.globl asm_inturbo_enter_debug
.globl asm_inturbo_enter_debug_END
asm_inturbo_enter_debug:
  sub REG_6502_PC, REG_6502_PC, REG_MEM_READ
  blr REG_DEBUG_FUNC
  add REG_6502_PC, REG_6502_PC, REG_MEM_READ

asm_inturbo_enter_debug_END:
  ret


.globl asm_inturbo_jump_call_interp
.globl asm_inturbo_jump_call_interp_END
asm_inturbo_jump_call_interp:
  br REG_INTERP_FUNC

asm_inturbo_jump_call_interp_END:
  ret


.globl asm_inturbo_call_interp
.globl asm_inturbo_call_interp_END
asm_inturbo_call_interp:
  sub REG_6502_PC, REG_6502_PC, REG_MEM_READ
  bl asm_save_AXYS_PC_flags

  sub sp, sp, #16
  ldr REG_PARAM1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_OBJECT]
  mov REG_PARAM2, sp
  mov REG_PARAM3, REG_COUNTDOWN_OLD
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_CALLBACK]
  blr REG_SCRATCH1
  ldr REG_COUNTDOWN, [sp, #0]
  ldr REG_SCRATCH1, [sp, #8]
  add sp, sp, #16

  tst REG_SCRATCH1, REG_SCRATCH1
  b.eq not_exiting
  mov REG_RETURN, REG_SCRATCH1
  b asm_exit

asm_inturbo_call_interp_END:
  ret

not_exiting:
  bl asm_restore_AXYS_PC_flags
  add REG_6502_PC, REG_6502_PC, REG_MEM_READ
  ldrb REG_SCRATCH1_32, [REG_6502_PC]
  b asm_inturbo_jump_next_opcode

not_exiting_END:
  ret


.globl asm_inturbo_check_page_crossing
.globl asm_inturbo_check_page_crossing_END
asm_inturbo_check_page_crossing:
  eor REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH1
  ubfx REG_SCRATCH2, REG_SCRATCH2, #8, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH2

asm_inturbo_check_page_crossing_END:
  ret


.globl asm_inturbo_add_1_to_scratch
.globl asm_inturbo_add_1_to_scratch_END
asm_inturbo_add_1_to_scratch:

  add REG_SCRATCH1, REG_SCRATCH1, #1

asm_inturbo_add_1_to_scratch_END:
  ret


.globl asm_inturbo_mode_imm
.globl asm_inturbo_mode_imm_END
asm_inturbo_mode_imm:

  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]

asm_inturbo_mode_imm_END:
  ret


# TODO: want to revisit this; not always needed to do the fetch.
.globl asm_inturbo_mode_rel
.globl asm_inturbo_mode_rel_END
asm_inturbo_mode_rel:
  ldrsb REG_SCRATCH2, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_6502_PC, #2
  add REG_SCRATCH2, REG_SCRATCH1, REG_SCRATCH2

asm_inturbo_mode_rel_END:
  ret


.globl asm_inturbo_mode_zpg
.globl asm_inturbo_mode_zpg_END
asm_inturbo_mode_zpg:

  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]

asm_inturbo_mode_zpg_END:
  ret


.globl asm_inturbo_mode_abs
.globl asm_inturbo_mode_abs_END
asm_inturbo_mode_abs:

  ldrh REG_SCRATCH1_32, [REG_6502_PC, #1]

asm_inturbo_mode_abs_END:
  ret


.globl asm_inturbo_mode_abx
.globl asm_inturbo_mode_abx_END
asm_inturbo_mode_abx:

  ldrh REG_SCRATCH2_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH2, REG_6502_X

asm_inturbo_mode_abx_END:
  ret


.globl asm_inturbo_mode_aby
.globl asm_inturbo_mode_aby_END
asm_inturbo_mode_aby:

  ldrh REG_SCRATCH2_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH2, REG_6502_Y

asm_inturbo_mode_aby_END:
  ret


.globl asm_inturbo_mode_zpx
.globl asm_inturbo_mode_zpx_END
asm_inturbo_mode_zpx:

  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_X
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF

asm_inturbo_mode_zpx_END:
  ret


.globl asm_inturbo_mode_zpy
.globl asm_inturbo_mode_zpy_END
asm_inturbo_mode_zpy:

  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_Y
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF

asm_inturbo_mode_zpy_END:
  ret


.globl asm_inturbo_mode_idx
.globl asm_inturbo_mode_idx_END
asm_inturbo_mode_idx:

  ldrb REG_SCRATCH2_32, [REG_6502_PC, #1]
  add REG_SCRATCH2, REG_SCRATCH2, REG_6502_X
  and REG_SCRATCH2, REG_SCRATCH2, #0xFF
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH2]
  add REG_SCRATCH2, REG_SCRATCH2, #1
  and REG_SCRATCH2, REG_SCRATCH2, #0xFF
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH2]
  add REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_inturbo_mode_idx_END:
  ret


.globl asm_inturbo_mode_idy
.globl asm_inturbo_mode_idy_END
asm_inturbo_mode_idy:

  ldrb REG_SCRATCH3_32, [REG_6502_PC, #1]
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH3]
  add REG_SCRATCH3_32, REG_SCRATCH3_32, #1
  and REG_SCRATCH3_32, REG_SCRATCH3_32, #0xFF
  ldrb REG_SCRATCH3_32, [REG_MEM_READ, REG_SCRATCH3]
  add REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH3, lsl #8
  add REG_SCRATCH1, REG_SCRATCH2, REG_6502_Y

asm_inturbo_mode_idy_END:
  ret


.globl asm_inturbo_mode_ind
.globl asm_inturbo_mode_ind_END
asm_inturbo_mode_ind:

  ldrh REG_SCRATCH1_32, [REG_6502_PC, #1]
  mov REG_SCRATCH2, REG_SCRATCH1
  and REG_SCRATCH2, REG_SCRATCH2, #0xFFFFFF00
  add REG_SCRATCH3, REG_SCRATCH1, #1
  and REG_SCRATCH3, REG_SCRATCH3, #0xFF
  orr REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH3
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH2]
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_inturbo_mode_ind_END:
  ret


.globl asm_inturbo_fetch_from_scratch
.globl asm_inturbo_fetch_from_scratch_END
asm_inturbo_fetch_from_scratch:

  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]

asm_inturbo_fetch_from_scratch_END:
  ret


.globl asm_inturbo_push_pc
.globl asm_inturbo_push_pc_END
asm_inturbo_push_pc:

  sub REG_SCRATCH1, REG_6502_PC, REG_MEM_READ
  add REG_SCRATCH1, REG_SCRATCH1, 2
  lsr REG_SCRATCH2, REG_SCRATCH1, #8
  strb REG_SCRATCH2_32, [REG_MEM_STACK, REG_6502_S]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF
  strb REG_SCRATCH1_32, [REG_MEM_STACK, REG_6502_S]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF

asm_inturbo_push_pc_END:
  ret


.globl asm_inturbo_interrupt_vector
.globl asm_inturbo_interrupt_vector_END
asm_inturbo_interrupt_vector:

  movz REG_SCRATCH1, #K_6502_VECTOR_IRQ
  ldrh REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  add REG_6502_PC, REG_MEM_READ, REG_SCRATCH1

asm_inturbo_interrupt_vector_END:
  ret


.globl asm_instruction_ADC_imm_interp
.globl asm_instruction_ADC_imm_interp_END
asm_instruction_ADC_imm_interp:

  add REG_SCRATCH1, REG_6502_CF, REG_SCRATCH1, lsl #56
  lsl REG_6502_A, REG_6502_A, #56
  orr REG_6502_A, REG_6502_A, #0x00FFFFFFFFFFFFFF
  adds REG_6502_A, REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A, #0xFF00000000000000
  lsr REG_6502_A, REG_6502_A, #56

asm_instruction_ADC_imm_interp_END:
  ret


.globl asm_instruction_ALR_scratch_interp
.globl asm_instruction_ALR_scratch_interp_END
asm_instruction_ALR_scratch_interp:

  and REG_6502_A, REG_6502_A, REG_SCRATCH1
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

asm_instruction_ALR_scratch_interp_END:
  ret


.globl asm_instruction_AND_scratch_interp_common
.globl asm_instruction_AND_scratch_interp_common_END
asm_instruction_AND_scratch_interp_common:

  and REG_6502_A, REG_6502_A, REG_SCRATCH1

asm_instruction_AND_scratch_interp_common_END:
  ret


.globl asm_instruction_ASL_acc_interp
.globl asm_instruction_ASL_acc_interp_END
asm_instruction_ASL_acc_interp:

  ubfx REG_6502_CF, REG_6502_A, #7, #1
  lsl REG_6502_A, REG_6502_A, #1
  and REG_6502_A, REG_6502_A, #0xFF

asm_instruction_ASL_acc_interp_END:
  ret


.globl asm_instruction_ASL_scratch_interp
.globl asm_instruction_ASL_scratch_interp_END
asm_instruction_ASL_scratch_interp:

  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  ubfx REG_6502_CF, REG_SCRATCH2, #8, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_ASL_scratch_interp_END:
  ret


.globl asm_instruction_BCC_interp
.globl asm_instruction_BCC_interp_END
asm_instruction_BCC_interp:
  tbnz REG_6502_CF, #0, asm_instruction_BCC_interp_END
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BCC_interp_END:
  ret


.globl asm_instruction_BCC_interp_accurate
.globl asm_instruction_BCC_interp_accurate_END
asm_instruction_BCC_interp_accurate:
  tbnz REG_6502_CF, #0, asm_instruction_BCC_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BCC_interp_accurate_END:
  ret


.globl asm_instruction_BCS_interp
.globl asm_instruction_BCS_interp_END
asm_instruction_BCS_interp:
  tbz REG_6502_CF, #0, asm_instruction_BCS_interp_END
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BCS_interp_END:
  ret


.globl asm_instruction_BCS_interp_accurate
.globl asm_instruction_BCS_interp_accurate_END
asm_instruction_BCS_interp_accurate:
  tbz REG_6502_CF, #0, asm_instruction_BCS_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BCS_interp_accurate_END:
  ret


.globl asm_instruction_BEQ_interp
.globl asm_instruction_BEQ_interp_END
asm_instruction_BEQ_interp:
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, eq

asm_instruction_BEQ_interp_END:
  ret


.globl asm_instruction_BEQ_interp_accurate
.globl asm_instruction_BEQ_interp_accurate_END
asm_instruction_BEQ_interp_accurate:
  b.ne asm_instruction_BEQ_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BEQ_interp_accurate_END:
  ret


.globl asm_instruction_BIT_interp
.globl asm_instruction_BIT_interp_END
asm_instruction_BIT_interp:

  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]

asm_instruction_BIT_interp_END:
  ret


.globl asm_instruction_BMI_interp
.globl asm_instruction_BMI_interp_END
asm_instruction_BMI_interp:
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, mi

asm_instruction_BMI_interp_END:
  ret


.globl asm_instruction_BMI_interp_accurate
.globl asm_instruction_BMI_interp_accurate_END
asm_instruction_BMI_interp_accurate:
  b.pl asm_instruction_BMI_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BMI_interp_accurate_END:
  ret


.globl asm_instruction_BNE_interp
.globl asm_instruction_BNE_interp_END
asm_instruction_BNE_interp:
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, ne

asm_instruction_BNE_interp_END:
  ret


.globl asm_instruction_BNE_interp_accurate
.globl asm_instruction_BNE_interp_accurate_END
asm_instruction_BNE_interp_accurate:
  b.eq asm_instruction_BNE_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BNE_interp_accurate_END:
  ret


.globl asm_instruction_BPL_interp
.globl asm_instruction_BPL_interp_END
asm_instruction_BPL_interp:
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, pl

asm_instruction_BPL_interp_END:
  ret


.globl asm_instruction_BPL_interp_accurate
.globl asm_instruction_BPL_interp_accurate_END
asm_instruction_BPL_interp_accurate:
  b.mi asm_instruction_BPL_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BPL_interp_accurate_END:
  ret


.globl asm_instruction_BVC_interp
.globl asm_instruction_BVC_interp_END
asm_instruction_BVC_interp:
  tbnz REG_6502_OF, #0, asm_instruction_BVC_interp_END
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BVC_interp_END:
  ret


.globl asm_instruction_BVC_interp_accurate
.globl asm_instruction_BVC_interp_accurate_END
asm_instruction_BVC_interp_accurate:
  tbnz REG_6502_OF, #0, asm_instruction_BVC_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BVC_interp_accurate_END:
  ret


.globl asm_instruction_BVS_interp
.globl asm_instruction_BVS_interp_END
asm_instruction_BVS_interp:
  tbz REG_6502_OF, #0, asm_instruction_BVS_interp_END
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BVS_interp_END:
  ret


.globl asm_instruction_BVS_interp_accurate
.globl asm_instruction_BVS_interp_accurate_END
asm_instruction_BVS_interp_accurate:
  tbz REG_6502_OF, #0, asm_instruction_BVS_interp_accurate_END
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

asm_instruction_BVS_interp_accurate_END:
  ret


.globl asm_instruction_CMP_imm_interp
.globl asm_instruction_CMP_imm_interp_END
asm_instruction_CMP_imm_interp:

  cmp REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs

asm_instruction_CMP_imm_interp_END:
  ret


.globl asm_instruction_CMP_scratch_interp
.globl asm_instruction_CMP_scratch_interp_END
asm_instruction_CMP_scratch_interp:

  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  cmp REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs

asm_instruction_CMP_scratch_interp_END:
  ret


.globl asm_instruction_CPX_imm_interp
.globl asm_instruction_CPX_imm_interp_END
asm_instruction_CPX_imm_interp:

  lsl REG_SCRATCH2, REG_6502_X, #56
  cmp REG_SCRATCH2, REG_SCRATCH1, lsl #56
  cset REG_6502_CF, cs

asm_instruction_CPX_imm_interp_END:
  ret


.globl asm_instruction_CPX_scratch_interp
.globl asm_instruction_CPX_scratch_interp_END
asm_instruction_CPX_scratch_interp:

  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  cmp REG_6502_X, REG_SCRATCH1
  cset REG_6502_CF, cs

asm_instruction_CPX_scratch_interp_END:
  ret


.globl asm_instruction_CPY_imm_interp
.globl asm_instruction_CPY_imm_interp_END
asm_instruction_CPY_imm_interp:

  lsl REG_SCRATCH2, REG_6502_Y, #56
  cmp REG_SCRATCH2, REG_SCRATCH1, lsl #56
  cset REG_6502_CF, cs

asm_instruction_CPY_imm_interp_END:
  ret


.globl asm_instruction_CPY_scratch_interp
.globl asm_instruction_CPY_scratch_interp_END
asm_instruction_CPY_scratch_interp:

  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  cmp REG_6502_Y, REG_SCRATCH1
  cset REG_6502_CF, cs

asm_instruction_CPY_scratch_interp_END:
  ret




.globl asm_instruction_DEC_scratch_interp
.globl asm_instruction_DEC_scratch_interp_END
asm_instruction_DEC_scratch_interp:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  sub REG_SCRATCH2, REG_SCRATCH2, #1
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_instruction_DEC_scratch_interp_END:
  ret


.globl asm_instruction_EOR_scratch_interp_common
.globl asm_instruction_EOR_scratch_interp_common_END
asm_instruction_EOR_scratch_interp_common:

  eor REG_6502_A, REG_6502_A, REG_SCRATCH1

asm_instruction_EOR_scratch_interp_common_END:
  ret


.globl asm_instruction_INC_scratch_interp
.globl asm_instruction_INC_scratch_interp_END
asm_instruction_INC_scratch_interp:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  add REG_SCRATCH2, REG_SCRATCH2, #1
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_instruction_INC_scratch_interp_END:
  ret


.globl asm_instruction_JMP_scratch_interp
.globl asm_instruction_JMP_scratch_interp_END
asm_instruction_JMP_scratch_interp:
  add REG_6502_PC, REG_MEM_READ, REG_SCRATCH1

asm_instruction_JMP_scratch_interp_END:
  ret


.globl asm_instruction_LDA_imm_interp
.globl asm_instruction_LDA_imm_interp_END
asm_instruction_LDA_imm_interp:

  ldrb REG_6502_A_32, [REG_6502_PC, #1]

asm_instruction_LDA_imm_interp_END:
  ret


.globl asm_instruction_LDA_scratch_interp
.globl asm_instruction_LDA_scratch_interp_END
asm_instruction_LDA_scratch_interp:

  ldrb REG_6502_A_32, [REG_MEM_READ, REG_SCRATCH1]

asm_instruction_LDA_scratch_interp_END:
  ret


.globl asm_instruction_LDX_imm_interp
.globl asm_instruction_LDX_imm_interp_END
asm_instruction_LDX_imm_interp:

  ldrb REG_6502_X_32, [REG_6502_PC, #1]

asm_instruction_LDX_imm_interp_END:
  ret


.globl asm_instruction_LDX_scratch_interp
.globl asm_instruction_LDX_scratch_interp_END
asm_instruction_LDX_scratch_interp:

  ldrb REG_6502_X_32, [REG_MEM_READ, REG_SCRATCH1]

asm_instruction_LDX_scratch_interp_END:
  ret


.globl asm_instruction_LDY_imm_interp
.globl asm_instruction_LDY_imm_interp_END
asm_instruction_LDY_imm_interp:

  ldrb REG_6502_Y_32, [REG_6502_PC, #1]

asm_instruction_LDY_imm_interp_END:
  ret


.globl asm_instruction_LDY_scratch_interp
.globl asm_instruction_LDY_scratch_interp_END
asm_instruction_LDY_scratch_interp:

  ldrb REG_6502_Y_32, [REG_MEM_READ, REG_SCRATCH1]

asm_instruction_LDY_scratch_interp_END:
  ret


.globl asm_instruction_LSR_acc_interp
.globl asm_instruction_LSR_acc_interp_END
asm_instruction_LSR_acc_interp:

  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

asm_instruction_LSR_acc_interp_END:
  ret


.globl asm_instruction_LSR_scratch_interp
.globl asm_instruction_LSR_scratch_interp_END
asm_instruction_LSR_scratch_interp:

  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH2, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_LSR_scratch_interp_END:
  ret


.globl asm_instruction_ORA_scratch_interp_common
.globl asm_instruction_ORA_scratch_interp_common_END
asm_instruction_ORA_scratch_interp_common:

  orr REG_6502_A, REG_6502_A, REG_SCRATCH1

asm_instruction_ORA_scratch_interp_common_END:
  ret


.globl asm_instruction_ROL_acc_interp
.globl asm_instruction_ROL_acc_interp_END
asm_instruction_ROL_acc_interp:

  add REG_SCRATCH1, REG_6502_CF, REG_6502_A, lsl #1
  ubfx REG_6502_CF, REG_6502_A, #7, #1
  and REG_6502_A, REG_SCRATCH1, #0xFF

asm_instruction_ROL_acc_interp_END:
  ret


.globl asm_instruction_ROL_scratch_interp
.globl asm_instruction_ROL_scratch_interp_END
asm_instruction_ROL_scratch_interp:

  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  add REG_SCRATCH2, REG_6502_CF, REG_SCRATCH2, lsl #1
  ubfx REG_6502_CF, REG_SCRATCH2, #8, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_ROL_scratch_interp_END:
  ret


.globl asm_instruction_ROR_acc_interp
.globl asm_instruction_ROR_acc_interp_END
asm_instruction_ROR_acc_interp:

  orr REG_SCRATCH1, REG_6502_A, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_SCRATCH1, #1

asm_instruction_ROR_acc_interp_END:
  ret


.globl asm_instruction_ROR_scratch_interp
.globl asm_instruction_ROR_scratch_interp_END
asm_instruction_ROR_scratch_interp:

  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  orr REG_SCRATCH2, REG_SCRATCH2, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH2, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_ROR_scratch_interp_END:
  ret


.globl asm_instruction_SAX_scratch_interp
.globl asm_instruction_SAX_scratch_interp_END
asm_instruction_SAX_scratch_interp:

  and REG_SCRATCH2, REG_6502_A, REG_6502_X
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_SAX_scratch_interp_END:
  ret


.globl asm_instruction_SBC_imm_interp
.globl asm_instruction_SBC_imm_interp_END
asm_instruction_SBC_imm_interp:

  eor REG_SCRATCH2, REG_6502_CF, #1
  add REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, lsl #56
  lsl REG_6502_A, REG_6502_A, #56
  subs REG_6502_A, REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A, #0xFF00000000000000
  lsr REG_6502_A, REG_6502_A, #56

asm_instruction_SBC_imm_interp_END:
  ret


.globl asm_instruction_SLO_scratch_interp
.globl asm_instruction_SLO_scratch_interp_END
asm_instruction_SLO_scratch_interp:

  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  ubfx REG_6502_CF, REG_SCRATCH2, #7, #1
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]
  orr REG_6502_A, REG_6502_A, REG_SCRATCH2
  and REG_6502_A, REG_6502_A, #0xFF

asm_instruction_SLO_scratch_interp_END:
  ret


.globl asm_instruction_STA_scratch_interp
.globl asm_instruction_STA_scratch_interp_END
asm_instruction_STA_scratch_interp:

  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_STA_scratch_interp_END:
  ret


.globl asm_instruction_STX_scratch_interp
.globl asm_instruction_STX_scratch_interp_END
asm_instruction_STX_scratch_interp:

  strb REG_6502_X_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_STX_scratch_interp_END:
  ret


.globl asm_instruction_STY_scratch_interp
.globl asm_instruction_STY_scratch_interp_END
asm_instruction_STY_scratch_interp:

  strb REG_6502_Y_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_STY_scratch_interp_END:
  ret
