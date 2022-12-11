#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "../asm_jit_defs.h"
#include "../asm_platform.h"
#include "asm_calling_convention_arm64.h"
#include "asm_defs_registers_arm64.h"

.file "asm/arm64/asm_jit_arm64.S"
.text
.p2align 2


.globl ASM_SYM(asm_jit_enter)
.globl ASM_SYM(asm_jit_enter_END)
ASM_SYM(asm_jit_enter):
  stp x29, x30, [sp, #-16]!
  bl ASM_SYM(asm_common_save_callee_regs)

  movz REG_MEM_READ, #(K_BBC_MEM_OFFSET_TO_READ_FULL >> 16), lsl #16
  movk REG_MEM_READ, #(K_BBC_MEM_OFFSET_TO_READ_FULL & 0xFFFF)
  add REG_MEM_READ, REG_PARAM4, REG_MEM_READ

  adr REG_JIT_COMPILE, ASM_SYM(asm_jit_compile_trampoline)
  # NOTE: can save a register by adjusting REG_CONTEXT as we bounce in and out
  # of JIT.
  # REG_PARAM1 is REG_CONTEXT.
  add REG_JIT_PTRS, REG_PARAM1, #K_JIT_CONTEXT_OFFSET_JIT_PTRS

  b ASM_SYM(asm_common_enter)

ASM_SYM(asm_jit_enter_END):
  ret


.globl ASM_SYM(asm_jit_compile_trampoline)
ASM_SYM(asm_jit_compile_trampoline):
  stp x29, x30, [sp, #-16]!

  bl ASM_SYM(asm_save_AXYS_PC_flags)

  # param1: context object
  mov REG_PARAM1, REG_CONTEXT
  # param2: instruction pointer that called here
  ldr REG_PARAM2, [sp, #8]
  sub REG_PARAM2, REG_PARAM2, #4
  # param3: countdown
  mov REG_PARAM3, REG_COUNTDOWN
  # param4: flags
  mrs REG_PARAM4, nzcv

  ldr REG_SCRATCH1, [REG_CONTEXT, #K_JIT_CONTEXT_OFFSET_JIT_CALLBACK]
  blr REG_SCRATCH1

  mov REG_COUNTDOWN, REG_RETURN
  bl ASM_SYM(asm_restore_AXYS_PC_flags)

  add sp, sp, 16

  lsl REG_6502_PC, REG_6502_PC, #K_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_JIT_ADDR
  br REG_6502_PC


.globl ASM_SYM(asm_jit_interp_trampoline)
.globl ASM_SYM(asm_jit_interp_trampoline_END)
ASM_SYM(asm_jit_interp_trampoline):
  stp x29, x30, [sp, #-32]!

  bl ASM_SYM(asm_save_AXYS_PC_flags)

  # param1: interp object, fetched from context
  ldr REG_PARAM1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_OBJECT]
  # param2: storage for 2x int64 return values
  add REG_PARAM2, sp, #16
  # param3: countdown
  mov REG_PARAM3, REG_COUNTDOWN
  # param4: flags
  mrs REG_PARAM4, nzcv

  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_CALLBACK]
  blr REG_SCRATCH1

  ldr REG_COUNTDOWN, [sp, #16]
  ldr REG_SCRATCH1, [sp, #24]

  ldp x29, x30, [sp], #32

  tst REG_SCRATCH1, REG_SCRATCH1
  b.eq not_exiting
  mov REG_RETURN, REG_SCRATCH1
  b ASM_SYM(asm_common_exit)

ASM_SYM(asm_jit_interp_trampoline_END):
  ret

not_exiting:
  bl ASM_SYM(asm_restore_AXYS_PC_flags)

  lsl REG_6502_PC, REG_6502_PC, #K_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_JIT_ADDR
  br REG_6502_PC

not_exiting_END:
  ret


.globl ASM_SYM(asm_jit_call_inturbo)
.globl ASM_SYM(asm_jit_call_inturbo_END)
ASM_SYM(asm_jit_call_inturbo):
  # Read opcode from PC.
  add REG_6502_PC, REG_6502_PC, REG_MEM_READ
  ldrb REG_SCRATCH1_32, [REG_6502_PC]

  # Calculate jump target.
  lsl REG_SCRATCH1, REG_SCRATCH1, #K_INTURBO_OPCODE_SHIFT
  orr REG_SCRATCH1, REG_SCRATCH1, #K_INTURBO_ADDR

  # Save JIT context register and load inturbo context register.
  stp REG_CONTEXT, x30, [sp, #-16]!
  ldr REG_CONTEXT, [REG_CONTEXT, #K_JIT_CONTEXT_OFFSET_INTURBO]
  # Twiddle registers from "JIT" to "inturbo".
  stp REG_JIT_COMPILE, REG_JIT_PTRS, [sp, #-16]!
  ldr REG_INTURBO_INTERP, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_ASM]

  # Call inturbo and restore.
  blr REG_SCRATCH1
  ldp REG_JIT_COMPILE, REG_JIT_PTRS, [sp], #16
  ldp REG_CONTEXT, x30, [sp], #16

  # Calculate new JIT code address and jump.
  sub REG_6502_PC, REG_6502_PC, REG_MEM_READ
  lsl REG_6502_PC, REG_6502_PC, #K_JIT_BYTES_SHIFT
  orr REG_6502_PC, REG_6502_PC, #K_JIT_ADDR
  br REG_6502_PC

ASM_SYM(asm_jit_call_inturbo_END):
  ret


.globl ASM_SYM(asm_jit_addr_add)
.globl ASM_SYM(asm_jit_addr_add_END)
ASM_SYM(asm_jit_addr_add):
  add REG_JIT_ADDR, REG_JIT_ADDR, #4095

ASM_SYM(asm_jit_addr_add_END):
  ret


.globl ASM_SYM(asm_jit_addr_add_base_constant)
.globl ASM_SYM(asm_jit_addr_add_base_constant_END)
ASM_SYM(asm_jit_addr_add_base_constant):
  add REG_JIT_ADDR, REG_JIT_ADDR_BASE, #4095

ASM_SYM(asm_jit_addr_add_base_constant_END):
  ret


.globl ASM_SYM(asm_jit_addr_add_base_y)
.globl ASM_SYM(asm_jit_addr_add_base_y_END)
ASM_SYM(asm_jit_addr_add_base_y):
  add REG_JIT_ADDR, REG_JIT_ADDR_BASE, REG_6502_Y

ASM_SYM(asm_jit_addr_add_base_y_END):
  ret


.globl ASM_SYM(asm_jit_addr_add_x)
.globl ASM_SYM(asm_jit_addr_add_x_END)
ASM_SYM(asm_jit_addr_add_x):
  add REG_JIT_ADDR, REG_JIT_ADDR, REG_6502_X

ASM_SYM(asm_jit_addr_add_x_END):
  ret


.globl ASM_SYM(asm_jit_addr_add_y)
.globl ASM_SYM(asm_jit_addr_add_y_END)
ASM_SYM(asm_jit_addr_add_y):
  add REG_JIT_ADDR, REG_JIT_ADDR, REG_6502_Y

ASM_SYM(asm_jit_addr_add_y_END):
  ret


.globl ASM_SYM(asm_jit_addr_base_load_addr_base)
.globl ASM_SYM(asm_jit_addr_base_load_addr_base_END)
ASM_SYM(asm_jit_addr_base_load_addr_base):
  ldrb REG_JIT_ADDR_BASE_32, [REG_MEM_READ, REG_JIT_ADDR_BASE]

ASM_SYM(asm_jit_addr_base_load_addr_base_END):
  ret


.globl ASM_SYM(asm_jit_addr_check_add)
.globl ASM_SYM(asm_jit_addr_check_add_END)
.globl ASM_SYM(asm_jit_addr_check_tbnz)
.globl ASM_SYM(asm_jit_addr_check_tbnz_END)
ASM_SYM(asm_jit_addr_check_add):
  # Trigger on >= 0xFC00.
  add REG_JIT_SCRATCH, REG_JIT_ADDR, #0x400

ASM_SYM(asm_jit_addr_check_add_END):
  ret

ASM_SYM(asm_jit_addr_check_tbnz):
  tbnz REG_JIT_SCRATCH, #16, ASM_SYM(asm_jit_addr_check_tbnz)

ASM_SYM(asm_jit_addr_check_tbnz_END):
  ret


.globl ASM_SYM(asm_jit_addr_load)
.globl ASM_SYM(asm_jit_addr_load_END)
ASM_SYM(asm_jit_addr_load):
  ldrb REG_JIT_ADDR_32, [REG_MEM_READ, #0xFFF]

ASM_SYM(asm_jit_addr_load_END):
  ret


.globl ASM_SYM(asm_jit_addr_load_base)
.globl ASM_SYM(asm_jit_addr_load_base_END)
ASM_SYM(asm_jit_addr_load_base):
  ldrb REG_JIT_ADDR_BASE_32, [REG_MEM_READ, #0xFFF]

ASM_SYM(asm_jit_addr_load_base_END):
  ret


.globl ASM_SYM(asm_jit_addr_load_16bit_wrap)
.globl ASM_SYM(asm_jit_addr_load_16bit_wrap_END)
ASM_SYM(asm_jit_addr_load_16bit_wrap):
  # NOTE: assumes zero-page.
  add REG_JIT_SCRATCH, REG_JIT_ADDR, #1
  and REG_JIT_SCRATCH, REG_JIT_SCRATCH, #0xFF
  ldrb REG_JIT_SCRATCH_32, [REG_MEM_READ, REG_JIT_SCRATCH]
  ldrb REG_JIT_ADDR_32, [REG_MEM_READ, REG_JIT_ADDR]
  orr REG_JIT_ADDR, REG_JIT_ADDR, REG_JIT_SCRATCH, lsl #8

ASM_SYM(asm_jit_addr_load_16bit_wrap_END):
  ret


.globl ASM_SYM(asm_jit_addr_load_addr)
.globl ASM_SYM(asm_jit_addr_load_addr_END)
ASM_SYM(asm_jit_addr_load_addr):
  ldrb REG_JIT_ADDR_32, [REG_MEM_READ, REG_JIT_ADDR]

ASM_SYM(asm_jit_addr_load_addr_END):
  ret


.globl ASM_SYM(asm_jit_addr_set)
.globl ASM_SYM(asm_jit_addr_set_END)
ASM_SYM(asm_jit_addr_set):
  movz REG_JIT_ADDR, 0xFFFF

ASM_SYM(asm_jit_addr_set_END):
  ret


.globl ASM_SYM(asm_jit_addr_set_base)
.globl ASM_SYM(asm_jit_addr_set_base_END)
ASM_SYM(asm_jit_addr_set_base):
  movz REG_JIT_ADDR_BASE, 0xFFFF

ASM_SYM(asm_jit_addr_set_base_END):
  ret


.globl ASM_SYM(asm_jit_addr_trunc_8bit)
.globl ASM_SYM(asm_jit_addr_trunc_8bit_END)
ASM_SYM(asm_jit_addr_trunc_8bit):
  and REG_JIT_ADDR, REG_JIT_ADDR, #0xFF

ASM_SYM(asm_jit_addr_trunc_8bit_END):
  ret


.globl ASM_SYM(asm_jit_check_bcd)
.globl ASM_SYM(asm_jit_check_bcd_END)
ASM_SYM(asm_jit_check_bcd):
  tbnz REG_6502_ID_F, #3, ASM_SYM(asm_jit_check_bcd)

ASM_SYM(asm_jit_check_bcd_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_ABX_sub)
.globl ASM_SYM(asm_jit_check_page_crossing_ABX_sub_END)
.globl ASM_SYM(asm_jit_check_page_crossing_ABX_add)
.globl ASM_SYM(asm_jit_check_page_crossing_ABX_add_END)
ASM_SYM(asm_jit_check_page_crossing_ABX_sub):
  sub REG_JIT_SCRATCH, REG_6502_X, #4095

ASM_SYM(asm_jit_check_page_crossing_ABX_sub_END):
  ret

ASM_SYM(asm_jit_check_page_crossing_ABX_add):
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_JIT_SCRATCH, lsr #63

ASM_SYM(asm_jit_check_page_crossing_ABX_add_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_ABY_sub)
.globl ASM_SYM(asm_jit_check_page_crossing_ABY_sub_END)
.globl ASM_SYM(asm_jit_check_page_crossing_ABY_add)
.globl ASM_SYM(asm_jit_check_page_crossing_ABY_add_END)
ASM_SYM(asm_jit_check_page_crossing_ABY_sub):
  sub REG_JIT_SCRATCH, REG_6502_Y, #4095

ASM_SYM(asm_jit_check_page_crossing_ABY_sub_END):
  ret

ASM_SYM(asm_jit_check_page_crossing_ABY_add):
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_JIT_SCRATCH, lsr #63

ASM_SYM(asm_jit_check_page_crossing_ABY_add_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_n)
.globl ASM_SYM(asm_jit_check_page_crossing_n_END)
ASM_SYM(asm_jit_check_page_crossing_n):
  eon REG_JIT_SCRATCH, REG_JIT_ADDR_BASE, REG_JIT_ADDR
  ubfx REG_JIT_SCRATCH, REG_JIT_SCRATCH, #8, #1
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_JIT_SCRATCH

ASM_SYM(asm_jit_check_page_crossing_n_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_x)
.globl ASM_SYM(asm_jit_check_page_crossing_x_END)
ASM_SYM(asm_jit_check_page_crossing_x):
  sub REG_JIT_SCRATCH, REG_JIT_ADDR, REG_6502_X
  eon REG_JIT_SCRATCH, REG_JIT_SCRATCH, REG_JIT_ADDR
  ubfx REG_JIT_SCRATCH, REG_JIT_SCRATCH, #8, #1
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_JIT_SCRATCH

ASM_SYM(asm_jit_check_page_crossing_x_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_y)
.globl ASM_SYM(asm_jit_check_page_crossing_y_END)
ASM_SYM(asm_jit_check_page_crossing_y):
  sub REG_JIT_SCRATCH, REG_JIT_ADDR, REG_6502_Y
  eon REG_JIT_SCRATCH, REG_JIT_SCRATCH, REG_JIT_ADDR
  ubfx REG_JIT_SCRATCH, REG_JIT_SCRATCH, #8, #1
  add REG_COUNTDOWN, REG_COUNTDOWN, REG_JIT_SCRATCH

ASM_SYM(asm_jit_check_page_crossing_y_END):
  ret


.globl ASM_SYM(asm_jit_check_pending_irq_load)
.globl ASM_SYM(asm_jit_check_pending_irq_load_END)
.globl ASM_SYM(asm_jit_check_pending_irq_cbnz)
.globl ASM_SYM(asm_jit_check_pending_irq_cbnz_END)
ASM_SYM(asm_jit_check_pending_irq_load):
  ldr REG_JIT_SCRATCH, [REG_CONTEXT, #K_CONTEXT_OFFSET_STATE_6502]
  ldr REG_JIT_SCRATCH_32, [REG_JIT_SCRATCH, #K_STATE_6502_OFFSET_REG_IRQ_FIRE]

ASM_SYM(asm_jit_check_pending_irq_load_END):
  ret

ASM_SYM(asm_jit_check_pending_irq_cbnz):
  cbnz REG_JIT_SCRATCH, ASM_SYM(asm_jit_check_pending_irq_cbnz)

ASM_SYM(asm_jit_check_pending_irq_cbnz_END):
  ret


.globl ASM_SYM(asm_jit_countdown_add)
.globl ASM_SYM(asm_jit_countdown_add_END)
ASM_SYM(asm_jit_countdown_add):
  add REG_COUNTDOWN, REG_COUNTDOWN, #4095

ASM_SYM(asm_jit_countdown_add_END):
  ret


.globl ASM_SYM(asm_jit_countdown_sub)
.globl ASM_SYM(asm_jit_countdown_sub_END)
.globl ASM_SYM(asm_jit_countdown_tbnz)
.globl ASM_SYM(asm_jit_countdown_tbnz_END)
ASM_SYM(asm_jit_countdown_sub):
  sub REG_COUNTDOWN, REG_COUNTDOWN, #4095

ASM_SYM(asm_jit_countdown_sub_END):
  ret

ASM_SYM(asm_jit_countdown_tbnz):
  tbnz REG_COUNTDOWN, #63, ASM_SYM(asm_jit_countdown_tbnz)

ASM_SYM(asm_jit_countdown_tbnz_END):
  ret


.globl ASM_SYM(asm_jit_call_debug)
.globl ASM_SYM(asm_jit_call_debug_END)
ASM_SYM(asm_jit_call_debug):
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_DEBUG_ASM]
  blr REG_SCRATCH1

ASM_SYM(asm_jit_call_debug_END):
  ret


.globl ASM_SYM(asm_jit_flags_nz_value)
.globl ASM_SYM(asm_jit_flags_nz_value_END)
ASM_SYM(asm_jit_flags_nz_value):
  adds xzr, xzr, REG_VALUE, lsl #56

ASM_SYM(asm_jit_flags_nz_value_END):
  ret


.globl ASM_SYM(asm_jit_jump_interp)
.globl ASM_SYM(asm_jit_jump_interp_END)
ASM_SYM(asm_jit_jump_interp):
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_ASM]
  br REG_SCRATCH1

ASM_SYM(asm_jit_jump_interp_END):
  ret


.globl ASM_SYM(asm_jit_load_byte_pair_base_or)
.globl ASM_SYM(asm_jit_load_byte_pair_base_or_END)
ASM_SYM(asm_jit_load_byte_pair_base_or):
  orr REG_JIT_ADDR_BASE, REG_JIT_ADDR_BASE, REG_JIT_SCRATCH, lsl #8

ASM_SYM(asm_jit_load_byte_pair_base_or_END):
  ret


.globl ASM_SYM(asm_jit_load_byte_pair_or)
.globl ASM_SYM(asm_jit_load_byte_pair_or_END)
ASM_SYM(asm_jit_load_byte_pair_or):
  orr REG_JIT_ADDR, REG_JIT_ADDR, REG_JIT_SCRATCH, lsl #8

ASM_SYM(asm_jit_load_byte_pair_or_END):
  ret


.globl ASM_SYM(asm_jit_load_PC)
.globl ASM_SYM(asm_jit_load_PC_END)
ASM_SYM(asm_jit_load_PC):
  movz REG_6502_PC, #0xFFFF

ASM_SYM(asm_jit_load_PC_END):
  ret


.globl ASM_SYM(asm_jit_mode_ABX)
.globl ASM_SYM(asm_jit_mode_ABX_END)
ASM_SYM(asm_jit_mode_ABX):
  add REG_JIT_ADDR, REG_6502_X, #4095

ASM_SYM(asm_jit_mode_ABX_END):
  ret


.globl ASM_SYM(asm_jit_mode_ABY)
.globl ASM_SYM(asm_jit_mode_ABY_END)
ASM_SYM(asm_jit_mode_ABY):
  add REG_JIT_ADDR, REG_6502_Y, #4095

ASM_SYM(asm_jit_mode_ABY_END):
  ret


.globl ASM_SYM(asm_jit_push)
.globl ASM_SYM(asm_jit_push_END)
ASM_SYM(asm_jit_push):
  add REG_SCRATCH1, REG_MEM_STACK, REG_6502_S
  strb REG_JIT_SCRATCH_32, [REG_SCRATCH1]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF

ASM_SYM(asm_jit_push_END):
  ret


.globl ASM_SYM(asm_jit_save_carry)
.globl ASM_SYM(asm_jit_save_carry_END)
ASM_SYM(asm_jit_save_carry):
  cset REG_6502_CF, cs

ASM_SYM(asm_jit_save_carry_END):
  ret


.globl ASM_SYM(asm_jit_save_overflow)
.globl ASM_SYM(asm_jit_save_overflow_END)
ASM_SYM(asm_jit_save_overflow):
  cset REG_6502_OF, vs

ASM_SYM(asm_jit_save_overflow_END):
  ret


.globl ASM_SYM(asm_jit_scratch_load_ABS)
.globl ASM_SYM(asm_jit_scratch_load_ABS_END)
ASM_SYM(asm_jit_scratch_load_ABS):
  ldrb REG_JIT_SCRATCH_32, [REG_MEM_READ, #0xFFF]

ASM_SYM(asm_jit_scratch_load_ABS_END):
  ret


.globl ASM_SYM(asm_jit_scratch_load_scratch)
.globl ASM_SYM(asm_jit_scratch_load_scratch_END)
ASM_SYM(asm_jit_scratch_load_scratch):
  ldrb REG_JIT_SCRATCH_32, [REG_MEM_READ, REG_JIT_SCRATCH]

ASM_SYM(asm_jit_scratch_load_scratch_END):
  ret


.globl ASM_SYM(asm_jit_scratch_set)
.globl ASM_SYM(asm_jit_scratch_set_END)
ASM_SYM(asm_jit_scratch_set):
  movz REG_JIT_SCRATCH, 0xFFFF

ASM_SYM(asm_jit_scratch_set_END):
  ret


.globl ASM_SYM(asm_jit_value_load_ABS)
.globl ASM_SYM(asm_jit_value_load_ABS_END)
ASM_SYM(asm_jit_value_load_ABS):
  ldrb REG_VALUE_32, [REG_MEM_READ, #0xFFF]

ASM_SYM(asm_jit_value_load_ABS_END):
  ret


.globl ASM_SYM(asm_jit_value_load_addr)
.globl ASM_SYM(asm_jit_value_load_addr_END)
ASM_SYM(asm_jit_value_load_addr):
  ldrb REG_VALUE_32, [REG_MEM_READ, REG_JIT_ADDR]

ASM_SYM(asm_jit_value_load_addr_END):
  ret


.globl ASM_SYM(asm_jit_value_set)
.globl ASM_SYM(asm_jit_value_set_END)
ASM_SYM(asm_jit_value_set):
  movz REG_VALUE, 0xFFFF

ASM_SYM(asm_jit_value_set_END):
  ret


.globl ASM_SYM(asm_jit_value_set_hi)
.globl ASM_SYM(asm_jit_value_set_hi_END)
ASM_SYM(asm_jit_value_set_hi):
  movz REG_VALUE, #0xFFFF, lsl #16

ASM_SYM(asm_jit_value_set_hi_END):
  ret


.globl ASM_SYM(asm_jit_value_store_ABS)
.globl ASM_SYM(asm_jit_value_store_ABS_END)
ASM_SYM(asm_jit_value_store_ABS):
  strb REG_VALUE_32, [REG_MEM_WRITE, #0xFFF]

ASM_SYM(asm_jit_value_store_ABS_END):
  ret


.globl ASM_SYM(asm_jit_value_store_addr)
.globl ASM_SYM(asm_jit_value_store_addr_END)
ASM_SYM(asm_jit_value_store_addr):
  strb REG_VALUE_32, [REG_MEM_WRITE, REG_JIT_ADDR]

ASM_SYM(asm_jit_value_store_addr_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_load)
.globl ASM_SYM(asm_jit_write_inv_load_END)
ASM_SYM(asm_jit_write_inv_load):
  ldr REG_JIT_SCRATCH_32, [REG_JIT_PTRS, REG_JIT_ADDR, lsl #2]

ASM_SYM(asm_jit_write_inv_load_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_orr)
.globl ASM_SYM(asm_jit_write_inv_orr_END)
ASM_SYM(asm_jit_write_inv_orr):
  orr REG_JIT_SCRATCH, REG_JIT_SCRATCH, #K_JIT_ADDR

ASM_SYM(asm_jit_write_inv_orr_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_store)
.globl ASM_SYM(asm_jit_write_inv_store_END)
ASM_SYM(asm_jit_write_inv_store):
  # Store anything convenient since it'll write fault.
  str REG_JIT_SCRATCH_32, [REG_JIT_SCRATCH]

ASM_SYM(asm_jit_write_inv_store_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_ABS_load)
.globl ASM_SYM(asm_jit_write_inv_ABS_load_END)
ASM_SYM(asm_jit_write_inv_ABS_load):
  # 6502 range is 0 - 0xFFF. Constant below is 4x that due to 32 bit ptrs.
  ldr REG_JIT_SCRATCH_32, [REG_JIT_PTRS, #0x3FFC]

ASM_SYM(asm_jit_write_inv_ABS_load_END):
  ret


.globl ASM_SYM(asm_jit_ADC)
.globl ASM_SYM(asm_jit_ADC_END)
ASM_SYM(asm_jit_ADC):
  add REG_JIT_SCRATCH, REG_VALUE, REG_6502_CF
  add REG_JIT_SCRATCH, REG_JIT_SCRATCH, REG_6502_A

  add REG_SCRATCH1, REG_6502_CF, REG_VALUE, lsl #24
  ubfx REG_6502_CF, REG_JIT_SCRATCH, #8, #1
  lsl REG_SCRATCH2, REG_6502_A, #24
  orr REG_SCRATCH2, REG_SCRATCH2, #0x00FFFFFF
  adds wzr, REG_SCRATCH2_32, REG_SCRATCH1_32

  and REG_6502_A, REG_JIT_SCRATCH, #0xFF

ASM_SYM(asm_jit_ADC_END):
  ret


.globl ASM_SYM(asm_jit_ADC_IMM)
.globl ASM_SYM(asm_jit_ADC_IMM_END)
ASM_SYM(asm_jit_ADC_IMM):
# TODO: restore an IMM variant that bakes in the IMM constant.

ASM_SYM(asm_jit_ADC_IMM_END):
  ret


.globl ASM_SYM(asm_jit_ADD)
.globl ASM_SYM(asm_jit_ADD_END)
ASM_SYM(asm_jit_ADD):
  add REG_JIT_SCRATCH, REG_VALUE, REG_6502_A
  ubfx REG_6502_CF, REG_JIT_SCRATCH, #8, #1
  lsl REG_SCRATCH1, REG_6502_A, #24
  and REG_6502_A, REG_JIT_SCRATCH, #0xFF
  adds wzr, REG_SCRATCH1_32, REG_VALUE_32, lsl #24

ASM_SYM(asm_jit_ADD_END):
  ret


.globl ASM_SYM(asm_jit_ADD_IMM)
.globl ASM_SYM(asm_jit_ADD_IMM_END)
ASM_SYM(asm_jit_ADD_IMM):
# TODO: restore an IMM variant that bakes in the IMM constant.

ASM_SYM(asm_jit_ADD_IMM_END):
  ret


.globl ASM_SYM(asm_jit_ALR)
.globl ASM_SYM(asm_jit_ALR_END)
ASM_SYM(asm_jit_ALR):
  and REG_6502_A, REG_6502_A, REG_VALUE
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

ASM_SYM(asm_jit_ALR_END):
  ret


.globl ASM_SYM(asm_jit_AND)
.globl ASM_SYM(asm_jit_AND_END)
ASM_SYM(asm_jit_AND):
  and REG_6502_A, REG_6502_A, REG_VALUE

ASM_SYM(asm_jit_AND_END):
  ret


.globl ASM_SYM(asm_jit_AND_IMM)
.globl ASM_SYM(asm_jit_AND_IMM_END)
ASM_SYM(asm_jit_AND_IMM):
  and REG_6502_A, REG_6502_A, #1

ASM_SYM(asm_jit_AND_IMM_END):
  ret


.globl ASM_SYM(asm_jit_ASL)
.globl ASM_SYM(asm_jit_ASL_END)
ASM_SYM(asm_jit_ASL):
  ubfx REG_6502_CF, REG_VALUE, #7, #1
  # NOTE: spills outside bounds of 8-bit.
  lsl REG_VALUE, REG_VALUE, #1

ASM_SYM(asm_jit_ASL_END):
  ret


.globl ASM_SYM(asm_jit_ASL_ACC_ubfm_carry)
.globl ASM_SYM(asm_jit_ASL_ACC_ubfm_carry_END)
.globl ASM_SYM(asm_jit_ASL_ACC_ubfm_shift)
.globl ASM_SYM(asm_jit_ASL_ACC_ubfm_shift_END)
.globl ASM_SYM(asm_jit_ASL_ACC_END)
ASM_SYM(asm_jit_ASL_ACC_ubfm_carry):
  # aka. ubfm REG_6502_CF, REG_6502_A, #7, #(7 + 1 - 1)
  ubfx REG_6502_CF, REG_6502_A, #7, #1
ASM_SYM(asm_jit_ASL_ACC_ubfm_carry_END):
ASM_SYM(asm_jit_ASL_ACC_ubfm_shift):
  # Shift the lower 7 bits left.
  ubfm REG_6502_A, REG_6502_A, #63, #6
ASM_SYM(asm_jit_ASL_ACC_ubfm_shift_END):
  ret


.globl ASM_SYM(asm_jit_BCC)
.globl ASM_SYM(asm_jit_BCC_END)
ASM_SYM(asm_jit_BCC):
  cbz REG_6502_CF, ASM_SYM(asm_jit_BCC)

ASM_SYM(asm_jit_BCC_END):
  ret


.globl ASM_SYM(asm_jit_BCS)
.globl ASM_SYM(asm_jit_BCS_END)
ASM_SYM(asm_jit_BCS):
  cbnz REG_6502_CF, ASM_SYM(asm_jit_BCS)

ASM_SYM(asm_jit_BCS_END):
  ret


.globl ASM_SYM(asm_jit_BEQ)
.globl ASM_SYM(asm_jit_BEQ_END)
ASM_SYM(asm_jit_BEQ):
  b.eq ASM_SYM(asm_jit_BEQ)

ASM_SYM(asm_jit_BEQ_END):
  ret


.globl ASM_SYM(asm_jit_BMI)
.globl ASM_SYM(asm_jit_BMI_END)
ASM_SYM(asm_jit_BMI):
  b.mi ASM_SYM(asm_jit_BMI)

ASM_SYM(asm_jit_BMI_END):
  ret


.globl ASM_SYM(asm_jit_BNE)
.globl ASM_SYM(asm_jit_BNE_END)
ASM_SYM(asm_jit_BNE):
  b.ne ASM_SYM(asm_jit_BNE)

ASM_SYM(asm_jit_BNE_END):
  ret


.globl ASM_SYM(asm_jit_BPL)
.globl ASM_SYM(asm_jit_BPL_END)
ASM_SYM(asm_jit_BPL):
  b.pl ASM_SYM(asm_jit_BPL)

ASM_SYM(asm_jit_BPL_END):
  ret


.globl ASM_SYM(asm_jit_BVC)
.globl ASM_SYM(asm_jit_BVC_END)
ASM_SYM(asm_jit_BVC):
  cbz REG_6502_OF, ASM_SYM(asm_jit_BVC)

ASM_SYM(asm_jit_BVC_END):
  ret


.globl ASM_SYM(asm_jit_BVS)
.globl ASM_SYM(asm_jit_BVS_END)
ASM_SYM(asm_jit_BVS):
  cbnz REG_6502_OF, ASM_SYM(asm_jit_BVS)

ASM_SYM(asm_jit_BVS_END):
  ret


.globl ASM_SYM(asm_jit_CMP_IMM_subs)
.globl ASM_SYM(asm_jit_CMP_IMM_subs_END)
.globl ASM_SYM(asm_jit_CMP_IMM_flags)
.globl ASM_SYM(asm_jit_CMP_IMM_flags_END)
ASM_SYM(asm_jit_CMP_IMM_subs):
  subs REG_VALUE, REG_6502_A, #4095
ASM_SYM(asm_jit_CMP_IMM_subs_END):
  ret

ASM_SYM(asm_jit_CMP_IMM_flags):
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_VALUE, lsl #56

ASM_SYM(asm_jit_CMP_IMM_flags_END):
  ret


.globl ASM_SYM(asm_jit_CMP)
.globl ASM_SYM(asm_jit_CMP_END)
ASM_SYM(asm_jit_CMP):
  subs REG_VALUE, REG_6502_A, REG_VALUE
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_VALUE, lsl #56

ASM_SYM(asm_jit_CMP_END):
  ret


.globl ASM_SYM(asm_jit_CPX)
.globl ASM_SYM(asm_jit_CPX_END)
ASM_SYM(asm_jit_CPX):
  subs REG_VALUE, REG_6502_X, REG_VALUE
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_VALUE, lsl #56

ASM_SYM(asm_jit_CPX_END):
  ret


.globl ASM_SYM(asm_jit_CPX_IMM_subs)
.globl ASM_SYM(asm_jit_CPX_IMM_subs_END)
.globl ASM_SYM(asm_jit_CPX_IMM_flags)
.globl ASM_SYM(asm_jit_CPX_IMM_flags_END)
ASM_SYM(asm_jit_CPX_IMM_subs):
  subs REG_VALUE, REG_6502_X, #4095
ASM_SYM(asm_jit_CPX_IMM_subs_END):
  ret

ASM_SYM(asm_jit_CPX_IMM_flags):
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_VALUE, lsl #56

ASM_SYM(asm_jit_CPX_IMM_flags_END):
  ret


.globl ASM_SYM(asm_jit_CPY)
.globl ASM_SYM(asm_jit_CPY_END)
ASM_SYM(asm_jit_CPY):
  subs REG_VALUE, REG_6502_Y, REG_VALUE
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_VALUE, lsl #56

ASM_SYM(asm_jit_CPY_END):
  ret


.globl ASM_SYM(asm_jit_CPY_IMM_subs)
.globl ASM_SYM(asm_jit_CPY_IMM_subs_END)
.globl ASM_SYM(asm_jit_CPY_IMM_flags)
.globl ASM_SYM(asm_jit_CPY_IMM_flags_END)
ASM_SYM(asm_jit_CPY_IMM_subs):
  subs REG_VALUE, REG_6502_Y, #4095
ASM_SYM(asm_jit_CPY_IMM_subs_END):
  ret

ASM_SYM(asm_jit_CPY_IMM_flags):
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_VALUE, lsl #56

ASM_SYM(asm_jit_CPY_IMM_flags_END):
  ret


.globl ASM_SYM(asm_jit_DEC)
.globl ASM_SYM(asm_jit_DEC_END)
ASM_SYM(asm_jit_DEC):
  # NOTE: spills outside bounds of 8-bit.
  sub REG_VALUE, REG_VALUE, #1

ASM_SYM(asm_jit_DEC_END):
  ret


.globl ASM_SYM(asm_jit_EOR)
.globl ASM_SYM(asm_jit_EOR_END)
ASM_SYM(asm_jit_EOR):
  eor REG_6502_A, REG_6502_A, REG_VALUE

ASM_SYM(asm_jit_EOR_END):
  ret


.globl ASM_SYM(asm_jit_EOR_IMM)
.globl ASM_SYM(asm_jit_EOR_IMM_END)
ASM_SYM(asm_jit_EOR_IMM):
  eor REG_6502_A, REG_6502_A, #1

ASM_SYM(asm_jit_EOR_IMM_END):
  ret


.globl ASM_SYM(asm_jit_INC)
.globl ASM_SYM(asm_jit_INC_END)
ASM_SYM(asm_jit_INC):
  # NOTE: spills outside bounds of 8-bit.
  add REG_VALUE, REG_VALUE, #1

ASM_SYM(asm_jit_INC_END):
  ret


.globl ASM_SYM(asm_jit_JMP)
.globl ASM_SYM(asm_jit_JMP_END)
ASM_SYM(asm_jit_JMP):
  # Range +- 128MB.
  b ASM_SYM(asm_jit_JMP)

ASM_SYM(asm_jit_JMP_END):
  ret


.globl ASM_SYM(asm_jit_JMP_addr)
.globl ASM_SYM(asm_jit_JMP_addr_END)
ASM_SYM(asm_jit_JMP_addr):
  lsl REG_JIT_SCRATCH, REG_JIT_ADDR, #K_JIT_BYTES_SHIFT
  orr REG_JIT_SCRATCH, REG_JIT_SCRATCH, #K_JIT_ADDR
  br REG_JIT_SCRATCH

ASM_SYM(asm_jit_JMP_addr_END):
  ret


.globl ASM_SYM(asm_jit_LDA)
.globl ASM_SYM(asm_jit_LDA_END)
ASM_SYM(asm_jit_LDA):
  ldrb REG_6502_A_32, [REG_MEM_READ, REG_JIT_ADDR]

ASM_SYM(asm_jit_LDA_END):
  ret


.globl ASM_SYM(asm_jit_LDA_ABS)
.globl ASM_SYM(asm_jit_LDA_ABS_END)
ASM_SYM(asm_jit_LDA_ABS):
  ldrb REG_6502_A_32, [REG_MEM_READ, #0xFFF]

ASM_SYM(asm_jit_LDA_ABS_END):
  ret


.globl ASM_SYM(asm_jit_LDA_IMM)
.globl ASM_SYM(asm_jit_LDA_IMM_END)
ASM_SYM(asm_jit_LDA_IMM):
  movz REG_6502_A, #0xFFFF

ASM_SYM(asm_jit_LDA_IMM_END):
  ret


.globl ASM_SYM(asm_jit_LDA_zero_and_flags)
.globl ASM_SYM(asm_jit_LDA_zero_and_flags_END)
ASM_SYM(asm_jit_LDA_zero_and_flags):
  subs REG_6502_A, REG_6502_A, REG_6502_A

ASM_SYM(asm_jit_LDA_zero_and_flags_END):
  ret


.globl ASM_SYM(asm_jit_LDX)
.globl ASM_SYM(asm_jit_LDX_END)
ASM_SYM(asm_jit_LDX):
  ldrb REG_6502_X_32, [REG_MEM_READ, REG_JIT_ADDR]

ASM_SYM(asm_jit_LDX_END):
  ret


.globl ASM_SYM(asm_jit_LDX_ABS)
.globl ASM_SYM(asm_jit_LDX_ABS_END)
ASM_SYM(asm_jit_LDX_ABS):
  ldrb REG_6502_X_32, [REG_MEM_READ, #0xFFF]

ASM_SYM(asm_jit_LDX_ABS_END):
  ret


.globl ASM_SYM(asm_jit_LDX_IMM)
.globl ASM_SYM(asm_jit_LDX_IMM_END)
ASM_SYM(asm_jit_LDX_IMM):
  movz REG_6502_X, #0xFFFF

ASM_SYM(asm_jit_LDX_IMM_END):
  ret


.globl ASM_SYM(asm_jit_LDX_zero_and_flags)
.globl ASM_SYM(asm_jit_LDX_zero_and_flags_END)
ASM_SYM(asm_jit_LDX_zero_and_flags):
  subs REG_6502_X, REG_6502_X, REG_6502_X

ASM_SYM(asm_jit_LDX_zero_and_flags_END):
  ret


.globl ASM_SYM(asm_jit_LDY)
.globl ASM_SYM(asm_jit_LDY_END)
ASM_SYM(asm_jit_LDY):
  ldrb REG_6502_Y_32, [REG_MEM_READ, REG_JIT_ADDR]

ASM_SYM(asm_jit_LDY_END):
  ret


.globl ASM_SYM(asm_jit_LDY_ABS)
.globl ASM_SYM(asm_jit_LDY_ABS_END)
ASM_SYM(asm_jit_LDY_ABS):
  ldrb REG_6502_Y_32, [REG_MEM_READ, #0xFFF]

ASM_SYM(asm_jit_LDY_ABS_END):
  ret


.globl ASM_SYM(asm_jit_LDY_IMM)
.globl ASM_SYM(asm_jit_LDY_IMM_END)
ASM_SYM(asm_jit_LDY_IMM):
  movz REG_6502_Y, #0xFFFF

ASM_SYM(asm_jit_LDY_IMM_END):
  ret


.globl ASM_SYM(asm_jit_LDY_zero_and_flags)
.globl ASM_SYM(asm_jit_LDY_zero_and_flags_END)
ASM_SYM(asm_jit_LDY_zero_and_flags):
  subs REG_6502_Y, REG_6502_Y, REG_6502_Y

ASM_SYM(asm_jit_LDY_zero_and_flags_END):
  ret


.globl ASM_SYM(asm_jit_LSR)
.globl ASM_SYM(asm_jit_LSR_END)
ASM_SYM(asm_jit_LSR):
  ubfx REG_6502_CF, REG_VALUE, #0, #1
  lsr REG_VALUE, REG_VALUE, #1

ASM_SYM(asm_jit_LSR_END):
  ret


.globl ASM_SYM(asm_jit_LSR_ACC_ubfm_carry)
.globl ASM_SYM(asm_jit_LSR_ACC_ubfm_carry_END)
.globl ASM_SYM(asm_jit_LSR_ACC_ubfm_shift)
.globl ASM_SYM(asm_jit_LSR_ACC_ubfm_shift_END)
ASM_SYM(asm_jit_LSR_ACC_ubfm_carry):
  # Extract one carry bit.
  # aka. ubfm REG_6502_CF, REG_6502_A, #0, #(0 + 1 - 1)
  ubfx REG_6502_CF, REG_6502_A, #0, #1
ASM_SYM(asm_jit_LSR_ACC_ubfm_carry_END):
ASM_SYM(asm_jit_LSR_ACC_ubfm_shift):
  # Shift lower 8 bits right one.
  # aka. ubfm REG_6502_A, REG_6502_A, #1, #63
  lsr REG_6502_A, REG_6502_A, #1
ASM_SYM(asm_jit_LSR_ACC_ubfm_shift_END):
  ret


.globl ASM_SYM(asm_jit_ORA)
.globl ASM_SYM(asm_jit_ORA_END)
ASM_SYM(asm_jit_ORA):
  orr REG_6502_A, REG_6502_A, REG_VALUE

ASM_SYM(asm_jit_ORA_END):
  ret


.globl ASM_SYM(asm_jit_ORA_IMM)
.globl ASM_SYM(asm_jit_ORA_IMM_END)
ASM_SYM(asm_jit_ORA_IMM):
  orr REG_6502_A, REG_6502_A, #1

ASM_SYM(asm_jit_ORA_IMM_END):
  ret


.globl ASM_SYM(asm_jit_pull_16bit)
.globl ASM_SYM(asm_jit_pull_16bit_END)
ASM_SYM(asm_jit_pull_16bit):
  # TODO: make these operate on 16-bit values?
  add REG_SCRATCH1, REG_6502_S, #1
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF
  add REG_SCRATCH1, REG_MEM_STACK, REG_SCRATCH1
  ldrb REG_SCRATCH1_32, [REG_SCRATCH1]

  add REG_6502_S, REG_6502_S, #2
  and REG_6502_S, REG_6502_S, #0xFF
  add REG_JIT_SCRATCH, REG_MEM_STACK, REG_6502_S
  ldrb REG_JIT_SCRATCH_32, [REG_JIT_SCRATCH]

  orr REG_JIT_ADDR, REG_SCRATCH1, REG_JIT_SCRATCH, lsl #8

ASM_SYM(asm_jit_pull_16bit_END):
  ret


.globl ASM_SYM(asm_jit_ROL)
.globl ASM_SYM(asm_jit_ROL_END)
ASM_SYM(asm_jit_ROL):
  mov REG_JIT_SCRATCH, REG_6502_CF
  ubfx REG_6502_CF, REG_VALUE, #7, #1
  # NOTE: spills outside bounds of 8-bit.
  lsl REG_VALUE, REG_VALUE, #1
  orr REG_VALUE, REG_VALUE, REG_JIT_SCRATCH

ASM_SYM(asm_jit_ROL_END):
  ret


.globl ASM_SYM(asm_jit_ROL_ACC)
.globl ASM_SYM(asm_jit_ROL_ACC_END)
ASM_SYM(asm_jit_ROL_ACC):
  mov REG_JIT_SCRATCH, REG_6502_CF
  ubfx REG_6502_CF, REG_6502_A, #7, #1
  ubfm REG_6502_A, REG_6502_A, #63, #6
  orr REG_6502_A, REG_JIT_SCRATCH, REG_6502_A

ASM_SYM(asm_jit_ROL_ACC_END):
  ret


.globl ASM_SYM(asm_jit_ROR)
.globl ASM_SYM(asm_jit_ROR_END)
ASM_SYM(asm_jit_ROR):
  ubfx REG_JIT_SCRATCH, REG_VALUE, #0, #1
  lsr REG_VALUE, REG_VALUE, #1
  lsl REG_6502_CF, REG_6502_CF, #7
  orr REG_VALUE, REG_VALUE, REG_6502_CF
  mov REG_6502_CF, REG_JIT_SCRATCH

ASM_SYM(asm_jit_ROR_END):
  ret


.globl ASM_SYM(asm_jit_ROR_ACC)
.globl ASM_SYM(asm_jit_ROR_ACC_END)
ASM_SYM(asm_jit_ROR_ACC):
  ubfx REG_JIT_SCRATCH, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1
  lsl REG_6502_CF, REG_6502_CF, #7
  orr REG_6502_A, REG_6502_A, REG_6502_CF
  mov REG_6502_CF, REG_JIT_SCRATCH

ASM_SYM(asm_jit_ROR_ACC_END):
  ret


.globl ASM_SYM(asm_jit_SAX)
.globl ASM_SYM(asm_jit_SAX_END)
ASM_SYM(asm_jit_SAX):
  and REG_VALUE, REG_6502_A, REG_6502_X

ASM_SYM(asm_jit_SAX_END):
  ret


.globl ASM_SYM(asm_jit_SBC)
.globl ASM_SYM(asm_jit_SBC_END)
ASM_SYM(asm_jit_SBC):
  eor REG_JIT_SCRATCH, REG_6502_CF, #1
  add REG_VALUE, REG_JIT_SCRATCH, REG_VALUE, lsl #24
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_VALUE_32
  lsr REG_6502_A, REG_6502_A, #24

ASM_SYM(asm_jit_SBC_END):
  ret


.globl ASM_SYM(asm_jit_SBC_IMM)
.globl ASM_SYM(asm_jit_SBC_IMM_END)
ASM_SYM(asm_jit_SBC_IMM):
  eor REG_JIT_SCRATCH, REG_6502_CF, #1
  add REG_VALUE, REG_VALUE, REG_JIT_SCRATCH
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_VALUE_32
  lsr REG_6502_A, REG_6502_A, #24

ASM_SYM(asm_jit_SBC_IMM_END):
  ret


.globl ASM_SYM(asm_jit_SHY_SCRATCH2)
.globl ASM_SYM(asm_jit_SHY_SCRATCH2_END)
ASM_SYM(asm_jit_SHY_SCRATCH2):
  lsr REG_VALUE, REG_JIT_ADDR, #8
  # NOTE: overflows 8-bit.
  add REG_VALUE, REG_VALUE, #1
  and REG_VALUE, REG_VALUE, REG_6502_Y

ASM_SYM(asm_jit_SHY_SCRATCH2_END):
  ret


.globl ASM_SYM(asm_jit_SLO)
.globl ASM_SYM(asm_jit_SLO_END)
ASM_SYM(asm_jit_SLO):
  ubfx REG_6502_CF, REG_VALUE, #7, #1
  lsl REG_VALUE, REG_VALUE, #1
  orr REG_6502_A, REG_6502_A, REG_VALUE
  and REG_6502_A, REG_6502_A, #0xFF

ASM_SYM(asm_jit_SLO_END):
  ret


.globl ASM_SYM(asm_jit_STA)
.globl ASM_SYM(asm_jit_STA_END)
ASM_SYM(asm_jit_STA):
  strb REG_6502_A_32, [REG_MEM_WRITE, REG_JIT_ADDR]

ASM_SYM(asm_jit_STA_END):
  ret


.globl ASM_SYM(asm_jit_STA_ABS)
.globl ASM_SYM(asm_jit_STA_ABS_END)
ASM_SYM(asm_jit_STA_ABS):
  strb REG_6502_A_32, [REG_MEM_WRITE, #0xFFF]

ASM_SYM(asm_jit_STA_ABS_END):
  ret


.globl ASM_SYM(asm_jit_STX)
.globl ASM_SYM(asm_jit_STX_END)
ASM_SYM(asm_jit_STX):
  strb REG_6502_X_32, [REG_MEM_WRITE, REG_JIT_ADDR]

ASM_SYM(asm_jit_STX_END):
  ret


.globl ASM_SYM(asm_jit_STX_ABS)
.globl ASM_SYM(asm_jit_STX_ABS_END)
ASM_SYM(asm_jit_STX_ABS):
  strb REG_6502_X_32, [REG_MEM_WRITE, #0xFFF]

ASM_SYM(asm_jit_STX_ABS_END):
  ret


.globl ASM_SYM(asm_jit_STY)
.globl ASM_SYM(asm_jit_STY_END)
ASM_SYM(asm_jit_STY):
  strb REG_6502_Y_32, [REG_MEM_WRITE, REG_JIT_ADDR]

ASM_SYM(asm_jit_STY_END):
  ret


.globl ASM_SYM(asm_jit_STY_ABS)
.globl ASM_SYM(asm_jit_STY_ABS_END)
ASM_SYM(asm_jit_STY_ABS):
  strb REG_6502_Y_32, [REG_MEM_WRITE, #0xFFF]

ASM_SYM(asm_jit_STY_ABS_END):
  ret


.globl ASM_SYM(asm_jit_SUB)
.globl ASM_SYM(asm_jit_SUB_END)
ASM_SYM(asm_jit_SUB):
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_VALUE_32, lsl #24
  lsr REG_6502_A, REG_6502_A, #24

ASM_SYM(asm_jit_SUB_END):
  ret


.globl ASM_SYM(asm_jit_SUB_IMM)
.globl ASM_SYM(asm_jit_SUB_IMM_END)
ASM_SYM(asm_jit_SUB_IMM):
  lsl REG_6502_A, REG_6502_A, #24
  subs REG_6502_A_32, REG_6502_A_32, REG_VALUE_32
  lsr REG_6502_A, REG_6502_A, #24

ASM_SYM(asm_jit_SUB_IMM_END):
  ret
