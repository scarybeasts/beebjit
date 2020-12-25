#include "asm_defs_registers_arm64.h"

.file "asm_inturbo_arm64.S"
.section rodata
.text


.globl asm_inturbo_advance_pc_and_next
.globl asm_inturbo_advance_pc_and_next_END
.globl asm_inturbo_advance_pc_and_next_ldr_patch
asm_inturbo_advance_pc_and_next:
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #-0x100]!
asm_inturbo_advance_pc_and_next_ldr_patch:

  add REG_SCRATCH1, REG_INTURBO_CODE, REG_SCRATCH1, lsl #8
  br REG_SCRATCH1

asm_inturbo_advance_pc_and_next_END:
  ret


.globl asm_inturbo_enter_debug
.globl asm_inturbo_enter_debug_END
asm_inturbo_enter_debug:

  blr REG_DEBUG_FUNC
asm_inturbo_enter_debug_END:
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


.globl asm_inturbo_mode_abx_check_page_crossing
.globl asm_inturbo_mode_abx_check_page_crossing_END
asm_inturbo_mode_abx_check_page_crossing:
  eor REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH1
  ubfx REG_SCRATCH2, REG_SCRATCH2, 8, 1

asm_inturbo_mode_abx_check_page_crossing_END:
  ret


.globl asm_instruction_BNE_interp
.globl asm_instruction_BNE_interp_END
asm_instruction_BNE_interp:
  ldrsb REG_SCRATCH1, [REG_6502_PC, #1]
  add REG_6502_PC, REG_6502_PC, #2
  add REG_SCRATCH1, REG_6502_PC, REG_SCRATCH1
  csel REG_6502_PC, REG_SCRATCH1, REG_6502_PC, ne

asm_instruction_BNE_interp_END:
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


.globl asm_instruction_LDA_scratch_interp_based
.globl asm_instruction_LDA_scratch_interp_based_END
asm_instruction_LDA_scratch_interp_based:

  ldrb REG_6502_A_32, [REG_MEM_READ, REG_SCRATCH1]

asm_instruction_LDA_scratch_interp_based_END:
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
