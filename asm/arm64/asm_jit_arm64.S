#include "../asm_defs_host.h"
#include "../asm_jit_defs.h"
#include "asm_calling_convention_arm64.h"
#include "asm_defs_registers_arm64.h"

.file "asm/arm64/asm_jit_arm64.S"
.section rodata
.text


.globl asm_jit_enter
.globl asm_jit_enter_END
asm_jit_enter:
  movz REG_MEM_READ, #(K_BBC_MEM_OFFSET_TO_READ_FULL >> 16), lsl #16
  movk REG_MEM_READ, #(K_BBC_MEM_OFFSET_TO_READ_FULL & 0xFFFF)
  add REG_MEM_READ, REG_PARAM4, REG_MEM_READ

  adr REG_JIT_COMPILE, asm_jit_compile_trampoline
  adr REG_INTERP_FUNC, asm_jit_call_interp
  # NOTE: can save a register by adjusting REG_CONTEXT as we bounce in and out
  # of JIT.
  # REG_PARAM1 is REG_CONTEXT.
  add REG_JIT_PTRS, REG_PARAM1, #K_JIT_CONTEXT_OFFSET_JIT_PTRS

  b asm_enter_common

asm_inturbo_enter_END:
  ret


.globl asm_jit_compile_trampoline
asm_jit_compile_trampoline:
  stp x29, x30, [sp, #-16]!

  bl asm_save_AXYS_PC_flags

  # param1: context object
  mov REG_PARAM1, REG_CONTEXT
  # param2: instruction pointer that called here
  ldr REG_PARAM2, [sp, #8]
  sub REG_PARAM2, REG_PARAM2, #4
  # param3: countdown
  mov REG_PARAM3, REG_COUNTDOWN
  # param4: flags
  # TODO
  movz REG_PARAM4, #0

  ldr REG_SCRATCH1, [REG_CONTEXT, #K_JIT_CONTEXT_OFFSET_JIT_CALLBACK]
  blr REG_SCRATCH1

  mov REG_COUNTDOWN, REG_RETURN
  bl asm_restore_AXYS_PC_flags

  add sp, sp, 16

  lsl REG_6502_PC, REG_6502_PC, #K_BBC_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_BBC_JIT_ADDR
  br REG_6502_PC


.globl asm_jit_call_interp
.globl asm_jit_call_interp_END
asm_jit_call_interp:
  stp x29, x30, [sp, #-32]!

  bl asm_save_AXYS_PC_flags

  # param1: interp object, fetched from context
  ldr REG_PARAM1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_OBJECT]
  # param2: storage for 2x int64 return values
  add REG_PARAM2, sp, #16
  # param3: countdown
  mov REG_PARAM3, REG_COUNTDOWN
  # param4: flags
  # TODO
  movz REG_PARAM4, #0

  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_CALLBACK]
  blr REG_SCRATCH1

  ldr REG_COUNTDOWN, [sp, #16]
  ldr REG_SCRATCH1, [sp, #24]

  ldp x29, x30, [sp], #32

  tst REG_SCRATCH1, REG_SCRATCH1
  b.eq not_exiting
  mov REG_RETURN, REG_SCRATCH1
  b asm_exit

asm_jit_call_interp_END:
  ret

not_exiting:
  bl asm_restore_AXYS_PC_flags

  lsl REG_6502_PC, REG_6502_PC, #K_BBC_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_BBC_JIT_ADDR
  br REG_6502_PC

not_exiting_END:
  ret


.globl asm_jit_countdown_sub
.globl asm_jit_countdown_sub_END
.globl asm_jit_countdown_tbnz
.globl asm_jit_countdown_tbnz_END
asm_jit_countdown_sub:
  sub REG_COUNTDOWN, REG_COUNTDOWN, #4095

asm_jit_countdown_sub_END:
  ret

asm_jit_countdown_tbnz:
  tbnz REG_COUNTDOWN, #63, asm_jit_countdown_tbnz

asm_jit_countdown_tbnz_END:
  ret


.globl asm_jit_load_12bit
.globl asm_jit_load_12bit_END
asm_jit_load_12bit:
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, #0xFFF]

asm_jit_load_12bit_END:
  ret


.globl asm_jit_load_PC
.globl asm_jit_load_PC_END
asm_jit_load_PC:
  movz REG_6502_PC, #0xFFFF

asm_jit_load_PC_END:
  ret


.globl asm_jit_call_debug
.globl asm_jit_call_debug_END
asm_jit_call_debug:
  blr REG_DEBUG_FUNC

asm_jit_call_debug_END:
  ret


.globl asm_jit_jump_interp
.globl asm_jit_jump_interp_END
asm_jit_jump_interp:
  br REG_INTERP_FUNC

asm_jit_jump_interp_END:
  ret


.globl asm_jit_WRITE_INV_SCRATCH
.globl asm_jit_WRITE_INV_SCRATCH_END
asm_jit_WRITE_INV_SCRATCH:
  ldr REG_SCRATCH1_32, [REG_JIT_PTRS, REG_SCRATCH1, lsl #2]
  # Store anything convenient since it'll write fault.
  str REG_SCRATCH1_32, [REG_SCRATCH1]

asm_jit_WRITE_INV_SCRATCH_END:
  ret


.globl asm_jit_ADC_IMM
.globl asm_jit_ADC_IMM_END
asm_jit_ADC_IMM:
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_CF
  lsl REG_6502_A, REG_6502_A, #24
  orr REG_6502_A, REG_6502_A, #0x00FFFFFF
  adds REG_6502_A_32, REG_6502_A_32, REG_SCRATCH1_32
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A_32, #0xFF000000
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_ADC_IMM_END:
  ret


.globl asm_jit_ADC_SCRATCH
.globl asm_jit_ADC_SCRATCH_END
asm_jit_ADC_SCRATCH:
  add REG_SCRATCH1, REG_6502_CF, REG_SCRATCH1, lsl #24
  lsl REG_6502_A, REG_6502_A, #24
  orr REG_6502_A, REG_6502_A, #0x00FFFFFF
  adds REG_6502_A_32, REG_6502_A_32, REG_SCRATCH1_32
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A_32, #0xFF000000
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_ADC_SCRATCH_END:
  ret


.globl asm_jit_ALR
.globl asm_jit_ALR_END
asm_jit_ALR:
  and REG_6502_A, REG_6502_A, REG_SCRATCH1
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1
  adds xzr, xzr, REG_6502_A, lsl #56

asm_jit_ALR_END:
  ret


.globl asm_jit_AND_IMM
.globl asm_jit_AND_IMM_END
asm_jit_AND_IMM:
  and REG_6502_A, REG_6502_A, #1

asm_jit_AND_IMM_END:
  ret


.globl asm_jit_AND_SCRATCH
.globl asm_jit_AND_SCRATCH_END
asm_jit_AND_SCRATCH:
  and REG_6502_A, REG_6502_A, REG_SCRATCH1
  adds xzr, xzr, REG_6502_A, lsl #56

asm_jit_AND_SCRATCH_END:
  ret


.globl asm_jit_ASL_ACC
.globl asm_jit_ASL_ACC_END
asm_jit_ASL_ACC:
  ubfx REG_6502_CF, REG_6502_A, #7, #1
  lsl REG_6502_A, REG_6502_A, #1
  adds xzr, xzr, REG_6502_A, lsl #56
  and REG_6502_A, REG_6502_A, #0xFF

asm_jit_ASL_ACC_END:
  ret


.globl asm_jit_ASL_SCRATCH2
.globl asm_jit_ASL_SCRATCH2_END
asm_jit_ASL_SCRATCH2:
  ubfx REG_6502_CF, REG_SCRATCH2, #7, #1
  # NOTE: spills outside bounds of 8-bit.
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_ASL_SCRATCH2_END:
  ret


.globl asm_jit_BCC
.globl asm_jit_BCC_END
asm_jit_BCC:
  cbz REG_6502_CF, asm_jit_BCC

asm_jit_BCC_END:
  ret


.globl asm_jit_BCS
.globl asm_jit_BCS_END
asm_jit_BCS:
  cbnz REG_6502_CF, asm_jit_BCS

asm_jit_BCS_END:
  ret


.globl asm_jit_BEQ
.globl asm_jit_BEQ_END
asm_jit_BEQ:
  b.eq asm_jit_BEQ

asm_jit_BEQ_END:
  ret


.globl asm_jit_BMI
.globl asm_jit_BMI_END
asm_jit_BMI:
  b.mi asm_jit_BMI

asm_jit_BMI_END:
  ret


.globl asm_jit_BNE
.globl asm_jit_BNE_END
asm_jit_BNE:
  b.ne asm_jit_BNE

asm_jit_BNE_END:
  ret


.globl asm_jit_BPL
.globl asm_jit_BPL_END
asm_jit_BPL:
  b.pl asm_jit_BPL

asm_jit_BPL_END:
  ret


.globl asm_jit_BVC
.globl asm_jit_BVC_END
asm_jit_BVC:
  cbz REG_6502_OF, asm_jit_BVC

asm_jit_BVC_END:
  ret


.globl asm_jit_BVS
.globl asm_jit_BVS_END
asm_jit_BVS:
  cbnz REG_6502_OF, asm_jit_BVS

asm_jit_BVS_END:
  ret


.globl asm_jit_CHECK_BCD
.globl asm_jit_CHECK_BCD_END
asm_jit_CHECK_BCD:
  tbnz REG_6502_ID_F, #3, asm_jit_CHECK_BCD

asm_jit_CHECK_BCD_END:
  ret


.globl asm_jit_CMP_IMM
.globl asm_jit_CMP_IMM_END
asm_jit_CMP_IMM:
  mov REG_SCRATCH2, REG_6502_A, lsl #24
  cmp REG_SCRATCH2_32, REG_SCRATCH1_32

asm_jit_CMP_IMM_END:
  ret


.globl asm_jit_CMP_SCRATCH
.globl asm_jit_CMP_SCRATCH_END
asm_jit_CMP_SCRATCH:
  mov REG_SCRATCH2, REG_6502_A, lsl #24
  cmp REG_SCRATCH2_32, REG_SCRATCH1_32, lsl #24

asm_jit_CMP_SCRATCH_END:
  ret


.globl asm_jit_CPX_IMM
.globl asm_jit_CPX_IMM_END
asm_jit_CPX_IMM:
  mov REG_SCRATCH2, REG_6502_X, lsl #24
  cmp REG_SCRATCH2_32, REG_SCRATCH1_32

asm_jit_CPX_IMM_END:
  ret


.globl asm_jit_CPX_SCRATCH
.globl asm_jit_CPX_SCRATCH_END
asm_jit_CPX_SCRATCH:
  mov REG_SCRATCH2, REG_6502_X, lsl #24
  cmp REG_SCRATCH2_32, REG_SCRATCH1_32, lsl #24

asm_jit_CPX_SCRATCH_END:
  ret


.globl asm_jit_CPY_IMM
.globl asm_jit_CPY_IMM_END
asm_jit_CPY_IMM:
  mov REG_SCRATCH2, REG_6502_Y, lsl #24
  cmp REG_SCRATCH2_32, REG_SCRATCH1_32

asm_jit_CPY_IMM_END:
  ret


.globl asm_jit_CPY_SCRATCH
.globl asm_jit_CPY_SCRATCH_END
asm_jit_CPY_SCRATCH:
  mov REG_SCRATCH2, REG_6502_Y, lsl #24
  cmp REG_SCRATCH2_32, REG_SCRATCH1_32, lsl #24

asm_jit_CPY_SCRATCH_END:
  ret


.globl asm_jit_DEC_SCRATCH2
.globl asm_jit_DEC_SCRATCH2_END
asm_jit_DEC_SCRATCH2:
  # NOTE: spills outside bounds of 8-bit.
  sub REG_SCRATCH2, REG_SCRATCH2, #1
  # TODO: don't always need to update flags.
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_DEC_SCRATCH2_END:
  ret


.globl asm_jit_EOR_IMM
.globl asm_jit_EOR_IMM_END
asm_jit_EOR_IMM:
  eor REG_6502_A, REG_6502_A, #1

asm_jit_EOR_IMM_END:
  ret


.globl asm_jit_EOR_SCRATCH
.globl asm_jit_EOR_SCRATCH_END
asm_jit_EOR_SCRATCH:
  eor REG_6502_A, REG_6502_A, REG_SCRATCH1
  adds xzr, xzr, REG_6502_A, lsl #56

asm_jit_EOR_SCRATCH_END:
  ret


.globl asm_jit_INC_SCRATCH2
.globl asm_jit_INC_SCRATCH2_END
asm_jit_INC_SCRATCH2:
  # NOTE: spills outside bounds of 8-bit.
  add REG_SCRATCH2, REG_SCRATCH2, #1
  # TODO: don't always need to update flags.
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_INC_SCRATCH2_END:
  ret


.globl asm_jit_JMP
.globl asm_jit_JMP_END
asm_jit_JMP:
  b asm_jit_JMP

asm_jit_JMP_END:
  ret


.globl asm_jit_LDA_ABS_12bit
.globl asm_jit_LDA_ABS_12bit_END
asm_jit_LDA_ABS_12bit:
  ldrb REG_6502_A_32, [REG_MEM_READ, #0xFFF]

asm_jit_LDA_ABS_12bit_END:
  ret


.globl asm_jit_LDA_IMM
.globl asm_jit_LDA_IMM_END
asm_jit_LDA_IMM:
  movz REG_6502_A, #0xFFFF

asm_jit_LDA_IMM_END:
  ret


.globl asm_jit_LDA_SCRATCH
.globl asm_jit_LDA_SCRATCH_END
asm_jit_LDA_SCRATCH:
  ldrb REG_6502_A_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_LDA_SCRATCH_END:
  ret


.globl asm_jit_LDX_ABS_12bit
.globl asm_jit_LDX_ABS_12bit_END
asm_jit_LDX_ABS_12bit:
  ldrb REG_6502_X_32, [REG_MEM_READ, #0xFFF]

asm_jit_LDX_ABS_12bit_END:
  ret


.globl asm_jit_LDX_IMM
.globl asm_jit_LDX_IMM_END
asm_jit_LDX_IMM:
  movz REG_6502_X, #0xFFFF

asm_jit_LDX_IMM_END:
  ret


.globl asm_jit_LDX_SCRATCH
.globl asm_jit_LDX_SCRATCH_END
asm_jit_LDX_SCRATCH:
  ldrb REG_6502_X_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_LDX_SCRATCH_END:
  ret


.globl asm_jit_LDY_ABS_12bit
.globl asm_jit_LDY_ABS_12bit_END
asm_jit_LDY_ABS_12bit:
  ldrb REG_6502_Y_32, [REG_MEM_READ, #0xFFF]

asm_jit_LDY_ABS_12bit_END:
  ret


.globl asm_jit_LDY_IMM
.globl asm_jit_LDY_IMM_END
asm_jit_LDY_IMM:
  movz REG_6502_Y, #0xFFFF

asm_jit_LDY_IMM_END:
  ret


.globl asm_jit_LDY_SCRATCH
.globl asm_jit_LDY_SCRATCH_END
asm_jit_LDY_SCRATCH:
  ldrb REG_6502_Y_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_LDY_SCRATCH_END:
  ret


.globl asm_jit_LOAD_BYTE_PAIR_or
.globl asm_jit_LOAD_BYTE_PAIR_or_END
asm_jit_LOAD_BYTE_PAIR_or:
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_jit_LOAD_BYTE_PAIR_or_END:
  ret


.globl asm_jit_LSR_ACC
.globl asm_jit_LSR_ACC_END
asm_jit_LSR_ACC:
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1
  adds xzr, xzr, REG_6502_A, lsl #56

asm_jit_LSR_ACC_END:
  ret


.globl asm_jit_LSR_SCRATCH2
.globl asm_jit_LSR_SCRATCH2_END
asm_jit_LSR_SCRATCH2:
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH2, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_LSR_SCRATCH2_END:
  ret


.globl asm_jit_MODE_ABX_12bit
.globl asm_jit_MODE_ABX_12bit_END
asm_jit_MODE_ABX_12bit:
  add REG_SCRATCH1, REG_6502_X, #4095

asm_jit_MODE_ABX_12bit_END:
  ret


.globl asm_jit_MODE_ABY_12bit
.globl asm_jit_MODE_ABY_12bit_END
asm_jit_MODE_ABY_12bit:
  add REG_SCRATCH1, REG_6502_Y, #4095

asm_jit_MODE_ABY_12bit_END:
  ret


.globl asm_jit_MODE_IND_SCRATCH_8
.globl asm_jit_MODE_IND_SCRATCH_8_END
asm_jit_MODE_IND_SCRATCH_8:
  add REG_SCRATCH2, REG_SCRATCH1, #1
  and REG_SCRATCH2, REG_SCRATCH2, #0xFF
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH2]
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_jit_MODE_IND_SCRATCH_8_END:
  ret


.globl asm_jit_ORA_IMM
.globl asm_jit_ORA_IMM_END
asm_jit_ORA_IMM:
  orr REG_6502_A, REG_6502_A, #1

asm_jit_ORA_IMM_END:
  ret


.globl asm_jit_ORA_SCRATCH
.globl asm_jit_ORA_SCRATCH_END
asm_jit_ORA_SCRATCH:
  orr REG_6502_A, REG_6502_A, REG_SCRATCH1
  adds xzr, xzr, REG_6502_A, lsl #56

asm_jit_ORA_SCRATCH_END:
  ret


.globl asm_jit_PULL_16
.globl asm_jit_PULL_16_END
asm_jit_PULL_16:
  # TODO: make these operate on 16-bit values?
  add REG_SCRATCH1, REG_6502_S, #1
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF
  add REG_SCRATCH1, REG_MEM_STACK, REG_SCRATCH1
  ldrb REG_SCRATCH1_32, [REG_SCRATCH1]

  add REG_6502_S, REG_6502_S, #2
  and REG_6502_S, REG_6502_S, #0xFF
  add REG_SCRATCH2, REG_MEM_STACK, REG_6502_S
  ldrb REG_SCRATCH2_32, [REG_SCRATCH2]

  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_jit_PULL_16_END:
  ret


.globl asm_jit_PUSH_16_store_dec
.globl asm_jit_PUSH_16_store_dec_END
asm_jit_PUSH_16_store_dec:
  add REG_SCRATCH2, REG_MEM_STACK, REG_6502_S
  lsr REG_SCRATCH3, REG_SCRATCH1, #8
  strb REG_SCRATCH3_32, [REG_SCRATCH2]

  sub REG_SCRATCH3, REG_6502_S, #1
  and REG_SCRATCH3, REG_SCRATCH3, #0xFF
  add REG_SCRATCH3, REG_MEM_STACK, REG_SCRATCH3
  strb REG_SCRATCH1_32, [REG_SCRATCH3]

  sub REG_6502_S, REG_6502_S, #2
  and REG_6502_S, REG_6502_S, #0xFF

asm_jit_PUSH_16_store_dec_END:
  ret


.globl asm_jit_ROL_ACC
.globl asm_jit_ROL_ACC_END
asm_jit_ROL_ACC:
  add REG_6502_A, REG_6502_CF, REG_6502_A, lsl #1
  ubfx REG_6502_CF, REG_6502_A, #8, #1
  adds xzr, xzr, REG_6502_A, lsl #56
  and REG_6502_A, REG_6502_A, #0xFF

asm_jit_ROL_ACC_END:
  ret


.globl asm_jit_ROL_SCRATCH2
.globl asm_jit_ROL_SCRATCH2_END
asm_jit_ROL_SCRATCH2:
  # NOTE: spills outside bounds of 8-bit.
  add REG_SCRATCH2, REG_6502_CF, REG_SCRATCH2, lsl #1
  ubfx REG_6502_CF, REG_SCRATCH2, #8, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_ROL_SCRATCH2_END:
  ret


.globl asm_jit_ROR_ACC
.globl asm_jit_ROR_ACC_END
asm_jit_ROR_ACC:
  orr REG_6502_A, REG_6502_A, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

asm_jit_ROR_ACC_END:
  ret


.globl asm_jit_ROR_SCRATCH2
.globl asm_jit_ROR_SCRATCH2_END
asm_jit_ROR_SCRATCH2:
  orr REG_SCRATCH3, REG_SCRATCH2, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH3, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_ROR_SCRATCH2_END:
  ret


.globl asm_jit_SAVE_CARRY
.globl asm_jit_SAVE_CARRY_END
asm_jit_SAVE_CARRY:
  cset REG_6502_CF, cs

asm_jit_SAVE_CARRY_END:
  ret


.globl asm_jit_SAX_SCRATCH2
.globl asm_jit_SAX_SCRATCH2_END
asm_jit_SAX_SCRATCH2:
  and REG_SCRATCH2, REG_6502_A, REG_6502_X

asm_jit_SAX_SCRATCH2_END:
  ret


.globl asm_jit_SBC_IMM
.globl asm_jit_SBC_IMM_END
asm_jit_SBC_IMM:
  eor REG_SCRATCH2, REG_6502_CF, #1
  add REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_SCRATCH1_32
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A_32, #0xFF000000
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_SBC_IMM_END:
  ret


.globl asm_jit_SBC_SCRATCH
.globl asm_jit_SBC_SCRATCH_END
asm_jit_SBC_SCRATCH:
  eor REG_SCRATCH2, REG_6502_CF, #1
  add REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, lsl #24
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_SCRATCH1_32
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A_32, #0xFF000000
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_SBC_SCRATCH_END:
  ret


.globl asm_jit_SCRATCH_12bit_store
.globl asm_jit_SCRATCH_12bit_store_END
asm_jit_SCRATCH_12bit_store:
  strb REG_SCRATCH1_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_SCRATCH_12bit_store_END:
  ret


.globl asm_jit_SCRATCH_ADD
.globl asm_jit_SCRATCH_ADD_END
asm_jit_SCRATCH_ADD:
  add REG_SCRATCH1, REG_SCRATCH1, #4095

asm_jit_SCRATCH_ADD_END:
  ret


.globl asm_jit_SCRATCH_ADD_X
.globl asm_jit_SCRATCH_ADD_X_END
asm_jit_SCRATCH_ADD_X:
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_X

asm_jit_SCRATCH_ADD_X_END:
  ret


.globl asm_jit_SCRATCH_ADD_Y
.globl asm_jit_SCRATCH_ADD_Y_END
asm_jit_SCRATCH_ADD_Y:
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_Y

asm_jit_SCRATCH_ADD_Y_END:
  ret


.globl asm_jit_SCRATCH_LOAD_12bit
.globl asm_jit_SCRATCH_LOAD_12bit_END
asm_jit_SCRATCH_LOAD_12bit:
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, #0xFFF]

asm_jit_SCRATCH_LOAD_12bit_END:
  ret


.globl asm_jit_SCRATCH_LOAD_SCRATCH
.globl asm_jit_SCRATCH_LOAD_SCRATCH_END
asm_jit_SCRATCH_LOAD_SCRATCH:
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_SCRATCH_LOAD_SCRATCH_END:
  ret


.globl asm_jit_SCRATCH_JMP
.globl asm_jit_SCRATCH_JMP_END
asm_jit_SCRATCH_JMP:
  lsl REG_SCRATCH1, REG_SCRATCH1, #K_BBC_JIT_BYTES_SHIFT
  orr REG_SCRATCH1, REG_SCRATCH1, #K_BBC_JIT_ADDR
  br REG_SCRATCH1

asm_jit_SCRATCH_JMP_END:
  ret


.globl asm_jit_SCRATCH_SET
.globl asm_jit_SCRATCH_SET_END
asm_jit_SCRATCH_SET:
  movz REG_SCRATCH1, #0xFFFF

asm_jit_SCRATCH_SET_END:
  ret


.globl asm_jit_SCRATCH_SET_HI
.globl asm_jit_SCRATCH_SET_HI_END
asm_jit_SCRATCH_SET_HI:
  movz REG_SCRATCH1, #0xFFFF, lsl #16

asm_jit_SCRATCH_SET_HI_END:
  ret


.globl asm_jit_SCRATCH_TRUNC_8
.globl asm_jit_SCRATCH_TRUNC_8_END
asm_jit_SCRATCH_TRUNC_8:
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF

asm_jit_SCRATCH_TRUNC_8_END:
  ret


.globl asm_jit_SCRATCH2_LOAD_12bit
.globl asm_jit_SCRATCH2_LOAD_12bit_END
asm_jit_SCRATCH2_LOAD_12bit:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, #0xFFF]

asm_jit_SCRATCH2_LOAD_12bit_END:
  ret


.globl asm_jit_SCRATCH2_LOAD_SCRATCH
.globl asm_jit_SCRATCH2_LOAD_SCRATCH_END
asm_jit_SCRATCH2_LOAD_SCRATCH:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_SCRATCH2_LOAD_SCRATCH_END:
  ret


.globl asm_jit_SCRATCH2_LOAD_SCRATCH3
.globl asm_jit_SCRATCH2_LOAD_SCRATCH3_END
asm_jit_SCRATCH2_LOAD_SCRATCH3:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH3]

asm_jit_SCRATCH2_LOAD_SCRATCH3_END:
  ret


.globl asm_jit_SCRATCH2_STORE_12bit
.globl asm_jit_SCRATCH2_STORE_12bit_END
asm_jit_SCRATCH2_STORE_12bit:
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_SCRATCH2_STORE_12bit_END:
  ret


.globl asm_jit_SCRATCH2_STORE_SCRATCH
.globl asm_jit_SCRATCH2_STORE_SCRATCH_END
asm_jit_SCRATCH2_STORE_SCRATCH:
  strb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_SCRATCH2_STORE_SCRATCH_END:
  ret


.globl asm_jit_SCRATCH2_STORE_SCRATCH3
.globl asm_jit_SCRATCH2_STORE_SCRATCH3_END
asm_jit_SCRATCH2_STORE_SCRATCH3:
  strb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH3]

asm_jit_SCRATCH2_STORE_SCRATCH3_END:
  ret


.globl asm_jit_SCRATCH3_SET
.globl asm_jit_SCRATCH3_SET_END
asm_jit_SCRATCH3_SET:
  movz REG_SCRATCH3, #0xFFFF

asm_jit_SCRATCH3_SET_END:
  ret


.globl asm_jit_SHY_SCRATCH2
.globl asm_jit_SHY_SCRATCH2_END
asm_jit_SHY_SCRATCH2:
  lsr REG_SCRATCH2, REG_SCRATCH1, #8
  # NOTE: overflows 8-bit.
  add REG_SCRATCH2, REG_SCRATCH2, #1
  and REG_SCRATCH2, REG_SCRATCH2, REG_6502_Y

asm_jit_SHY_SCRATCH2_END:
  ret


.globl asm_jit_SLO_SCRATCH2
.globl asm_jit_SLO_SCRATCH2_END
asm_jit_SLO_SCRATCH2:
  ubfx REG_6502_CF, REG_SCRATCH2, #7, #1
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  orr REG_6502_A, REG_6502_A, REG_SCRATCH2
  adds xzr, xzr, REG_6502_A, lsl #56
  and REG_6502_A, REG_6502_A, #0xFF

asm_jit_SLO_SCRATCH2_END:
  ret


.globl asm_jit_STA_ABS_12bit
.globl asm_jit_STA_ABS_12bit_END
asm_jit_STA_ABS_12bit:
  strb REG_6502_A_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_STA_ABS_12bit_END:
  ret


.globl asm_jit_STA_ABS_16bit
.globl asm_jit_STA_ABS_16bit_END
asm_jit_STA_ABS_16bit:
  movz REG_SCRATCH1, #0xFFFF
  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STA_ABS_16bit_END:
  ret


.globl asm_jit_STA_SCRATCH
.globl asm_jit_STA_SCRATCH_END
asm_jit_STA_SCRATCH:
  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STA_SCRATCH_END:
  ret


.globl asm_jit_STX_ABS_12bit
.globl asm_jit_STX_ABS_12bit_END
asm_jit_STX_ABS_12bit:
  strb REG_6502_X_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_STX_ABS_12bit_END:
  ret


.globl asm_jit_STX_SCRATCH
.globl asm_jit_STX_SCRATCH_END
asm_jit_STX_SCRATCH:
  strb REG_6502_X_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STX_SCRATCH_END:
  ret


.globl asm_jit_STY_ABS_12bit
.globl asm_jit_STY_ABS_12bit_END
asm_jit_STY_ABS_12bit:
  strb REG_6502_Y_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_STY_ABS_12bit_END:
  ret


.globl asm_jit_STY_SCRATCH
.globl asm_jit_STY_SCRATCH_END
asm_jit_STY_SCRATCH:
  strb REG_6502_Y_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STY_SCRATCH_END:
  ret
