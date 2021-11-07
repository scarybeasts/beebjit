#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "../asm_jit_defs.h"
#include "asm_calling_convention_x64.h"
#include "asm_defs_registers_x64.h"

.file "asm/x64/asm_jit_x64.S"
.intel_syntax noprefix
.section rodata
.text


.globl asm_jit_enter
.globl asm_jit_enter_END
asm_jit_enter:
  jmp asm_enter_common

asm_jit_enter_END:
  ret


.globl asm_jit_compile_trampoline
asm_jit_compile_trampoline:
  # At this point: stack is aligned to 8 bytes.
  # This is because the JIT engine gets here via call [rdi].
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  # Trashes REG_SCRATCH1, REG_SCRATCH3
  # Preserves RFLAGS
  call asm_save_AXYS_PC_flags

  # param1: context object.
  mov REG_PARAM1, REG_CONTEXT
  # param2: x64 rip that called here.
  mov REG_PARAM2, [rsp]
  # The actual instruction address of interest is the call itself, so
  # subtract two to get that.
  lea REG_PARAM2_32, [REG_PARAM2 - 2]
  # param3: countdown
  mov REG_PARAM3, REG_COUNTDOWN
  # param4: Intel rflags
  # Needed because carry / overflow flags state could be here in certain
  # optimization conditions.
  pushfq
  pop REG_PARAM4

  # One push prior to the call.
  # That takes us back to 16 bytes stack alignment.
  push REG_CONTEXT
  # Win x64 shadow space convention.
  sub rsp, 32
  call [REG_CONTEXT + K_JIT_CONTEXT_OFFSET_JIT_CALLBACK]
  add rsp, 32
  pop REG_CONTEXT

  mov REG_COUNTDOWN, REG_RETURN

  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_restore_AXYS_PC_flags

  lahf
  shl REG_6502_PC, K_JIT_BYTES_SHIFT
  sahf
  lea REG_6502_PC, [REG_6502_PC + K_JIT_ADDR]

  # We're jumping out of a call so pop the return address.
  pop REG_SCRATCH2

  jmp REG_6502_PC


.globl asm_jit_interp
asm_jit_interp:
  # At this point: stack is aligned to 16 bytes.
  # This is because the JIT engine gets here via jmp.
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  # Trashes REG_SCRATCH1, REG_SCRATCH3
  # Preserves RFLAGS
  call asm_save_AXYS_PC_flags

  # Save REG_CONTEXT because it's currently the same as REG_PARAM1 in the
  # AMD64 calling convention, which is overwritten below.
  # Use REG_6502_A_64 because that won't be overwritten as a parameter in either
  # AMD64 or Win x64 calling convention.
  mov REG_6502_A_64, REG_CONTEXT

  # Double push to keep stack aligned to 16 bytes before the call.
  push REG_CONTEXT
  push REG_CONTEXT
  # param1 is interp object.
  mov REG_PARAM1, [REG_CONTEXT + K_CONTEXT_OFFSET_INTERP_OBJECT]
  # param2 is stack storage for 2x int64 return values.
  lea REG_PARAM2, [rsp - 16]
  # param3 is current countdown value.
  mov REG_PARAM3, REG_COUNTDOWN
  # param4: Intel rflags
  # Needed because carry / overflow flags state could be here in certain
  # optimization conditions.
  pushfq
  pop REG_PARAM4

  # Return value space and Win x64 shadow space convention.
  sub rsp, 16 + 32
  call [REG_6502_A_64 + K_CONTEXT_OFFSET_INTERP_CALLBACK]
  add rsp, 16 + 32
  mov REG_COUNTDOWN, [rsp - 16]
  mov REG_RETURN, [rsp - 8]
  pop REG_CONTEXT
  pop REG_CONTEXT

  test REG_RETURN, REG_RETURN
  je not_exiting
  ret

not_exiting:
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  call asm_restore_AXYS_PC_flags

  lahf
  shl REG_6502_PC, K_JIT_BYTES_SHIFT
  sahf
  lea REG_6502_PC, [REG_6502_PC + K_JIT_ADDR]

  jmp REG_6502_PC


.globl asm_jit_jump_interp_trampoline
.globl asm_jit_jump_interp_trampoline_pc_patch
.globl asm_jit_jump_interp_trampoline_jump_patch
.globl asm_jit_jump_interp_trampoline_END
asm_jit_jump_interp_trampoline:
  mov REG_6502_PC_32, 0x7fffffff
asm_jit_jump_interp_trampoline_pc_patch:
  jmp asm_unpatched_branch_target
asm_jit_jump_interp_trampoline_jump_patch:

asm_jit_jump_interp_trampoline_END:
  ret


.globl asm_jit_check_countdown
.globl asm_jit_check_countdown_count_patch
.globl asm_jit_check_countdown_jump_patch
.globl asm_jit_check_countdown_END
asm_jit_check_countdown:
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 0x80000000]
asm_jit_check_countdown_count_patch:
  bt REG_COUNTDOWN, 63
  jb asm_unpatched_branch_target
asm_jit_check_countdown_jump_patch:

asm_jit_check_countdown_END:
  ret


.globl asm_jit_check_countdown_8bit
.globl asm_jit_check_countdown_8bit_count_patch
.globl asm_jit_check_countdown_8bit_jump_patch
.globl asm_jit_check_countdown_8bit_END
asm_jit_check_countdown_8bit:
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 0x80]
asm_jit_check_countdown_8bit_count_patch:
  bt REG_COUNTDOWN, 63
  jb asm_unpatched_branch_target
asm_jit_check_countdown_8bit_jump_patch:

asm_jit_check_countdown_8bit_END:
  ret


.globl asm_jit_countdown_add
.globl asm_jit_countdown_add_END
asm_jit_countdown_add:
  lea REG_COUNTDOWN, [REG_COUNTDOWN + 0x7f]

asm_jit_countdown_add_END:
  ret


.globl asm_jit_call_debug
.globl asm_jit_call_debug_pc_patch
.globl asm_jit_call_debug_call_patch
.globl asm_jit_call_debug_END
asm_jit_call_debug:
  mov REG_6502_PC_32, 0x7fffffff
asm_jit_call_debug_pc_patch:
  # Some optimizations cache values across opcodes in REG_ADDR or host flags.
  # So they must be saved.
  pushfq
  push REG_ADDR
  call asm_unpatched_branch_target
asm_jit_call_debug_call_patch:
  pop REG_ADDR
  popf

asm_jit_call_debug_END:
  ret


.globl asm_jit_jump_interp
.globl asm_jit_jump_interp_pc_patch
.globl asm_jit_jump_interp_jump_patch
.globl asm_jit_jump_interp_END
asm_jit_jump_interp:
  mov REG_6502_PC_32, 0x7fffffff
asm_jit_jump_interp_pc_patch:
  jmp asm_unpatched_branch_target
asm_jit_jump_interp_jump_patch:

asm_jit_jump_interp_END:
  ret


.globl asm_jit_call_inturbo
.globl asm_jit_call_inturbo_pc_patch
.globl asm_jit_call_inturbo_END
asm_jit_call_inturbo:
  mov REG_6502_PC_32, 0x7fffffff
asm_jit_call_inturbo_pc_patch:
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC]
  lahf
  shl REG_SCRATCH1_32, K_INTURBO_OPCODE_SHIFT
  sahf
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_INTURBO_ADDR]
  # Save JIT REG_CONTEXT.
  # Keeps stack alignment to 16 after the call.
  push REG_CONTEXT
  # Swith REG_CONTEXT to inturbo one.
  mov REG_CONTEXT, [REG_CONTEXT + K_JIT_CONTEXT_OFFSET_INTURBO]
  call REG_SCRATCH1
  pop REG_CONTEXT
  lahf
  lea REG_6502_PC_32, [REG_6502_PC - K_BBC_MEM_READ_FULL_ADDR]
  shl REG_6502_PC_32, K_JIT_BYTES_SHIFT
  sahf
  lea REG_6502_PC_32, [REG_6502_PC + K_JIT_ADDR]

  jmp REG_6502_PC

