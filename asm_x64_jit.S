#include "asm_x64_defs.h"

#define K_JIT_CONTEXT_OFFSET_JIT_CALLBACK  (40 + 24)

.file "asm_x64_jit.S"
.intel_syntax noprefix
.section rodata
.text


.globl asm_x64_jit_do_compile
asm_x64_jit_do_compile:

  mov REG_SCRATCH1, [rsp]

  lahf
  push rax
  push rcx
  push rsi
  push rdi

  # param1: already in rdi, it's the context object.
  # param2: x64 rip that called here.
  mov REG_PARAM2, REG_SCRATCH1

  call [REG_CONTEXT + K_JIT_CONTEXT_OFFSET_JIT_CALLBACK]

  mov REG_SCRATCH1, REG_RETURN

  pop rdi
  pop rsi
  pop rcx
  pop rax
  sahf

  # We're jumping out of a call so pop the return address.
  pop REG_SCRATCH2

  jmp REG_SCRATCH1
