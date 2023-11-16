#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "../asm_platform.h"
#include "asm_calling_convention_arm64.h"
#include "asm_defs_registers_arm64.h"

.file "asm/arm64/asm_inturbo_arm64.S"
.text
.p2align 2


.globl ASM_SYM(asm_inturbo_enter)
.globl ASM_SYM(asm_inturbo_enter_END)
ASM_SYM(asm_inturbo_enter):
  stp x29, x30, [sp, #-16]!
  bl ASM_SYM(asm_common_save_callee_regs)

  mov REG_MEM_READ, REG_PARAM4
  ldr REG_INTURBO_INTERP, [REG_PARAM1, #K_CONTEXT_OFFSET_INTERP_ASM]

  # We have to go through this dance due to a 64-bit truncation issue in the
  # ABI (which we could fix if we were less in a hurry).
  # This leaves a raw 6502 PC in the context.
  ldr REG_SCRATCH1, [REG_PARAM1, #K_CONTEXT_OFFSET_STATE_6502]
  ldr REG_SCRATCH2_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_PC]
  sub REG_SCRATCH2, REG_SCRATCH2, REG_MEM_READ
  and REG_SCRATCH2, REG_SCRATCH2, #0xFFFF
  str REG_SCRATCH2_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_PC]

  b ASM_SYM(asm_common_enter)

ASM_SYM(asm_inturbo_enter_END):
  ret


.globl ASM_SYM(asm_inturbo_start_countdown)
.globl ASM_SYM(asm_inturbo_start_countdown_END)
ASM_SYM(asm_inturbo_start_countdown):
  sub REG_INTURBO_SCRATCH3, REG_COUNTDOWN, #4095

ASM_SYM(asm_inturbo_start_countdown_END):
  ret


.globl ASM_SYM(asm_inturbo_check_special_address_movz)
.globl ASM_SYM(asm_inturbo_check_special_address_movz_END)
ASM_SYM(asm_inturbo_check_special_address_movz):
  movz REG_INTURBO_SCRATCH3, #0xFFFF

ASM_SYM(asm_inturbo_check_special_address_movz_END):
  ret


.globl ASM_SYM(asm_inturbo_check_special_address_sub_and_tbz)
.globl ASM_SYM(asm_inturbo_check_special_address_sub_and_tbz_END)
ASM_SYM(asm_inturbo_check_special_address_sub_and_tbz):
  sub REG_INTURBO_SCRATCH3, REG_SCRATCH1, REG_INTURBO_SCRATCH3
  tbz REG_INTURBO_SCRATCH3, \
      #16, \
      ASM_SYM(asm_inturbo_check_special_address_sub_and_tbz)

ASM_SYM(asm_inturbo_check_special_address_sub_and_tbz_END):
  ret


.globl ASM_SYM(asm_inturbo_check_and_commit_countdown_tbnz)
.globl ASM_SYM(asm_inturbo_check_and_commit_countdown_tbnz_END)
.globl ASM_SYM(asm_inturbo_check_and_commit_countdown_mov)
.globl ASM_SYM(asm_inturbo_check_and_commit_countdown_mov_END)
ASM_SYM(asm_inturbo_check_and_commit_countdown_tbnz):
  tbnz REG_INTURBO_SCRATCH3, \
       #63, \
       ASM_SYM(asm_inturbo_check_and_commit_countdown_tbnz)

ASM_SYM(asm_inturbo_check_and_commit_countdown_tbnz_END):
  ret

ASM_SYM(asm_inturbo_check_and_commit_countdown_mov):
  mov REG_COUNTDOWN, REG_INTURBO_SCRATCH3

ASM_SYM(asm_inturbo_check_and_commit_countdown_mov_END):
  ret


.globl ASM_SYM(asm_inturbo_commit_branch)
.globl ASM_SYM(asm_inturbo_commit_branch_END)
ASM_SYM(asm_inturbo_commit_branch):
  mov REG_6502_PC, REG_SCRATCH1

ASM_SYM(asm_inturbo_commit_branch_END):
  ret


.globl ASM_SYM(asm_inturbo_check_decimal_tbnz)
.globl ASM_SYM(asm_inturbo_check_decimal_tbnz_END)
ASM_SYM(asm_inturbo_check_decimal_tbnz):
  tbnz REG_6502_ID_F, #3, ASM_SYM(asm_inturbo_check_decimal_tbnz)

ASM_SYM(asm_inturbo_check_decimal_tbnz_END):
  ret


.globl ASM_SYM(asm_inturbo_check_interrupt_cbnz)
.globl ASM_SYM(asm_inturbo_check_interrupt_cbnz_END)
ASM_SYM(asm_inturbo_check_interrupt_cbnz):
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_STATE_6502]
  ldr REG_SCRATCH1_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_IRQ_FIRE]
  cbnz REG_SCRATCH1, ASM_SYM(asm_inturbo_check_interrupt_cbnz)

ASM_SYM(asm_inturbo_check_interrupt_cbnz_END):
  ret


.globl ASM_SYM(asm_inturbo_load_and_advance_pc)
.globl ASM_SYM(asm_inturbo_load_and_advance_pc_END)
ASM_SYM(asm_inturbo_load_and_advance_pc):
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #-0x100]!

ASM_SYM(asm_inturbo_load_and_advance_pc_END):
  ret


.globl ASM_SYM(asm_inturbo_advance_pc)
.globl ASM_SYM(asm_inturbo_advance_pc_END)
ASM_SYM(asm_inturbo_advance_pc):
  add REG_6502_PC, REG_6502_PC, #4095

ASM_SYM(asm_inturbo_advance_pc_END):
  ret


.globl ASM_SYM(asm_inturbo_ret)
.globl ASM_SYM(asm_inturbo_ret_END)
ASM_SYM(asm_inturbo_ret):
  ret

ASM_SYM(asm_inturbo_ret_END):
  ret


.globl ASM_SYM(asm_inturbo_do_write_invalidation)
.globl ASM_SYM(asm_inturbo_do_write_invalidation_END)
ASM_SYM(asm_inturbo_do_write_invalidation):
  ldr REG_SCRATCH2, [REG_CONTEXT]
  ldr REG_SCRATCH2_32, [REG_SCRATCH2, REG_SCRATCH1, lsl #2]
  orr REG_SCRATCH2, REG_SCRATCH2, #K_JIT_ADDR
  str REG_SCRATCH2_32, [REG_SCRATCH2]

ASM_SYM(asm_inturbo_do_write_invalidation_END):
  ret


.globl ASM_SYM(asm_inturbo_jump_next_opcode)
.globl ASM_SYM(asm_inturbo_jump_next_opcode_END)
ASM_SYM(asm_inturbo_jump_next_opcode):
  lsl REG_SCRATCH1, REG_SCRATCH1, #K_INTURBO_OPCODE_SHIFT
  orr REG_SCRATCH1, REG_SCRATCH1, #K_INTURBO_ADDR
  br REG_SCRATCH1

ASM_SYM(asm_inturbo_jump_next_opcode_END):
  ret


.globl ASM_SYM(asm_inturbo_call_debug)
.globl ASM_SYM(asm_inturbo_call_debug_END)
ASM_SYM(asm_inturbo_call_debug):
  sub REG_6502_PC, REG_6502_PC, REG_MEM_READ
  stp x29, x30, [sp, #-16]!

  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_DEBUG_ASM]
  blr REG_SCRATCH1

  ldp x29, x30, [sp], #16
  add REG_6502_PC, REG_6502_PC, REG_MEM_READ

ASM_SYM(asm_inturbo_call_debug_END):
  ret


.globl ASM_SYM(asm_inturbo_jump_interp)
.globl ASM_SYM(asm_inturbo_jump_interp_END)
ASM_SYM(asm_inturbo_jump_interp):
  br REG_INTURBO_INTERP

ASM_SYM(asm_inturbo_jump_interp_END):
  ret


.globl ASM_SYM(asm_inturbo_interp_trampoline)
.globl ASM_SYM(asm_inturbo_interp_trampoline_END)
ASM_SYM(asm_inturbo_interp_trampoline):
  bl ASM_SYM(asm_inturbo_do_interp_trampoline)
  ldrb REG_SCRATCH1_32, [REG_6502_PC]
  b ASM_SYM(asm_inturbo_jump_next_opcode)

ASM_SYM(asm_inturbo_interp_trampoline_END):
  ret


.globl ASM_SYM(asm_inturbo_interp_trampoline_ret)
.globl ASM_SYM(asm_inturbo_interp_trampoline_ret_END)
ASM_SYM(asm_inturbo_interp_trampoline_ret):
  stp x29, x30, [sp, #-16]!
  bl ASM_SYM(asm_inturbo_do_interp_trampoline)
  ldp x29, x30, [sp], #16
  ret

ASM_SYM(asm_inturbo_interp_trampoline_ret_END):
  ret


.globl ASM_SYM(asm_inturbo_do_interp_trampoline)
.globl ASM_SYM(asm_inturbo_do_interp_trampoline_END)
ASM_SYM(asm_inturbo_do_interp_trampoline):
  stp x29, x30, [sp, #-16]!
  sub REG_6502_PC, REG_6502_PC, REG_MEM_READ
  bl ASM_SYM(asm_save_AXYS_PC_flags)

  sub sp, sp, #16
  ldr REG_PARAM1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_OBJECT]
  mov REG_PARAM2, sp
  mov REG_PARAM3, REG_COUNTDOWN
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_INTERP_CALLBACK]
  blr REG_SCRATCH1
  ldr REG_COUNTDOWN, [sp, #0]
  ldr REG_SCRATCH1, [sp, #8]
  add sp, sp, #16

  tst REG_SCRATCH1, REG_SCRATCH1
  b.eq not_exiting

  mov REG_RETURN, REG_SCRATCH1
  ldp x29, x30, [sp], #16
  # We might be exiting inturbo mode, or exiting JIT mode within a call to
  # inturbo.
  # It's a layering violation, but for now handle those two cases here by
  # walking up the stack one more call and some data pops if we're in JIT mode.
  ldr REG_SCRATCH1, [REG_CONTEXT]
  tst REG_SCRATCH1, REG_SCRATCH1
  b.eq exiting_inturbo
  add sp, sp, #48
exiting_inturbo:
  b ASM_SYM(asm_common_exit)

ASM_SYM(asm_inturbo_do_interp_trampoline_END):
  ret

not_exiting:
  bl ASM_SYM(asm_restore_AXYS_PC_flags)
  add REG_6502_PC, REG_6502_PC, REG_MEM_READ
  ldp x29, x30, [sp], #16
  ret

not_exiting_END:
  ret


.globl ASM_SYM(asm_inturbo_check_page_crossing)
.globl ASM_SYM(asm_inturbo_check_page_crossing_END)
ASM_SYM(asm_inturbo_check_page_crossing):
  eor REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH1
  ubfx REG_SCRATCH2, REG_SCRATCH2, #8, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH2

ASM_SYM(asm_inturbo_check_page_crossing_END):
  ret


.globl ASM_SYM(asm_inturbo_add_1_to_scratch)
.globl ASM_SYM(asm_inturbo_add_1_to_scratch_END)
ASM_SYM(asm_inturbo_add_1_to_scratch):
  add REG_SCRATCH1, REG_SCRATCH1, #1

ASM_SYM(asm_inturbo_add_1_to_scratch_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_imm)
.globl ASM_SYM(asm_inturbo_mode_imm_END)
ASM_SYM(asm_inturbo_mode_imm):
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]

ASM_SYM(asm_inturbo_mode_imm_END):
  ret


# TODO: want to revisit this; not always needed to do the fetch.
.globl ASM_SYM(asm_inturbo_mode_rel)
.globl ASM_SYM(asm_inturbo_mode_rel_END)
ASM_SYM(asm_inturbo_mode_rel):
  ldrsb REG_SCRATCH2, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_6502_PC, #2
  add REG_SCRATCH2, REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_inturbo_mode_rel_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_zpg)
.globl ASM_SYM(asm_inturbo_mode_zpg_END)
ASM_SYM(asm_inturbo_mode_zpg):
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]