asm_jit_call_inturbo_END:
  ret


asm_jit_for_testing_END:


.globl asm_jit_value_load
.globl asm_jit_value_load_END
asm_jit_value_load:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_value_load_END:
  ret


.globl asm_jit_value_store
.globl asm_jit_value_store_END
asm_jit_value_store:
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_SCRATCH2_8

asm_jit_value_store_END:
  ret


.globl asm_jit_carry_invert
.globl asm_jit_carry_invert_END
asm_jit_carry_invert:
  cmc

asm_jit_carry_invert_END:
  ret


.globl asm_jit_check_bcd
.globl asm_jit_check_bcd_END
asm_jit_check_bcd:
  # This is fault fixup, i.e. it faults if the 6502 D flag is set.
  # You would expect movzx into a 32-bit register here to be faster due to
  # eliminated dependencies but it is about 10% slower on one of the CLOCKSP
  # microbenchmarks. Also, using REG_SCRATCH3_8 seems a little faster than
  # REG_SCRATCH2_8 :shrug:.
  mov REG_SCRATCH3_8, \
      [REG_6502_ID_F_64 + K_BBC_MEM_READ_FULL_ADDR + K_6502_ADDR_SPACE_SIZE - 6]

asm_jit_check_bcd_END:
  ret


.globl asm_jit_check_page_crossing_ABX
.globl asm_jit_check_page_crossing_ABX_END
asm_jit_check_page_crossing_ABX:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_X_64 + 0x7fffffff]

asm_jit_check_page_crossing_ABX_END:
  ret


.globl asm_jit_check_page_crossing_ABY
.globl asm_jit_check_page_crossing_ABY_END
asm_jit_check_page_crossing_ABY:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_Y_64 + 0x7fffffff]

asm_jit_check_page_crossing_ABY_END:
  ret


.globl asm_jit_check_page_crossing_adjust
.globl asm_jit_check_page_crossing_adjust_END
asm_jit_check_page_crossing_adjust:
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH2]

asm_jit_check_page_crossing_adjust_END:
  ret


.globl asm_jit_check_page_crossing_n
.globl asm_jit_check_page_crossing_n_END
asm_jit_check_page_crossing_n:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH2 + 0x7fffffff]

asm_jit_check_page_crossing_n_END:
  ret


.globl asm_jit_check_page_crossing_x
.globl asm_jit_check_page_crossing_x_END
asm_jit_check_page_crossing_x:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_X_64 + \
                                   REG_SCRATCH2 + \
                                   K_ASM_TABLE_PAGE_WRAP_CYCLE_INV]

asm_jit_check_page_crossing_x_END:
  ret


.globl asm_jit_check_page_crossing_y
.globl asm_jit_check_page_crossing_y_END
asm_jit_check_page_crossing_y:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_Y_64 + \
                                   REG_SCRATCH2 + \
                                   K_ASM_TABLE_PAGE_WRAP_CYCLE_INV]

asm_jit_check_page_crossing_y_END:
  ret


.globl asm_jit_CHECK_PENDING_IRQ
.globl asm_jit_CHECK_PENDING_IRQ_jump_patch
.globl asm_jit_CHECK_PENDING_IRQ_END
asm_jit_CHECK_PENDING_IRQ:
  # TODO: could use a register for IRQ_FIRE, if CLI / PLP is common?
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  mov REG_SCRATCH2_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_IRQ_FIRE]
  lea REG_SCRATCH2_32, [REG_SCRATCH2 - 1]
  bt REG_SCRATCH2_32, 31
  jae asm_unpatched_branch_target
asm_jit_CHECK_PENDING_IRQ_jump_patch:

asm_jit_CHECK_PENDING_IRQ_END:
  ret


.globl asm_jit_flags_nz_mem_ABS
.globl asm_jit_flags_nz_mem_ABS_END
asm_jit_flags_nz_mem_ABS:
  test BYTE PTR [REG_MEM + 0x7fffffff], 0xff

asm_jit_flags_nz_mem_ABS_END:
  ret


.globl asm_jit_flags_nz_mem_ZPG
.globl asm_jit_flags_nz_mem_ZPG_END
asm_jit_flags_nz_mem_ZPG:
  test BYTE PTR [REG_MEM + 0x7f], 0xff

asm_jit_flags_nz_mem_ZPG_END:
  ret


.globl asm_jit_flags_nz_value
.globl asm_jit_flags_nz_value_END
asm_jit_flags_nz_value:
  test REG_SCRATCH2_8, REG_SCRATCH2_8

asm_jit_flags_nz_value_END:
  ret


.globl asm_jit_JMP_SCRATCH_n
.globl asm_jit_JMP_SCRATCH_n_lea_patch
.globl asm_jit_JMP_SCRATCH_n_END
asm_jit_JMP_SCRATCH_n:
  # TODO: the rorx is quite a bit faster, at least in microbenchmarks.
  #rorx REG_SCRATCH1_32, REG_SCRATCH1_32, (32 - K_JIT_BYTES_SHIFT)
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + 0x7fffffff]
asm_jit_JMP_SCRATCH_n_lea_patch:
  lahf
  shl REG_SCRATCH1_32, K_JIT_BYTES_SHIFT
  sahf
  jmp REG_SCRATCH1

asm_jit_JMP_SCRATCH_n_END:
  ret


.globl asm_jit_load_carry_for_branch
.globl asm_jit_load_carry_for_branch_END
asm_jit_load_carry_for_branch:
  bt REG_6502_CF_64, 0

asm_jit_load_carry_for_branch_END:
  ret


.globl asm_jit_load_carry_for_calc
.globl asm_jit_load_carry_for_calc_END
asm_jit_load_carry_for_calc:
  shr REG_6502_CF, 1

asm_jit_load_carry_for_calc_END:
  ret


.globl asm_jit_load_carry_inv_for_calc
.globl asm_jit_load_carry_inv_for_calc_END
asm_jit_load_carry_inv_for_calc:
  cmp REG_6502_CF, 1

asm_jit_load_carry_inv_for_calc_END:
  ret


.globl asm_jit_load_overflow
.globl asm_jit_load_overflow_END
asm_jit_load_overflow:
  bt REG_6502_OF_64, 0

asm_jit_load_overflow_END:
  ret


.globl asm_jit_mode_ABX
.globl asm_jit_mode_ABX_END
asm_jit_mode_ABX:
  lea REG_ADDR_32, [REG_6502_X_64 + 0x7fffffff]

asm_jit_mode_ABX_END:
  ret


.globl asm_jit_mode_abx_and_load
.globl asm_jit_mode_abx_and_load_END
asm_jit_mode_abx_and_load:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_X_64 + 0x7fffffff]

asm_jit_mode_abx_and_load_END:
  ret


.globl asm_jit_mode_abx_store
.globl asm_jit_mode_abx_store_END
asm_jit_mode_abx_store:
  mov [REG_6502_X_64 + 0x7fffffff], REG_SCRATCH2_8

asm_jit_mode_abx_store_END:
  ret


.globl asm_jit_mode_ABY
.globl asm_jit_mode_ABY_END
asm_jit_mode_ABY:
  lea REG_ADDR_32, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_mode_ABY_END:
  ret


