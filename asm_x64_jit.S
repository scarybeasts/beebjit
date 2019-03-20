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


.globl asm_x64_jit_call_debug
.globl asm_x64_jit_call_debug_pc_patch
.globl asm_x64_jit_call_debug_call_patch
.globl asm_x64_jit_call_debug_END
asm_x64_jit_call_debug:
  mov REG_6502_PC_32, 0xFFFF
asm_x64_jit_call_debug_pc_patch:
  call 0
asm_x64_jit_call_debug_call_patch:

asm_x64_jit_call_debug_END:
  ret


.globl asm_x64_jit_ADD_IMM
.globl asm_x64_jit_ADD_IMM_END
asm_x64_jit_ADD_IMM:
  add REG_6502_A, 0

asm_x64_jit_ADD_IMM_END:
  ret


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


.globl asm_x64_jit_INC_SCRATCH
.globl asm_x64_jit_INC_SCRATCH_END
asm_x64_jit_INC_SCRATCH:
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + 1]

asm_x64_jit_INC_SCRATCH_END:
  ret


.globl asm_x64_jit_JMP_SCRATCH
.globl asm_x64_jit_JMP_SCRATCH_END
asm_x64_jit_JMP_SCRATCH:
  rorx REG_SCRATCH1_32, REG_SCRATCH1_32, 56
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_BBC_JIT_ADDR]
  jmp REG_SCRATCH1

asm_x64_jit_JMP_SCRATCH_END:
  ret


.globl asm_x64_jit_LOAD_CARRY
.globl asm_x64_jit_LOAD_CARRY_END
asm_x64_jit_LOAD_CARRY:
  bt REG_6502_CF_64, 0

asm_x64_jit_LOAD_CARRY_END:
  ret


.globl asm_x64_jit_LOAD_CARRY_INV
.globl asm_x64_jit_LOAD_CARRY_INV_END
asm_x64_jit_LOAD_CARRY_INV:
  bt REG_6502_CF_64, 0
  cmc

asm_x64_jit_LOAD_CARRY_INV_END:
  ret


.globl asm_x64_jit_LOAD_OVERFLOW
.globl asm_x64_jit_LOAD_OVERFLOW_END
asm_x64_jit_LOAD_OVERFLOW:
  bt REG_6502_OF_64, 0

asm_x64_jit_LOAD_OVERFLOW_END:
  ret


.globl asm_x64_jit_MODE_IND
.globl asm_x64_jit_MODE_IND_END
asm_x64_jit_MODE_IND:
  movzx REG_SCRATCH1_32, WORD PTR [0x7fffffff]

asm_x64_jit_MODE_IND_END:
  ret


.globl asm_x64_jit_MODE_IND_SCRATCH
.globl asm_x64_jit_MODE_IND_SCRATCH_jump_patch
.globl asm_x64_jit_MODE_IND_SCRATCH_END
asm_x64_jit_MODE_IND_SCRATCH:
  lea REG_SCRATCH2_32, [REG_SCRATCH1 + 1]
  movzx REG_SCRATCH1_32, WORD PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  # This jumps if the indirect address was 0xFF.
  # Pretty much never happens so we optimize for full word fetches from zero
  # page addresses 0x00 -> 0xFE.
  bt REG_SCRATCH2_32, 8
  jb 0
asm_x64_jit_MODE_IND_SCRATCH_jump_patch:

asm_x64_jit_MODE_IND_SCRATCH_END:
  ret


.globl asm_x64_jit_MODE_ZPX
.globl asm_x64_jit_MODE_ZPX_lea_patch
.globl asm_x64_jit_MODE_ZPX_END
asm_x64_jit_MODE_ZPX:
  lea REG_SCRATCH1_32, [REG_6502_X_64 + 0x7fffffff]
asm_x64_jit_MODE_ZPX_lea_patch:
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_x64_jit_MODE_ZPX_END:
  ret


.globl asm_x64_jit_MODE_ZPX_8bit
.globl asm_x64_jit_MODE_ZPX_8bit_lea_patch
.globl asm_x64_jit_MODE_ZPX_8bit_END
asm_x64_jit_MODE_ZPX_8bit:
  lea REG_SCRATCH1_32, [REG_6502_X_64 + 0x7f]
