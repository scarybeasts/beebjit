#include "asm_x64_defs.h"
#include "asm_x64_jit_defs.h"

.file "asm_x64_jit.S"
.intel_syntax noprefix
.section rodata
.text


.globl asm_x64_jit_compile_trampoline
asm_x64_jit_compile_trampoline:
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  # Trashes REG_SCRATCH1, REG_SCRATCH3
  call asm_x64_save_AXYS_PC_flags

  # param1: context object.
  mov REG_PARAM1, REG_CONTEXT
  # param2: x64 rip that called here.
  mov REG_PARAM2, [rsp]
  # The actual instruction address of interest is the call itself, so
  # subtract two to get that.
  lea REG_PARAM2_32, [REG_PARAM2 - 2]
  # param3: countdown
  mov REG_PARAM3, REG_COUNTDOWN

  push REG_CONTEXT
  call [REG_CONTEXT + K_JIT_CONTEXT_OFFSET_JIT_CALLBACK]
  pop REG_CONTEXT

  mov REG_COUNTDOWN, REG_RETURN

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_x64_restore_AXYS_PC_flags

  mov REG_SCRATCH1_32, REG_6502_PC_32
  rorx REG_SCRATCH1_32, REG_SCRATCH1_32, (32 - K_BBC_JIT_BYTES_SHIFT)
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_BBC_JIT_ADDR]

  # We're jumping out of a call so pop the return address.
  pop REG_SCRATCH2

  jmp REG_SCRATCH1


.globl asm_x64_jit_interp
asm_x64_jit_interp:
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  # Trashes REG_SCRATCH1, REG_SCRATCH3
  call asm_x64_save_AXYS_PC_flags

  # Save REG_CONTEXT because it's currently the same as REG_PARAM1, which is
  # overwritten below.
  mov REG_SCRATCH3, REG_CONTEXT
  # param1 is interp object.
  mov REG_PARAM1, [REG_SCRATCH3 + K_CONTEXT_OFFSET_INTERP_OBJECT]
  # param2 is current countdown value.
  mov REG_PARAM2, REG_COUNTDOWN

  push REG_SCRATCH3
  call [REG_SCRATCH3 + K_CONTEXT_OFFSET_INTERP_CALLBACK]
  pop REG_CONTEXT

  mov REG_COUNTDOWN, REG_RETURN
  test REG_RETURN2, REG_RETURN2
  je not_exiting
  mov REG_RETURN, REG_RETURN2
  ret

not_exiting:
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_x64_restore_AXYS_PC_flags

  mov REG_SCRATCH1_32, REG_6502_PC_32
  rorx REG_SCRATCH1_32, REG_SCRATCH1_32, (32 - K_BBC_JIT_BYTES_SHIFT)
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_BBC_JIT_ADDR]

  jmp REG_SCRATCH1


.globl asm_x64_jit_call_compile_trampoline
.globl asm_x64_jit_call_compile_trampoline_END
asm_x64_jit_call_compile_trampoline:
  call [REG_CONTEXT]

asm_x64_jit_call_compile_trampoline_END:
  ret


.globl asm_x64_jit_jump_interp_trampoline
.globl asm_x64_jit_jump_interp_trampoline_pc_patch
.globl asm_x64_jit_jump_interp_trampoline_jump_patch
.globl asm_x64_jit_jump_interp_trampoline_END
asm_x64_jit_jump_interp_trampoline:
  mov REG_6502_PC_32, 0xFFFF
asm_x64_jit_jump_interp_trampoline_pc_patch:
  jmp 0
asm_x64_jit_jump_interp_trampoline_jump_patch:

asm_x64_jit_jump_interp_trampoline_END:
  ret


.globl asm_x64_jit_check_countdown
.globl asm_x64_jit_check_countdown_count_patch
.globl asm_x64_jit_check_countdown_jump_patch
.globl asm_x64_jit_check_countdown_END
asm_x64_jit_check_countdown:
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 0x80000000]
asm_x64_jit_check_countdown_count_patch:
  bt REG_COUNTDOWN, 63
  jb 0
asm_x64_jit_check_countdown_jump_patch:

asm_x64_jit_check_countdown_END:
  ret


.globl asm_x64_jit_check_countdown_8bit
.globl asm_x64_jit_check_countdown_8bit_count_patch
.globl asm_x64_jit_check_countdown_8bit_jump_patch
.globl asm_x64_jit_check_countdown_8bit_END
asm_x64_jit_check_countdown_8bit:
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 0x80]
asm_x64_jit_check_countdown_8bit_count_patch:
  bt REG_COUNTDOWN, 63
  jb 0
asm_x64_jit_check_countdown_8bit_jump_patch:

asm_x64_jit_check_countdown_8bit_END:
  ret


.globl asm_x64_jit_call_debug
.globl asm_x64_jit_call_debug_pc_patch
.globl asm_x64_jit_call_debug_call_patch
.globl asm_x64_jit_call_debug_END
asm_x64_jit_call_debug:
  mov REG_6502_PC_32, 0xFFFF
asm_x64_jit_call_debug_pc_patch:
  # Some optimizations cache values across opcodes in REG_SCRATCH1.
  # So it must be saved.
  push REG_SCRATCH1
  call 0
asm_x64_jit_call_debug_call_patch:
  pop REG_SCRATCH1

asm_x64_jit_call_debug_END:
  ret


.globl asm_x64_jit_jump_interp
.globl asm_x64_jit_jump_interp_pc_patch
.globl asm_x64_jit_jump_interp_jump_patch
.globl asm_x64_jit_jump_interp_END
asm_x64_jit_jump_interp:
  mov REG_6502_PC_32, 0xFFFF
asm_x64_jit_jump_interp_pc_patch:
  jmp 0
asm_x64_jit_jump_interp_jump_patch:

asm_x64_jit_jump_interp_END:
  ret


.globl asm_x64_jit_ADD_CYCLES
.globl asm_x64_jit_ADD_CYCLES_END
asm_x64_jit_ADD_CYCLES:
  lea REG_COUNTDOWN, [REG_COUNTDOWN + 0x7f]

asm_x64_jit_ADD_CYCLES_END:
  ret