.globl asm_jit_mode_IDY_load_mov1
.globl asm_jit_mode_IDY_load_mov1_END
.globl asm_jit_mode_IDY_load_mov2
.globl asm_jit_mode_IDY_load_mov2_END
asm_jit_mode_IDY_load_mov1:
  # Deliberately done as 2x 8 byte loads, not 1x 16.
  # It's faster, possibly store-to-load forwarding fails?
  movzx REG_ADDR_32, BYTE PTR [REG_MEM + 0x7f]
asm_jit_mode_IDY_load_mov1_END:
  ret

asm_jit_mode_IDY_load_mov2:
  mov REG_ADDR_8_HI, [REG_MEM + 0x7f]

asm_jit_mode_IDY_load_mov2_END:
  ret


.globl asm_jit_mode_IND_mov1
.globl asm_jit_mode_IND_mov1_END
.globl asm_jit_mode_IND_mov2
.globl asm_jit_mode_IND_mov2_END
asm_jit_mode_IND_mov1:
  # Deliberately done as 2x 8 byte loads, not 1x 16.
  # It's faster, possibly store-to-load forwarding fails?
  movzx REG_ADDR_32, BYTE PTR [REG_MEM + 0x7fffffff]
asm_jit_mode_IND_mov1_END:
  ret

asm_jit_mode_IND_mov2:
  mov REG_ADDR_8_HI, [REG_MEM + 0x7fffffff]

asm_jit_mode_IND_mov2_END:
  ret


.globl asm_jit_addr_add_x
.globl asm_jit_addr_add_x_END
asm_jit_addr_add_x:
  lea REG_ADDR_32, [REG_ADDR + REG_6502_X_64]

asm_jit_addr_add_x_END:
  ret


.globl asm_jit_addr_add_y
.globl asm_jit_addr_add_y_END
asm_jit_addr_add_y:
  lea REG_ADDR_32, [REG_ADDR + REG_6502_Y_64]

asm_jit_addr_add_y_END:
  ret


.globl asm_jit_addr_load_16bit_wrap
.globl asm_jit_addr_load_16bit_wrap_END
asm_jit_addr_load_16bit_wrap:
  lea REG_SCRATCH2_32, [REG_ADDR + 1]
  movzx REG_SCRATCH2_32, REG_SCRATCH2_8
  mov REG_ADDR_8, [REG_ADDR + REG_MEM - REG_MEM_OFFSET]
  mov REG_ADDR_8_HI, [REG_SCRATCH2 + REG_MEM - REG_MEM_OFFSET]

asm_jit_addr_load_16bit_wrap_END:
  ret


.globl asm_jit_MODE_ZPX
.globl asm_jit_MODE_ZPX_lea_patch
.globl asm_jit_MODE_ZPX_END
asm_jit_MODE_ZPX:
  lea REG_ADDR_32, [REG_6502_X_64 + 0x7fffffff]
asm_jit_MODE_ZPX_lea_patch:
  movzx REG_ADDR_32, REG_ADDR_8

asm_jit_MODE_ZPX_END:
  ret


.globl asm_jit_MODE_ZPX_8bit
.globl asm_jit_MODE_ZPX_8bit_lea_patch
.globl asm_jit_MODE_ZPX_8bit_END
asm_jit_MODE_ZPX_8bit:
  lea REG_ADDR_32, [REG_6502_X_64 + 0x7f]
asm_jit_MODE_ZPX_8bit_lea_patch:
  movzx REG_ADDR_32, REG_ADDR_8

asm_jit_MODE_ZPX_8bit_END:
  ret


.globl asm_jit_MODE_ZPY
.globl asm_jit_MODE_ZPY_lea_patch
.globl asm_jit_MODE_ZPY_END
asm_jit_MODE_ZPY:
  lea REG_ADDR_32, [REG_6502_Y_64 + 0x7fffffff]
asm_jit_MODE_ZPY_lea_patch:
  movzx REG_ADDR_32, REG_ADDR_8

asm_jit_MODE_ZPY_END:
  ret


.globl asm_jit_MODE_ZPY_8bit
.globl asm_jit_MODE_ZPY_8bit_lea_patch
.globl asm_jit_MODE_ZPY_8bit_END
asm_jit_MODE_ZPY_8bit:
  lea REG_ADDR_32, [REG_6502_Y_64 + 0x7f]
asm_jit_MODE_ZPY_8bit_lea_patch:
  movzx REG_ADDR_32, REG_ADDR_8

asm_jit_MODE_ZPY_8bit_END:
  ret


.globl asm_jit_PULL_16
.globl asm_jit_PULL_16_END
asm_jit_PULL_16:
  # Magic constant to cause a fault + fixup when incrementing stack by 2 would
  # overflow.
  mov REG_SCRATCH3_8, [REG_6502_S_64 + K_BBC_MEM_OFFSET_TO_READ_FULL + 0xfe02]
  movzx REG_SCRATCH1_32, WORD PTR [REG_6502_S_64 + 1]
  lea REG_6502_S_32, [REG_6502_S_64 + 2]

asm_jit_PULL_16_END:
  ret


.globl asm_jit_PUSH_16
.globl asm_jit_PUSH_16_word_patch
.globl asm_jit_PUSH_16_END
asm_jit_PUSH_16:
  # Magic constant to cause a fault + fixup when decrementing stack by 2 would
  # overflow.
  # Seems to be faster without movzx, as per other similar cases, which is
  # strange.
  mov REG_SCRATCH3_8, [REG_6502_S_64 + K_BBC_MEM_OFFSET_TO_READ_FULL - 0x102]
  mov WORD PTR [REG_6502_S_64 - 1], 0xffff
asm_jit_PUSH_16_word_patch:
  lea REG_6502_S_32, [REG_6502_S_64 - 2]

asm_jit_PUSH_16_END:
  ret


.globl asm_jit_save_addr_low_byte
.globl asm_jit_save_addr_low_byte_END
asm_jit_save_addr_low_byte:
  movzx REG_SCRATCH2_32, REG_ADDR_8

asm_jit_save_addr_low_byte_END:
  ret


.globl asm_jit_save_carry
.globl asm_jit_save_carry_END
asm_jit_save_carry:
  setb REG_6502_CF

asm_jit_save_carry_END:
  ret


.globl asm_jit_save_carry_inv
.globl asm_jit_save_carry_inv_END
asm_jit_save_carry_inv:
  setae REG_6502_CF

asm_jit_save_carry_inv_END:
  ret


.globl asm_jit_save_overflow
.globl asm_jit_save_overflow_END
asm_jit_save_overflow:
  seto REG_6502_OF

asm_jit_save_overflow_END:
  ret


.globl asm_jit_store_ABS
.globl asm_jit_store_ABS_END
asm_jit_store_ABS:
  mov [REG_MEM + 0x7fffffff], REG_SCRATCH2_8

asm_jit_store_ABS_END:
  ret


.globl asm_jit_store_ZPG
.globl asm_jit_store_ZPG_END
asm_jit_store_ZPG:
  mov [REG_MEM + 0x7f], REG_SCRATCH2_8

asm_jit_store_ZPG_END:
  ret


.globl asm_jit_write_inv_commit
.globl asm_jit_write_inv_commit_END
asm_jit_write_inv_commit:
  mov WORD PTR [REG_SCRATCH2], 0x17ff

asm_jit_write_inv_commit_END:
  ret


.globl asm_jit_write_inv
.globl asm_jit_write_inv_END
asm_jit_write_inv:
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_ADDR * 4]

asm_jit_write_inv_END:
  ret


.globl asm_jit_write_inv_ABS
.globl asm_jit_write_inv_ABS_END
asm_jit_write_inv_ABS:
  mov REG_SCRATCH2_32, [REG_CONTEXT + 0x7fffffff]

asm_jit_write_inv_ABS_END:
  ret


.globl asm_jit_write_inv_IDY
.globl asm_jit_write_inv_IDY_END
asm_jit_write_inv_IDY:
  lea REG_SCRATCH2_32, [REG_ADDR + REG_6502_Y_64]
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_SCRATCH2 * 4]