ASM_SYM(asm_inturbo_mode_zpg_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_abs)
.globl ASM_SYM(asm_inturbo_mode_abs_END)
ASM_SYM(asm_inturbo_mode_abs):
  ldrh REG_SCRATCH1_32, [REG_6502_PC, #1]

ASM_SYM(asm_inturbo_mode_abs_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_abx)
.globl ASM_SYM(asm_inturbo_mode_abx_END)
ASM_SYM(asm_inturbo_mode_abx):
  ldrh REG_SCRATCH2_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH2, REG_6502_X

ASM_SYM(asm_inturbo_mode_abx_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_aby)
.globl ASM_SYM(asm_inturbo_mode_aby_END)
ASM_SYM(asm_inturbo_mode_aby):
  ldrh REG_SCRATCH2_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH2, REG_6502_Y

ASM_SYM(asm_inturbo_mode_aby_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_zpx)
.globl ASM_SYM(asm_inturbo_mode_zpx_END)
ASM_SYM(asm_inturbo_mode_zpx):
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_X
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF

ASM_SYM(asm_inturbo_mode_zpx_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_zpy)
.globl ASM_SYM(asm_inturbo_mode_zpy_END)
ASM_SYM(asm_inturbo_mode_zpy):
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #1]
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_Y
  and REG_SCRATCH1, REG_SCRATCH1, #0xFF

ASM_SYM(asm_inturbo_mode_zpy_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_idx)
.globl ASM_SYM(asm_inturbo_mode_idx_END)
ASM_SYM(asm_inturbo_mode_idx):
  ldrb REG_SCRATCH2_32, [REG_6502_PC, #1]
  add REG_SCRATCH2, REG_SCRATCH2, REG_6502_X
  and REG_SCRATCH2, REG_SCRATCH2, #0xFF
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH2]
  add REG_SCRATCH2, REG_SCRATCH2, #1
  and REG_SCRATCH2, REG_SCRATCH2, #0xFF
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH2]
  add REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

