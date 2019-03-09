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
  test REG_6502_A, REG_6502_A

asm_x64_jit_FLAGA_END:
  ret


.globl asm_x64_jit_FLAGX
.globl asm_x64_jit_FLAGX_END
asm_x64_jit_FLAGX:
  test REG_6502_X, REG_6502_X

asm_x64_jit_FLAGX_END:
  ret


.globl asm_x64_jit_FLAGY
.globl asm_x64_jit_FLAGY_END
asm_x64_jit_FLAGY:
  test REG_6502_Y, REG_6502_Y

asm_x64_jit_FLAGY_END:
  ret


.globl asm_x64_jit_STOA_IMM
.globl asm_x64_jit_STOA_IMM_END
asm_x64_jit_STOA_IMM:
  mov BYTE PTR [0x7fffffff], 0

asm_x64_jit_STOA_IMM_END:
  ret

.globl asm_x64_jit_BNE
.globl asm_x64_jit_BNE_END
asm_x64_jit_BNE:
  jne 0

asm_x64_jit_BNE_END:
  ret


.globl asm_x64_jit_BNE_8bit
.globl asm_x64_jit_BNE_8bit_END
asm_x64_jit_BNE_8bit:
  jne asm_x64_jit_BNE_8bit

asm_x64_jit_BNE_8bit_END:
  ret


.globl asm_x64_jit_INC_ZPG
.globl asm_x64_jit_INC_ZPG_END
asm_x64_jit_INC_ZPG:
  inc BYTE PTR [0x7fffffff]

asm_x64_jit_INC_ZPG_END:
  ret


.globl asm_x64_jit_JMP
.globl asm_x64_jit_JMP_END
asm_x64_jit_JMP:
  jmp 0

asm_x64_jit_JMP_END:
  ret

.globl asm_x64_jit_JMP_8bit
.globl asm_x64_jit_JMP_8bit_END
asm_x64_jit_JMP_8bit:
  jmp asm_x64_jit_JMP_8bit

asm_x64_jit_JMP_8bit_END:
  ret


.globl asm_x64_jit_LDA_IMM
.globl asm_x64_jit_LDA_IMM_END
asm_x64_jit_LDA_IMM:
  mov REG_6502_A, 0

asm_x64_jit_LDA_IMM_END:
  ret


.globl asm_x64_jit_LDA_ABX
.globl asm_x64_jit_LDA_ABX_END
asm_x64_jit_LDA_ABX:
  mov REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_LDA_ABX_END:
  ret


.globl asm_x64_jit_LDX_IMM
.globl asm_x64_jit_LDX_IMM_END
asm_x64_jit_LDX_IMM:
  mov REG_6502_X, 0

asm_x64_jit_LDX_IMM_END:
  ret


.globl asm_x64_jit_LDY_IMM
.globl asm_x64_jit_LDY_IMM_END
asm_x64_jit_LDY_IMM:
  mov REG_6502_Y, 0

asm_x64_jit_LDY_IMM_END:
  ret


.globl asm_x64_jit_STA_ABX
.globl asm_x64_jit_STA_ABX_END
asm_x64_jit_STA_ABX:
  mov [REG_6502_X_64 + 0xFFFF], REG_6502_A

asm_x64_jit_STA_ABX_END:
  ret
