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
  adr REG_JIT_COMPILE, asm_jit_compile_trampoline
  adr REG_INTERP_FUNC, asm_jit_call_interp

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


.globl asm_jit_BEQ
.globl asm_jit_BEQ_END
asm_jit_BEQ:
  b.eq asm_jit_BEQ

asm_jit_BEQ_END:
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


.globl asm_jit_CMP_SCRATCH
.globl asm_jit_CMP_SCRATCH_END
asm_jit_CMP_SCRATCH:
  cmp REG_6502_A, REG_SCRATCH1

asm_jit_CMP_SCRATCH_END:
  ret


.globl asm_jit_INC_ABS_12bit_load
.globl asm_jit_INC_ABS_12bit_load_END
.globl asm_jit_INC_ABS_12bit_inc
.globl asm_jit_INC_ABS_12bit_inc_END
.globl asm_jit_INC_ABS_12bit_store
.globl asm_jit_INC_ABS_12bit_store_END
asm_jit_INC_ABS_12bit_load:
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, #0xFFF]
asm_jit_INC_ABS_12bit_load_END:
asm_jit_INC_ABS_12bit_inc:
  add REG_SCRATCH1, REG_SCRATCH1, #1
  adds xzr, xzr, REG_SCRATCH1, lsl #56
asm_jit_INC_ABS_12bit_inc_END:
asm_jit_INC_ABS_12bit_store:
  strb REG_SCRATCH1_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_INC_ABS_12bit_store_END:
  ret


.globl asm_jit_JMP
.globl asm_jit_JMP_END
asm_jit_JMP:
  b asm_jit_JMP

asm_jit_JMP_END:
  ret


.globl asm_jit_LDA_IMM
.globl asm_jit_LDA_IMM_END
asm_jit_LDA_IMM:
  movz REG_6502_A, #0xFFFF

asm_jit_LDA_IMM_END:
  ret


.globl asm_jit_LDX_IMM
.globl asm_jit_LDX_IMM_END
asm_jit_LDX_IMM:
  movz REG_6502_X, #0xFFFF

asm_jit_LDX_IMM_END:
  ret


.globl asm_jit_MODE_IND_8_load_lo
.globl asm_jit_MODE_IND_8_load_lo_END
.globl asm_jit_MODE_IND_8_load_hi
.globl asm_jit_MODE_IND_8_load_hi_END
.globl asm_jit_MODE_IND_8_or
.globl asm_jit_MODE_IND_8_or_END
asm_jit_MODE_IND_8_load_lo:
  ldrb REG_SCRATCH1_32, [REG_MEM_READ, #4095]
asm_jit_MODE_IND_8_load_lo_END:
asm_jit_MODE_IND_8_load_hi:
  ldrb REG_SCRATCH2_32, [REG_MEM_READ, #4095]
asm_jit_MODE_IND_8_load_hi_END:
asm_jit_MODE_IND_8_or:
  orr REG_SCRATCH1, REG_SCRATCH1, REG_SCRATCH2, lsl #8

asm_jit_MODE_IND_8_or_END:
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


.globl asm_jit_STA_SCRATCH_Y
.globl asm_jit_STA_SCRATCH_Y_END
asm_jit_STA_SCRATCH_Y:
  add REG_SCRATCH1, REG_SCRATCH1, REG_6502_Y
  strb REG_6502_A_32, [REG_MEM_WRITE, REG_SCRATCH1]

asm_jit_STA_SCRATCH_Y_END:
  ret


.globl asm_jit_STX_ABS_12bit
.globl asm_jit_STX_ABS_12bit_END
asm_jit_STX_ABS_12bit:
  strb REG_6502_X_32, [REG_MEM_WRITE, #0xFFF]

asm_jit_STX_ABS_12bit_END:
  ret
