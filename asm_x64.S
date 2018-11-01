.file "asm_x64.S"
.intel_syntax noprefix
.section rodata
.text

#define BBC_MEM_READ_ADDR 0x10000000

#define REG_6502_A    al
#define REG_6502_X    bl
#define REG_6502_Y    cl
#define REG_6502_S    sil
#define REG_6502_A64  rax
#define REG_6502_X64  rbx
#define REG_6502_Y64  rcx
#define REG_6502_S64  rsi
#define REG_6502_PC   r10
#define REG_SCRATCH1  rdx

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


.globl asm_x64_instruction_A_NZ_flags
.globl asm_x64_instruction_A_NZ_flags_END
asm_x64_instruction_A_NZ_flags:

  test REG_6502_A, REG_6502_A

asm_x64_instruction_A_NZ_flags_END:


.globl asm_x64_instruction_X_NZ_flags
.globl asm_x64_instruction_X_NZ_flags_END
asm_x64_instruction_X_NZ_flags:

  test REG_6502_X, REG_6502_X

asm_x64_instruction_X_NZ_flags_END:


.globl asm_x64_instruction_Y_NZ_flags
.globl asm_x64_instruction_Y_NZ_flags_END
asm_x64_instruction_Y_NZ_flags:

  test REG_6502_Y, REG_6502_Y

asm_x64_instruction_Y_NZ_flags_END:


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


.globl asm_x64_instruction_LDA_imm_interp
.globl asm_x64_instruction_LDA_imm_interp_END
asm_x64_instruction_LDA_imm_interp:

  mov REG_6502_A, BYTE PTR [REG_6502_PC + 1]

asm_x64_instruction_LDA_imm_interp_END:


.globl asm_x64_instruction_LDA_scratch_interp
.globl asm_x64_instruction_LDA_scratch_interp_END
asm_x64_instruction_LDA_scratch_interp:

  mov REG_6502_A, BYTE PTR [REG_SCRATCH1 + BBC_MEM_READ_ADDR]

asm_x64_instruction_LDA_scratch_interp_END:


.globl asm_x64_asm_enter
.globl asm_x64_asm_enter_END
asm_x64_asm_enter:

  # param1, rdi == context
  # param2, rsi == start address

  push rbp
  mov rbp, rsp
  push rbx
  push r12
  push r13
  push r14
  push r15

  mov rdx, rsi

  # 6502 A, X, Y, S.
  mov REG_6502_A64, 0
  mov REG_6502_X64, 0
  mov REG_6502_Y64, 0
  mov REG_6502_S64, 0

  # PC hack
  mov REG_6502_PC, 0x10000000
  # TODO: rest of registers.
  call rdx

  pop r15
  pop r14
  pop r13
  pop r12
  pop rbx
  pop rbp
  ret

asm_x64_asm_enter_END:


.globl asm_x64_inturbo_next_opcode
.globl asm_x64_inturbo_next_opcode_END
asm_x64_inturbo_next_opcode:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC]
  jmp [REG_SCRATCH1 * 4 + 0x3f000000]

asm_x64_inturbo_next_opcode_END:


.globl asm_x64_inturbo_mode_zpg
.globl asm_x64_inturbo_mode_zpg_END
asm_x64_inturbo_mode_zpg:

  movzx REG_SCRATCH1, BYTE PTR [REG_6502_PC + 1]
  lea REG_6502_PC, [REG_6502_PC + 2]

asm_x64_inturbo_mode_zpg_END:


.globl asm_x64_inturbo_mode_abs
.globl asm_x64_inturbo_mode_abs_END
asm_x64_inturbo_mode_abs:

  movzx REG_SCRATCH1, WORD PTR [REG_6502_PC + 1]
  lea REG_6502_PC, [REG_6502_PC + 3]

asm_x64_inturbo_mode_abs_END:
