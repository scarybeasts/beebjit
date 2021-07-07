#include "../asm_defs_host.h"
#include "../asm_jit_defs.h"
#include "asm_calling_convention_arm64.h"
#include "asm_defs_registers_arm64.h"

.file "asm/arm64/asm_jit_arm64.S"
.section rodata
.text


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