ASM_SYM(asm_inturbo_mode_idx_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_idy)
.globl ASM_SYM(asm_inturbo_mode_idy_END)
ASM_SYM(asm_inturbo_mode_idy):
  ldrb REG_INTURBO_SCRATCH3_32, [REG_6502_PC, #1]
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_INTURBO_SCRATCH3]
  add REG_INTURBO_SCRATCH3_32, REG_INTURBO_SCRATCH3_32, #1
  and REG_INTURBO_SCRATCH3_32, REG_INTURBO_SCRATCH3_32, #0xFF
  ldrb REG_INTURBO_SCRATCH3_32, [REG_MEM_READ, REG_INTURBO_SCRATCH3]
  add REG_SCRATCH2, REG_SCRATCH2, REG_INTURBO_SCRATCH3, lsl #8
  add REG_SCRATCH1, REG_SCRATCH2, REG_6502_Y

ASM_SYM(asm_inturbo_mode_idy_END):
  ret


.globl ASM_SYM(asm_inturbo_mode_ind)
.globl ASM_SYM(asm_inturbo_mode_ind_END)
ASM_SYM(asm_inturbo_mode_ind):
  ldrh REG_SCRATCH1_32, [REG_6502_PC, #1]
  mov REG_SCRATCH2, REG_SCRATCH1
  and REG_SCRATCH2, REG_SCRATCH2, #0xFFFFFF00
  add REG_INTURBO_SCRATCH3, REG_SCRATCH1, #1
  and REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, #0xFF
  orr REG_SCRATCH2, REG_SCRATCH2, REG_INTURBO_SCRATCH3
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH2]
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

ASM_SYM(asm_inturbo_mode_ind_END):
  ret


.globl ASM_SYM(asm_inturbo_fetch_from_scratch)
.globl ASM_SYM(asm_inturbo_fetch_from_scratch_END)
ASM_SYM(asm_inturbo_fetch_from_scratch):
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]

ASM_SYM(asm_inturbo_fetch_from_scratch_END):
  ret


