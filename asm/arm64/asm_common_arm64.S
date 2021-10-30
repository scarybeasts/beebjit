#include "asm_defs_registers_arm64.h"

#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "asm_calling_convention_arm64.h"

.file "asm/arm64/asm_common_arm64.S"
.section rodata
.text


.globl asm_enter_common
.globl asm_enter_common_END
asm_enter_common:
  stp x29, x30, [sp, #-16]!

  # param1, x0 == context
  # param2, x1 == x64 start address
  # param3, x2 == countdown
  # param4, x3 == mem base

  # Save callee save registers.
  stp x27, x28, [sp, #-16]!
  stp x25, x26, [sp, #-16]!
  stp x23, x24, [sp, #-16]!
  stp x21, x22, [sp, #-16]!

  mov REG_CONTEXT, REG_PARAM1
  # Temporary storage for the final jump location.
  mov REG_MEM_WRITE, REG_PARAM2
  mov REG_COUNTDOWN, REG_PARAM3
  # REG_MEM_READ, from REG_PARAM4, is set up by JIT or inturbo entry.

  bl asm_restore_AXYS_PC_flags

  # More juggling of final jump location.
  mov REG_SCRATCH2, REG_MEM_WRITE

  movz REG_SCRATCH1, #(K_BBC_MEM_OFFSET_READ_TO_WRITE >> 16), lsl #16
  movk REG_SCRATCH1, #(K_BBC_MEM_OFFSET_READ_TO_WRITE & 0xFFFF)
  add REG_MEM_WRITE, REG_MEM_READ, REG_SCRATCH1
  add REG_MEM_STACK, REG_MEM_READ, #0x100

  br REG_SCRATCH2

asm_enter_common_END:
  ret


.globl asm_instruction_BIT_value
.globl asm_instruction_BIT_value_END
asm_instruction_BIT_value:

  tst REG_6502_A, REG_SCRATCH2
  # O flag
  ubfx REG_6502_OF, REG_SCRATCH2, #6, #1
  # N flag
  ubfx REG_SCRATCH1, REG_SCRATCH2, #7, #1
  # Z flag
  cset REG_SCRATCH2, eq
  lsl REG_SCRATCH2, REG_SCRATCH2, #30
  lsl REG_SCRATCH1, REG_SCRATCH1, #31
  add REG_SCRATCH2, REG_SCRATCH2, REG_SCRATCH1
  msr nzcv, REG_SCRATCH2

asm_instruction_BIT_value_END:
  ret


.globl asm_instruction_CLC
.globl asm_instruction_CLC_END
asm_instruction_CLC:

  movz REG_6502_CF, #0

asm_instruction_CLC_END:
  ret


.globl asm_instruction_CLD
.globl asm_instruction_CLD_END
asm_instruction_CLD:

  bic REG_6502_ID_F, REG_6502_ID_F, #0x08

asm_instruction_CLD_END:
  ret


.globl asm_instruction_CLI
.globl asm_instruction_CLI_END
asm_instruction_CLI:

  bic REG_6502_ID_F, REG_6502_ID_F, #0x04

asm_instruction_CLI_END:
  ret


.globl asm_instruction_CLV
.globl asm_instruction_CLV_END
asm_instruction_CLV:

  movz REG_6502_OF, #0

asm_instruction_CLV_END:
  ret


.globl asm_instruction_DEX
.globl asm_instruction_DEX_END
asm_instruction_DEX:

  sub REG_6502_X, REG_6502_X, #1
  adds xzr, xzr, REG_6502_X, lsl #56
  and REG_6502_X, REG_6502_X, #0xFF

asm_instruction_DEX_END:
  ret


.globl asm_instruction_DEY
.globl asm_instruction_DEY_END
asm_instruction_DEY:

  sub REG_6502_Y, REG_6502_Y, #1
  adds xzr, xzr, REG_6502_Y, lsl #56
  and REG_6502_Y, REG_6502_Y, #0xFF

asm_instruction_DEY_END:
  ret


.globl asm_instruction_INX
.globl asm_instruction_INX_END
asm_instruction_INX:

  add REG_6502_X, REG_6502_X, #1
  adds xzr, xzr, REG_6502_X, lsl #56
  and REG_6502_X, REG_6502_X, #0xFF

asm_instruction_INX_END:
  ret


.globl asm_instruction_INY
.globl asm_instruction_INY_END
asm_instruction_INY:

  add REG_6502_Y, REG_6502_Y, #1
  adds xzr, xzr, REG_6502_Y, lsl #56
  and REG_6502_Y, REG_6502_Y, #0xFF

asm_instruction_INY_END:
  ret


.globl asm_instruction_NOP
.globl asm_instruction_NOP_END
asm_instruction_NOP:

  nop

asm_instruction_NOP_END:
  ret


.globl asm_instruction_PHA
.globl asm_instruction_PHA_END
asm_instruction_PHA:

  strb REG_6502_A_32, [REG_MEM_STACK, REG_6502_S]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF

asm_instruction_PHA_END:
  ret


.globl asm_instruction_PLA
.globl asm_instruction_PLA_END
asm_instruction_PLA:

  add REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF
  ldrb REG_6502_A_32, [REG_MEM_STACK, REG_6502_S]

asm_instruction_PLA_END:
  ret


.globl asm_instruction_SEC
.globl asm_instruction_SEC_END
asm_instruction_SEC:

  movz REG_6502_CF, #1

asm_instruction_SEC_END:
  ret


.globl asm_instruction_SED
.globl asm_instruction_SED_END
asm_instruction_SED:

  orr REG_6502_ID_F, REG_6502_ID_F, #0x08

asm_instruction_SED_END:
  ret


.globl asm_instruction_SEI
.globl asm_instruction_SEI_END
asm_instruction_SEI:

  orr REG_6502_ID_F, REG_6502_ID_F, #0x04

asm_instruction_SEI_END:
  ret


.globl asm_instruction_TAX
.globl asm_instruction_TAX_END
asm_instruction_TAX:

  mov REG_6502_X, REG_6502_A

asm_instruction_TAX_END:
  ret


.globl asm_instruction_TAY
.globl asm_instruction_TAY_END
asm_instruction_TAY:

  mov REG_6502_Y, REG_6502_A

asm_instruction_TAY_END:
  ret


.globl asm_instruction_TSX
.globl asm_instruction_TSX_END
asm_instruction_TSX:

  mov REG_6502_X, REG_6502_S

asm_instruction_TSX_END:
  ret


.globl asm_instruction_TXA
.globl asm_instruction_TXA_END
asm_instruction_TXA:

  mov REG_6502_A, REG_6502_X

asm_instruction_TXA_END:
  ret


.globl asm_instruction_TXS
.globl asm_instruction_TXS_END
asm_instruction_TXS:

  mov REG_6502_S, REG_6502_X

asm_instruction_TXS_END:
  ret


.globl asm_instruction_TYA
.globl asm_instruction_TYA_END
asm_instruction_TYA:

  mov REG_6502_A, REG_6502_Y

asm_instruction_TYA_END:
  ret


.globl asm_instruction_A_NZ_flags
.globl asm_instruction_A_NZ_flags_END
asm_instruction_A_NZ_flags:

  adds xzr, xzr, REG_6502_A, lsl #56

asm_instruction_A_NZ_flags_END:
  ret


.globl asm_instruction_X_NZ_flags
.globl asm_instruction_X_NZ_flags_END
asm_instruction_X_NZ_flags:

  adds xzr, xzr, REG_6502_X, lsl #56

asm_instruction_X_NZ_flags_END:
  ret


.globl asm_instruction_Y_NZ_flags
.globl asm_instruction_Y_NZ_flags_END
asm_instruction_Y_NZ_flags:

  adds xzr, xzr, REG_6502_Y, lsl #56

asm_instruction_Y_NZ_flags_END:
  ret


.globl asm_save_AXYS_PC_flags
.globl asm_save_AXYS_PC_flags_END
asm_save_AXYS_PC_flags:
  # x29 is currently REG_JIT_COMPILE, which is safe to use temporarily.
  stp x29, x30, [sp, #-16]!
  ldr x29, [REG_CONTEXT, #K_CONTEXT_OFFSET_STATE_6502]
  str REG_6502_A_32, [x29, #K_STATE_6502_OFFSET_REG_A]
  str REG_6502_X_32, [x29, #K_STATE_6502_OFFSET_REG_X]
  str REG_6502_Y_32, [x29, #K_STATE_6502_OFFSET_REG_Y]
  str REG_6502_S_32, [x29, #K_STATE_6502_OFFSET_REG_S]
  str REG_6502_PC_32, [x29, #K_STATE_6502_OFFSET_REG_PC]

  bl asm_emit_arm64_flags_to_scratch
  str REG_SCRATCH1_32, [x29, #K_STATE_6502_OFFSET_REG_FLAGS]

  ldp x29, x30, [sp], #16
  ret

asm_save_AXYS_PC_flags_END:
  ret


.globl asm_restore_AXYS_PC_flags
.globl asm_restore_AXYS_PC_flags_END
asm_restore_AXYS_PC_flags:
  stp x29, x30, [sp, #-16]!
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_STATE_6502]
  ldr REG_6502_A_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_A]
  ldr REG_6502_X_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_X]
  ldr REG_6502_Y_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_Y]
  ldr REG_6502_S_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_S]
  ldr REG_6502_PC_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_PC]

  ldr REG_SCRATCH1_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_FLAGS]
  bl asm_set_arm64_flags_from_scratch

  ldp x29, x30, [sp], #16
  ret

asm_restore_AXYS_PC_flags_END:
  ret


.globl asm_emit_arm64_flags_to_scratch
.globl asm_emit_arm64_flags_to_scratch_END
asm_emit_arm64_flags_to_scratch:
  orr REG_SCRATCH1, REG_6502_ID_F, #0x20

  cset REG_SCRATCH2, eq
  lsl REG_SCRATCH2, REG_SCRATCH2, #1
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  cset REG_SCRATCH2, mi
  lsl REG_SCRATCH2, REG_SCRATCH2, #7
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2

  lsl REG_SCRATCH2, REG_6502_OF, #6
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2
  orr REG_SCRATCH1, REG_SCRATCH1, REG_6502_CF

asm_emit_arm64_flags_to_scratch_END:
  ret


.globl asm_set_arm64_flags_from_scratch
.globl asm_set_arm64_flags_from_scratch_END
asm_set_arm64_flags_from_scratch:
  and REG_6502_ID_F, REG_SCRATCH1, 0x0C

  # Use REG_6502_CF and REG_6502_OF as temporaries until they are set below.
  # N flag.
  ubfx REG_6502_CF, REG_SCRATCH1, #7, #1
  lsl REG_6502_CF, REG_6502_CF, #31
  # Z flag.
  ubfx REG_6502_OF, REG_SCRATCH1, #1, #1
  lsl REG_6502_OF, REG_6502_OF, #30
  add REG_6502_CF, REG_6502_CF, REG_6502_OF
  msr nzcv, REG_6502_CF

  ubfx REG_6502_CF, REG_SCRATCH1, #0, #1
  ubfx REG_6502_OF, REG_SCRATCH1, #6, #1

asm_set_arm64_flags_from_scratch_END:
  ret


.globl asm_set_brk_flag_in_scratch
.globl asm_set_brk_flag_in_scratch_END
asm_set_brk_flag_in_scratch:

  orr REG_SCRATCH1, REG_SCRATCH1, #0x10

asm_set_brk_flag_in_scratch_END:
  ret


.globl asm_push_from_scratch
.globl asm_push_from_scratch_END
asm_push_from_scratch:

  strb REG_SCRATCH1_32, [REG_MEM_STACK, REG_6502_S]
  sub REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF

asm_push_from_scratch_END:
  ret


.globl asm_pull_to_scratch
.globl asm_pull_to_scratch_END
asm_pull_to_scratch:

  add REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF
  ldrb REG_SCRATCH1_32, [REG_MEM_STACK, REG_6502_S]

asm_pull_to_scratch_END:
  ret


.globl asm_pull_word_to_scratch
.globl asm_pull_word_to_scratch_END
asm_pull_word_to_scratch:

  add REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF
  ldrb REG_SCRATCH2_32, [REG_MEM_STACK, REG_6502_S]
  add REG_6502_S, REG_6502_S, #1
  and REG_6502_S, REG_6502_S, #0xFF
  ldrb REG_SCRATCH1_32, [REG_MEM_STACK, REG_6502_S]
  lsl REG_SCRATCH1, REG_SCRATCH1, #8
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2

asm_pull_word_to_scratch_END:
  ret


.globl asm_exit
asm_exit:
  # Restore callee save registers.
  ldp x21, x22, [sp], #16
  ldp x23, x24, [sp], #16
  ldp x25, x26, [sp], #16
  ldp x27, x28, [sp], #16
  # Restore stack frame.
  ldp x29, x30, [sp], #16
  ret

asm_exit_END:
  ret


.globl asm_debug
.globl asm_debug_END
asm_debug:
  stp x29, x30, [sp, #-16]!
  # Save the host flags because carry / overflow save elimination can leave
  # state there.
  mrs x29, nzcv

  bl asm_save_AXYS_PC_flags

  mov REG_PARAM1, REG_CONTEXT
  movz REG_PARAM2, #0
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_DEBUG_CALLBACK]
  blr REG_SCRATCH1

  bl asm_restore_AXYS_PC_flags

  # TODO: handle different 6502 PC!
  msr nzcv, x29
  ldp x29, x30, [sp], #16
  ret

asm_debug_end:
  ret