asm_jit_write_inv_IDY_END:
  ret


.globl asm_jit_write_inv_IDY_n
.globl asm_jit_write_inv_IDY_n_END
asm_jit_write_inv_IDY_n:
  # The contant encodes both K_JIT_CONTEXT_OFFSET_JIT_PTRS and the "n".
  mov REG_SCRATCH2_32, [REG_CONTEXT + 0x7fffffff + REG_ADDR * 4]

asm_jit_write_inv_IDY_n_END:
  ret


.globl asm_jit_write_inv_IDY_X
.globl asm_jit_write_inv_IDY_X_END
asm_jit_write_inv_IDY_X:
  lea REG_SCRATCH2_32, [REG_ADDR + REG_6502_X_64]
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_SCRATCH2 * 4]

asm_jit_write_inv_IDY_X_END:
  ret


.globl asm_jit_ADC_ABS
.globl asm_jit_ADC_ABS_END
asm_jit_ADC_ABS:
  adc REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_ADC_ABS_END:
  ret


.globl asm_jit_ADC_ABX
.globl asm_jit_ADC_ABX_END
asm_jit_ADC_ABX:
  adc REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_ADC_ABX_END:
  ret


.globl asm_jit_ADC_ABY
.globl asm_jit_ADC_ABY_END
asm_jit_ADC_ABY:
  adc REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_ADC_ABY_END:
  ret


.globl asm_jit_ADC_addr
.globl asm_jit_ADC_addr_END
asm_jit_ADC_addr:
  adc REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ADC_addr_END:
  ret


.globl asm_jit_ADC_addr_n
.globl asm_jit_ADC_addr_n_END
asm_jit_ADC_addr_n:
  adc REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_ADC_addr_n_END:
  ret


.globl asm_jit_ADC_addr_X
.globl asm_jit_ADC_addr_X_END
asm_jit_ADC_addr_X:
  adc REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ADC_addr_X_END:
  ret


.globl asm_jit_ADC_addr_Y
.globl asm_jit_ADC_addr_Y_END
asm_jit_ADC_addr_Y:
  adc REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ADC_addr_Y_END:
  ret


.globl asm_jit_ADC_IMM
.globl asm_jit_ADC_IMM_END
asm_jit_ADC_IMM:
  adc REG_6502_A, 0

asm_jit_ADC_IMM_END:
  ret


.globl asm_jit_ADC_ZPG
.globl asm_jit_ADC_ZPG_END
asm_jit_ADC_ZPG:
  adc REG_6502_A, [REG_MEM + 0x7f]

asm_jit_ADC_ZPG_END:
  ret


.globl asm_jit_ADD_ABS
.globl asm_jit_ADD_ABS_END
asm_jit_ADD_ABS:
  add REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_ADD_ABS_END:
  ret


.globl asm_jit_ADD_ABX
.globl asm_jit_ADD_ABX_END
asm_jit_ADD_ABX:
  add REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_ADD_ABX_END:
  ret


.globl asm_jit_ADD_ABY
.globl asm_jit_ADD_ABY_END
asm_jit_ADD_ABY:
  add REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_ADD_ABY_END:
  ret


.globl asm_jit_ADD_addr
.globl asm_jit_ADD_addr_END
asm_jit_ADD_addr:
  add REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ADD_addr_END:
  ret


.globl asm_jit_ADD_addr_n
.globl asm_jit_ADD_addr_n_END
asm_jit_ADD_addr_n:
  add REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_ADD_addr_n_END:
  ret


.globl asm_jit_ADD_addr_X
.globl asm_jit_ADD_addr_X_END
asm_jit_ADD_addr_X:
  add REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ADD_addr_X_END:
  ret


.globl asm_jit_ADD_addr_Y
.globl asm_jit_ADD_addr_Y_END
asm_jit_ADD_addr_Y:
  add REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ADD_addr_Y_END:
  ret


.globl asm_jit_ADD_IMM
.globl asm_jit_ADD_IMM_END
asm_jit_ADD_IMM:
  add REG_6502_A, 0

asm_jit_ADD_IMM_END:
  ret


.globl asm_jit_ADD_ZPG
.globl asm_jit_ADD_ZPG_END
asm_jit_ADD_ZPG:
  add REG_6502_A, [REG_MEM + 0x7f]

asm_jit_ADD_ZPG_END:
  ret


.globl asm_jit_ALR_IMM_and
.globl asm_jit_ALR_IMM_and_END
.globl asm_jit_ALR_IMM_shr
.globl asm_jit_ALR_IMM_shr_END
asm_jit_ALR_IMM_and:
  and REG_6502_A, 0xff

asm_jit_ALR_IMM_and_END:
  ret

asm_jit_ALR_IMM_shr:
  shr REG_6502_A, 1

asm_jit_ALR_IMM_shr_END:
  ret


.globl asm_jit_AND_ABS
.globl asm_jit_AND_ABS_END
asm_jit_AND_ABS:
  and REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_AND_ABS_END:
  ret


.globl asm_jit_AND_ABX
.globl asm_jit_AND_ABX_END
asm_jit_AND_ABX:
  and REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_AND_ABX_END:
  ret


.globl asm_jit_AND_ABY
.globl asm_jit_AND_ABY_END
asm_jit_AND_ABY:
  and REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_AND_ABY_END:
  ret


.globl asm_jit_AND_addr
.globl asm_jit_AND_addr_END
asm_jit_AND_addr:
  and REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_AND_addr_END:
  ret


.globl asm_jit_AND_addr_n
.globl asm_jit_AND_addr_n_END
asm_jit_AND_addr_n:
  and REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_AND_addr_n_END:
  ret


.globl asm_jit_AND_addr_X
.globl asm_jit_AND_addr_X_END
asm_jit_AND_addr_X:
  and REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_AND_addr_X_END:
  ret


.globl asm_jit_AND_addr_Y
.globl asm_jit_AND_addr_Y_END
asm_jit_AND_addr_Y:
  and REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_AND_addr_Y_END:
  ret


.globl asm_jit_AND_IMM
.globl asm_jit_AND_IMM_END
asm_jit_AND_IMM:
  and REG_6502_A, 0

asm_jit_AND_IMM_END:
  ret


.globl asm_jit_AND_ZPG
.globl asm_jit_AND_ZPG_END
asm_jit_AND_ZPG:
  and REG_6502_A, [REG_MEM + 0x7f]

asm_jit_AND_ZPG_END:
  ret


.globl asm_jit_ASL_ABS
.globl asm_jit_ASL_ABS_END
asm_jit_ASL_ABS:
  shl BYTE PTR [REG_MEM + 0x7fffffff], 1

asm_jit_ASL_ABS_END:
  ret


.globl asm_jit_ASL_ACC
.globl asm_jit_ASL_ACC_END
asm_jit_ASL_ACC:
  shl REG_6502_A, 1

asm_jit_ASL_ACC_END:
  ret


.globl asm_jit_ASL_ACC_n
.globl asm_jit_ASL_ACC_n_END
asm_jit_ASL_ACC_n:
  shl REG_6502_A, 2

asm_jit_ASL_ACC_n_END:
  ret


.globl asm_jit_ASL_value
.globl asm_jit_ASL_value_END
asm_jit_ASL_value:
  shl REG_SCRATCH2_8, 1

asm_jit_ASL_value_END:
  ret


.globl asm_jit_ASL_ZPG
.globl asm_jit_ASL_ZPG_END
asm_jit_ASL_ZPG:
  shl BYTE PTR [REG_MEM + 0x7f], 1

asm_jit_ASL_ZPG_END:
  ret