.globl asm_x64_jit_ADD_ABS
.globl asm_x64_jit_ADD_ABS_END
asm_x64_jit_ADD_ABS:
  add REG_6502_A, [0x7fffffff]

asm_x64_jit_ADD_ABS_END:
  ret


.globl asm_x64_jit_ADD_ABX
.globl asm_x64_jit_ADD_ABX_END
asm_x64_jit_ADD_ABX:
  add REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_ADD_ABX_END:
  ret


.globl asm_x64_jit_ADD_ABY
.globl asm_x64_jit_ADD_ABY_END
asm_x64_jit_ADD_ABY:
  add REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_ADD_ABY_END:
  ret


.globl asm_x64_jit_ADD_IMM
.globl asm_x64_jit_ADD_IMM_END
asm_x64_jit_ADD_IMM:
  add REG_6502_A, 0

asm_x64_jit_ADD_IMM_END:
  ret


.globl asm_x64_jit_ADD_SCRATCH
.globl asm_x64_jit_ADD_SCRATCH_END
asm_x64_jit_ADD_SCRATCH:
  add REG_6502_A, [REG_SCRATCH1 + 0x7fffffff]

asm_x64_jit_ADD_SCRATCH_END:
  ret


.globl asm_x64_jit_ADD_SCRATCH_Y
.globl asm_x64_jit_ADD_SCRATCH_Y_END
asm_x64_jit_ADD_SCRATCH_Y:
  add REG_6502_A, \
      [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_ADD_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_ABX_CHECK_PAGE_CROSSING
.globl asm_x64_jit_ABX_CHECK_PAGE_CROSSING_lea_patch
.globl asm_x64_jit_ABX_CHECK_PAGE_CROSSING_END
asm_x64_jit_ABX_CHECK_PAGE_CROSSING:
  mov REG_SCRATCH2_32, 31
  lea REG_SCRATCH1_32, [REG_6502_X_64 + 0x7f000000]
asm_x64_jit_ABX_CHECK_PAGE_CROSSING_lea_patch:
  shrx REG_SCRATCH1_32, REG_SCRATCH1_32, REG_SCRATCH2_32
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH1]

asm_x64_jit_ABX_CHECK_PAGE_CROSSING_END:
  ret


.globl asm_x64_jit_ABY_CHECK_PAGE_CROSSING
.globl asm_x64_jit_ABY_CHECK_PAGE_CROSSING_lea_patch
.globl asm_x64_jit_ABY_CHECK_PAGE_CROSSING_END
asm_x64_jit_ABY_CHECK_PAGE_CROSSING:
  mov REG_SCRATCH2_32, 31
  lea REG_SCRATCH1_32, [REG_6502_Y_64 + 0x7f000000]
asm_x64_jit_ABY_CHECK_PAGE_CROSSING_lea_patch:
  shrx REG_SCRATCH1_32, REG_SCRATCH1_32, REG_SCRATCH2_32
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH1]

asm_x64_jit_ABY_CHECK_PAGE_CROSSING_END:
  ret


.globl asm_x64_jit_CHECK_BCD
.globl asm_x64_jit_CHECK_BCD_jump_patch
.globl asm_x64_jit_CHECK_BCD_END
asm_x64_jit_CHECK_BCD:
  bt REG_6502_ID_F_32, 3
  jb 0
asm_x64_jit_CHECK_BCD_jump_patch:

asm_x64_jit_CHECK_BCD_END:
  ret


.globl asm_x64_jit_CHECK_PAGE_CROSSING_SCRATCH_n
.globl asm_x64_jit_CHECK_PAGE_CROSSING_SCRATCH_n_lea_patch
.globl asm_x64_jit_CHECK_PAGE_CROSSING_SCRATCH_n_END
asm_x64_jit_CHECK_PAGE_CROSSING_SCRATCH_n:
  movzx REG_SCRATCH2_32, REG_SCRATCH1_8
  lea REG_SCRATCH2_32, [REG_SCRATCH2 - 0x100]
asm_x64_jit_CHECK_PAGE_CROSSING_SCRATCH_n_lea_patch:
  mov REG_SCRATCH3_32, 31
  shrx REG_SCRATCH2_32, REG_SCRATCH2_32, REG_SCRATCH3_32
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH2]

asm_x64_jit_CHECK_PAGE_CROSSING_SCRATCH_n_END:
  ret


.globl asm_x64_jit_CHECK_PENDING_IRQ
.globl asm_x64_jit_CHECK_PENDING_IRQ_jump_patch
.globl asm_x64_jit_CHECK_PENDING_IRQ_END
asm_x64_jit_CHECK_PENDING_IRQ:
  # TODO: could use a register for IRQ_FIRE, if CLI / PLP is common?
  mov REG_SCRATCH1, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  mov REG_SCRATCH1_32, \
      DWORD PTR [REG_SCRATCH1 + K_STATE_6502_OFFSET_REG_IRQ_FIRE]
  lea REG_SCRATCH1_32, [REG_SCRATCH1 - 1]
  bt REG_SCRATCH1_32, 31
  jae 0
asm_x64_jit_CHECK_PENDING_IRQ_jump_patch:

asm_x64_jit_CHECK_PENDING_IRQ_END:
  ret


.globl asm_x64_jit_IDY_CHECK_PAGE_CROSSING
.globl asm_x64_jit_IDY_CHECK_PAGE_CROSSING_END
asm_x64_jit_IDY_CHECK_PAGE_CROSSING:
  lea REG_SCRATCH2_32, [REG_6502_Y_64 - 0x10000100]
  movzx REG_SCRATCH3_32, REG_SCRATCH1_8
  lea REG_SCRATCH2_32, [REG_SCRATCH2 + REG_SCRATCH3]
  mov REG_SCRATCH3_32, 31
  shrx REG_SCRATCH2_32, REG_SCRATCH2_32, REG_SCRATCH3_32
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH2]

asm_x64_jit_IDY_CHECK_PAGE_CROSSING_END:
  ret


.globl asm_x64_jit_INC_SCRATCH
.globl asm_x64_jit_INC_SCRATCH_END
asm_x64_jit_INC_SCRATCH:
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + 1]

asm_x64_jit_INC_SCRATCH_END:
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


.globl asm_x64_jit_JMP_SCRATCH
.globl asm_x64_jit_JMP_SCRATCH_END
asm_x64_jit_JMP_SCRATCH:
  rorx REG_SCRATCH1_32, REG_SCRATCH1_32, (32 - K_BBC_JIT_BYTES_SHIFT)
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_BBC_JIT_ADDR]
  jmp REG_SCRATCH1

asm_x64_jit_JMP_SCRATCH_END:
  ret


.globl asm_x64_jit_LDA_Z
.globl asm_x64_jit_LDA_Z_END
asm_x64_jit_LDA_Z:
  xor REG_6502_A, REG_6502_A

asm_x64_jit_LDA_Z_END:
  ret


.globl asm_x64_jit_LDX_Z
.globl asm_x64_jit_LDX_Z_END
asm_x64_jit_LDX_Z:
  xor REG_6502_X, REG_6502_X

asm_x64_jit_LDX_Z_END:
  ret


.globl asm_x64_jit_LDY_Z
.globl asm_x64_jit_LDY_Z_END
asm_x64_jit_LDY_Z:
  xor REG_6502_Y, REG_6502_Y

asm_x64_jit_LDY_Z_END:
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


.globl asm_x64_jit_MODE_ABX
.globl asm_x64_jit_MODE_ABX_lea_patch
.globl asm_x64_jit_MODE_ABX_END
asm_x64_jit_MODE_ABX:
  lea REG_SCRATCH1_32, [REG_6502_X_64 + 0xFFFF]
asm_x64_jit_MODE_ABX_lea_patch:
  movzx REG_SCRATCH1_32, REG_SCRATCH1_16

asm_x64_jit_MODE_ABX_END:
  ret


.globl asm_x64_jit_MODE_ABY
.globl asm_x64_jit_MODE_ABY_lea_patch
.globl asm_x64_jit_MODE_ABY_END
asm_x64_jit_MODE_ABY:
  lea REG_SCRATCH1_32, [REG_6502_Y_64 + 0xFFFF]
asm_x64_jit_MODE_ABY_lea_patch:
  movzx REG_SCRATCH1_32, REG_SCRATCH1_16

asm_x64_jit_MODE_ABY_END:
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
  # This faults (with a fixup handler) if we're trying to load from 0xFF,
  # which is highly unusual.
  mov REG_SCRATCH2_8, \
      [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR + K_6502_ADDR_SPACE_SIZE - 0xFF]
  movzx REG_SCRATCH1_32, WORD PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

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


.globl asm_x64_jit_WRITE_INV_ABS
.globl asm_x64_jit_WRITE_INV_ABS_offset_patch
.globl asm_x64_jit_WRITE_INV_ABS_END
asm_x64_jit_WRITE_INV_ABS:
  mov REG_SCRATCH2_32, [REG_CONTEXT + 0x7fffffff]
asm_x64_jit_WRITE_INV_ABS_offset_patch:
  # NOTE: 16-bit imm contant may case LCP stall but quick tests didn't show any
  # problems vs. using a 16-bit register.
  mov WORD PTR [REG_SCRATCH2], 0x17ff

asm_x64_jit_WRITE_INV_ABS_END:
  ret


.globl asm_x64_jit_WRITE_INV_SCRATCH
.globl asm_x64_jit_WRITE_INV_SCRATCH_END
asm_x64_jit_WRITE_INV_SCRATCH:
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_SCRATCH1 * 4]
  mov WORD PTR [REG_SCRATCH2], 0x17ff

asm_x64_jit_WRITE_INV_SCRATCH_END:
  ret


.globl asm_x64_jit_WRITE_INV_SCRATCH_Y
.globl asm_x64_jit_WRITE_INV_SCRATCH_Y_END
asm_x64_jit_WRITE_INV_SCRATCH_Y:
  lea REG_SCRATCH2_32, [REG_SCRATCH1 + REG_6502_Y_64]
  movzx REG_SCRATCH2_32, REG_SCRATCH2_16
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_SCRATCH2 * 4]
  mov WORD PTR [REG_SCRATCH2], 0x17ff

asm_x64_jit_WRITE_INV_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_ADC_ABS
.globl asm_x64_jit_ADC_ABS_END
asm_x64_jit_ADC_ABS:
  adc REG_6502_A, [0x7fffffff]

asm_x64_jit_ADC_ABS_END:
  ret


.globl asm_x64_jit_ADC_ABX
.globl asm_x64_jit_ADC_ABX_END
asm_x64_jit_ADC_ABX:
  adc REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_ADC_ABX_END:
  ret


.globl asm_x64_jit_ADC_ABY
.globl asm_x64_jit_ADC_ABY_END
asm_x64_jit_ADC_ABY:
  adc REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_ADC_ABY_END:
  ret


.globl asm_x64_jit_ADC_IMM
.globl asm_x64_jit_ADC_IMM_END
asm_x64_jit_ADC_IMM:
  adc REG_6502_A, 0

asm_x64_jit_ADC_IMM_END:
  ret


.globl asm_x64_jit_ADC_SCRATCH
.globl asm_x64_jit_ADC_SCRATCH_END
asm_x64_jit_ADC_SCRATCH:
  adc REG_6502_A, [REG_SCRATCH1 + 0x7fffffff]

asm_x64_jit_ADC_SCRATCH_END:
  ret