asm_x64_jit_MODE_ZPX_8bit_lea_patch:
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_x64_jit_MODE_ZPX_8bit_END:
  ret


.globl asm_x64_jit_MODE_ZPY
.globl asm_x64_jit_MODE_ZPY_lea_patch
.globl asm_x64_jit_MODE_ZPY_END
asm_x64_jit_MODE_ZPY:
  lea REG_SCRATCH1_32, [REG_6502_Y_64 + 0x7fffffff]
asm_x64_jit_MODE_ZPY_lea_patch:
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_x64_jit_MODE_ZPY_END:
  ret


.globl asm_x64_jit_MODE_ZPY_8bit
.globl asm_x64_jit_MODE_ZPY_8bit_lea_patch
.globl asm_x64_jit_MODE_ZPY_8bit_END
asm_x64_jit_MODE_ZPY_8bit:
  lea REG_SCRATCH1_32, [REG_6502_Y_64 + 0x7f]
asm_x64_jit_MODE_ZPY_8bit_lea_patch:
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8

asm_x64_jit_MODE_ZPY_8bit_END:
  ret


.globl asm_x64_jit_PUSH_16
.globl asm_x64_jit_PUSH_16_byte1_patch
.globl asm_x64_jit_PUSH_16_byte2_patch
.globl asm_x64_jit_PUSH_16_END
asm_x64_jit_PUSH_16:
  mov BYTE PTR [REG_6502_S_64], 0xff
asm_x64_jit_PUSH_16_byte1_patch:
  lea REG_SCRATCH1_32, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH1_8

  mov BYTE PTR [REG_6502_S_64], 0xff
asm_x64_jit_PUSH_16_byte2_patch:
  lea REG_SCRATCH1_32, [REG_6502_S_64 - 1]
  mov REG_6502_S, REG_SCRATCH1_8

asm_x64_jit_PUSH_16_END:
  ret


.globl asm_x64_jit_SAVE_CARRY
.globl asm_x64_jit_SAVE_CARRY_END
asm_x64_jit_SAVE_CARRY:
  setb REG_6502_CF

asm_x64_jit_SAVE_CARRY_END:
  ret


.globl asm_x64_jit_SAVE_CARRY_INV
.globl asm_x64_jit_SAVE_CARRY_INV_END
asm_x64_jit_SAVE_CARRY_INV:
  setae REG_6502_CF

asm_x64_jit_SAVE_CARRY_INV_END:
  ret


.globl asm_x64_jit_SAVE_OVERFLOW
.globl asm_x64_jit_SAVE_OVERFLOW_END
asm_x64_jit_SAVE_OVERFLOW:
  seto REG_6502_OF

asm_x64_jit_SAVE_OVERFLOW_END:
  ret


.globl asm_x64_jit_STOA_IMM
.globl asm_x64_jit_STOA_IMM_END
asm_x64_jit_STOA_IMM:
  mov BYTE PTR [0x7fffffff], 0

asm_x64_jit_STOA_IMM_END:
  ret


.globl asm_x64_jit_SUB_IMM
.globl asm_x64_jit_SUB_IMM_END
asm_x64_jit_SUB_IMM:
  sub REG_6502_A, 0

asm_x64_jit_SUB_IMM_END:
  ret


.globl asm_x64_jit_ADC_IMM
.globl asm_x64_jit_ADC_IMM_END
asm_x64_jit_ADC_IMM:
  adc REG_6502_A, 0

asm_x64_jit_ADC_IMM_END:
  ret


.globl asm_x64_jit_AND_IMM
.globl asm_x64_jit_AND_IMM_END
asm_x64_jit_AND_IMM:
  and REG_6502_A, 0

asm_x64_jit_AND_IMM_END:
  ret


.globl asm_x64_jit_ASL_ACC
.globl asm_x64_jit_ASL_ACC_END
asm_x64_jit_ASL_ACC:
  shl al, 1

asm_x64_jit_ASL_ACC_END:
  ret


.globl asm_x64_jit_BCC
.globl asm_x64_jit_BCC_END
asm_x64_jit_BCC:
  jae 0