.globl asm_jit_BCC
.globl asm_jit_BCC_END
asm_jit_BCC:
  jae asm_unpatched_branch_target

asm_jit_BCC_END:
  ret


.globl asm_jit_BCC_8bit
.globl asm_jit_BCC_8bit_END
asm_jit_BCC_8bit:
  jae asm_jit_BCC_8bit

asm_jit_BCC_8bit_END:
  ret


.globl asm_jit_BCS
.globl asm_jit_BCS_END
asm_jit_BCS:
  jb asm_unpatched_branch_target

asm_jit_BCS_END:
  ret


.globl asm_jit_BCS_8bit
.globl asm_jit_BCS_8bit_END
asm_jit_BCS_8bit:
  jb asm_jit_BCS_8bit

asm_jit_BCS_8bit_END:
  ret


.globl asm_jit_BEQ
.globl asm_jit_BEQ_END
asm_jit_BEQ:
  je asm_unpatched_branch_target

asm_jit_BEQ_END:
  ret


.globl asm_jit_BEQ_8bit
.globl asm_jit_BEQ_8bit_END
asm_jit_BEQ_8bit:
  je asm_jit_BEQ_8bit

asm_jit_BEQ_8bit_END:
  ret


.globl asm_jit_load_ABS
.globl asm_jit_load_ABS_END
asm_jit_load_ABS:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_MEM + 0x7fffffff]

asm_jit_load_ABS_END:
  ret


.globl asm_jit_load_ZPG
.globl asm_jit_load_ZPG_END
asm_jit_load_ZPG:
  movzx REG_SCRATCH2_32, BYTE PTR [REG_MEM + 0x7f]

asm_jit_load_ZPG_END:
  ret


.globl asm_jit_BMI
.globl asm_jit_BMI_END
asm_jit_BMI:
  js asm_unpatched_branch_target

asm_jit_BMI_END:
  ret


.globl asm_jit_BMI_8bit
.globl asm_jit_BMI_8bit_END
asm_jit_BMI_8bit:
  js asm_jit_BMI_8bit

asm_jit_BMI_8bit_END:
  ret


.globl asm_jit_BNE
.globl asm_jit_BNE_END
asm_jit_BNE:
  jne asm_unpatched_branch_target

asm_jit_BNE_END:
  ret


.globl asm_jit_BNE_8bit
.globl asm_jit_BNE_8bit_END
asm_jit_BNE_8bit:
  jne asm_jit_BNE_8bit

asm_jit_BNE_8bit_END:
  ret


.globl asm_jit_BPL
.globl asm_jit_BPL_END
asm_jit_BPL:
  jns asm_unpatched_branch_target

asm_jit_BPL_END:
  ret


.globl asm_jit_BPL_8bit
.globl asm_jit_BPL_8bit_END
asm_jit_BPL_8bit:
  jns asm_jit_BPL_8bit

asm_jit_BPL_8bit_END:
  ret


.globl asm_jit_BVC
.globl asm_jit_BVC_END
asm_jit_BVC:
  jae asm_unpatched_branch_target

asm_jit_BVC_END:
  ret


.globl asm_jit_BVC_8bit
.globl asm_jit_BVC_8bit_END
asm_jit_BVC_8bit:
  jae asm_jit_BVC_8bit

asm_jit_BVC_8bit_END:
  ret


.globl asm_jit_BVS
.globl asm_jit_BVS_END
asm_jit_BVS:
  jb asm_unpatched_branch_target

asm_jit_BVS_END:
  ret


.globl asm_jit_BVS_8bit
.globl asm_jit_BVS_8bit_END
asm_jit_BVS_8bit:
  jb asm_jit_BVS_8bit

asm_jit_BVS_8bit_END:
  ret


.globl asm_jit_CMP_ABS
.globl asm_jit_CMP_ABS_END
asm_jit_CMP_ABS:
  cmp REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_CMP_ABS_END:
  ret


.globl asm_jit_CMP_ABX
.globl asm_jit_CMP_ABX_END
asm_jit_CMP_ABX:
  cmp REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_CMP_ABX_END:
  ret


.globl asm_jit_CMP_ABY
.globl asm_jit_CMP_ABY_END
asm_jit_CMP_ABY:
  cmp REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_CMP_ABY_END:
  ret


.globl asm_jit_CMP_addr
.globl asm_jit_CMP_addr_END
asm_jit_CMP_addr:
  cmp REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_CMP_addr_END:
  ret


.globl asm_jit_CMP_addr_n
.globl asm_jit_CMP_addr_n_END
asm_jit_CMP_addr_n:
  cmp REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_CMP_addr_n_END:
  ret


.globl asm_jit_CMP_addr_X
.globl asm_jit_CMP_addr_X_END
asm_jit_CMP_addr_X:
  cmp REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_CMP_addr_X_END:
  ret


.globl asm_jit_CMP_addr_Y
.globl asm_jit_CMP_addr_Y_END
asm_jit_CMP_addr_Y:
  cmp REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_CMP_addr_Y_END:
  ret


.globl asm_jit_CMP_IMM
.globl asm_jit_CMP_IMM_END
asm_jit_CMP_IMM:
  cmp REG_6502_A, 0

asm_jit_CMP_IMM_END:
  ret


.globl asm_jit_CMP_ZPG
.globl asm_jit_CMP_ZPG_END
asm_jit_CMP_ZPG:
  cmp REG_6502_A, [REG_MEM + 0x7f]

asm_jit_CMP_ZPG_END:
  ret


.globl asm_jit_CPX_ABS
.globl asm_jit_CPX_ABS_END
asm_jit_CPX_ABS:
  cmp REG_6502_X, [REG_MEM + 0x7fffffff]

asm_jit_CPX_ABS_END:
  ret


.globl asm_jit_CPX_IMM
.globl asm_jit_CPX_IMM_END
asm_jit_CPX_IMM:
  cmp REG_6502_X, 0

asm_jit_CPX_IMM_END:
  ret


.globl asm_jit_CPX_ZPG
.globl asm_jit_CPX_ZPG_END
asm_jit_CPX_ZPG:
  cmp REG_6502_X, [REG_MEM + 0x7f]

asm_jit_CPX_ZPG_END:
  ret


.globl asm_jit_CPY_ABS
.globl asm_jit_CPY_ABS_END
asm_jit_CPY_ABS:
  cmp REG_6502_Y, [REG_MEM + 0x7fffffff]

asm_jit_CPY_ABS_END:
  ret


.globl asm_jit_CPY_IMM
.globl asm_jit_CPY_IMM_END
asm_jit_CPY_IMM:
  cmp REG_6502_Y, 0

asm_jit_CPY_IMM_END:
  ret


.globl asm_jit_CPY_ZPG
.globl asm_jit_CPY_ZPG_END
asm_jit_CPY_ZPG:
  cmp REG_6502_Y, [REG_MEM + 0x7f]

asm_jit_CPY_ZPG_END:
  ret


.globl asm_jit_DEC_ABS
.globl asm_jit_DEC_ABS_END
asm_jit_DEC_ABS:
  dec BYTE PTR [REG_MEM + 0x7fffffff]

asm_jit_DEC_ABS_END:
  ret


.globl asm_jit_DEC_value
.globl asm_jit_DEC_value_END
asm_jit_DEC_value:
  dec REG_SCRATCH2_8

asm_jit_DEC_value_END:
  ret


.globl asm_jit_DEC_ZPG
.globl asm_jit_DEC_ZPG_END
asm_jit_DEC_ZPG:
  dec BYTE PTR [REG_MEM + 0x7f]

asm_jit_DEC_ZPG_END:
  ret


.globl asm_jit_EOR_ABS
.globl asm_jit_EOR_ABS_END
asm_jit_EOR_ABS:
  xor REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_EOR_ABS_END:
  ret