.globl ASM_SYM(asm_inturbo_JSR_load_pc_lo)
.globl ASM_SYM(asm_inturbo_JSR_load_pc_lo_END)
ASM_SYM(asm_inturbo_JSR_load_pc_lo):
  ldrb REG_INTURBO_SCRATCH3_32, [REG_6502_PC, #1]

ASM_SYM(asm_inturbo_JSR_load_pc_lo_END):
  ret


.globl ASM_SYM(asm_inturbo_JSR_load_pc_hi)
.globl ASM_SYM(asm_inturbo_JSR_load_pc_hi_END)
ASM_SYM(asm_inturbo_JSR_load_pc_hi):
  ldrb REG_SCRATCH1_32, [REG_6502_PC, #2]
  lsl REG_SCRATCH1, REG_SCRATCH1, #8
  orr REG_SCRATCH1, REG_SCRATCH1, REG_INTURBO_SCRATCH3

ASM_SYM(asm_inturbo_JSR_load_pc_hi_END):
  ret


.globl ASM_SYM(asm_inturbo_push_pc)
.globl ASM_SYM(asm_inturbo_push_pc_END)
ASM_SYM(asm_inturbo_push_pc):
  sub REG_SCRATCH1, REG_6502_PC, REG_MEM_READ
  add REG_SCRATCH1, REG_SCRATCH1, 2
  lsr REG_SCRATCH2, REG_SCRATCH1, #8
  strb REG_SCRATCH2_32, [REG_MEM_STACK, REG_6502_S]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF
  strb REG_SCRATCH1_32, [REG_MEM_STACK, REG_6502_S]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF

ASM_SYM(asm_inturbo_push_pc_END):
  ret


.globl ASM_SYM(asm_inturbo_interrupt_vector)
.globl ASM_SYM(asm_inturbo_interrupt_vector_END)
ASM_SYM(asm_inturbo_interrupt_vector):
  movz REG_SCRATCH1, #K_6502_VECTOR_IRQ
  ldrh REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  add REG_6502_PC, REG_MEM_READ, REG_SCRATCH1

ASM_SYM(asm_inturbo_interrupt_vector_END):
  ret


.globl ASM_SYM(asm_instruction_ADC_imm_interp)
.globl ASM_SYM(asm_instruction_ADC_imm_interp_END)
ASM_SYM(asm_instruction_ADC_imm_interp):
  add REG_SCRATCH1, REG_6502_CF, REG_SCRATCH1, lsl #56
  lsl REG_6502_A, REG_6502_A, #56
  orr REG_6502_A, REG_6502_A, #0x00FFFFFFFFFFFFFF
  adds REG_6502_A, REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A, #0xFF00000000000000
  lsr REG_6502_A, REG_6502_A, #56

ASM_SYM(asm_instruction_ADC_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_ALR_scratch_interp)
.globl ASM_SYM(asm_instruction_ALR_scratch_interp_END)
ASM_SYM(asm_instruction_ALR_scratch_interp):
  and REG_6502_A, REG_6502_A, REG_SCRATCH1
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

ASM_SYM(asm_instruction_ALR_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_AND_scratch_interp_common)
.globl ASM_SYM(asm_instruction_AND_scratch_interp_common_END)
ASM_SYM(asm_instruction_AND_scratch_interp_common):
  and REG_6502_A, REG_6502_A, REG_SCRATCH1

ASM_SYM(asm_instruction_AND_scratch_interp_common_END):
  ret


.globl ASM_SYM(asm_instruction_ASL_acc_interp)
.globl ASM_SYM(asm_instruction_ASL_acc_interp_END)
ASM_SYM(asm_instruction_ASL_acc_interp):
  ubfx REG_6502_CF, REG_6502_A, #7, #1
  lsl REG_6502_A, REG_6502_A, #1
  and REG_6502_A, REG_6502_A, #0xFF

ASM_SYM(asm_instruction_ASL_acc_interp_END):
  ret


.globl ASM_SYM(asm_instruction_ASL_scratch_interp)
.globl ASM_SYM(asm_instruction_ASL_scratch_interp_END)
ASM_SYM(asm_instruction_ASL_scratch_interp):
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  ubfx REG_6502_CF, REG_SCRATCH2, #8, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_ASL_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BCC_interp)
.globl ASM_SYM(asm_instruction_BCC_interp_END)
ASM_SYM(asm_instruction_BCC_interp):
  tbnz REG_6502_CF, #0, ASM_SYM(asm_instruction_BCC_interp_END)
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BCC_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BCC_interp_accurate)
.globl ASM_SYM(asm_instruction_BCC_interp_accurate_END)
ASM_SYM(asm_instruction_BCC_interp_accurate):
  tbnz REG_6502_CF, #0, ASM_SYM(asm_instruction_BCC_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BCC_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_BCS_interp)
.globl ASM_SYM(asm_instruction_BCS_interp_END)
ASM_SYM(asm_instruction_BCS_interp):
  tbz REG_6502_CF, #0, ASM_SYM(asm_instruction_BCS_interp_END)
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BCS_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BCS_interp_accurate)
.globl ASM_SYM(asm_instruction_BCS_interp_accurate_END)
ASM_SYM(asm_instruction_BCS_interp_accurate):
  tbz REG_6502_CF, #0, ASM_SYM(asm_instruction_BCS_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BCS_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_BEQ_interp)
.globl ASM_SYM(asm_instruction_BEQ_interp_END)
ASM_SYM(asm_instruction_BEQ_interp):
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, eq

ASM_SYM(asm_instruction_BEQ_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BEQ_interp_accurate)
.globl ASM_SYM(asm_instruction_BEQ_interp_accurate_END)
ASM_SYM(asm_instruction_BEQ_interp_accurate):
  b.ne ASM_SYM(asm_instruction_BEQ_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BEQ_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_BIT_interp)
.globl ASM_SYM(asm_instruction_BIT_interp_END)
ASM_SYM(asm_instruction_BIT_interp):
  ldrb REG_VALUE_32, [REG_MEM_READ, REG_SCRATCH1]

ASM_SYM(asm_instruction_BIT_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BMI_interp)
.globl ASM_SYM(asm_instruction_BMI_interp_END)
ASM_SYM(asm_instruction_BMI_interp):
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, mi

