.file "asm_x64.S"
.intel_syntax noprefix
.section rodata
.text

#define REG_6502_A   al
#define REG_6502_X   bl
#define REG_6502_Y   cl
#define REG_6502_S   sil
#define REG_6502_PC  r10
#define REG_SCRATCH1 rdx

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


.globl asm_x64_instruction_TAX
.globl asm_x64_instruction_TAX_END
asm_x64_instruction_TAX:

  mov REG_6502_X, REG_6502_A

asm_x64_instruction_TAX_END:


.globl asm_x64_instruction_TAY
.globl asm_x64_instruction_TAY_END
asm_x64_instruction_TAY:

  mov REG_6502_Y, REG_6502_A

asm_x64_instruction_TAY_END:


.globl asm_x64_inturbo_next_opcode
.globl asm_x64_inturbo_next_opcode_END
asm_x64_inturbo_next_opcode:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC]
  lea REG_6502_PC, [REG_6502_PC + 1]
  jmp [REG_SCRATCH1 * 8 + 0x3f000000]

asm_x64_inturbo_next_opcode_END:
