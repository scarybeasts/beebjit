.file "asm_x64.S"
.intel_syntax noprefix
.section rodata
.text

.globl asm_x64_instruction_CRASH
.globl asm_x64_instruction_CRASH_END
asm_x64_instruction_CRASH:
  mov al, [0x0000dead]
asm_x64_instruction_CRASH_END:

.globl asm_x64_instruction_EXIT
.globl asm_x64_instruction_EXIT_END
asm_x64_instruction_EXIT:
  ret
asm_x64_instruction_EXIT_END:

.globl asm_x64_instruction_TRAP
.globl asm_x64_instruction_TRAP_END
asm_x64_instruction_TRAP:
  int 3
asm_x64_instruction_TRAP_END:
