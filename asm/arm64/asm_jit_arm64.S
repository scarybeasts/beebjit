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

  sub REG_SCRATCH1, REG_6502_PC, REG_MEM_READ
  lsl REG_SCRATCH1, REG_SCRATCH1, #K_BBC_JIT_BYTES_SHIFT
  orr REG_SCRATCH1, REG_SCRATCH1, #K_BBC_JIT_ADDR
  br REG_SCRATCH1
