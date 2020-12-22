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


.globl asm_instruction_LDX_imm_interp
.globl asm_instruction_LDX_imm_interp_END
asm_instruction_LDX_imm_interp:

  ldrb REG_6502_X_32, [REG_6502_PC, #1]

asm_instruction_LDX_imm_interp_END:
  ret