.globl asm_x64_jit_ADC_SCRATCH_Y
.globl asm_x64_jit_ADC_SCRATCH_Y_END
asm_x64_jit_ADC_SCRATCH_Y:
  adc REG_6502_A, \
      [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_ADC_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_ALR_IMM
.globl asm_x64_jit_ALR_IMM_patch_byte
.globl asm_x64_jit_ALR_IMM_END
asm_x64_jit_ALR_IMM:
  and REG_6502_A, 0xff
asm_x64_jit_ALR_IMM_patch_byte:
  shr REG_6502_A, 1

asm_x64_jit_ALR_IMM_END:
  ret


.globl asm_x64_jit_AND_ABS
.globl asm_x64_jit_AND_ABS_END
asm_x64_jit_AND_ABS:
  and REG_6502_A, [0x7fffffff]

asm_x64_jit_AND_ABS_END:
  ret


.globl asm_x64_jit_AND_ABX
.globl asm_x64_jit_AND_ABX_END
asm_x64_jit_AND_ABX:
  and REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_AND_ABX_END:
  ret


.globl asm_x64_jit_AND_ABY
.globl asm_x64_jit_AND_ABY_END
asm_x64_jit_AND_ABY:
  and REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_AND_ABY_END:
  ret


.globl asm_x64_jit_AND_IMM
.globl asm_x64_jit_AND_IMM_END
asm_x64_jit_AND_IMM:
  and REG_6502_A, 0

asm_x64_jit_AND_IMM_END:
  ret


.globl asm_x64_jit_AND_SCRATCH
.globl asm_x64_jit_AND_SCRATCH_END
asm_x64_jit_AND_SCRATCH:
  and REG_6502_A, [REG_SCRATCH1 + 0x7fffffff]

asm_x64_jit_AND_SCRATCH_END:
  ret


.globl asm_x64_jit_AND_SCRATCH_Y
.globl asm_x64_jit_AND_SCRATCH_Y_END
asm_x64_jit_AND_SCRATCH_Y:
  and REG_6502_A, \
      [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_AND_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_ASL_ABS
.globl asm_x64_jit_ASL_ABS_END
asm_x64_jit_ASL_ABS:
  shl BYTE PTR [0x7fffffff], 1

asm_x64_jit_ASL_ABS_END:
  ret


.globl asm_x64_jit_ASL_ABS_RMW
.globl asm_x64_jit_ASL_ABS_RMW_mov1_patch
.globl asm_x64_jit_ASL_ABS_RMW_mov2_patch
.globl asm_x64_jit_ASL_ABS_RMW_END
asm_x64_jit_ASL_ABS_RMW:
  mov REG_SCRATCH1_8, [0x7fffffff]
asm_x64_jit_ASL_ABS_RMW_mov1_patch:
  shl REG_SCRATCH1_8, 1
  mov [0x7fffffff], REG_SCRATCH1_8
asm_x64_jit_ASL_ABS_RMW_mov2_patch:

asm_x64_jit_ASL_ABS_RMW_END:
  ret


.globl asm_x64_jit_ASL_ABX
.globl asm_x64_jit_ASL_ABX_END
asm_x64_jit_ASL_ABX:
  shl BYTE PTR [REG_6502_X_64 + 0xFFFF], 1

asm_x64_jit_ASL_ABX_END:
  ret


.globl asm_x64_jit_ASL_ABX_RMW
.globl asm_x64_jit_ASL_ABX_RMW_mov1_patch
.globl asm_x64_jit_ASL_ABX_RMW_mov2_patch
.globl asm_x64_jit_ASL_ABX_RMW_END
asm_x64_jit_ASL_ABX_RMW:
  mov REG_SCRATCH1_8, [REG_6502_X_64 + 0xFFFF]
asm_x64_jit_ASL_ABX_RMW_mov1_patch:
  shl REG_SCRATCH1_8, 1
  mov [REG_6502_X_64 + 0xFFFF], REG_SCRATCH1_8
asm_x64_jit_ASL_ABX_RMW_mov2_patch:

asm_x64_jit_ASL_ABX_RMW_END:
  ret


.globl asm_x64_jit_ASL_ACC
.globl asm_x64_jit_ASL_ACC_END
asm_x64_jit_ASL_ACC:
  shl al, 1

asm_x64_jit_ASL_ACC_END:
  ret


.globl asm_x64_jit_ASL_ACC_n
.globl asm_x64_jit_ASL_ACC_n_END
asm_x64_jit_ASL_ACC_n:
  shl al, 2

asm_x64_jit_ASL_ACC_n_END:
  ret


.globl asm_x64_jit_ASL_scratch
.globl asm_x64_jit_ASL_scratch_END
asm_x64_jit_ASL_scratch:
  # NOTE: only used for mode zpx so it's safe to assume RAM.
  shl BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR], 1

asm_x64_jit_ASL_scratch_END:
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
  movzx REG_SCRATCH1_32, BYTE PTR [0x7fffffff]
asm_x64_jit_BIT_mov_patch:

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


.globl asm_x64_jit_CMP_ABS
.globl asm_x64_jit_CMP_ABS_END
asm_x64_jit_CMP_ABS:
  cmp REG_6502_A, [0x7fffffff]

asm_x64_jit_CMP_ABS_END:
  ret


.globl asm_x64_jit_CMP_ABX
.globl asm_x64_jit_CMP_ABX_END
asm_x64_jit_CMP_ABX:
  cmp REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_CMP_ABX_END:
  ret


.globl asm_x64_jit_CMP_ABY
.globl asm_x64_jit_CMP_ABY_END
asm_x64_jit_CMP_ABY:
  cmp REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_CMP_ABY_END:
  ret


.globl asm_x64_jit_CMP_IMM
.globl asm_x64_jit_CMP_IMM_END
asm_x64_jit_CMP_IMM:
  cmp REG_6502_A, 0

asm_x64_jit_CMP_IMM_END:
  ret


.globl asm_x64_jit_CMP_SCRATCH
.globl asm_x64_jit_CMP_SCRATCH_END
asm_x64_jit_CMP_SCRATCH:
  cmp REG_6502_A, [REG_SCRATCH1 + 0x7fffffff]

asm_x64_jit_CMP_SCRATCH_END:
  ret


.globl asm_x64_jit_CMP_SCRATCH_Y
.globl asm_x64_jit_CMP_SCRATCH_Y_END
asm_x64_jit_CMP_SCRATCH_Y:
  cmp REG_6502_A, \
      [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_CMP_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_CPX_ABS
.globl asm_x64_jit_CPX_ABS_END
asm_x64_jit_CPX_ABS:
  cmp REG_6502_X, [0x7fffffff]

asm_x64_jit_CPX_ABS_END:
  ret


.globl asm_x64_jit_CPX_IMM
.globl asm_x64_jit_CPX_IMM_END
asm_x64_jit_CPX_IMM:
  cmp REG_6502_X, 0

asm_x64_jit_CPX_IMM_END:
  ret


.globl asm_x64_jit_CPY_ABS
.globl asm_x64_jit_CPY_ABS_END
asm_x64_jit_CPY_ABS:
  cmp REG_6502_Y, [0x7fffffff]

asm_x64_jit_CPY_ABS_END:
  ret


.globl asm_x64_jit_CPY_IMM
.globl asm_x64_jit_CPY_IMM_END
asm_x64_jit_CPY_IMM:
  cmp REG_6502_Y, 0

asm_x64_jit_CPY_IMM_END:
  ret


.globl asm_x64_jit_DEC_ABS
.globl asm_x64_jit_DEC_ABS_END
asm_x64_jit_DEC_ABS:
  dec BYTE PTR [0x7fffffff]

asm_x64_jit_DEC_ABS_END:
  ret


.globl asm_x64_jit_DEC_ABS_RMW
.globl asm_x64_jit_DEC_ABS_RMW_mov1_patch
.globl asm_x64_jit_DEC_ABS_RMW_mov2_patch
.globl asm_x64_jit_DEC_ABS_RMW_END
asm_x64_jit_DEC_ABS_RMW:
  mov REG_SCRATCH1_8, [0x7fffffff]
asm_x64_jit_DEC_ABS_RMW_mov1_patch:
  dec REG_SCRATCH1_8
  mov [0x7fffffff], REG_SCRATCH1_8
asm_x64_jit_DEC_ABS_RMW_mov2_patch:

asm_x64_jit_DEC_ABS_RMW_END:
  ret


.globl asm_x64_jit_DEC_ABX
.globl asm_x64_jit_DEC_ABX_END
asm_x64_jit_DEC_ABX:
  dec BYTE PTR [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_DEC_ABX_END:
  ret


.globl asm_x64_jit_DEC_ABX_RMW
.globl asm_x64_jit_DEC_ABX_RMW_mov1_patch
.globl asm_x64_jit_DEC_ABX_RMW_mov2_patch
.globl asm_x64_jit_DEC_ABX_RMW_END
asm_x64_jit_DEC_ABX_RMW:
  mov REG_SCRATCH1_8, [REG_6502_X_64 + 0xFFFF]
asm_x64_jit_DEC_ABX_RMW_mov1_patch:
  dec REG_SCRATCH1_8
  mov [REG_6502_X_64 + 0xFFFF], REG_SCRATCH1_8
asm_x64_jit_DEC_ABX_RMW_mov2_patch:

asm_x64_jit_DEC_ABX_RMW_END:
  ret


.globl asm_x64_jit_DEC_scratch
.globl asm_x64_jit_DEC_scratch_END
asm_x64_jit_DEC_scratch:
  # NOTE: only used for mode zpx so it's safe to assume RAM.
  dec BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_jit_DEC_scratch_END:
  ret


.globl asm_x64_jit_EOR_ABS
.globl asm_x64_jit_EOR_ABS_END
asm_x64_jit_EOR_ABS:
  xor REG_6502_A, [0x7fffffff]

asm_x64_jit_EOR_ABS_END:
  ret


.globl asm_x64_jit_EOR_ABX
.globl asm_x64_jit_EOR_ABX_END
asm_x64_jit_EOR_ABX:
  xor REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_EOR_ABX_END:
  ret


.globl asm_x64_jit_EOR_ABY
.globl asm_x64_jit_EOR_ABY_END
asm_x64_jit_EOR_ABY:
  xor REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_EOR_ABY_END:
  ret


.globl asm_x64_jit_EOR_IMM
.globl asm_x64_jit_EOR_IMM_END
asm_x64_jit_EOR_IMM:
  xor REG_6502_A, 0

asm_x64_jit_EOR_IMM_END:
  ret


.globl asm_x64_jit_EOR_SCRATCH
.globl asm_x64_jit_EOR_SCRATCH_END
asm_x64_jit_EOR_SCRATCH:
  xor REG_6502_A, [REG_SCRATCH1 + 0x7fffffff]

asm_x64_jit_EOR_SCRATCH_END:
  ret


.globl asm_x64_jit_EOR_SCRATCH_Y
.globl asm_x64_jit_EOR_SCRATCH_Y_END
asm_x64_jit_EOR_SCRATCH_Y:
  xor REG_6502_A, \
      [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_EOR_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_INC_ABS
.globl asm_x64_jit_INC_ABS_END
asm_x64_jit_INC_ABS:
  inc BYTE PTR [0x7fffffff]

asm_x64_jit_INC_ABS_END:
  ret


.globl asm_x64_jit_INC_ABS_RMW
.globl asm_x64_jit_INC_ABS_RMW_mov1_patch
.globl asm_x64_jit_INC_ABS_RMW_mov2_patch
.globl asm_x64_jit_INC_ABS_RMW_END
asm_x64_jit_INC_ABS_RMW:
  mov REG_SCRATCH1_8, [0x7fffffff]
asm_x64_jit_INC_ABS_RMW_mov1_patch:
  inc REG_SCRATCH1_8
  mov [0x7fffffff], REG_SCRATCH1_8
asm_x64_jit_INC_ABS_RMW_mov2_patch:

asm_x64_jit_INC_ABS_RMW_END:
 ret


.globl asm_x64_jit_INC_ABX
.globl asm_x64_jit_INC_ABX_END
asm_x64_jit_INC_ABX:
  inc BYTE PTR [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_INC_ABX_END:
  ret


.globl asm_x64_jit_INC_ABX_RMW
.globl asm_x64_jit_INC_ABX_RMW_mov1_patch
.globl asm_x64_jit_INC_ABX_RMW_mov2_patch
.globl asm_x64_jit_INC_ABX_RMW_END
asm_x64_jit_INC_ABX_RMW:
  mov REG_SCRATCH1_8, [REG_6502_X_64 + 0xFFFF]
asm_x64_jit_INC_ABX_RMW_mov1_patch:
  inc REG_SCRATCH1_8
  mov [REG_6502_X_64 + 0xFFFF], REG_SCRATCH1_8
asm_x64_jit_INC_ABX_RMW_mov2_patch:

asm_x64_jit_INC_ABX_RMW_END:
  ret


.globl asm_x64_jit_INC_scratch
.globl asm_x64_jit_INC_scratch_END
asm_x64_jit_INC_scratch:
  # NOTE: only used for mode zpx so it's safe to assume RAM.
  inc BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_jit_INC_scratch_END:
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


.globl asm_x64_jit_LDA_SCRATCH
.globl asm_x64_jit_LDA_SCRATCH_END
asm_x64_jit_LDA_SCRATCH:
  mov REG_6502_A, [REG_SCRATCH1 + 0x7fffffff]

asm_x64_jit_LDA_SCRATCH_END:
  ret


.globl asm_x64_jit_LDA_SCRATCH_Y
.globl asm_x64_jit_LDA_SCRATCH_Y_END
asm_x64_jit_LDA_SCRATCH_Y:
  mov REG_6502_A, \
      [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_LDA_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_LDA_ABX
.globl asm_x64_jit_LDA_ABX_END
asm_x64_jit_LDA_ABX:
  mov REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_LDA_ABX_END:
  ret


.globl asm_x64_jit_LDA_ABY
.globl asm_x64_jit_LDA_ABY_END
asm_x64_jit_LDA_ABY:
  mov REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_LDA_ABY_END:
  ret


.globl asm_x64_jit_LDX_ABS
.globl asm_x64_jit_LDX_ABS_END
asm_x64_jit_LDX_ABS:
  mov REG_6502_X, [0x7fffffff]

asm_x64_jit_LDX_ABS_END:
  ret


.globl asm_x64_jit_LDX_ABY
.globl asm_x64_jit_LDX_ABY_END
asm_x64_jit_LDX_ABY:
  mov REG_6502_X, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_LDX_ABY_END:
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


.globl asm_x64_jit_LDY_ABS
.globl asm_x64_jit_LDY_ABS_END
asm_x64_jit_LDY_ABS:
  mov REG_6502_Y, [0x7fffffff]

asm_x64_jit_LDY_ABS_END:
  ret


.globl asm_x64_jit_LDY_ABX
.globl asm_x64_jit_LDY_ABX_END
asm_x64_jit_LDY_ABX:
  mov REG_6502_Y, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_LDY_ABX_END:
  ret


.globl asm_x64_jit_LDY_IMM
.globl asm_x64_jit_LDY_IMM_END
asm_x64_jit_LDY_IMM:
  mov REG_6502_Y, 0

asm_x64_jit_LDY_IMM_END:
  ret


.globl asm_x64_jit_LDY_scratch
.globl asm_x64_jit_LDY_scratch_END
asm_x64_jit_LDY_scratch:
  mov REG_6502_Y, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]

asm_x64_jit_LDY_scratch_END:
  ret


.globl asm_x64_jit_LSR_ABS
.globl asm_x64_jit_LSR_ABS_END
asm_x64_jit_LSR_ABS:
  shr BYTE PTR [0x7fffffff], 1

asm_x64_jit_LSR_ABS_END:
  ret


.globl asm_x64_jit_LSR_ABS_RMW
.globl asm_x64_jit_LSR_ABS_RMW_mov1_patch
.globl asm_x64_jit_LSR_ABS_RMW_mov2_patch
.globl asm_x64_jit_LSR_ABS_RMW_END
asm_x64_jit_LSR_ABS_RMW:
  mov REG_SCRATCH1_8, [0x7fffffff]
asm_x64_jit_LSR_ABS_RMW_mov1_patch:
  shr REG_SCRATCH1_8, 1
  mov [0x7fffffff], REG_SCRATCH1_8
asm_x64_jit_LSR_ABS_RMW_mov2_patch:

asm_x64_jit_LSR_ABS_RMW_END:
  ret


.globl asm_x64_jit_LSR_ABX
.globl asm_x64_jit_LSR_ABX_END
asm_x64_jit_LSR_ABX:
  shr BYTE PTR [REG_6502_X_64 + 0xFFFF], 1

asm_x64_jit_LSR_ABX_END:
  ret


.globl asm_x64_jit_LSR_ABX_RMW
.globl asm_x64_jit_LSR_ABX_RMW_mov1_patch
.globl asm_x64_jit_LSR_ABX_RMW_mov2_patch
.globl asm_x64_jit_LSR_ABX_RMW_END
asm_x64_jit_LSR_ABX_RMW:
  mov REG_SCRATCH1_8, [REG_6502_X_64 + 0xFFFF]
asm_x64_jit_LSR_ABX_RMW_mov1_patch:
  shr REG_SCRATCH1_8, 1
  mov [REG_6502_X_64 + 0xFFFF], REG_SCRATCH1_8
asm_x64_jit_LSR_ABX_RMW_mov2_patch:

asm_x64_jit_LSR_ABX_RMW_END:
  ret


.globl asm_x64_jit_LSR_ACC
.globl asm_x64_jit_LSR_ACC_END
asm_x64_jit_LSR_ACC:
  shr al, 1

asm_x64_jit_LSR_ACC_END:
  ret


.globl asm_x64_jit_LSR_ACC_n
.globl asm_x64_jit_LSR_ACC_n_END
asm_x64_jit_LSR_ACC_n:
  shr al, 2

asm_x64_jit_LSR_ACC_n_END:
  ret


.globl asm_x64_jit_LSR_scratch
.globl asm_x64_jit_LSR_scratch_END
asm_x64_jit_LSR_scratch:
  # NOTE: only used for mode zpx so it's safe to assume RAM.
  shr BYTE PTR [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR], 1

asm_x64_jit_LSR_scratch_END:
  ret


.globl asm_x64_jit_ORA_ABX
.globl asm_x64_jit_ORA_ABX_END
asm_x64_jit_ORA_ABX:
  or REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_ORA_ABX_END:
  ret


.globl asm_x64_jit_ORA_ABY
.globl asm_x64_jit_ORA_ABY_END
asm_x64_jit_ORA_ABY:
  or REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_ORA_ABY_END:
  ret


.globl asm_x64_jit_ORA_ABS
.globl asm_x64_jit_ORA_ABS_END
asm_x64_jit_ORA_ABS:
  or REG_6502_A, [0x7fffffff]

asm_x64_jit_ORA_ABS_END:
  ret


.globl asm_x64_jit_ORA_IMM
.globl asm_x64_jit_ORA_IMM_END
asm_x64_jit_ORA_IMM:
  or REG_6502_A, 0

asm_x64_jit_ORA_IMM_END:
  ret


.globl asm_x64_jit_ORA_SCRATCH
.globl asm_x64_jit_ORA_SCRATCH_END
asm_x64_jit_ORA_SCRATCH:
  or REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_IND_ADDR]

asm_x64_jit_ORA_SCRATCH_END:
  ret


.globl asm_x64_jit_ORA_SCRATCH_Y
.globl asm_x64_jit_ORA_SCRATCH_Y_END
asm_x64_jit_ORA_SCRATCH_Y:
  or REG_6502_A, \
     [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_ORA_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_ROL_ABS_RMW
.globl asm_x64_jit_ROL_ABS_RMW_mov1_patch
.globl asm_x64_jit_ROL_ABS_RMW_mov2_patch
.globl asm_x64_jit_ROL_ABS_RMW_END
asm_x64_jit_ROL_ABS_RMW:
  mov REG_SCRATCH1_8, [0x7fffffff]
asm_x64_jit_ROL_ABS_RMW_mov1_patch:
  movzx REG_SCRATCH2_32, REG_SCRATCH1_8
  rcl REG_SCRATCH1_8, 1
  test REG_SCRATCH1_8, REG_SCRATCH1_8
  bt REG_SCRATCH2_32, 7
  mov [0x7fffffff], REG_SCRATCH1_8
asm_x64_jit_ROL_ABS_RMW_mov2_patch:

asm_x64_jit_ROL_ABS_RMW_END:
  ret


.globl asm_x64_jit_ROL_ABX_RMW
.globl asm_x64_jit_ROL_ABX_RMW_mov1_patch
.globl asm_x64_jit_ROL_ABX_RMW_mov2_patch
.globl asm_x64_jit_ROL_ABX_RMW_END
asm_x64_jit_ROL_ABX_RMW:
  mov REG_SCRATCH1_8, [REG_6502_X_64 + 0xFFFF]
asm_x64_jit_ROL_ABX_RMW_mov1_patch:
  movzx REG_SCRATCH2_32, REG_SCRATCH1_8
  rcl REG_SCRATCH1_8, 1
  test REG_SCRATCH1_8, REG_SCRATCH1_8
  bt REG_SCRATCH2_32, 7
  mov [REG_6502_X_64 + 0xFFFF], REG_SCRATCH1_8
asm_x64_jit_ROL_ABX_RMW_mov2_patch:

asm_x64_jit_ROL_ABX_RMW_END:
  ret


.globl asm_x64_jit_ROL_ACC
.globl asm_x64_jit_ROL_ACC_END
asm_x64_jit_ROL_ACC:
  rcl al, 1

asm_x64_jit_ROL_ACC_END:
  ret


.globl asm_x64_jit_ROL_ACC_n
.globl asm_x64_jit_ROL_ACC_n_END
asm_x64_jit_ROL_ACC_n:
  rcl al, 2

asm_x64_jit_ROL_ACC_n_END:
  ret


.globl asm_x64_jit_ROL_scratch
.globl asm_x64_jit_ROL_scratch_END
asm_x64_jit_ROL_scratch:
  mov REG_SCRATCH2_8, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  movzx REG_SCRATCH3_32, REG_SCRATCH2_8
  rcl REG_SCRATCH2_8, 1
  test REG_SCRATCH2_8, REG_SCRATCH2_8
  bt REG_SCRATCH3_32, 7
  mov [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR], REG_SCRATCH2_8

asm_x64_jit_ROL_scratch_END:
  ret


.globl asm_x64_jit_ROR_ABS_RMW
.globl asm_x64_jit_ROR_ABS_RMW_mov1_patch
.globl asm_x64_jit_ROR_ABS_RMW_mov2_patch
.globl asm_x64_jit_ROR_ABS_RMW_END
asm_x64_jit_ROR_ABS_RMW:
  mov REG_SCRATCH1_8, [0x7fffffff]
asm_x64_jit_ROR_ABS_RMW_mov1_patch:
  movzx REG_SCRATCH2_32, REG_SCRATCH1_8
  rcr REG_SCRATCH1_8, 1
  test REG_SCRATCH1_8, REG_SCRATCH1_8
  bt REG_SCRATCH2_32, 0
  mov [0x7fffffff], REG_SCRATCH1_8
asm_x64_jit_ROR_ABS_RMW_mov2_patch:

asm_x64_jit_ROR_ABS_RMW_END:
  ret


.globl asm_x64_jit_ROR_ABX_RMW
.globl asm_x64_jit_ROR_ABX_RMW_mov1_patch
.globl asm_x64_jit_ROR_ABX_RMW_mov2_patch
.globl asm_x64_jit_ROR_ABX_RMW_END
asm_x64_jit_ROR_ABX_RMW:
  mov REG_SCRATCH1_8, [REG_6502_X_64 + 0xFFFF]
asm_x64_jit_ROR_ABX_RMW_mov1_patch:
  movzx REG_SCRATCH2_32, REG_SCRATCH1_8
  rcr REG_SCRATCH1_8, 1
  test REG_SCRATCH1_8, REG_SCRATCH1_8
  bt REG_SCRATCH2_32, 0
  mov [REG_6502_X_64 + 0xFFFF], REG_SCRATCH1_8
asm_x64_jit_ROR_ABX_RMW_mov2_patch:

asm_x64_jit_ROR_ABX_RMW_END:
  ret


.globl asm_x64_jit_ROR_ACC
.globl asm_x64_jit_ROR_ACC_END
asm_x64_jit_ROR_ACC:
  rcr al, 1

asm_x64_jit_ROR_ACC_END:
  ret


.globl asm_x64_jit_ROR_ACC_n
.globl asm_x64_jit_ROR_ACC_n_END
asm_x64_jit_ROR_ACC_n:
  rcr al, 2

asm_x64_jit_ROR_ACC_n_END:
  ret


.globl asm_x64_jit_ROR_scratch
.globl asm_x64_jit_ROR_scratch_END
asm_x64_jit_ROR_scratch:
  mov REG_SCRATCH2_8, [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR]
  movzx REG_SCRATCH3_32, REG_SCRATCH2_8
  rcr REG_SCRATCH2_8, 1
  test REG_SCRATCH2_8, REG_SCRATCH2_8
  bt REG_SCRATCH3_32, 0
  mov [REG_SCRATCH1 + K_BBC_MEM_READ_ADDR], REG_SCRATCH2_8

asm_x64_jit_ROR_scratch_END:
  ret


.globl asm_x64_jit_SAX_ABS
.globl asm_x64_jit_SAX_ABS_END
asm_x64_jit_SAX_ABS:
  lahf
  mov REG_SCRATCH1_8, REG_6502_X
  and REG_SCRATCH1_8, REG_6502_A
  sahf
  mov [0x7fffffff], REG_SCRATCH1_8

asm_x64_jit_SAX_ABS_END:
  ret


.globl asm_x64_jit_SBC_ABS
.globl asm_x64_jit_SBC_ABS_END
asm_x64_jit_SBC_ABS:
  sbb REG_6502_A, [0x7fffffff]

asm_x64_jit_SBC_ABS_END:
  ret


.globl asm_x64_jit_SBC_ABX
.globl asm_x64_jit_SBC_ABX_END
asm_x64_jit_SBC_ABX:
  sbb REG_6502_A, [REG_6502_X_64 + 0xFFFF]

asm_x64_jit_SBC_ABX_END:
  ret


.globl asm_x64_jit_SBC_ABY
.globl asm_x64_jit_SBC_ABY_END
asm_x64_jit_SBC_ABY:
  sbb REG_6502_A, [REG_6502_Y_64 + 0xFFFF]

asm_x64_jit_SBC_ABY_END:
  ret


.globl asm_x64_jit_SBC_IMM
.globl asm_x64_jit_SBC_IMM_END
asm_x64_jit_SBC_IMM:
  # NOTE: Intel has subtract with borrow, not subtract with carry, so
  # appropriate carry flag management also needs to be used.
  sbb REG_6502_A, 0

asm_x64_jit_SBC_IMM_END:
  ret


.globl asm_x64_jit_SBC_SCRATCH
.globl asm_x64_jit_SBC_SCRATCH_END
asm_x64_jit_SBC_SCRATCH:
  sbb REG_6502_A, [REG_SCRATCH1 + K_BBC_MEM_READ_IND_ADDR]

asm_x64_jit_SBC_SCRATCH_END:
  ret


.globl asm_x64_jit_SBC_SCRATCH_Y
.globl asm_x64_jit_SBC_SCRATCH_Y_END
asm_x64_jit_SBC_SCRATCH_Y:
  sbb REG_6502_A, \
      [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_READ_OFFSET]

asm_x64_jit_SBC_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_SHY_ABX
.globl asm_x64_jit_SHY_ABX_byte_patch
.globl asm_x64_jit_SHY_ABX_mov_patch
.globl asm_x64_jit_SHY_ABX_END
asm_x64_jit_SHY_ABX:
  lahf
  mov REG_SCRATCH1_8, REG_6502_Y
  and REG_SCRATCH1_8, 0xff
asm_x64_jit_SHY_ABX_byte_patch:
  sahf
  mov [REG_6502_X_64 + 0xFFFF], REG_SCRATCH1_8
asm_x64_jit_SHY_ABX_mov_patch:

asm_x64_jit_SHY_ABX_END:
  ret


.globl asm_x64_jit_SLO_ABS
.globl asm_x64_jit_SLO_ABS_mov1_patch
.globl asm_x64_jit_SLO_ABS_mov2_patch
.globl asm_x64_jit_SLO_ABS_END
asm_x64_jit_SLO_ABS:
  mov REG_SCRATCH1_8, [0x7fffffff]
asm_x64_jit_SLO_ABS_mov1_patch:
  movzx REG_SCRATCH2_32, REG_SCRATCH1_8
  shl REG_SCRATCH1_8, 1
  mov [0x7fffffff], REG_SCRATCH1_8
asm_x64_jit_SLO_ABS_mov2_patch:
  or REG_6502_A, REG_SCRATCH1_8
  bt REG_SCRATCH2_32, 7

asm_x64_jit_SLO_ABS_END:
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


.globl asm_x64_jit_STA_ABY
.globl asm_x64_jit_STA_ABY_END
asm_x64_jit_STA_ABY:
  mov [REG_6502_Y_64 + 0xFFFF], REG_6502_A

asm_x64_jit_STA_ABY_END:
  ret


.globl asm_x64_jit_STA_SCRATCH
.globl asm_x64_jit_STA_SCRATCH_END
asm_x64_jit_STA_SCRATCH:
  mov [REG_SCRATCH1 + 0x7fffffff], REG_6502_A

asm_x64_jit_STA_SCRATCH_END:
  ret


.globl asm_x64_jit_STA_SCRATCH_Y
.globl asm_x64_jit_STA_SCRATCH_Y_END
asm_x64_jit_STA_SCRATCH_Y:
  mov [REG_SCRATCH1 + REG_6502_Y_64 + K_BBC_MEM_READ_TO_IND_WRITE_OFFSET], \
      REG_6502_A

asm_x64_jit_STA_SCRATCH_Y_END:
  ret


.globl asm_x64_jit_STX_ABS
.globl asm_x64_jit_STX_ABS_END
asm_x64_jit_STX_ABS:
  mov [0x7fffffff], REG_6502_X

asm_x64_jit_STX_ABS_END:
  ret


.globl asm_x64_jit_STX_scratch
.globl asm_x64_jit_STX_scratch_END
asm_x64_jit_STX_scratch:
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_6502_X

asm_x64_jit_STX_scratch_END:
  ret


.globl asm_x64_jit_STY_ABS
.globl asm_x64_jit_STY_ABS_END
asm_x64_jit_STY_ABS:
  mov [0x7fffffff], REG_6502_Y

asm_x64_jit_STY_ABS_END:
  ret


.globl asm_x64_jit_STY_scratch
.globl asm_x64_jit_STY_scratch_END
asm_x64_jit_STY_scratch:
  mov [REG_SCRATCH1 + K_BBC_MEM_WRITE_ADDR], REG_6502_Y

asm_x64_jit_STY_scratch_END:
  ret