asm_x64_jit_BCC_END:
  ret


.globl asm_x64_jit_BCC_8bit
.globl asm_x64_jit_BCC_8bit_END
asm_x64_jit_BCC_8bit:
  jae asm_x64_jit_BCC_8bit

asm_x64_jit_BCC_8bit_END:
  ret


.globl asm_x64_jit_BCS
.globl asm_x64_jit_BCS_END
asm_x64_jit_BCS:
  jb 0

asm_x64_jit_BCS_END:
  ret


.globl asm_x64_jit_BCS_8bit
.globl asm_x64_jit_BCS_8bit_END
asm_x64_jit_BCS_8bit:
  jb asm_x64_jit_BCS_8bit

asm_x64_jit_BCS_8bit_END:
  ret


.globl asm_x64_jit_BEQ
.globl asm_x64_jit_BEQ_END
asm_x64_jit_BEQ:
  je 0

asm_x64_jit_BEQ_END:
  ret


.globl asm_x64_jit_BEQ_8bit
.globl asm_x64_jit_BEQ_8bit_END
asm_x64_jit_BEQ_8bit:
  je asm_x64_jit_BEQ_8bit

asm_x64_jit_BEQ_8bit_END:
  ret


.globl asm_x64_jit_BIT
.globl asm_x64_jit_BIT_mov_patch
.globl asm_x64_jit_BIT_END
asm_x64_jit_BIT:
  mov ah, [0x7fffffff]
asm_x64_jit_BIT_mov_patch:
  # Load OF.
  # TODO: faster with pext, avoids dependency?
  # TODO: share code with inturbo.
  bt REG_6502_A_32, 14
  setb REG_6502_OF
  # Load ZF.
  test ah, REG_6502_A
  sete REG_SCRATCH1_8
  shl REG_SCRATCH1_8, 6
  # Load NF.
  and ah, 0x80
  # Put ZF / NF together.
  or ah, REG_SCRATCH1_8
  sahf

asm_x64_jit_BIT_END:
  ret


.globl asm_x64_jit_BMI
.globl asm_x64_jit_BMI_END
asm_x64_jit_BMI:
  js 0

asm_x64_jit_BMI_END:
  ret


.globl asm_x64_jit_BMI_8bit
.globl asm_x64_jit_BMI_8bit_END
asm_x64_jit_BMI_8bit:
  js asm_x64_jit_BMI_8bit

asm_x64_jit_BMI_8bit_END:
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


.globl asm_x64_jit_BPL
.globl asm_x64_jit_BPL_END
asm_x64_jit_BPL:
  jns 0

asm_x64_jit_BPL_END:
  ret


.globl asm_x64_jit_BPL_8bit
.globl asm_x64_jit_BPL_8bit_END
asm_x64_jit_BPL_8bit:
  jns asm_x64_jit_BPL_8bit

asm_x64_jit_BPL_8bit_END:
  ret


.globl asm_x64_jit_BVC
.globl asm_x64_jit_BVC_END
asm_x64_jit_BVC:
  jae 0

asm_x64_jit_BVC_END:
  ret


.globl asm_x64_jit_BVC_8bit
.globl asm_x64_jit_BVC_8bit_END
asm_x64_jit_BVC_8bit:
  jae asm_x64_jit_BVC_8bit

asm_x64_jit_BVC_8bit_END:
  ret


.globl asm_x64_jit_BVS
.globl asm_x64_jit_BVS_END
asm_x64_jit_BVS:
  jb 0

asm_x64_jit_BVS_END:
  ret


.globl asm_x64_jit_BVS_8bit
.globl asm_x64_jit_BVS_8bit_END
asm_x64_jit_BVS_8bit:
  jb asm_x64_jit_BVS_8bit

asm_x64_jit_BVS_8bit_END:
  ret


.globl asm_x64_jit_CMP_IMM
.globl asm_x64_jit_CMP_IMM_END
asm_x64_jit_CMP_IMM:
  cmp REG_6502_A, 0

asm_x64_jit_CMP_IMM_END:
  ret


.globl asm_x64_jit_CPX_IMM
.globl asm_x64_jit_CPX_IMM_END
asm_x64_jit_CPX_IMM:
  cmp REG_6502_X, 0

