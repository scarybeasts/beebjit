#include "asm_x64_defs.h"

#define K_BBC_JIT_ADDR                     0x20000000
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


.globl asm_x64_jit_do_interrupt
asm_x64_jit_do_interrupt:

  # On entry:
  # REG_SCRATCH1 is current 6502 PC.
  # REG_SCRATCH2 is 6502 vector address (IRQ or NMI).

  call asm_x64_push_word_from_scratch

  call asm_x64_asm_emit_intel_flags_to_scratch

  mov [REG_6502_S_64], REG_SCRATCH1_8
  lea REG_SCRATCH3, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH3_8

  call asm_x64_instruction_SEI

  movzx REG_SCRATCH1_32, WORD PTR [K_BBC_MEM_READ_ADDR + REG_SCRATCH2]

  # Shift left 8.
  rorx REG_SCRATCH1_32, REG_SCRATCH1_32, 24
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_BBC_JIT_ADDR]
  jmp REG_SCRATCH1
