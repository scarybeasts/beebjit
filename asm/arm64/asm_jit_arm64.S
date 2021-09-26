#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
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
  # NOTE: can save a register by adjusting REG_CONTEXT as we bounce in and out
  # of JIT.
  # REG_PARAM1 is REG_CONTEXT.
  add REG_JIT_PTRS, REG_PARAM1, #K_JIT_CONTEXT_OFFSET_JIT_PTRS

  b asm_enter_common

asm_jit_enter_END:
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

  lsl REG_6502_PC, REG_6502_PC, #K_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_JIT_ADDR
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

  lsl REG_6502_PC, REG_6502_PC, #K_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_JIT_ADDR
  br REG_6502_PC

not_exiting_END:
  ret


.globl asm_jit_call_inturbo
.globl asm_jit_call_inturbo_END
asm_jit_call_inturbo:
  # Read opcode from PC.
  add REG_6502_PC, REG_6502_PC, REG_MEM_READ
  ldrb REG_SCRATCH1_32, [REG_6502_PC]

  # Calculate jump target.
  lsl REG_SCRATCH1, REG_SCRATCH1, #K_INTURBO_OPCODE_SHIFT
  orr REG_SCRATCH1, REG_SCRATCH1, #K_INTURBO_ADDR

  # Save JIT context register and load inturbo context register.
  stp REG_CONTEXT, x30, [sp, #-16]!
  ldr REG_CONTEXT, [REG_CONTEXT, #K_JIT_CONTEXT_OFFSET_INTURBO]

  # Call inturbo and restore.
  blr REG_SCRATCH1
  ldp REG_CONTEXT, x30, [sp], #16

  # Calculate new JIT code address and jump.
  sub REG_6502_PC, REG_6502_PC, REG_MEM_READ
  lsl REG_6502_PC, REG_6502_PC, #K_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_JIT_ADDR
  br REG_6502_PC

asm_jit_call_inturbo_END:
  ret


.globl asm_jit_addr_add
.globl asm_jit_addr_add_END
asm_jit_addr_add:
  add REG_SCRATCH1, REG_SCRATCH1, #4095

asm_jit_addr_add_END:
  ret


.globl asm_jit_addr_add_x
.globl asm_jit_addr_add_x_END
asm_jit_addr_add_x:
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_X

asm_jit_addr_add_x_END:
  ret


.globl asm_jit_addr_add_y
.globl asm_jit_addr_add_y_END
asm_jit_addr_add_y:
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_Y

asm_jit_addr_add_y_END:
  ret


.globl asm_jit_addr_check_add
.globl asm_jit_addr_check_add_END
.globl asm_jit_addr_check_tbnz
.globl asm_jit_addr_check_tbnz_END
asm_jit_addr_check_add:
  # Trigger on >= 0xFC00.
  add REG_SCRATCH2, REG_SCRATCH1, #0x400

asm_jit_addr_check_add_END:
  ret

asm_jit_addr_check_tbnz:
  tbnz REG_SCRATCH2, #16, asm_jit_addr_check_tbnz

asm_jit_addr_check_tbnz_END:
  ret


.globl asm_jit_addr_load
.globl asm_jit_addr_load_END
asm_jit_addr_load:
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, #0xFFF]

asm_jit_addr_load_END:
  ret


.globl asm_jit_addr_load_16bit_wrap
.globl asm_jit_addr_load_16bit_wrap_END
asm_jit_addr_load_16bit_wrap:
  # NOTE: assumes zero-page.
  add REG_SCRATCH2, REG_SCRATCH1, #1
  and REG_SCRATCH2, REG_SCRATCH2, #0xFF
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH2]
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_jit_addr_load_16bit_wrap_END:
  ret


.globl asm_jit_addr_load_addr
.globl asm_jit_addr_load_addr_END
asm_jit_addr_load_addr:
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_addr_load_addr_END:
  ret


.globl asm_jit_addr_set
.globl asm_jit_addr_set_END
asm_jit_addr_set:
  movz REG_SCRATCH1, 0xFFFF

asm_jit_addr_set_END:
  ret


.globl asm_jit_addr_trunc_8bit
.globl asm_jit_addr_trunc_8bit_END
asm_jit_addr_trunc_8bit:
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF

asm_jit_addr_trunc_8bit_END:
  ret


.globl asm_jit_check_bcd
.globl asm_jit_check_bcd_END
asm_jit_check_bcd:
  tbnz REG_6502_ID_F, #3, asm_jit_check_bcd

asm_jit_check_bcd_END:
  ret


.globl asm_jit_check_page_crossing_ABX_sub
.globl asm_jit_check_page_crossing_ABX_sub_END
.globl asm_jit_check_page_crossing_ABX_add
.globl asm_jit_check_page_crossing_ABX_add_END
asm_jit_check_page_crossing_ABX_sub:
  sub REG_SCRATCH2, REG_6502_X, #4095

asm_jit_check_page_crossing_ABX_sub_END:
  ret

asm_jit_check_page_crossing_ABX_add:
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH2, lsr #63

asm_jit_check_page_crossing_ABX_add_END:
  ret


.globl asm_jit_check_page_crossing_ABY_sub
.globl asm_jit_check_page_crossing_ABY_sub_END
.globl asm_jit_check_page_crossing_ABY_add
.globl asm_jit_check_page_crossing_ABY_add_END
asm_jit_check_page_crossing_ABY_sub:
  sub REG_SCRATCH2, REG_6502_Y, #4095

asm_jit_check_page_crossing_ABY_sub_END:
  ret

asm_jit_check_page_crossing_ABY_add:
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH2, lsr #63

asm_jit_check_page_crossing_ABY_add_END:
  ret


.globl asm_jit_check_page_crossing_x
.globl asm_jit_check_page_crossing_x_END
asm_jit_check_page_crossing_x:
  sub REG_SCRATCH2, REG_SCRATCH1, REG_6502_X
  eon REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH1
  ubfx REG_SCRATCH2, REG_SCRATCH2, #8, #1
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH2

asm_jit_check_page_crossing_x_END:
  ret


.globl asm_jit_check_page_crossing_y
.globl asm_jit_check_page_crossing_y_END
asm_jit_check_page_crossing_y:
  sub REG_SCRATCH2, REG_SCRATCH1, REG_6502_Y
  eon REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH1
  ubfx REG_SCRATCH2, REG_SCRATCH2, #8, #1
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_SCRATCH2

asm_jit_check_page_crossing_y_END:
  ret


.globl asm_jit_check_pending_irq_load
.globl asm_jit_check_pending_irq_load_END
.globl asm_jit_check_pending_irq_cbnz
.globl asm_jit_check_pending_irq_cbnz_END
asm_jit_check_pending_irq_load:
  ldr REG_SCRATCH2, [REG_CONTEXT, #K_CONTEXT_OFFSET_STATE_6502]
  ldr REG_SCRATCH2_32, [REG_SCRATCH2, #K_STATE_6502_OFFSET_REG_IRQ_FIRE]

asm_jit_check_pending_irq_load_END:
  ret

asm_jit_check_pending_irq_cbnz:
  cbnz REG_SCRATCH2, asm_jit_check_pending_irq_cbnz

asm_jit_check_pending_irq_cbnz_END:
  ret


.globl asm_jit_countdown_add
.globl asm_jit_countdown_add_END
asm_jit_countdown_add:
  add REG_COUNTDOWN, REG_COUNTDOWN, #4095

asm_jit_countdown_add_END:
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


.globl asm_jit_call_debug
.globl asm_jit_call_debug_END
asm_jit_call_debug:
  bl asm_jit_call_debug

asm_jit_call_debug_END:
  ret


.globl asm_jit_flags_nz_value
.globl asm_jit_flags_nz_value_END
asm_jit_flags_nz_value:
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_flags_nz_value_END:
  ret


.globl asm_jit_jump_interp
.globl asm_jit_jump_interp_END
asm_jit_jump_interp:
  b asm_jit_jump_interp

asm_jit_jump_interp_END:
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


.globl asm_jit_mode_ABX
.globl asm_jit_mode_ABX_END
asm_jit_mode_ABX:
  add REG_SCRATCH1, REG_6502_X, #4095

asm_jit_mode_ABX_END:
  ret


.globl asm_jit_mode_ABY
.globl asm_jit_mode_ABY_END
asm_jit_mode_ABY:
  add REG_SCRATCH1, REG_6502_Y, #4095

asm_jit_mode_ABY_END:
  ret


.globl asm_jit_push
.globl asm_jit_push_END
asm_jit_push:
  add REG_SCRATCH3, REG_MEM_STACK, REG_6502_S
  strb REG_SCRATCH2_32, [REG_SCRATCH3]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF

asm_jit_push_END:
  ret


.globl asm_jit_save_carry
.globl asm_jit_save_carry_END
asm_jit_save_carry:
  cset REG_6502_CF, cs

asm_jit_save_carry_END:
  ret


.globl asm_jit_save_overflow
.globl asm_jit_save_overflow_END
asm_jit_save_overflow:
  cset REG_6502_OF, vs

asm_jit_save_overflow_END:
  ret


.globl asm_jit_value_load_ABS
.globl asm_jit_value_load_ABS_END
asm_jit_value_load_ABS:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, #0xFFF]

asm_jit_value_load_ABS_END:
  ret


.globl asm_jit_value_load_addr
.globl asm_jit_value_load_addr_END
asm_jit_value_load_addr:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_value_load_addr_END:
  ret


.globl asm_jit_value_load_value
.globl asm_jit_value_load_value_END
asm_jit_value_load_value:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH2]

asm_jit_value_load_value_END:
  ret


.globl asm_jit_value_set
.globl asm_jit_value_set_END
asm_jit_value_set:
  movz REG_SCRATCH2, 0xFFFF

asm_jit_value_set_END:
  ret


.globl asm_jit_value_set_hi
.globl asm_jit_value_set_hi_END
asm_jit_value_set_hi:
  movz REG_SCRATCH2, #0xFFFF, lsl #16

asm_jit_value_set_hi_END:
  ret


.globl asm_jit_value_store_ABS
.globl asm_jit_value_store_ABS_END
asm_jit_value_store_ABS:
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_value_store_ABS_END:
  ret


.globl asm_jit_value_store_addr
.globl asm_jit_value_store_addr_END
asm_jit_value_store_addr:
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_value_store_addr_END:
  ret


.globl asm_jit_write_inv
.globl asm_jit_write_inv_END
asm_jit_write_inv:
  ldr REG_SCRATCH3_32, [REG_JIT_PTRS, REG_SCRATCH1, lsl #2]
  # Store anything convenient since it'll write fault.
  str REG_SCRATCH3_32, [REG_SCRATCH3]

asm_jit_write_inv_END:
  ret


.globl asm_jit_write_inv_ABS_load
.globl asm_jit_write_inv_ABS_load_END
.globl asm_jit_write_inv_ABS_store
.globl asm_jit_write_inv_ABS_store_END
asm_jit_write_inv_ABS_load:
  # 6502 range is 0 - 0xFFF. Constant below is 4x that due to 32 bit ptrs.
  ldr REG_SCRATCH3_32, [REG_JIT_PTRS, #0x3FFC]

asm_jit_write_inv_ABS_load_END:
  ret

asm_jit_write_inv_ABS_store:
  # Store anything convenient since it'll write fault.
  str REG_SCRATCH3_32, [REG_SCRATCH3]

asm_jit_write_inv_ABS_store_END:
  ret


.globl asm_jit_ADC
.globl asm_jit_ADC_END
asm_jit_ADC:
  add REG_SCRATCH2, REG_6502_CF, REG_SCRATCH2, lsl #24
  lsl REG_6502_A, REG_6502_A, #24
  orr REG_6502_A, REG_6502_A, #0x00FFFFFF
  adds REG_6502_A_32, REG_6502_A_32, REG_SCRATCH2_32
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_ADC_END:
  ret


.globl asm_jit_ADC_IMM
.globl asm_jit_ADC_IMM_END
asm_jit_ADC_IMM:
  add REG_SCRATCH2, REG_SCRATCH2, REG_6502_CF
  lsl REG_6502_A, REG_6502_A, #24
  orr REG_6502_A, REG_6502_A, #0x00FFFFFF
  adds REG_6502_A_32, REG_6502_A_32, REG_SCRATCH2_32
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_ADC_IMM_END:
  ret


.globl asm_jit_ADD
.globl asm_jit_ADD_END
asm_jit_ADD:
  lsl REG_6502_A, REG_6502_A, #24
  adds REG_6502_A_32, REG_6502_A_32, REG_SCRATCH2_32, lsl #24
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_ADD_END:
  ret


.globl asm_jit_ADD_IMM
.globl asm_jit_ADD_IMM_END
asm_jit_ADD_IMM:
  adds REG_6502_A_32, REG_SCRATCH2_32, REG_6502_A_32, lsl #24
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_ADD_IMM_END:
  ret


.globl asm_jit_ALR
.globl asm_jit_ALR_END
asm_jit_ALR:
  and REG_6502_A, REG_6502_A, REG_SCRATCH2
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

asm_jit_ALR_END:
  ret


.globl asm_jit_AND
.globl asm_jit_AND_END
asm_jit_AND:
  and REG_6502_A, REG_6502_A, REG_SCRATCH2

asm_jit_AND_END:
  ret


.globl asm_jit_AND_IMM
.globl asm_jit_AND_IMM_END
asm_jit_AND_IMM:
  and REG_6502_A, REG_6502_A, #1

asm_jit_AND_IMM_END:
  ret


.globl asm_jit_ASL
.globl asm_jit_ASL_END
asm_jit_ASL:
  ubfx REG_6502_CF, REG_SCRATCH2, #7, #1
  # NOTE: spills outside bounds of 8-bit.
  lsl REG_SCRATCH2, REG_SCRATCH2, #1

asm_jit_ASL_END:
  ret


.globl asm_jit_ASL_ACC
.globl asm_jit_ASL_ACC_END
asm_jit_ASL_ACC:
  ubfx REG_6502_CF, REG_6502_A, #7, #1
  lsl REG_6502_A, REG_6502_A, #1
  and REG_6502_A, REG_6502_A, #0xFF

asm_jit_ASL_ACC_END:
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


.globl asm_jit_CMP_IMM_subs
.globl asm_jit_CMP_IMM_subs_END
.globl asm_jit_CMP_IMM_flags
.globl asm_jit_CMP_IMM_flags_END
asm_jit_CMP_IMM_subs:
  subs REG_SCRATCH2, REG_6502_A, #4095
asm_jit_CMP_IMM_subs_END:
  ret

asm_jit_CMP_IMM_flags:
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_CMP_IMM_flags_END:
  ret


.globl asm_jit_CMP
.globl asm_jit_CMP_END
asm_jit_CMP:
  subs REG_SCRATCH2, REG_6502_A, REG_SCRATCH2
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_CMP_END:
  ret


.globl asm_jit_CPX
.globl asm_jit_CPX_END
asm_jit_CPX:
  subs REG_SCRATCH2, REG_6502_X, REG_SCRATCH2
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_CPX_END:
  ret


.globl asm_jit_CPX_IMM_subs
.globl asm_jit_CPX_IMM_subs_END
.globl asm_jit_CPX_IMM_flags
.globl asm_jit_CPX_IMM_flags_END
asm_jit_CPX_IMM_subs:
  subs REG_SCRATCH2, REG_6502_X, #4095
asm_jit_CPX_IMM_subs_END:
  ret

asm_jit_CPX_IMM_flags:
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_CPX_IMM_flags_END:
  ret


.globl asm_jit_CPY
.globl asm_jit_CPY_END
asm_jit_CPY:
  subs REG_SCRATCH2, REG_6502_Y, REG_SCRATCH2
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_CPY_END:
  ret


.globl asm_jit_CPY_IMM_subs
.globl asm_jit_CPY_IMM_subs_END
.globl asm_jit_CPY_IMM_flags
.globl asm_jit_CPY_IMM_flags_END
asm_jit_CPY_IMM_subs:
  subs REG_SCRATCH2, REG_6502_Y, #4095
asm_jit_CPY_IMM_subs_END:
  ret

asm_jit_CPY_IMM_flags:
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH2, lsl #56

asm_jit_CPY_IMM_flags_END:
  ret


.globl asm_jit_DEC
.globl asm_jit_DEC_END
asm_jit_DEC:
  # NOTE: spills outside bounds of 8-bit.
  sub REG_SCRATCH2, REG_SCRATCH2, #1

asm_jit_DEC_END:
  ret


.globl asm_jit_EOR
.globl asm_jit_EOR_END
asm_jit_EOR:
  eor REG_6502_A, REG_6502_A, REG_SCRATCH2

asm_jit_EOR_END:
  ret


.globl asm_jit_EOR_IMM
.globl asm_jit_EOR_IMM_END
asm_jit_EOR_IMM:
  eor REG_6502_A, REG_6502_A, #1

asm_jit_EOR_IMM_END:
  ret


.globl asm_jit_INC
.globl asm_jit_INC_END
asm_jit_INC:
  # NOTE: spills outside bounds of 8-bit.
  add REG_SCRATCH2, REG_SCRATCH2, #1

asm_jit_INC_END:
  ret


.globl asm_jit_JMP
.globl asm_jit_JMP_END
asm_jit_JMP:
  # Range +- 128MB.
  b asm_jit_JMP

asm_jit_JMP_END:
  ret


.globl asm_jit_JMP_addr
.globl asm_jit_JMP_addr_END
asm_jit_JMP_addr:
  lsl REG_SCRATCH1, REG_SCRATCH1, #K_JIT_BYTES_SHIFT
  orr REG_SCRATCH1, REG_SCRATCH1, #K_JIT_ADDR
  br REG_SCRATCH1

asm_jit_JMP_addr_END:
  ret


.globl asm_jit_LDA
.globl asm_jit_LDA_END
asm_jit_LDA:
  ldrb REG_6502_A_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_LDA_END:
  ret


.globl asm_jit_LDA_ABS
.globl asm_jit_LDA_ABS_END
asm_jit_LDA_ABS:
  ldrb REG_6502_A_32, [REG_MEM_READ, #0xFFF]

asm_jit_LDA_ABS_END:
  ret


.globl asm_jit_LDA_IMM
.globl asm_jit_LDA_IMM_END
asm_jit_LDA_IMM:
  movz REG_6502_A, #0xFFFF

asm_jit_LDA_IMM_END:
  ret


.globl asm_jit_LDX
.globl asm_jit_LDX_END
asm_jit_LDX:
  ldrb REG_6502_X_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_LDX_END:
  ret


.globl asm_jit_LDX_ABS
.globl asm_jit_LDX_ABS_END
asm_jit_LDX_ABS:
  ldrb REG_6502_X_32, [REG_MEM_READ, #0xFFF]

asm_jit_LDX_ABS_END:
  ret


.globl asm_jit_LDX_IMM
.globl asm_jit_LDX_IMM_END
asm_jit_LDX_IMM:
  movz REG_6502_X, #0xFFFF

asm_jit_LDX_IMM_END:
  ret


.globl asm_jit_LDY
.globl asm_jit_LDY_END
asm_jit_LDY:
  ldrb REG_6502_Y_32, [REG_MEM_READ, REG_SCRATCH1]

asm_jit_LDY_END:
  ret


.globl asm_jit_LDY_ABS
.globl asm_jit_LDY_ABS_END
asm_jit_LDY_ABS:
  ldrb REG_6502_Y_32, [REG_MEM_READ, #0xFFF]

asm_jit_LDY_ABS_END:
  ret


.globl asm_jit_LDY_IMM
.globl asm_jit_LDY_IMM_END
asm_jit_LDY_IMM:
  movz REG_6502_Y, #0xFFFF

asm_jit_LDY_IMM_END:
  ret


.globl asm_jit_LOAD_BYTE_PAIR_or
.globl asm_jit_LOAD_BYTE_PAIR_or_END
asm_jit_LOAD_BYTE_PAIR_or:
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_jit_LOAD_BYTE_PAIR_or_END:
  ret


.globl asm_jit_LSR
.globl asm_jit_LSR_END
asm_jit_LSR:
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH2, #1

asm_jit_LSR_END:
  ret


.globl asm_jit_LSR_ACC
.globl asm_jit_LSR_ACC_END
asm_jit_LSR_ACC:
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

asm_jit_LSR_ACC_END:
  ret


.globl asm_jit_ORA
.globl asm_jit_ORA_END
asm_jit_ORA:
  orr REG_6502_A, REG_6502_A, REG_SCRATCH2

asm_jit_ORA_END:
  ret


.globl asm_jit_ORA_IMM
.globl asm_jit_ORA_IMM_END
asm_jit_ORA_IMM:
  orr REG_6502_A, REG_6502_A, #1

asm_jit_ORA_IMM_END:
  ret


.globl asm_jit_pull_16bit
.globl asm_jit_pull_16bit_END
asm_jit_pull_16bit:
  # TODO: make these operate on 16-bit values?
  add REG_SCRATCH2, REG_6502_S, #1
  and REG_SCRATCH2, REG_SCRATCH2, #0xFF
  add REG_SCRATCH2, REG_MEM_STACK, REG_SCRATCH2
  ldrb REG_SCRATCH2_32, [REG_SCRATCH2]

  add REG_6502_S, REG_6502_S, #2
  and REG_6502_S, REG_6502_S, #0xFF
  add REG_SCRATCH3, REG_MEM_STACK, REG_6502_S
  ldrb REG_SCRATCH3_32, [REG_SCRATCH3]

  orr REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH3, lsl #8

asm_jit_pull_16bit_END:
  ret


.globl asm_jit_ROL
.globl asm_jit_ROL_END
asm_jit_ROL:
  # NOTE: spills outside bounds of 8-bit.
  add REG_SCRATCH2, REG_6502_CF, REG_SCRATCH2, lsl #1
  ubfx REG_6502_CF, REG_SCRATCH2, #8, #1

asm_jit_ROL_END:
  ret


.globl asm_jit_ROL_ACC
.globl asm_jit_ROL_ACC_END
asm_jit_ROL_ACC:
  add REG_6502_A, REG_6502_CF, REG_6502_A, lsl #1
  ubfx REG_6502_CF, REG_6502_A, #8, #1
  and REG_6502_A, REG_6502_A, #0xFF

asm_jit_ROL_ACC_END:
  ret


.globl asm_jit_ROR
.globl asm_jit_ROR_END
asm_jit_ROR:
  orr REG_SCRATCH3, REG_SCRATCH2, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH3, #1

asm_jit_ROR_END:
  ret


.globl asm_jit_ROR_ACC
.globl asm_jit_ROR_ACC_END
asm_jit_ROR_ACC:
  orr REG_6502_A, REG_6502_A, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

asm_jit_ROR_ACC_END:
  ret


.globl asm_jit_SAX
.globl asm_jit_SAX_END
asm_jit_SAX:
  and REG_SCRATCH2, REG_6502_A, REG_6502_X

asm_jit_SAX_END:
  ret


.globl asm_jit_SBC
.globl asm_jit_SBC_END
asm_jit_SBC:
  eor REG_SCRATCH3, REG_6502_CF, #1
  add REG_SCRATCH2, REG_SCRATCH3, REG_SCRATCH2, lsl #24
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_SCRATCH2_32
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_SBC_END:
  ret


.globl asm_jit_SBC_IMM
.globl asm_jit_SBC_IMM_END
asm_jit_SBC_IMM:
  eor REG_SCRATCH3, REG_6502_CF, #1
  add REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH3
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_SCRATCH2_32
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_SBC_IMM_END:
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


.globl asm_jit_SHY_SCRATCH2
.globl asm_jit_SHY_SCRATCH2_END
asm_jit_SHY_SCRATCH2:
  lsr REG_SCRATCH2, REG_SCRATCH1, #8
  # NOTE: overflows 8-bit.
  add REG_SCRATCH2, REG_SCRATCH2, #1
  and REG_SCRATCH2, REG_SCRATCH2, REG_6502_Y

asm_jit_SHY_SCRATCH2_END:
  ret


.globl asm_jit_SLO
.globl asm_jit_SLO_END
asm_jit_SLO:
  ubfx REG_6502_CF, REG_SCRATCH2, #7, #1
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  orr REG_6502_A, REG_6502_A, REG_SCRATCH2
  and REG_6502_A, REG_6502_A, #0xFF

asm_jit_SLO_END:
  ret


.globl asm_jit_STA
.globl asm_jit_STA_END
asm_jit_STA:
  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STA_END:
  ret


.globl asm_jit_STA_ABS
.globl asm_jit_STA_ABS_END
asm_jit_STA_ABS:
  strb REG_6502_A_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_STA_ABS_END:
  ret


.globl asm_jit_STX
.globl asm_jit_STX_END
asm_jit_STX:
  strb REG_6502_X_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STX_END:
  ret


.globl asm_jit_STX_ABS
.globl asm_jit_STX_ABS_END
asm_jit_STX_ABS:
  strb REG_6502_X_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_STX_ABS_END:
  ret


.globl asm_jit_STY
.globl asm_jit_STY_END
asm_jit_STY:
  strb REG_6502_Y_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STY_END:
  ret


.globl asm_jit_STY_ABS
.globl asm_jit_STY_ABS_END
asm_jit_STY_ABS:
  strb REG_6502_Y_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_STY_ABS_END:
  ret


.globl asm_jit_SUB
.globl asm_jit_SUB_END
asm_jit_SUB:
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_SCRATCH2_32, lsl #24
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_SUB_END:
  ret


.globl asm_jit_SUB_IMM
.globl asm_jit_SUB_IMM_END
asm_jit_SUB_IMM:
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_SCRATCH2_32
  lsr REG_6502_A, REG_6502_A, #24

asm_jit_SUB_IMM_END:
  ret