.globl asm_jit_EOR_ABX
.globl asm_jit_EOR_ABX_END
asm_jit_EOR_ABX:
  xor REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_EOR_ABX_END:
  ret


.globl asm_jit_EOR_ABY
.globl asm_jit_EOR_ABY_END
asm_jit_EOR_ABY:
  xor REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_EOR_ABY_END:
  ret


.globl asm_jit_EOR_addr
.globl asm_jit_EOR_addr_END
asm_jit_EOR_addr:
  xor REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_EOR_addr_END:
  ret


.globl asm_jit_EOR_addr_n
.globl asm_jit_EOR_addr_n_END
asm_jit_EOR_addr_n:
  xor REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_EOR_addr_n_END:
  ret


.globl asm_jit_EOR_addr_X
.globl asm_jit_EOR_addr_X_END
asm_jit_EOR_addr_X:
  xor REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_EOR_addr_X_END:
  ret


.globl asm_jit_EOR_addr_Y
.globl asm_jit_EOR_addr_Y_END
asm_jit_EOR_addr_Y:
  xor REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_EOR_addr_Y_END:
  ret


.globl asm_jit_EOR_IMM
.globl asm_jit_EOR_IMM_END
asm_jit_EOR_IMM:
  xor REG_6502_A, 0

asm_jit_EOR_IMM_END:
  ret


.globl asm_jit_EOR_ZPG
.globl asm_jit_EOR_ZPG_END
asm_jit_EOR_ZPG:
  xor REG_6502_A, [REG_MEM + 0x7f]

asm_jit_EOR_ZPG_END:
  ret


.globl asm_jit_INC_ABS
.globl asm_jit_INC_ABS_END
asm_jit_INC_ABS:
  inc BYTE PTR [REG_MEM + 0x7fffffff]

asm_jit_INC_ABS_END:
  ret


.globl asm_jit_INC_addr_zpx
.globl asm_jit_INC_addr_zpx_END
asm_jit_INC_addr_zpx:
  # NOTE: only used for mode zpx so it's safe to assume RAM.
  # TODO: shorter encoding with REG_MEM?
  inc BYTE PTR [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_INC_addr_zpx_END:
  ret


.globl asm_jit_INC_value
.globl asm_jit_INC_value_END
asm_jit_INC_value:
  inc REG_SCRATCH2_8

asm_jit_INC_value_END:
  ret


.globl asm_jit_INC_ZPG
.globl asm_jit_INC_ZPG_END
asm_jit_INC_ZPG:
  inc BYTE PTR [REG_MEM + 0x7f]

asm_jit_INC_ZPG_END:
  ret


.globl asm_jit_JMP
.globl asm_jit_JMP_END
asm_jit_JMP:
  jmp asm_unpatched_branch_target

asm_jit_JMP_END:
  ret

.globl asm_jit_JMP_8bit
.globl asm_jit_JMP_8bit_END
asm_jit_JMP_8bit:
  jmp asm_jit_JMP_8bit

asm_jit_JMP_8bit_END:
  ret


.globl asm_jit_LDA_ABS
.globl asm_jit_LDA_ABS_END
asm_jit_LDA_ABS:
  movzx REG_6502_A_32, BYTE PTR [REG_MEM + 0x7fffffff]

asm_jit_LDA_ABS_END:
  ret


.globl asm_jit_LDA_addr
.globl asm_jit_LDA_addr_END
asm_jit_LDA_addr:
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_LDA_addr_END:
  ret


.globl asm_jit_LDA_addr_n
.globl asm_jit_LDA_addr_n_END
asm_jit_LDA_addr_n:
  # TODO: this is a 3-component lea. It's possible to fold REG_MEM and the
  # constant into one larger constant, resulting in a 2-component lea, but a
  # larger encoding. I don't really observe any timing difference in a
  # microbenchmark on my 11th gen Intel, but AMD is alleged to prefer
  # 2-component lea's so it may be worth revisiting in the future.
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + REG_MEM + 0x7f]

asm_jit_LDA_addr_n_END:
  ret


.globl asm_jit_LDA_addr_X
.globl asm_jit_LDA_addr_X_END
asm_jit_LDA_addr_X:
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + \
                                 REG_6502_X_64 + \
                                 K_BBC_MEM_READ_IND_ADDR]

asm_jit_LDA_addr_X_END:
  ret


.globl asm_jit_LDA_addr_Y
.globl asm_jit_LDA_addr_Y_END
asm_jit_LDA_addr_Y:
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + \
                                 REG_6502_Y_64 + \
                                 K_BBC_MEM_READ_IND_ADDR]

asm_jit_LDA_addr_Y_END:
  ret


.globl asm_jit_LDA_IMM
.globl asm_jit_LDA_IMM_END
asm_jit_LDA_IMM:
  mov REG_6502_A_32, 0x00000000

asm_jit_LDA_IMM_END:
  ret


.globl asm_jit_LDA_ABX
.globl asm_jit_LDA_ABX_END
asm_jit_LDA_ABX:
  movzx REG_6502_A_32, BYTE PTR [REG_6502_X_64 + 0x7fffffff]

asm_jit_LDA_ABX_END:
  ret


.globl asm_jit_LDA_ABY
.globl asm_jit_LDA_ABY_END
asm_jit_LDA_ABY:
  movzx REG_6502_A_32, BYTE PTR [REG_6502_Y_64 + 0x7fffffff]

asm_jit_LDA_ABY_END:
  ret


.globl asm_jit_LDA_zero
.globl asm_jit_LDA_zero_END
asm_jit_LDA_zero:
  xor REG_6502_A_32, REG_6502_A_32

asm_jit_LDA_zero_END:
  ret


.globl asm_jit_LDA_ZPG
.globl asm_jit_LDA_ZPG_END
asm_jit_LDA_ZPG:
  movzx REG_6502_A_32, BYTE PTR [REG_MEM + 0x7f]

asm_jit_LDA_ZPG_END:
  ret


.globl asm_jit_LDX_ABS
.globl asm_jit_LDX_ABS_END
asm_jit_LDX_ABS:
  mov REG_6502_X, [REG_MEM + 0x7fffffff]

asm_jit_LDX_ABS_END:
  ret


.globl asm_jit_LDX_ABY
.globl asm_jit_LDX_ABY_END
asm_jit_LDX_ABY:
  mov REG_6502_X, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_LDX_ABY_END:
  ret


.globl asm_jit_LDX_addr
.globl asm_jit_LDX_addr_END
asm_jit_LDX_addr:
  mov REG_6502_X, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_LDX_addr_END:
  ret


.globl asm_jit_LDX_addr_Y
.globl asm_jit_LDX_addr_Y_END
asm_jit_LDX_addr_Y:
  mov REG_6502_X, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_LDX_addr_Y_END:
  ret


.globl asm_jit_LDX_IMM
.globl asm_jit_LDX_IMM_END
asm_jit_LDX_IMM:
  mov REG_6502_X, 0

asm_jit_LDX_IMM_END:
  ret


.globl asm_jit_LDX_zero
.globl asm_jit_LDX_zero_END
asm_jit_LDX_zero:
  xor REG_6502_X_32, REG_6502_X_32

asm_jit_LDX_zero_END:
  ret


.globl asm_jit_LDX_ZPG
.globl asm_jit_LDX_ZPG_END
asm_jit_LDX_ZPG:
  mov REG_6502_X, [REG_MEM + 0x7f]

asm_jit_LDX_ZPG_END:
  ret


.globl asm_jit_LDY_ABS
.globl asm_jit_LDY_ABS_END
asm_jit_LDY_ABS:
  mov REG_6502_Y, [REG_MEM + 0x7fffffff]