ASM_SYM(asm_instruction_BMI_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BMI_interp_accurate)
.globl ASM_SYM(asm_instruction_BMI_interp_accurate_END)
ASM_SYM(asm_instruction_BMI_interp_accurate):
  b.pl ASM_SYM(asm_instruction_BMI_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BMI_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_BNE_interp)
.globl ASM_SYM(asm_instruction_BNE_interp_END)
ASM_SYM(asm_instruction_BNE_interp):
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, ne

ASM_SYM(asm_instruction_BNE_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BNE_interp_accurate)
.globl ASM_SYM(asm_instruction_BNE_interp_accurate_END)
ASM_SYM(asm_instruction_BNE_interp_accurate):
  b.eq ASM_SYM(asm_instruction_BNE_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BNE_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_BPL_interp)
.globl ASM_SYM(asm_instruction_BPL_interp_END)
ASM_SYM(asm_instruction_BPL_interp):
  csel REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, pl

ASM_SYM(asm_instruction_BPL_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BPL_interp_accurate)
.globl ASM_SYM(asm_instruction_BPL_interp_accurate_END)
ASM_SYM(asm_instruction_BPL_interp_accurate):
  b.mi ASM_SYM(asm_instruction_BPL_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BPL_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_BVC_interp)
.globl ASM_SYM(asm_instruction_BVC_interp_END)
ASM_SYM(asm_instruction_BVC_interp):
  tbnz REG_6502_OF, #0, ASM_SYM(asm_instruction_BVC_interp_END)
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BVC_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BVC_interp_accurate)
.globl ASM_SYM(asm_instruction_BVC_interp_accurate_END)
ASM_SYM(asm_instruction_BVC_interp_accurate):
  tbnz REG_6502_OF, #0, ASM_SYM(asm_instruction_BVC_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BVC_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_BVS_interp)
.globl ASM_SYM(asm_instruction_BVS_interp_END)
ASM_SYM(asm_instruction_BVS_interp):
  tbz REG_6502_OF, #0, ASM_SYM(asm_instruction_BVS_interp_END)
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BVS_interp_END):
  ret


.globl ASM_SYM(asm_instruction_BVS_interp_accurate)
.globl ASM_SYM(asm_instruction_BVS_interp_accurate_END)
ASM_SYM(asm_instruction_BVS_interp_accurate):
  tbz REG_6502_OF, #0, ASM_SYM(asm_instruction_BVS_interp_accurate_END)
  eor REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  ubfx REG_SCRATCH1, REG_SCRATCH1, #8, #1
  add REG_SCRATCH1, REG_SCRATCH1, #1
  sub REG_INTURBO_SCRATCH3, REG_INTURBO_SCRATCH3, REG_SCRATCH1
  mov REG_SCRATCH1, REG_SCRATCH2

ASM_SYM(asm_instruction_BVS_interp_accurate_END):
  ret


.globl ASM_SYM(asm_instruction_CMP_imm_interp)
.globl ASM_SYM(asm_instruction_CMP_imm_interp_END)
ASM_SYM(asm_instruction_CMP_imm_interp):
  subs REG_SCRATCH1, REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH1, lsl #56

ASM_SYM(asm_instruction_CMP_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_CMP_scratch_interp)
.globl ASM_SYM(asm_instruction_CMP_scratch_interp_END)
ASM_SYM(asm_instruction_CMP_scratch_interp):
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  subs REG_SCRATCH1, REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH1, lsl #56

ASM_SYM(asm_instruction_CMP_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_CPX_imm_interp)
.globl ASM_SYM(asm_instruction_CPX_imm_interp_END)
ASM_SYM(asm_instruction_CPX_imm_interp):
  subs REG_SCRATCH1, REG_6502_X, REG_SCRATCH1
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH1, lsl #56

ASM_SYM(asm_instruction_CPX_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_CPX_scratch_interp)
.globl ASM_SYM(asm_instruction_CPX_scratch_interp_END)
ASM_SYM(asm_instruction_CPX_scratch_interp):
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  subs REG_SCRATCH1, REG_6502_X, REG_SCRATCH1
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH1, lsl #56

ASM_SYM(asm_instruction_CPX_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_CPY_imm_interp)
.globl ASM_SYM(asm_instruction_CPY_imm_interp_END)
ASM_SYM(asm_instruction_CPY_imm_interp):
  subs REG_SCRATCH1, REG_6502_Y, REG_SCRATCH1
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH1, lsl #56

