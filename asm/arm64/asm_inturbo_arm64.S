#include "asm_defs_registers_arm64.h"

#include "../asm_defs_host.h"
#include "asm_calling_convention_arm64.h"

.file "asm/arm64/asm_inturbo_arm64.S"
.section rodata
.text


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


.globl asm_inturbo_load_and_advance_pc
.globl asm_inturbo_load_and_advance_pc_END
asm_inturbo_load_and_advance_pc:
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #-0x100]!

asm_inturbo_load_and_advance_pc_END:
  ret

.globl asm_inturbo_jump_next_opcode
.globl asm_inturbo_jump_next_opcode_END
asm_inturbo_jump_next_opcode:
  add REG_SCRATCH1, REG_INTURBO_CODE, REG_SCRATCH1, lsl #8
  br REG_SCRATCH1

asm_inturbo_jump_next_opcode_END:
  ret


.globl asm_inturbo_enter_debug
.globl asm_inturbo_enter_debug_END
asm_inturbo_enter_debug:
  blr REG_DEBUG_FUNC

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
  b asm_exit

asm_inturbo_call_interp_END:
  ret

not_exiting:
  bl asm_restore_AXYS_PC_flags
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


.globl asm_instruction_BCS_interp
.globl asm_instruction_BCS_interp_END
asm_instruction_BCS_interp:
  mov REG_6502_PC, REG_SCRATCH1
  tbz REG_6502_CF, #0, asm_instruction_BCS_interp_END
  mov REG_6502_PC, REG_SCRATCH2

asm_instruction_BCS_interp_END:
  ret


# TODO: this is broken! Doesn't re-check countdown.
.globl asm_instruction_BCS_interp_accurate
.globl asm_instruction_BCS_interp_accurate_END
asm_instruction_BCS_interp_accurate:
  mov REG_6502_PC, REG_SCRATCH1
  tbz REG_6502_CF, #0, asm_instruction_BCS_interp_accurate_END
  mov REG_6502_PC, REG_SCRATCH2
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1

asm_instruction_BCS_interp_accurate_END:
  ret


.globl asm_instruction_BEQ_interp
.globl asm_instruction_BEQ_interp_END
asm_instruction_BEQ_interp:
  csel REG_6502_PC, REG_SCRATCH2, REG_SCRATCH1, eq

asm_instruction_BEQ_interp_END:
  ret


.globl asm_instruction_BEQ_interp_accurate
.globl asm_instruction_BEQ_interp_accurate_END
asm_instruction_BEQ_interp_accurate:
  mov REG_6502_PC, REG_SCRATCH1
  b.ne asm_instruction_BEQ_interp_accurate_END
  mov REG_6502_PC, REG_SCRATCH2
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1

asm_instruction_BEQ_interp_accurate_END:
  ret


.globl asm_instruction_BMI_interp
.globl asm_instruction_BMI_interp_END
asm_instruction_BMI_interp:
  csel REG_6502_PC, REG_SCRATCH2, REG_SCRATCH1, mi

asm_instruction_BMI_interp_END:
  ret


.globl asm_instruction_BMI_interp_accurate
.globl asm_instruction_BMI_interp_accurate_END
asm_instruction_BMI_interp_accurate:
  mov REG_6502_PC, REG_SCRATCH1
  b.pl asm_instruction_BMI_interp_accurate_END
  mov REG_6502_PC, REG_SCRATCH2
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1

asm_instruction_BMI_interp_accurate_END:
  ret


.globl asm_instruction_BNE_interp
.globl asm_instruction_BNE_interp_END
asm_instruction_BNE_interp:
  csel REG_6502_PC, REG_SCRATCH2, REG_SCRATCH1, ne

asm_instruction_BNE_interp_END:
  ret


.globl asm_instruction_BNE_interp_accurate
.globl asm_instruction_BNE_interp_accurate_END
asm_instruction_BNE_interp_accurate:
  mov REG_6502_PC, REG_SCRATCH1
  b.eq asm_instruction_BNE_interp_accurate_END
  mov REG_6502_PC, REG_SCRATCH2
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1

asm_instruction_BNE_interp_accurate_END:
  ret


.globl asm_instruction_BPL_interp
.globl asm_instruction_BPL_interp_END
asm_instruction_BPL_interp:
  csel REG_6502_PC, REG_SCRATCH2, REG_SCRATCH1, pl

asm_instruction_BPL_interp_END:
  ret


.globl asm_instruction_BPL_interp_accurate
.globl asm_instruction_BPL_interp_accurate_END
asm_instruction_BPL_interp_accurate:
  mov REG_6502_PC, REG_SCRATCH1
  b.mi asm_instruction_BPL_interp_accurate_END
  mov REG_6502_PC, REG_SCRATCH2
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH1

asm_instruction_BPL_interp_accurate_END:
  ret


.globl asm_instruction_CMP_imm_interp
.globl asm_instruction_CMP_imm_interp_END
asm_instruction_CMP_imm_interp:

  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]
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

  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]
  cmp REG_6502_X, REG_SCRATCH1
  cset REG_6502_CF, cs

asm_instruction_CPX_imm_interp_END:
  ret


.globl asm_instruction_INC_scratch_interp
.globl asm_instruction_INC_scratch_interp_END
asm_instruction_INC_scratch_interp:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  add REG_SCRATCH2, REG_SCRATCH2, #1
  strb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  movn REG_SCRATCH1, #0
  tst REG_SCRATCH1, REG_6502_X, lsl #56

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


.globl asm_instruction_STA_scratch_interp
.globl asm_instruction_STA_scratch_interp_END
asm_instruction_STA_scratch_interp:

  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_STA_scratch_interp_END:
  ret


.globl asm_instruction_STA_scratch_interp_based
.globl asm_instruction_STA_scratch_interp_based_END
asm_instruction_STA_scratch_interp_based:

  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_STA_scratch_interp_based_END:
  ret


.globl asm_instruction_STX_scratch_interp
.globl asm_instruction_STX_scratch_interp_END
asm_instruction_STX_scratch_interp:

  strb REG_6502_X_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_instruction_STX_scratch_interp_END:
  ret