asm_jit_LDY_ABS_END:
  ret


.globl asm_jit_LDY_ABX
.globl asm_jit_LDY_ABX_END
asm_jit_LDY_ABX:
  mov REG_6502_Y, [REG_6502_X_64 + 0x7fffffff]

asm_jit_LDY_ABX_END:
  ret


.globl asm_jit_LDY_addr
.globl asm_jit_LDY_addr_END
asm_jit_LDY_addr:
  mov REG_6502_Y, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_LDY_addr_END:
  ret


.globl asm_jit_LDY_addr_X
.globl asm_jit_LDY_addr_X_END
asm_jit_LDY_addr_X:
  mov REG_6502_Y, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_LDY_addr_X_END:
  ret


.globl asm_jit_LDY_IMM
.globl asm_jit_LDY_IMM_END
asm_jit_LDY_IMM:
  mov REG_6502_Y, 0

asm_jit_LDY_IMM_END:
  ret


.globl asm_jit_LDY_zero
.globl asm_jit_LDY_zero_END
asm_jit_LDY_zero:
  xor REG_6502_Y_32, REG_6502_Y_32

asm_jit_LDY_zero_END:
  ret


.globl asm_jit_LDY_ZPG
.globl asm_jit_LDY_ZPG_END
asm_jit_LDY_ZPG:
  mov REG_6502_Y, [REG_MEM + 0x7f]

asm_jit_LDY_ZPG_END:
  ret


.globl asm_jit_LSR_ABS
.globl asm_jit_LSR_ABS_END
asm_jit_LSR_ABS:
  shr BYTE PTR [REG_MEM + 0x7fffffff], 1

asm_jit_LSR_ABS_END:
  ret


.globl asm_jit_LSR_ACC
.globl asm_jit_LSR_ACC_END
asm_jit_LSR_ACC:
  shr REG_6502_A, 1

asm_jit_LSR_ACC_END:
  ret


.globl asm_jit_LSR_ACC_n
.globl asm_jit_LSR_ACC_n_END
asm_jit_LSR_ACC_n:
  shr REG_6502_A, 2

asm_jit_LSR_ACC_n_END:
  ret


.globl asm_jit_LSR_value
.globl asm_jit_LSR_value_END
asm_jit_LSR_value:
  shr REG_SCRATCH2_8, 1

asm_jit_LSR_value_END:
  ret


.globl asm_jit_LSR_ZPG
.globl asm_jit_LSR_ZPG_END
asm_jit_LSR_ZPG:
  shr BYTE PTR [REG_MEM + 0x7f], 1

asm_jit_LSR_ZPG_END:
  ret


.globl asm_jit_ORA_ABS
.globl asm_jit_ORA_ABS_END
asm_jit_ORA_ABS:
  or REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_ORA_ABS_END:
  ret


.globl asm_jit_ORA_ABX
.globl asm_jit_ORA_ABX_END
asm_jit_ORA_ABX:
  or REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_ORA_ABX_END:
  ret


.globl asm_jit_ORA_ABY
.globl asm_jit_ORA_ABY_END
asm_jit_ORA_ABY:
  or REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_ORA_ABY_END:
  ret


.globl asm_jit_ORA_addr
.globl asm_jit_ORA_addr_END
asm_jit_ORA_addr:
  or REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ORA_addr_END:
  ret


.globl asm_jit_ORA_addr_n
.globl asm_jit_ORA_addr_n_END
asm_jit_ORA_addr_n:
  or REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_ORA_addr_n_END:
  ret


.globl asm_jit_ORA_addr_X
.globl asm_jit_ORA_addr_X_END
asm_jit_ORA_addr_X:
  or REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ORA_addr_X_END:
  ret


.globl asm_jit_ORA_addr_Y
.globl asm_jit_ORA_addr_Y_END
asm_jit_ORA_addr_Y:
  or REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_ORA_addr_Y_END:
  ret


.globl asm_jit_ORA_IMM
.globl asm_jit_ORA_IMM_END
asm_jit_ORA_IMM:
  or REG_6502_A, 0

asm_jit_ORA_IMM_END:
  ret


.globl asm_jit_ORA_ZPG
.globl asm_jit_ORA_ZPG_END
asm_jit_ORA_ZPG:
  or REG_6502_A, [REG_MEM + 0x7f]

asm_jit_ORA_ZPG_END:
  ret


.globl asm_jit_ROL_ABS
.globl asm_jit_ROL_ABS_END
asm_jit_ROL_ABS:
  rcl BYTE PTR [REG_MEM + 0x7fffffff]

asm_jit_ROL_ABS_END:
  ret


.globl asm_jit_ROL_ACC
.globl asm_jit_ROL_ACC_END
asm_jit_ROL_ACC:
  rcl REG_6502_A, 1

asm_jit_ROL_ACC_END:
  ret


.globl asm_jit_ROL_ACC_n
.globl asm_jit_ROL_ACC_n_END
asm_jit_ROL_ACC_n:
  rcl REG_6502_A, 2

asm_jit_ROL_ACC_n_END:
  ret


.globl asm_jit_ROL_value
.globl asm_jit_ROL_value_END
asm_jit_ROL_value:
  rcl REG_SCRATCH2_8, 1

asm_jit_ROL_value_END:
  ret


.globl asm_jit_ROL_ZPG
.globl asm_jit_ROL_ZPG_END
asm_jit_ROL_ZPG:
  rcl BYTE PTR [REG_MEM + 0x7f]

asm_jit_ROL_ZPG_END:
  ret


.globl asm_jit_ROR_ABS
.globl asm_jit_ROR_ABS_END
asm_jit_ROR_ABS:
  rcr BYTE PTR [REG_MEM + 0x7fffffff]

asm_jit_ROR_ABS_END:
  ret


.globl asm_jit_ROR_ACC
.globl asm_jit_ROR_ACC_END
asm_jit_ROR_ACC:
  rcr REG_6502_A, 1

asm_jit_ROR_ACC_END:
  ret


.globl asm_jit_ROR_ACC_n
.globl asm_jit_ROR_ACC_n_END
asm_jit_ROR_ACC_n:
  rcr REG_6502_A, 2

asm_jit_ROR_ACC_n_END:
  ret


.globl asm_jit_ROR_value
.globl asm_jit_ROR_value_END
asm_jit_ROR_value:
  rcr REG_SCRATCH2_8, 1

asm_jit_ROR_value_END:
  ret


.globl asm_jit_ROR_ZPG
.globl asm_jit_ROR_ZPG_END
asm_jit_ROR_ZPG:
  rcr BYTE PTR [REG_MEM + 0x7f]

asm_jit_ROR_ZPG_END:
  ret


.globl asm_jit_SAX_ABS
.globl asm_jit_SAX_ABS_END
asm_jit_SAX_ABS:
  lahf
  mov REG_SCRATCH2_8, REG_6502_X
  and REG_SCRATCH2_8, REG_6502_A
  sahf
  mov [REG_MEM + 0x7fffffff], REG_SCRATCH2_8

asm_jit_SAX_ABS_END:
  ret


.globl asm_jit_SBC_ABS
.globl asm_jit_SBC_ABS_END
asm_jit_SBC_ABS:
  sbb REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_SBC_ABS_END:
  ret


.globl asm_jit_SBC_ABX
.globl asm_jit_SBC_ABX_END
asm_jit_SBC_ABX:
  sbb REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_SBC_ABX_END:
  ret


.globl asm_jit_SBC_ABY
.globl asm_jit_SBC_ABY_END
asm_jit_SBC_ABY:
  sbb REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_SBC_ABY_END:
  ret


.globl asm_jit_SBC_addr
.globl asm_jit_SBC_addr_END
asm_jit_SBC_addr:
  sbb REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_SBC_addr_END:
  ret