ASM_SYM(asm_instruction_CPY_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_CPY_scratch_interp)
.globl ASM_SYM(asm_instruction_CPY_scratch_interp_END)
ASM_SYM(asm_instruction_CPY_scratch_interp):
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, REG_SCRATCH1]
  subs REG_SCRATCH1, REG_6502_Y, REG_SCRATCH1
  cset REG_6502_CF, cs
  adds xzr, xzr, REG_SCRATCH1, lsl #56

ASM_SYM(asm_instruction_CPY_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_DEC_scratch_interp)
.globl ASM_SYM(asm_instruction_DEC_scratch_interp_END)
ASM_SYM(asm_instruction_DEC_scratch_interp):
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  sub REG_SCRATCH2, REG_SCRATCH2, #1
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]
  adds xzr, xzr, REG_SCRATCH2, lsl #56

ASM_SYM(asm_instruction_DEC_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_EOR_scratch_interp_common)
.globl ASM_SYM(asm_instruction_EOR_scratch_interp_common_END)
ASM_SYM(asm_instruction_EOR_scratch_interp_common):
  eor REG_6502_A, REG_6502_A, REG_SCRATCH1

ASM_SYM(asm_instruction_EOR_scratch_interp_common_END):
  ret


.globl ASM_SYM(asm_instruction_INC_scratch_interp)
.globl ASM_SYM(asm_instruction_INC_scratch_interp_END)
ASM_SYM(asm_instruction_INC_scratch_interp):
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  add REG_SCRATCH2, REG_SCRATCH2, #1
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]
  adds xzr, xzr, REG_SCRATCH2, lsl #56

ASM_SYM(asm_instruction_INC_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_JMP_scratch_interp)
.globl ASM_SYM(asm_instruction_JMP_scratch_interp_END)
ASM_SYM(asm_instruction_JMP_scratch_interp):
  add REG_6502_PC, REG_MEM_READ, REG_SCRATCH1

ASM_SYM(asm_instruction_JMP_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LDA_imm_interp)
.globl ASM_SYM(asm_instruction_LDA_imm_interp_END)
ASM_SYM(asm_instruction_LDA_imm_interp):
  ldrb REG_6502_A_32, [REG_6502_PC, #1]

ASM_SYM(asm_instruction_LDA_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LDA_scratch_interp)
.globl ASM_SYM(asm_instruction_LDA_scratch_interp_END)
ASM_SYM(asm_instruction_LDA_scratch_interp):
  ldrb REG_6502_A_32, [REG_MEM_READ, REG_SCRATCH1]

ASM_SYM(asm_instruction_LDA_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LDX_imm_interp)
.globl ASM_SYM(asm_instruction_LDX_imm_interp_END)
ASM_SYM(asm_instruction_LDX_imm_interp):
  ldrb REG_6502_X_32, [REG_6502_PC, #1]

ASM_SYM(asm_instruction_LDX_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LDX_scratch_interp)
.globl ASM_SYM(asm_instruction_LDX_scratch_interp_END)
ASM_SYM(asm_instruction_LDX_scratch_interp):
  ldrb REG_6502_X_32, [REG_MEM_READ, REG_SCRATCH1]

ASM_SYM(asm_instruction_LDX_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LDY_imm_interp)
.globl ASM_SYM(asm_instruction_LDY_imm_interp_END)
ASM_SYM(asm_instruction_LDY_imm_interp):
  ldrb REG_6502_Y_32, [REG_6502_PC, #1]

ASM_SYM(asm_instruction_LDY_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LDY_scratch_interp)
.globl ASM_SYM(asm_instruction_LDY_scratch_interp_END)
ASM_SYM(asm_instruction_LDY_scratch_interp):
  ldrb REG_6502_Y_32, [REG_MEM_READ, REG_SCRATCH1]

ASM_SYM(asm_instruction_LDY_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LSR_acc_interp)
.globl ASM_SYM(asm_instruction_LSR_acc_interp_END)
ASM_SYM(asm_instruction_LSR_acc_interp):
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_6502_A, #1

ASM_SYM(asm_instruction_LSR_acc_interp_END):
  ret


.globl ASM_SYM(asm_instruction_LSR_scratch_interp)
.globl ASM_SYM(asm_instruction_LSR_scratch_interp_END)
ASM_SYM(asm_instruction_LSR_scratch_interp):
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH2, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_LSR_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_ORA_scratch_interp_common)
.globl ASM_SYM(asm_instruction_ORA_scratch_interp_common_END)
ASM_SYM(asm_instruction_ORA_scratch_interp_common):
  orr REG_6502_A, REG_6502_A, REG_SCRATCH1

ASM_SYM(asm_instruction_ORA_scratch_interp_common_END):
  ret


.globl ASM_SYM(asm_instruction_ROL_acc_interp)
.globl ASM_SYM(asm_instruction_ROL_acc_interp_END)
ASM_SYM(asm_instruction_ROL_acc_interp):
  add REG_SCRATCH1, REG_6502_CF, REG_6502_A, lsl #1
  ubfx REG_6502_CF, REG_6502_A, #7, #1
  and REG_6502_A, REG_SCRATCH1, #0xFF

ASM_SYM(asm_instruction_ROL_acc_interp_END):
  ret


.globl ASM_SYM(asm_instruction_ROL_scratch_interp)
.globl ASM_SYM(asm_instruction_ROL_scratch_interp_END)
ASM_SYM(asm_instruction_ROL_scratch_interp):
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  add REG_SCRATCH2, REG_6502_CF, REG_SCRATCH2, lsl #1
  ubfx REG_6502_CF, REG_SCRATCH2, #8, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_ROL_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_ROR_acc_interp)
.globl ASM_SYM(asm_instruction_ROR_acc_interp_END)
ASM_SYM(asm_instruction_ROR_acc_interp):
  orr REG_SCRATCH1, REG_6502_A, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_6502_A, #0, #1
  lsr REG_6502_A, REG_SCRATCH1, #1

ASM_SYM(asm_instruction_ROR_acc_interp_END):
  ret


.globl ASM_SYM(asm_instruction_ROR_scratch_interp)
.globl ASM_SYM(asm_instruction_ROR_scratch_interp_END)
ASM_SYM(asm_instruction_ROR_scratch_interp):
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  orr REG_SCRATCH2, REG_SCRATCH2, REG_6502_CF, lsl #8
  ubfx REG_6502_CF, REG_SCRATCH2, #0, #1
  lsr REG_SCRATCH2, REG_SCRATCH2, #1
  adds xzr, xzr, REG_SCRATCH2, lsl #56
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_ROR_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_SAX_scratch_interp)
.globl ASM_SYM(asm_instruction_SAX_scratch_interp_END)
ASM_SYM(asm_instruction_SAX_scratch_interp):
  and REG_SCRATCH2, REG_6502_A, REG_6502_X
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_SAX_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_SBC_imm_interp)
.globl ASM_SYM(asm_instruction_SBC_imm_interp_END)
ASM_SYM(asm_instruction_SBC_imm_interp):
  eor REG_SCRATCH2, REG_6502_CF, #1
  add REG_SCRATCH1, REG_SCRATCH2, REG_SCRATCH1, lsl #56
  lsl REG_6502_A, REG_6502_A, #56
  subs REG_6502_A, REG_6502_A, REG_SCRATCH1
  cset REG_6502_CF, cs
  cset REG_6502_OF, vs
  tst REG_6502_A, #0xFF00000000000000
  lsr REG_6502_A, REG_6502_A, #56

ASM_SYM(asm_instruction_SBC_imm_interp_END):
  ret


.globl ASM_SYM(asm_instruction_SLO_scratch_interp)
.globl ASM_SYM(asm_instruction_SLO_scratch_interp_END)
ASM_SYM(asm_instruction_SLO_scratch_interp):
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, REG_SCRATCH1]
  ubfx REG_6502_CF, REG_SCRATCH2, #7, #1
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  strb REG_SCRATCH2_32, [REG_MEM_WRITE, REG_SCRATCH1]
  orr REG_6502_A, REG_6502_A, REG_SCRATCH2
  and REG_6502_A, REG_6502_A, #0xFF

ASM_SYM(asm_instruction_SLO_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_STA_scratch_interp)
.globl ASM_SYM(asm_instruction_STA_scratch_interp_END)
ASM_SYM(asm_instruction_STA_scratch_interp):
  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_STA_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_STX_scratch_interp)
.globl ASM_SYM(asm_instruction_STX_scratch_interp_END)
ASM_SYM(asm_instruction_STX_scratch_interp):
  strb REG_6502_X_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_STX_scratch_interp_END):
  ret


.globl ASM_SYM(asm_instruction_STY_scratch_interp)
.globl ASM_SYM(asm_instruction_STY_scratch_interp_END)
ASM_SYM(asm_instruction_STY_scratch_interp):
  strb REG_6502_Y_32, [REG_MEM_WRITE, REG_SCRATCH1]

ASM_SYM(asm_instruction_STY_scratch_interp_END):
  ret
