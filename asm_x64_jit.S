#include "asm_x64_defs.h"

#define K_BBC_JIT_ADDR                     0x20000000
#define K_JIT_CONTEXT_OFFSET_JIT_CALLBACK  (K_CONTEXT_OFFSET_DRIVER_END + 0)

.file "asm_x64_jit.S"
.intel_syntax noprefix
.section rodata
.text


.globl asm_x64_jit_call_compile_trampoline
.globl asm_x64_jit_call_compile_trampoline_END
asm_x64_jit_call_compile_trampoline:
  call [REG_CONTEXT]

asm_x64_jit_call_compile_trampoline_END:
  ret


.globl asm_x64_jit_compile_trampoline
asm_x64_jit_compile_trampoline:

  # We got here with a raw call instruction so the calling rip is in [rsp].
  mov REG_SCRATCH1, [rsp]

  lahf
  push rax
  push rcx
  push rsi
  push rdi

  # param1: context object.
  mov REG_PARAM1, REG_CONTEXT
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


.globl asm_x64_jit_FLAGA
.globl asm_x64_jit_FLAGA_END
asm_x64_jit_FLAGA:
  test al, al

asm_x64_jit_FLAGA_END:
  ret


.globl asm_x64_jit_LODA_IMM
.globl asm_x64_jit_LODA_IMM_END
asm_x64_jit_LODA_IMM:
  mov al, 0

asm_x64_jit_LODA_IMM_END:
  ret


.globl asm_x64_jit_STOA_IMM
.globl asm_x64_jit_STOA_IMM_END
asm_x64_jit_STOA_IMM:
  mov BYTE PTR [0x7fffffff], 0

asm_x64_jit_STOA_IMM_END:
  ret