.globl asm_jit_SBC_addr_n
.globl asm_jit_SBC_addr_n_END
asm_jit_SBC_addr_n:
  sbb REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_SBC_addr_n_END:
  ret


.globl asm_jit_SBC_addr_X
.globl asm_jit_SBC_addr_X_END
asm_jit_SBC_addr_X:
  sbb REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_SBC_addr_X_END:
  ret


.globl asm_jit_SBC_addr_Y
.globl asm_jit_SBC_addr_Y_END
asm_jit_SBC_addr_Y:
  sbb REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_SBC_addr_Y_END:
  ret


.globl asm_jit_SBC_IMM
.globl asm_jit_SBC_IMM_END
asm_jit_SBC_IMM:
  # NOTE: Intel has subtract with borrow, not subtract with carry, so
  # appropriate carry flag management also needs to be used.
  sbb REG_6502_A, 0

asm_jit_SBC_IMM_END:
  ret


.globl asm_jit_SBC_ZPG
.globl asm_jit_SBC_ZPG_END
asm_jit_SBC_ZPG:
  sbb REG_6502_A, [REG_MEM + 0x7f]

asm_jit_SBC_ZPG_END:
  ret


.globl asm_jit_SLO_ABS
.globl asm_jit_SLO_ABS_mov1_patch
.globl asm_jit_SLO_ABS_mov2_patch
.globl asm_jit_SLO_ABS_END
asm_jit_SLO_ABS:
  mov REG_SCRATCH2_8, [REG_MEM + 0x7fffffff]
asm_jit_SLO_ABS_mov1_patch:
  mov REG_SCRATCH3, REG_SCRATCH2
  shl REG_SCRATCH2_8, 1
  mov [REG_MEM + 0x7fffffff], REG_SCRATCH2_8
asm_jit_SLO_ABS_mov2_patch:
  or REG_6502_A, REG_SCRATCH2_8
  bt REG_SCRATCH3_32, 7

asm_jit_SLO_ABS_END:
  ret


.globl asm_jit_ST_IMM_ABS
.globl asm_jit_ST_IMM_ABS_END
asm_jit_ST_IMM_ABS:
  mov BYTE PTR [REG_MEM + 0x7fffffff], 0

asm_jit_ST_IMM_ABS_END:
  ret


.globl asm_jit_ST_IMM_ZPG
.globl asm_jit_ST_IMM_ZPG_END
asm_jit_ST_IMM_ZPG:
  mov BYTE PTR [REG_MEM + 0x7f], 0

asm_jit_ST_IMM_ZPG_END:
  ret


.globl asm_jit_STA_ABS
.globl asm_jit_STA_ABS_END
asm_jit_STA_ABS:
  mov [REG_MEM + 0x7fffffff], REG_6502_A

asm_jit_STA_ABS_END:
  ret


.globl asm_jit_STA_ABX
.globl asm_jit_STA_ABX_END
asm_jit_STA_ABX:
  mov [REG_6502_X_64 + 0x7fffffff], REG_6502_A

asm_jit_STA_ABX_END:
  ret


.globl asm_jit_STA_ABY
.globl asm_jit_STA_ABY_END
asm_jit_STA_ABY:
  mov [REG_6502_Y_64 + 0x7fffffff], REG_6502_A

asm_jit_STA_ABY_END:
  ret


.globl asm_jit_STA_addr
.globl asm_jit_STA_addr_END
asm_jit_STA_addr:
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_A

asm_jit_STA_addr_END:
  ret


.globl asm_jit_STA_addr_n
.globl asm_jit_STA_addr_n_END
asm_jit_STA_addr_n:
  mov [REG_ADDR + 0x7fffffff], REG_6502_A

asm_jit_STA_addr_n_END:
  ret


.globl asm_jit_STA_addr_X
.globl asm_jit_STA_addr_X_END
asm_jit_STA_addr_X:
  mov [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_A

asm_jit_STA_addr_X_END:
  ret


.globl asm_jit_STA_addr_Y
.globl asm_jit_STA_addr_Y_END
asm_jit_STA_addr_Y:
  mov [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_A

asm_jit_STA_addr_Y_END:
  ret


.globl asm_jit_STA_ZPG
.globl asm_jit_STA_ZPG_END
asm_jit_STA_ZPG:
  mov [REG_MEM + 0x7f], REG_6502_A

asm_jit_STA_ZPG_END:
  ret


.globl asm_jit_STX_ABS
.globl asm_jit_STX_ABS_END
asm_jit_STX_ABS:
  mov [REG_MEM + 0x7fffffff], REG_6502_X

asm_jit_STX_ABS_END:
  ret


.globl asm_jit_STX_addr
.globl asm_jit_STX_addr_END
asm_jit_STX_addr:
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_X

asm_jit_STX_addr_END:
  ret


.globl asm_jit_STX_ZPG
.globl asm_jit_STX_ZPG_END
asm_jit_STX_ZPG:
  mov [REG_MEM + 0x7f], REG_6502_X

asm_jit_STX_ZPG_END:
  ret


.globl asm_jit_STY_ABS
.globl asm_jit_STY_ABS_END
asm_jit_STY_ABS:
  mov [REG_MEM + 0x7fffffff], REG_6502_Y

asm_jit_STY_ABS_END:
  ret


.globl asm_jit_STY_addr
.globl asm_jit_STY_addr_END
asm_jit_STY_addr:
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_Y

asm_jit_STY_addr_END:
  ret


.globl asm_jit_STY_ZPG
.globl asm_jit_STY_ZPG_END
asm_jit_STY_ZPG:
  mov [REG_MEM + 0x7f], REG_6502_Y

asm_jit_STY_ZPG_END:
  ret


.globl asm_jit_SUB_ABS
.globl asm_jit_SUB_ABS_END
asm_jit_SUB_ABS:
  sub REG_6502_A, [REG_MEM + 0x7fffffff]

asm_jit_SUB_ABS_END:
  ret


.globl asm_jit_SUB_ABX
.globl asm_jit_SUB_ABX_END
asm_jit_SUB_ABX:
  sub REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

asm_jit_SUB_ABX_END:
  ret


.globl asm_jit_SUB_ABY
.globl asm_jit_SUB_ABY_END
asm_jit_SUB_ABY:
  sub REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

asm_jit_SUB_ABY_END:
  ret


.globl asm_jit_SUB_addr
.globl asm_jit_SUB_addr_END
asm_jit_SUB_addr:
  sub REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

asm_jit_SUB_addr_END:
  ret


.globl asm_jit_SUB_addr_n
.globl asm_jit_SUB_addr_n_END
asm_jit_SUB_addr_n:
  sub REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

asm_jit_SUB_addr_n_END:
  ret


.globl asm_jit_SUB_addr_X
.globl asm_jit_SUB_addr_X_END
asm_jit_SUB_addr_X:
  sub REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_SUB_addr_X_END:
  ret


.globl asm_jit_SUB_addr_Y
.globl asm_jit_SUB_addr_Y_END
asm_jit_SUB_addr_Y:
  sub REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

asm_jit_SUB_addr_Y_END:
  ret


.globl asm_jit_SUB_IMM
.globl asm_jit_SUB_IMM_END
asm_jit_SUB_IMM:
  # NOTE: Intel has subtract with borrow, not subtract with carry, so
  # appropriate carry flag management also needs to be used.
  sub REG_6502_A, 0

asm_jit_SUB_IMM_END:
  ret


.globl asm_jit_SUB_ZPG
.globl asm_jit_SUB_ZPG_END
asm_jit_SUB_ZPG:
  sub REG_6502_A, [REG_MEM + 0x7f]

asm_jit_SUB_ZPG_END:
  ret