asm_x64_jit_CPX_IMM_END:
  ret


.globl asm_x64_jit_CPY_IMM
.globl asm_x64_jit_CPY_IMM_END
asm_x64_jit_CPY_IMM:
  cmp REG_6502_Y, 0

asm_x64_jit_CPY_IMM_END:
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


.globl asm_x64_jit_LDA_ABS
.globl asm_x64_jit_LDA_ABS_END
asm_x64_jit_LDA_ABS:
  mov REG_6502_A, [0x7fffffff]

asm_x64_jit_LDA_ABS_END:
  ret


.globl asm_x64_jit_LDA_scratch
.globl asm_x64_jit_LDA_scratch_END
asm_x64_jit_LDA_scratch:
  mov REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_jit_LDA_scratch_END:
  ret


.globl asm_x64_jit_LDA_ABX
.globl asm_x64_jit_LDA_ABX_END
asm_x64_jit_LDA_ABX:
  mov REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_LDA_ABX_END:
  ret


.globl asm_x64_jit_LDX_ABS
.globl asm_x64_jit_LDX_ABS_END
asm_x64_jit_LDX_ABS:
  mov REG_6502_X, [0x7fffffff]

asm_x64_jit_LDX_ABS_END:
  ret


.globl asm_x64_jit_LDX_IMM
.globl asm_x64_jit_LDX_IMM_END
asm_x64_jit_LDX_IMM:
  mov REG_6502_X, 0

asm_x64_jit_LDX_IMM_END:
  ret


.globl asm_x64_jit_LDX_scratch
.globl asm_x64_jit_LDX_scratch_END
asm_x64_jit_LDX_scratch:
  mov REG_6502_X, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_jit_LDX_scratch_END:
  ret


.globl asm_x64_jit_LDY_IMM
.globl asm_x64_jit_LDY_IMM_END
asm_x64_jit_LDY_IMM:
  mov REG_6502_Y, 0

asm_x64_jit_LDY_IMM_END:
  ret


.globl asm_x64_jit_LSR_ACC
.globl asm_x64_jit_LSR_ACC_END
asm_x64_jit_LSR_ACC:
  shr al, 1

asm_x64_jit_LSR_ACC_END:
  ret


.globl asm_x64_jit_ROL_ACC
.globl asm_x64_jit_ROL_ACC_END
asm_x64_jit_ROL_ACC:
  rcl al, 1

asm_x64_jit_ROL_ACC_END:
  ret


.globl asm_x64_jit_ROR_ACC
.globl asm_x64_jit_ROR_ACC_END
asm_x64_jit_ROR_ACC:
  rcr al, 1

asm_x64_jit_ROR_ACC_END:
  ret


.globl asm_x64_jit_SBC_IMM
.globl asm_x64_jit_SBC_IMM_END
asm_x64_jit_SBC_IMM:
  # NOTE: Intel has subtract with borrow, not subtract with carry, so
  # appropriate carry flag management also needs to be used.
  sbb REG_6502_A, 0

asm_x64_jit_SBC_IMM_END:
  ret


.globl asm_x64_jit_STA_ABS
.globl asm_x64_jit_STA_ABS_END
asm_x64_jit_STA_ABS:
  mov [0x7fffffff], REG_6502_A

asm_x64_jit_STA_ABS_END:
  ret


.globl asm_x64_jit_STA_ABX
.globl asm_x64_jit_STA_ABX_END
asm_x64_jit_STA_ABX:
  mov [REG_6502_X_64 + 0xFFFF], REG_6502_A

asm_x64_jit_STA_ABX_END:
  ret


.globl asm_x64_jit_STX_ABS
.globl asm_x64_jit_STX_ABS_END
asm_x64_jit_STX_ABS:
  mov [0x7fffffff], REG_6502_X

asm_x64_jit_STX_ABS_END:
  ret


.globl asm_x64_jit_STY_ABS
.globl asm_x64_jit_STY_ABS_END
asm_x64_jit_STY_ABS:
  mov [0x7fffffff], REG_6502_Y

asm_x64_jit_STY_ABS_END:
  ret
