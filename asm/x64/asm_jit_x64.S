#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "../asm_jit_defs.h"
#include "../asm_tables_defs.h"
#include "asm_calling_convention_x64.h"
#include "asm_defs_registers_x64.h"

.file "asm/x64/asm_jit_x64.S"
.intel_syntax noprefix
.text


.globl ASM_SYM(asm_jit_check_x64_rorx)
ASM_SYM(asm_jit_check_x64_rorx):
  rorx eax, eax, 1
  ret


.globl ASM_SYM(asm_jit_enter)
.globl ASM_SYM(asm_jit_enter_END)
ASM_SYM(asm_jit_enter):
  jmp ASM_SYM(asm_enter_common)

ASM_SYM(asm_jit_enter_END):
  ret


.globl ASM_SYM(asm_jit_compile_trampoline)
ASM_SYM(asm_jit_compile_trampoline):
  # At this point: stack is aligned to 8 bytes.
  # This is because the JIT engine gets here via call [rdi].
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  # Trashes REG_SCRATCH1, REG_SCRATCH3
  # Preserves RFLAGS
  call ASM_SYM(asm_save_AXYS_PC_flags)

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
  call ASM_SYM(asm_restore_AXYS_PC_flags)

  lahf
  shl REG_6502_PC, K_JIT_BYTES_SHIFT
  sahf
  lea REG_6502_PC, [REG_6502_PC + K_JIT_ADDR]

  # We're jumping out of a call so pop the return address.
  pop REG_SCRATCH2

  jmp REG_6502_PC


.globl ASM_SYM(asm_jit_interp)
ASM_SYM(asm_jit_interp):
  # At this point: stack is aligned to 16 bytes.
  # This is because the JIT engine gets here via jmp.
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  # Trashes REG_SCRATCH1, REG_SCRATCH3
  # Preserves RFLAGS
  call ASM_SYM(asm_save_AXYS_PC_flags)

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
  call ASM_SYM(asm_restore_AXYS_PC_flags)

  lahf
  shl REG_6502_PC, K_JIT_BYTES_SHIFT
  sahf
  lea REG_6502_PC, [REG_6502_PC + K_JIT_ADDR]

  jmp REG_6502_PC


.globl ASM_SYM(asm_jit_jump_interp_trampoline)
.globl ASM_SYM(asm_jit_jump_interp_trampoline_pc_patch)
.globl ASM_SYM(asm_jit_jump_interp_trampoline_jump_patch)
.globl ASM_SYM(asm_jit_jump_interp_trampoline_END)
ASM_SYM(asm_jit_jump_interp_trampoline):
  mov REG_6502_PC_32, 0x7fffffff
ASM_SYM(asm_jit_jump_interp_trampoline_pc_patch):
  jmp ASM_SYM(asm_unpatched_branch_target)
ASM_SYM(asm_jit_jump_interp_trampoline_jump_patch):

ASM_SYM(asm_jit_jump_interp_trampoline_END):
  ret


.globl ASM_SYM(asm_jit_copy_scratch2_x)
.globl ASM_SYM(asm_jit_copy_scratch2_x_END)
ASM_SYM(asm_jit_copy_scratch2_x):
  mov REG_6502_X_32, REG_SCRATCH2_32

ASM_SYM(asm_jit_copy_scratch2_x_END):
  ret


.globl ASM_SYM(asm_jit_copy_scratch2_y)
.globl ASM_SYM(asm_jit_copy_scratch2_y_END)
ASM_SYM(asm_jit_copy_scratch2_y):
  mov REG_6502_Y_32, REG_SCRATCH2_32

ASM_SYM(asm_jit_copy_scratch2_y_END):
  ret


.globl ASM_SYM(asm_jit_copy_x_scratch2)
.globl ASM_SYM(asm_jit_copy_x_scratch2_END)
ASM_SYM(asm_jit_copy_x_scratch2):
  mov REG_SCRATCH2_32, REG_6502_X_32

ASM_SYM(asm_jit_copy_x_scratch2_END):
  ret


.globl ASM_SYM(asm_jit_copy_y_scratch2)
.globl ASM_SYM(asm_jit_copy_y_scratch2_END)
ASM_SYM(asm_jit_copy_y_scratch2):
  mov REG_SCRATCH2_32, REG_6502_Y_32

ASM_SYM(asm_jit_copy_y_scratch2_END):
  ret


.globl ASM_SYM(asm_jit_check_countdown_lea_8bit)
.globl ASM_SYM(asm_jit_check_countdown_lea_8bit_END)
ASM_SYM(asm_jit_check_countdown_lea_8bit):
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 0x80]

ASM_SYM(asm_jit_check_countdown_lea_8bit_END):
  ret


.globl ASM_SYM(asm_jit_check_countdown_lea)
.globl ASM_SYM(asm_jit_check_countdown_lea_END)
ASM_SYM(asm_jit_check_countdown_lea):
  lea REG_COUNTDOWN, [REG_COUNTDOWN - 0x80000000]

ASM_SYM(asm_jit_check_countdown_lea_END):
  ret


.globl ASM_SYM(asm_jit_check_countdown_bt)
.globl ASM_SYM(asm_jit_check_countdown_bt_END)
ASM_SYM(asm_jit_check_countdown_bt):
  bt REG_COUNTDOWN, 63

ASM_SYM(asm_jit_check_countdown_bt_END):
  ret


.globl ASM_SYM(asm_jit_check_countdown_sub_8bit)
.globl ASM_SYM(asm_jit_check_countdown_sub_8bit_END)
ASM_SYM(asm_jit_check_countdown_sub_8bit):
  sub REG_COUNTDOWN, 0x7f

ASM_SYM(asm_jit_check_countdown_sub_8bit_END):
  ret


.globl ASM_SYM(asm_jit_check_countdown_sub)
.globl ASM_SYM(asm_jit_check_countdown_sub_END)
ASM_SYM(asm_jit_check_countdown_sub):
  sub REG_COUNTDOWN, 0x7fffffff

ASM_SYM(asm_jit_check_countdown_sub_END):
  ret


.globl ASM_SYM(asm_jit_check_countdown_jb)
.globl ASM_SYM(asm_jit_check_countdown_jb_END)
ASM_SYM(asm_jit_check_countdown_jb):
  # Force short jump encoding for "jb".
  .byte 0x72
  .byte 0x00

ASM_SYM(asm_jit_check_countdown_jb_END):
  ret


.globl ASM_SYM(asm_jit_countdown_add)
.globl ASM_SYM(asm_jit_countdown_add_END)
ASM_SYM(asm_jit_countdown_add):
  lea REG_COUNTDOWN, [REG_COUNTDOWN + 0x7f]

ASM_SYM(asm_jit_countdown_add_END):
  ret


.globl ASM_SYM(asm_jit_countdown_add_scratch)
.globl ASM_SYM(asm_jit_countdown_add_scratch_END)
ASM_SYM(asm_jit_countdown_add_scratch):
  add REG_COUNTDOWN, REG_SCRATCH1

ASM_SYM(asm_jit_countdown_add_scratch_END):
  ret


.globl ASM_SYM(asm_jit_countdown_sub_scratch)
.globl ASM_SYM(asm_jit_countdown_sub_scratch_END)
ASM_SYM(asm_jit_countdown_sub_scratch):
  sub REG_COUNTDOWN, REG_SCRATCH1

ASM_SYM(asm_jit_countdown_sub_scratch_END):
  ret


.globl ASM_SYM(asm_jit_call_debug)
.globl ASM_SYM(asm_jit_call_debug_pc_patch)
.globl ASM_SYM(asm_jit_call_debug_call_patch)
.globl ASM_SYM(asm_jit_call_debug_END)
ASM_SYM(asm_jit_call_debug):
  mov REG_6502_PC_32, 0x7fffffff
ASM_SYM(asm_jit_call_debug_pc_patch):
  # Some optimizations cache values across opcodes in REG_ADDR or host flags.
  # So they must be saved.
  pushfq
  push REG_ADDR
  call ASM_SYM(asm_unpatched_branch_target)
ASM_SYM(asm_jit_call_debug_call_patch):
  pop REG_ADDR
  popf

ASM_SYM(asm_jit_call_debug_END):
  ret


.globl ASM_SYM(asm_jit_jump_interp)
.globl ASM_SYM(asm_jit_jump_interp_pc_patch)
.globl ASM_SYM(asm_jit_jump_interp_jump_patch)
.globl ASM_SYM(asm_jit_jump_interp_END)
ASM_SYM(asm_jit_jump_interp):
  mov REG_6502_PC_32, 0x7fffffff
ASM_SYM(asm_jit_jump_interp_pc_patch):
  jmp ASM_SYM(asm_unpatched_branch_target)
ASM_SYM(asm_jit_jump_interp_jump_patch):

ASM_SYM(asm_jit_jump_interp_END):
  ret


.globl ASM_SYM(asm_jit_inturbo_set_pc)
.globl ASM_SYM(asm_jit_inturbo_set_pc_END)
.globl ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump)
.globl ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump_END)
.globl ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump_rorx)
.globl ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump_rorx_END)
.globl ASM_SYM(asm_jit_inturbo_call)
.globl ASM_SYM(asm_jit_inturbo_call_END)
.globl ASM_SYM(asm_jit_inturbo_do_jit_jump)
.globl ASM_SYM(asm_jit_inturbo_do_jit_jump_END)
.globl ASM_SYM(asm_jit_inturbo_do_jit_jump_rorx)
.globl ASM_SYM(asm_jit_inturbo_do_jit_jump_rorx_END)
.globl ASM_SYM(asm_jit_inturbo_call)
.globl ASM_SYM(asm_jit_inturbo_call_END)
ASM_SYM(asm_jit_inturbo_set_pc):
  mov REG_6502_PC_32, 0x7fffffff
ASM_SYM(asm_jit_inturbo_set_pc_END):
ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump):
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC]
  lahf
  shl REG_SCRATCH1_32, K_INTURBO_OPCODE_SHIFT
  sahf
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_INTURBO_ADDR]
ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump_END):
ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump_rorx):
  movzx REG_SCRATCH1_32, BYTE PTR [REG_6502_PC]
  rorx REG_SCRATCH1_32, REG_SCRATCH1_32, (32 - K_INTURBO_OPCODE_SHIFT)
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + K_INTURBO_ADDR]
ASM_SYM(asm_jit_inturbo_calculate_inturbo_jump_rorx_END):
ASM_SYM(asm_jit_inturbo_call):
  # Save JIT REG_CONTEXT.
  # Keeps stack alignment to 16 after the call.
  push REG_CONTEXT
  # Swith REG_CONTEXT to inturbo one.
  mov REG_CONTEXT, [REG_CONTEXT + K_JIT_CONTEXT_OFFSET_INTURBO]
  call REG_SCRATCH1

  pop REG_CONTEXT
ASM_SYM(asm_jit_inturbo_call_END):
ASM_SYM(asm_jit_inturbo_do_jit_jump):
  lahf
  lea REG_6502_PC_32, [REG_6502_PC - K_BBC_MEM_READ_FULL_ADDR]
  shl REG_6502_PC_32, K_JIT_BYTES_SHIFT
  sahf
  lea REG_6502_PC_32, [REG_6502_PC + K_JIT_ADDR]

  jmp REG_6502_PC
ASM_SYM(asm_jit_inturbo_do_jit_jump_END):
ASM_SYM(asm_jit_inturbo_do_jit_jump_rorx):
  lea REG_6502_PC_32, [REG_6502_PC - K_BBC_MEM_READ_FULL_ADDR]
  rorx REG_6502_PC_32, REG_6502_PC_32, (32 - K_JIT_BYTES_SHIFT)
  lea REG_6502_PC_32, [REG_6502_PC + K_JIT_ADDR]

  jmp REG_6502_PC
ASM_SYM(asm_jit_inturbo_do_jit_jump_rorx_END):

  ret


ASM_SYM(asm_jit_for_testing_END):


.globl ASM_SYM(asm_jit_value_load)
.globl ASM_SYM(asm_jit_value_load_END)
ASM_SYM(asm_jit_value_load):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_value_load_END):
  ret


.globl ASM_SYM(asm_jit_value_store)
.globl ASM_SYM(asm_jit_value_store_END)
ASM_SYM(asm_jit_value_store):
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_SCRATCH2_8

ASM_SYM(asm_jit_value_store_END):
  ret


.globl ASM_SYM(asm_jit_carry_invert)
.globl ASM_SYM(asm_jit_carry_invert_END)
ASM_SYM(asm_jit_carry_invert):
  cmc

ASM_SYM(asm_jit_carry_invert_END):
  ret


.globl ASM_SYM(asm_jit_check_bcd)
.globl ASM_SYM(asm_jit_check_bcd_END)
ASM_SYM(asm_jit_check_bcd):
  # This is fault fixup, i.e. it faults if the 6502 D flag is set.
  # You would expect movzx into a 32-bit register here to be faster due to
  # eliminated dependencies but it is about 10% slower on one of the CLOCKSP
  # microbenchmarks. Also, using REG_SCRATCH3_8 seems a little faster than
  # REG_SCRATCH2_8 :shrug:.
  mov REG_SCRATCH3_8, \
      [REG_6502_ID_F_64 + K_BBC_MEM_READ_FULL_ADDR + K_6502_ADDR_SPACE_SIZE - 6]

ASM_SYM(asm_jit_check_bcd_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_ABX)
.globl ASM_SYM(asm_jit_check_page_crossing_ABX_END)
ASM_SYM(asm_jit_check_page_crossing_ABX):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_check_page_crossing_ABX_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_ABY)
.globl ASM_SYM(asm_jit_check_page_crossing_ABY_END)
ASM_SYM(asm_jit_check_page_crossing_ABY):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_check_page_crossing_ABY_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_adjust)
.globl ASM_SYM(asm_jit_check_page_crossing_adjust_END)
ASM_SYM(asm_jit_check_page_crossing_adjust):
  lea REG_COUNTDOWN, [REG_COUNTDOWN + REG_SCRATCH2]

ASM_SYM(asm_jit_check_page_crossing_adjust_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_n)
.globl ASM_SYM(asm_jit_check_page_crossing_n_END)
ASM_SYM(asm_jit_check_page_crossing_n):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_SCRATCH2 + 0x7fffffff]

ASM_SYM(asm_jit_check_page_crossing_n_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_x)
.globl ASM_SYM(asm_jit_check_page_crossing_x_END)
ASM_SYM(asm_jit_check_page_crossing_x):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_X_64 + \
                                   REG_SCRATCH2 + \
                                   K_ASM_TABLE_PAGE_WRAP_CYCLE_INV]

ASM_SYM(asm_jit_check_page_crossing_x_END):
  ret


.globl ASM_SYM(asm_jit_check_page_crossing_y)
.globl ASM_SYM(asm_jit_check_page_crossing_y_END)
ASM_SYM(asm_jit_check_page_crossing_y):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_Y_64 + \
                                   REG_SCRATCH2 + \
                                   K_ASM_TABLE_PAGE_WRAP_CYCLE_INV]

ASM_SYM(asm_jit_check_page_crossing_y_END):
  ret


.globl ASM_SYM(asm_jit_CHECK_PENDING_IRQ)
.globl ASM_SYM(asm_jit_CHECK_PENDING_IRQ_jump_patch)
.globl ASM_SYM(asm_jit_CHECK_PENDING_IRQ_END)
ASM_SYM(asm_jit_CHECK_PENDING_IRQ):
  # TODO: could use a register for IRQ_FIRE, if CLI / PLP is common?
  mov REG_SCRATCH2, [REG_CONTEXT + K_CONTEXT_OFFSET_STATE_6502]
  mov REG_SCRATCH2_32, [REG_SCRATCH2 + K_STATE_6502_OFFSET_REG_IRQ_FIRE]
  lea REG_SCRATCH2_32, [REG_SCRATCH2 - 1]
  bt REG_SCRATCH2_32, 31
  jae ASM_SYM(asm_unpatched_branch_target)
ASM_SYM(asm_jit_CHECK_PENDING_IRQ_jump_patch):

ASM_SYM(asm_jit_CHECK_PENDING_IRQ_END):
  ret


.globl ASM_SYM(asm_jit_collapse_loop)
.globl ASM_SYM(asm_jit_collapse_loop_END)
ASM_SYM(asm_jit_collapse_loop):
  lahf
  mov REG_SCRATCH2_32, REG_6502_A_32
  xor REG_SCRATCH1_32, REG_SCRATCH1_32
  mov REG_6502_A_64, REG_COUNTDOWN
  div REG_6502_PC
  mov REG_COUNTDOWN, REG_SCRATCH1
  mov REG_6502_A_32, REG_SCRATCH2_32
  sahf
  movzx REG_6502_A_32, REG_6502_A

ASM_SYM(asm_jit_collapse_loop_END):
  ret


.globl ASM_SYM(asm_jit_deref_context)
.globl ASM_SYM(asm_jit_deref_context_END)
ASM_SYM(asm_jit_deref_context):
  mov REG_6502_PC, [REG_CONTEXT + 0x7fffffff]

ASM_SYM(asm_jit_deref_context_END):
  ret


.globl ASM_SYM(asm_jit_deref_scratch)
.globl ASM_SYM(asm_jit_deref_scratch_END)
ASM_SYM(asm_jit_deref_scratch):
  mov REG_SCRATCH2, [REG_6502_PC + 0x7fffffff]

ASM_SYM(asm_jit_deref_scratch_END):
  ret


.globl ASM_SYM(asm_jit_load_deref_scratch)
.globl ASM_SYM(asm_jit_load_deref_scratch_END)
ASM_SYM(asm_jit_load_deref_scratch):
  movzx REG_SCRATCH2, BYTE PTR [REG_SCRATCH2 + 0x7fffffff]

ASM_SYM(asm_jit_load_deref_scratch_END):
  ret


.globl ASM_SYM(asm_jit_dex_loop_calc_iters)
.globl ASM_SYM(asm_jit_dex_loop_calc_iters_END)
ASM_SYM(asm_jit_dex_loop_calc_iters):
  lea REG_SCRATCH1_32, [REG_6502_X_64 - 1]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8
  add REG_SCRATCH1_32, 1

ASM_SYM(asm_jit_dex_loop_calc_iters_END):
  ret


.globl ASM_SYM(asm_jit_dey_loop_calc_iters)
.globl ASM_SYM(asm_jit_dey_loop_calc_iters_END)
ASM_SYM(asm_jit_dey_loop_calc_iters):
  lea REG_SCRATCH1_32, [REG_6502_Y_64 - 1]
  movzx REG_SCRATCH1_32, REG_SCRATCH1_8
  add REG_SCRATCH1_32, 1

ASM_SYM(asm_jit_dey_loop_calc_iters_END):
  ret


.globl ASM_SYM(asm_jit_flags_nz_mem_ABS)
.globl ASM_SYM(asm_jit_flags_nz_mem_ABS_END)
ASM_SYM(asm_jit_flags_nz_mem_ABS):
  test BYTE PTR [REG_MEM + 0x7fffffff], 0xff

ASM_SYM(asm_jit_flags_nz_mem_ABS_END):
  ret


.globl ASM_SYM(asm_jit_flags_nz_mem_ZPG)
.globl ASM_SYM(asm_jit_flags_nz_mem_ZPG_END)
ASM_SYM(asm_jit_flags_nz_mem_ZPG):
  test BYTE PTR [REG_MEM + 0x7f], 0xff

ASM_SYM(asm_jit_flags_nz_mem_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_flags_nz_value)
.globl ASM_SYM(asm_jit_flags_nz_value_END)
ASM_SYM(asm_jit_flags_nz_value):
  test REG_SCRATCH2_8, REG_SCRATCH2_8

ASM_SYM(asm_jit_flags_nz_value_END):
  ret


.globl ASM_SYM(asm_jit_JMP_SCRATCH_add_n)
.globl ASM_SYM(asm_jit_JMP_SCRATCH_add_n_END)
.globl ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump)
.globl ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump_END)
.globl ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump_rorx)
.globl ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump_rorx_END)
ASM_SYM(asm_jit_JMP_SCRATCH_add_n):
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + 0x7fffffff]
ASM_SYM(asm_jit_JMP_SCRATCH_add_n_END):
ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump):
  lahf
  shl REG_SCRATCH1_32, K_JIT_BYTES_SHIFT
  sahf
  jmp REG_SCRATCH1
ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump_END):
ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump_rorx):
  rorx REG_SCRATCH1_32, REG_SCRATCH1_32, (32 - K_JIT_BYTES_SHIFT)
  jmp REG_SCRATCH1
ASM_SYM(asm_jit_JMP_SCRATCH_shift_jump_rorx_END):
  ret


.globl ASM_SYM(asm_jit_load_carry_for_branch)
.globl ASM_SYM(asm_jit_load_carry_for_branch_END)
ASM_SYM(asm_jit_load_carry_for_branch):
  bt REG_6502_CF_64, 0

ASM_SYM(asm_jit_load_carry_for_branch_END):
  ret


.globl ASM_SYM(asm_jit_load_carry_for_calc)
.globl ASM_SYM(asm_jit_load_carry_for_calc_END)
ASM_SYM(asm_jit_load_carry_for_calc):
  shr REG_6502_CF, 1

ASM_SYM(asm_jit_load_carry_for_calc_END):
  ret


.globl ASM_SYM(asm_jit_load_carry_inv_for_calc)
.globl ASM_SYM(asm_jit_load_carry_inv_for_calc_END)
ASM_SYM(asm_jit_load_carry_inv_for_calc):
  cmp REG_6502_CF, 1

ASM_SYM(asm_jit_load_carry_inv_for_calc_END):
  ret


.globl ASM_SYM(asm_jit_load_overflow)
.globl ASM_SYM(asm_jit_load_overflow_END)
ASM_SYM(asm_jit_load_overflow):
  bt REG_6502_OF_64, 0

ASM_SYM(asm_jit_load_overflow_END):
  ret


.globl ASM_SYM(asm_jit_load_PC)
.globl ASM_SYM(asm_jit_load_PC_END)
ASM_SYM(asm_jit_load_PC):
  mov REG_6502_PC_32, 0x7fffffff

ASM_SYM(asm_jit_load_PC_END):
  ret


.globl ASM_SYM(asm_jit_mode_ABX_to_temp_addr)
.globl ASM_SYM(asm_jit_mode_ABX_to_temp_addr_END)
ASM_SYM(asm_jit_mode_ABX_to_temp_addr):
  lea REG_SCRATCH2_32, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_mode_ABX_to_temp_addr_END):
  ret


.globl ASM_SYM(asm_jit_mode_abx_and_load)
.globl ASM_SYM(asm_jit_mode_abx_and_load_END)
ASM_SYM(asm_jit_mode_abx_and_load):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_mode_abx_and_load_END):
  ret


.globl ASM_SYM(asm_jit_mode_abx_store)
.globl ASM_SYM(asm_jit_mode_abx_store_END)
ASM_SYM(asm_jit_mode_abx_store):
  mov [REG_6502_X_64 + 0x7fffffff], REG_SCRATCH2_8

ASM_SYM(asm_jit_mode_abx_store_END):
  ret


.globl ASM_SYM(asm_jit_mode_ABY_to_temp_addr)
.globl ASM_SYM(asm_jit_mode_ABY_to_temp_addr_END)
ASM_SYM(asm_jit_mode_ABY_to_temp_addr):
  lea REG_SCRATCH2_32, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_mode_ABY_to_temp_addr_END):
  ret


.globl ASM_SYM(asm_jit_mode_IDY_load_mov1)
.globl ASM_SYM(asm_jit_mode_IDY_load_mov1_END)
.globl ASM_SYM(asm_jit_mode_IDY_load_mov2)
.globl ASM_SYM(asm_jit_mode_IDY_load_mov2_END)
ASM_SYM(asm_jit_mode_IDY_load_mov1):
  # Deliberately done as 2x 8 byte loads, not 1x 16.
  # It's faster, possibly store-to-load forwarding fails?
  movzx REG_ADDR_32, BYTE PTR [REG_MEM + 0x7f]
ASM_SYM(asm_jit_mode_IDY_load_mov1_END):
  ret

ASM_SYM(asm_jit_mode_IDY_load_mov2):
  mov REG_ADDR_8_HI, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_mode_IDY_load_mov2_END):
  ret


.globl ASM_SYM(asm_jit_mode_IND_mov1)
.globl ASM_SYM(asm_jit_mode_IND_mov1_END)
.globl ASM_SYM(asm_jit_mode_IND_mov2)
.globl ASM_SYM(asm_jit_mode_IND_mov2_END)
ASM_SYM(asm_jit_mode_IND_mov1):
  # Deliberately done as 2x 8 byte loads, not 1x 16.
  # It's faster, possibly store-to-load forwarding fails?
  movzx REG_ADDR_32, BYTE PTR [REG_MEM + 0x7fffffff]
ASM_SYM(asm_jit_mode_IND_mov1_END):
  ret

ASM_SYM(asm_jit_mode_IND_mov2):
  mov REG_ADDR_8_HI, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_mode_IND_mov2_END):
  ret


.globl ASM_SYM(asm_jit_addr_add_x)
.globl ASM_SYM(asm_jit_addr_add_x_END)
ASM_SYM(asm_jit_addr_add_x):
  lea REG_ADDR_32, [REG_ADDR + REG_6502_X_64]

ASM_SYM(asm_jit_addr_add_x_END):
  ret


.globl ASM_SYM(asm_jit_addr_add_y)
.globl ASM_SYM(asm_jit_addr_add_y_END)
ASM_SYM(asm_jit_addr_add_y):
  lea REG_ADDR_32, [REG_ADDR + REG_6502_Y_64]

ASM_SYM(asm_jit_addr_add_y_END):
  ret


.globl ASM_SYM(asm_jit_addr_load_16bit_wrap)
.globl ASM_SYM(asm_jit_addr_load_16bit_wrap_END)
ASM_SYM(asm_jit_addr_load_16bit_wrap):
  lea REG_SCRATCH2_32, [REG_ADDR + 1]
  movzx REG_SCRATCH2_32, REG_SCRATCH2_8
  mov REG_ADDR_8, [REG_ADDR + REG_MEM - REG_MEM_OFFSET]
  mov REG_ADDR_8_HI, [REG_SCRATCH2 + REG_MEM - REG_MEM_OFFSET]

ASM_SYM(asm_jit_addr_load_16bit_wrap_END):
  ret


.globl ASM_SYM(asm_jit_MODE_ZPX)
.globl ASM_SYM(asm_jit_MODE_ZPX_lea_patch)
.globl ASM_SYM(asm_jit_MODE_ZPX_END)
ASM_SYM(asm_jit_MODE_ZPX):
  lea REG_ADDR_32, [REG_6502_X_64 + 0x7fffffff]
ASM_SYM(asm_jit_MODE_ZPX_lea_patch):
  movzx REG_ADDR_32, REG_ADDR_8

ASM_SYM(asm_jit_MODE_ZPX_END):
  ret


.globl ASM_SYM(asm_jit_MODE_ZPX_8bit)
.globl ASM_SYM(asm_jit_MODE_ZPX_8bit_lea_patch)
.globl ASM_SYM(asm_jit_MODE_ZPX_8bit_END)
ASM_SYM(asm_jit_MODE_ZPX_8bit):
  lea REG_ADDR_32, [REG_6502_X_64 + 0x7f]
ASM_SYM(asm_jit_MODE_ZPX_8bit_lea_patch):
  movzx REG_ADDR_32, REG_ADDR_8

ASM_SYM(asm_jit_MODE_ZPX_8bit_END):
  ret


.globl ASM_SYM(asm_jit_MODE_ZPY)
.globl ASM_SYM(asm_jit_MODE_ZPY_lea_patch)
.globl ASM_SYM(asm_jit_MODE_ZPY_END)
ASM_SYM(asm_jit_MODE_ZPY):
  lea REG_ADDR_32, [REG_6502_Y_64 + 0x7fffffff]
ASM_SYM(asm_jit_MODE_ZPY_lea_patch):
  movzx REG_ADDR_32, REG_ADDR_8

ASM_SYM(asm_jit_MODE_ZPY_END):
  ret


.globl ASM_SYM(asm_jit_MODE_ZPY_8bit)
.globl ASM_SYM(asm_jit_MODE_ZPY_8bit_lea_patch)
.globl ASM_SYM(asm_jit_MODE_ZPY_8bit_END)
ASM_SYM(asm_jit_MODE_ZPY_8bit):
  lea REG_ADDR_32, [REG_6502_Y_64 + 0x7f]
ASM_SYM(asm_jit_MODE_ZPY_8bit_lea_patch):
  movzx REG_ADDR_32, REG_ADDR_8

ASM_SYM(asm_jit_MODE_ZPY_8bit_END):
  ret


.globl ASM_SYM(asm_jit_PULL_16)
.globl ASM_SYM(asm_jit_PULL_16_END)
ASM_SYM(asm_jit_PULL_16):
  # Magic constant to cause a fault + fixup when incrementing stack by 2 would
  # overflow.
  mov REG_SCRATCH3_8, [REG_6502_S_64 + K_BBC_MEM_OFFSET_TO_READ_FULL + 0xfe02]
  movzx REG_SCRATCH1_32, WORD PTR [REG_6502_S_64 + 1]
  lea REG_6502_S_32, [REG_6502_S_64 + 2]

ASM_SYM(asm_jit_PULL_16_END):
  ret


.globl ASM_SYM(asm_jit_PUSH_16)
.globl ASM_SYM(asm_jit_PUSH_16_word_patch)
.globl ASM_SYM(asm_jit_PUSH_16_END)
ASM_SYM(asm_jit_PUSH_16):
  # Magic constant to cause a fault + fixup when decrementing stack by 2 would
  # overflow.
  # Seems to be faster without movzx, as per other similar cases, which is
  # strange.
  mov REG_SCRATCH3_8, [REG_6502_S_64 + K_BBC_MEM_OFFSET_TO_READ_FULL - 0x102]
  mov WORD PTR [REG_6502_S_64 - 1], 0xffff
ASM_SYM(asm_jit_PUSH_16_word_patch):
  lea REG_6502_S_32, [REG_6502_S_64 - 2]

ASM_SYM(asm_jit_PUSH_16_END):
  ret


.globl ASM_SYM(asm_jit_save_addr_low_byte)
.globl ASM_SYM(asm_jit_save_addr_low_byte_END)
ASM_SYM(asm_jit_save_addr_low_byte):
  movzx REG_SCRATCH2_32, REG_ADDR_8

ASM_SYM(asm_jit_save_addr_low_byte_END):
  ret


.globl ASM_SYM(asm_jit_save_carry)
.globl ASM_SYM(asm_jit_save_carry_END)
ASM_SYM(asm_jit_save_carry):
  setb REG_6502_CF

ASM_SYM(asm_jit_save_carry_END):
  ret


.globl ASM_SYM(asm_jit_save_carry_inv)
.globl ASM_SYM(asm_jit_save_carry_inv_END)
ASM_SYM(asm_jit_save_carry_inv):
  setae REG_6502_CF

ASM_SYM(asm_jit_save_carry_inv_END):
  ret


.globl ASM_SYM(asm_jit_save_overflow)
.globl ASM_SYM(asm_jit_save_overflow_END)
ASM_SYM(asm_jit_save_overflow):
  seto REG_6502_OF

ASM_SYM(asm_jit_save_overflow_END):
  ret


.globl ASM_SYM(asm_jit_scratch_mul)
.globl ASM_SYM(asm_jit_scratch_mul_END)
ASM_SYM(asm_jit_scratch_mul):
  imul REG_SCRATCH1_32, REG_SCRATCH1_32, 0x7f

ASM_SYM(asm_jit_scratch_mul_END):
  ret


.globl ASM_SYM(asm_jit_scratch_mul_5)
.globl ASM_SYM(asm_jit_scratch_mul_5_END)
ASM_SYM(asm_jit_scratch_mul_5):
  lea REG_SCRATCH1_32, [REG_SCRATCH1 + REG_SCRATCH1 * 4]

ASM_SYM(asm_jit_scratch_mul_5_END):
  ret


.globl ASM_SYM(asm_jit_scratch_sub)
.globl ASM_SYM(asm_jit_scratch_sub_END)
ASM_SYM(asm_jit_scratch_sub):
  sub REG_SCRATCH1_32, 0x7f

ASM_SYM(asm_jit_scratch_sub_END):
  ret


.globl ASM_SYM(asm_jit_store_ABS)
.globl ASM_SYM(asm_jit_store_ABS_END)
ASM_SYM(asm_jit_store_ABS):
  mov [REG_MEM + 0x7fffffff], REG_SCRATCH2_8

ASM_SYM(asm_jit_store_ABS_END):
  ret


.globl ASM_SYM(asm_jit_store_ZPG)
.globl ASM_SYM(asm_jit_store_ZPG_END)
ASM_SYM(asm_jit_store_ZPG):
  mov [REG_MEM + 0x7f], REG_SCRATCH2_8

ASM_SYM(asm_jit_store_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_commit)
.globl ASM_SYM(asm_jit_write_inv_commit_END)
ASM_SYM(asm_jit_write_inv_commit):
  mov WORD PTR [REG_SCRATCH2], 0x17ff

ASM_SYM(asm_jit_write_inv_commit_END):
  ret


.globl ASM_SYM(asm_jit_write_inv)
.globl ASM_SYM(asm_jit_write_inv_END)
ASM_SYM(asm_jit_write_inv):
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_ADDR * 4]

ASM_SYM(asm_jit_write_inv_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_from_temp_addr)
.globl ASM_SYM(asm_jit_write_inv_from_temp_addr_END)
ASM_SYM(asm_jit_write_inv_from_temp_addr):
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_SCRATCH2 * 4]

ASM_SYM(asm_jit_write_inv_from_temp_addr_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_ABS)
.globl ASM_SYM(asm_jit_write_inv_ABS_END)
ASM_SYM(asm_jit_write_inv_ABS):
  mov REG_SCRATCH2_32, [REG_CONTEXT + 0x7fffffff]

ASM_SYM(asm_jit_write_inv_ABS_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_IDY)
.globl ASM_SYM(asm_jit_write_inv_IDY_END)
ASM_SYM(asm_jit_write_inv_IDY):
  lea REG_SCRATCH2_32, [REG_ADDR + REG_6502_Y_64]
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_SCRATCH2 * 4]

ASM_SYM(asm_jit_write_inv_IDY_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_IDY_n)
.globl ASM_SYM(asm_jit_write_inv_IDY_n_END)
ASM_SYM(asm_jit_write_inv_IDY_n):
  # The contant encodes both K_JIT_CONTEXT_OFFSET_JIT_PTRS and the "n".
  mov REG_SCRATCH2_32, [REG_CONTEXT + 0x7fffffff + REG_ADDR * 4]

ASM_SYM(asm_jit_write_inv_IDY_n_END):
  ret


.globl ASM_SYM(asm_jit_write_inv_IDY_X)
.globl ASM_SYM(asm_jit_write_inv_IDY_X_END)
ASM_SYM(asm_jit_write_inv_IDY_X):
  lea REG_SCRATCH2_32, [REG_ADDR + REG_6502_X_64]
  mov REG_SCRATCH2_32, [REG_CONTEXT + \
                        K_JIT_CONTEXT_OFFSET_JIT_PTRS + \
                        REG_SCRATCH2 * 4]

ASM_SYM(asm_jit_write_inv_IDY_X_END):
  ret


.globl ASM_SYM(asm_jit_ADC_ABS)
.globl ASM_SYM(asm_jit_ADC_ABS_END)
ASM_SYM(asm_jit_ADC_ABS):
  adc REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_ADC_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ADC_ABX)
.globl ASM_SYM(asm_jit_ADC_ABX_END)
ASM_SYM(asm_jit_ADC_ABX):
  adc REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_ADC_ABX_END):
  ret


.globl ASM_SYM(asm_jit_ADC_ABY)
.globl ASM_SYM(asm_jit_ADC_ABY_END)
ASM_SYM(asm_jit_ADC_ABY):
  adc REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_ADC_ABY_END):
  ret


.globl ASM_SYM(asm_jit_ADC_addr)
.globl ASM_SYM(asm_jit_ADC_addr_END)
ASM_SYM(asm_jit_ADC_addr):
  adc REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ADC_addr_END):
  ret


.globl ASM_SYM(asm_jit_ADC_addr_n)
.globl ASM_SYM(asm_jit_ADC_addr_n_END)
ASM_SYM(asm_jit_ADC_addr_n):
  adc REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_ADC_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_ADC_addr_X)
.globl ASM_SYM(asm_jit_ADC_addr_X_END)
ASM_SYM(asm_jit_ADC_addr_X):
  adc REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ADC_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_ADC_addr_Y)
.globl ASM_SYM(asm_jit_ADC_addr_Y_END)
ASM_SYM(asm_jit_ADC_addr_Y):
  adc REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ADC_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_ADC_IMM)
.globl ASM_SYM(asm_jit_ADC_IMM_END)
ASM_SYM(asm_jit_ADC_IMM):
  adc REG_6502_A, 0

ASM_SYM(asm_jit_ADC_IMM_END):
  ret


.globl ASM_SYM(asm_jit_ADC_ZPG)
.globl ASM_SYM(asm_jit_ADC_ZPG_END)
ASM_SYM(asm_jit_ADC_ZPG):
  adc REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_ADC_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_ADD_ABS)
.globl ASM_SYM(asm_jit_ADD_ABS_END)
ASM_SYM(asm_jit_ADD_ABS):
  add REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_ADD_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ADD_ABX)
.globl ASM_SYM(asm_jit_ADD_ABX_END)
ASM_SYM(asm_jit_ADD_ABX):
  add REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_ADD_ABX_END):
  ret


.globl ASM_SYM(asm_jit_ADD_ABY)
.globl ASM_SYM(asm_jit_ADD_ABY_END)
ASM_SYM(asm_jit_ADD_ABY):
  add REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_ADD_ABY_END):
  ret


.globl ASM_SYM(asm_jit_ADD_addr)
.globl ASM_SYM(asm_jit_ADD_addr_END)
ASM_SYM(asm_jit_ADD_addr):
  add REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ADD_addr_END):
  ret


.globl ASM_SYM(asm_jit_ADD_addr_n)
.globl ASM_SYM(asm_jit_ADD_addr_n_END)
ASM_SYM(asm_jit_ADD_addr_n):
  add REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_ADD_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_ADD_addr_X)
.globl ASM_SYM(asm_jit_ADD_addr_X_END)
ASM_SYM(asm_jit_ADD_addr_X):
  add REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ADD_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_ADD_addr_Y)
.globl ASM_SYM(asm_jit_ADD_addr_Y_END)
ASM_SYM(asm_jit_ADD_addr_Y):
  add REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ADD_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_ADD_IMM)
.globl ASM_SYM(asm_jit_ADD_IMM_END)
ASM_SYM(asm_jit_ADD_IMM):
  add REG_6502_A, 0

ASM_SYM(asm_jit_ADD_IMM_END):
  ret


.globl ASM_SYM(asm_jit_ADD_ZPG)
.globl ASM_SYM(asm_jit_ADD_ZPG_END)
ASM_SYM(asm_jit_ADD_ZPG):
  add REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_ADD_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_ALR_IMM_and)
.globl ASM_SYM(asm_jit_ALR_IMM_and_END)
.globl ASM_SYM(asm_jit_ALR_IMM_shr)
.globl ASM_SYM(asm_jit_ALR_IMM_shr_END)
ASM_SYM(asm_jit_ALR_IMM_and):
  and REG_6502_A, 0xff

ASM_SYM(asm_jit_ALR_IMM_and_END):
  ret

ASM_SYM(asm_jit_ALR_IMM_shr):
  shr REG_6502_A, 1

ASM_SYM(asm_jit_ALR_IMM_shr_END):
  ret


.globl ASM_SYM(asm_jit_AND_ABS)
.globl ASM_SYM(asm_jit_AND_ABS_END)
ASM_SYM(asm_jit_AND_ABS):
  and REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_AND_ABS_END):
  ret


.globl ASM_SYM(asm_jit_AND_ABX)
.globl ASM_SYM(asm_jit_AND_ABX_END)
ASM_SYM(asm_jit_AND_ABX):
  and REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_AND_ABX_END):
  ret


.globl ASM_SYM(asm_jit_AND_ABY)
.globl ASM_SYM(asm_jit_AND_ABY_END)
ASM_SYM(asm_jit_AND_ABY):
  and REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_AND_ABY_END):
  ret


.globl ASM_SYM(asm_jit_AND_addr)
.globl ASM_SYM(asm_jit_AND_addr_END)
ASM_SYM(asm_jit_AND_addr):
  and REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_AND_addr_END):
  ret


.globl ASM_SYM(asm_jit_AND_addr_n)
.globl ASM_SYM(asm_jit_AND_addr_n_END)
ASM_SYM(asm_jit_AND_addr_n):
  and REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_AND_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_AND_addr_X)
.globl ASM_SYM(asm_jit_AND_addr_X_END)
ASM_SYM(asm_jit_AND_addr_X):
  and REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_AND_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_AND_addr_Y)
.globl ASM_SYM(asm_jit_AND_addr_Y_END)
ASM_SYM(asm_jit_AND_addr_Y):
  and REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_AND_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_AND_IMM)
.globl ASM_SYM(asm_jit_AND_IMM_END)
ASM_SYM(asm_jit_AND_IMM):
  and REG_6502_A, 0

ASM_SYM(asm_jit_AND_IMM_END):
  ret


.globl ASM_SYM(asm_jit_AND_ZPG)
.globl ASM_SYM(asm_jit_AND_ZPG_END)
ASM_SYM(asm_jit_AND_ZPG):
  and REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_AND_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_ASL_ABS)
.globl ASM_SYM(asm_jit_ASL_ABS_END)
ASM_SYM(asm_jit_ASL_ABS):
  shl BYTE PTR [REG_MEM + 0x7fffffff], 1

ASM_SYM(asm_jit_ASL_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ASL_ACC)
.globl ASM_SYM(asm_jit_ASL_ACC_END)
ASM_SYM(asm_jit_ASL_ACC):
  shl REG_6502_A, 1

ASM_SYM(asm_jit_ASL_ACC_END):
  ret


.globl ASM_SYM(asm_jit_ASL_ACC_n)
.globl ASM_SYM(asm_jit_ASL_ACC_n_END)
ASM_SYM(asm_jit_ASL_ACC_n):
  shl REG_6502_A, 2

ASM_SYM(asm_jit_ASL_ACC_n_END):
  ret


.globl ASM_SYM(asm_jit_ASL_value)
.globl ASM_SYM(asm_jit_ASL_value_END)
ASM_SYM(asm_jit_ASL_value):
  shl REG_SCRATCH2_8, 1

ASM_SYM(asm_jit_ASL_value_END):
  ret


.globl ASM_SYM(asm_jit_ASL_ZPG)
.globl ASM_SYM(asm_jit_ASL_ZPG_END)
ASM_SYM(asm_jit_ASL_ZPG):
  shl BYTE PTR [REG_MEM + 0x7f], 1

ASM_SYM(asm_jit_ASL_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_BCC)
.globl ASM_SYM(asm_jit_BCC_END)
ASM_SYM(asm_jit_BCC):
  jae ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BCC_END):
  ret


.globl ASM_SYM(asm_jit_BCC_8bit)
.globl ASM_SYM(asm_jit_BCC_8bit_END)
ASM_SYM(asm_jit_BCC_8bit):
  # Force short jump encoding for "jae".
  .byte 0x73
  .byte 0x00

ASM_SYM(asm_jit_BCC_8bit_END):
  ret


.globl ASM_SYM(asm_jit_BCS)
.globl ASM_SYM(asm_jit_BCS_END)
ASM_SYM(asm_jit_BCS):
  jb ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BCS_END):
  ret


.globl ASM_SYM(asm_jit_BCS_8bit)
.globl ASM_SYM(asm_jit_BCS_8bit_END)
ASM_SYM(asm_jit_BCS_8bit):
  # Force short jump encoding for "jb".
  .byte 0x72
  .byte 0x00

ASM_SYM(asm_jit_BCS_8bit_END):
  ret


.globl ASM_SYM(asm_jit_BEQ)
.globl ASM_SYM(asm_jit_BEQ_END)
ASM_SYM(asm_jit_BEQ):
  je ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BEQ_END):
  ret


.globl ASM_SYM(asm_jit_BEQ_8bit)
.globl ASM_SYM(asm_jit_BEQ_8bit_END)
ASM_SYM(asm_jit_BEQ_8bit):
  # Force short jump encoding for "je".
  .byte 0x74
  .byte 0x00

ASM_SYM(asm_jit_BEQ_8bit_END):
  ret


.globl ASM_SYM(asm_jit_load_ABS)
.globl ASM_SYM(asm_jit_load_ABS_END)
ASM_SYM(asm_jit_load_ABS):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_load_ABS_END):
  ret


.globl ASM_SYM(asm_jit_load_ZPG)
.globl ASM_SYM(asm_jit_load_ZPG_END)
ASM_SYM(asm_jit_load_ZPG):
  movzx REG_SCRATCH2_32, BYTE PTR [REG_MEM + 0x7f]

ASM_SYM(asm_jit_load_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_BMI)
.globl ASM_SYM(asm_jit_BMI_END)
ASM_SYM(asm_jit_BMI):
  js ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BMI_END):
  ret


.globl ASM_SYM(asm_jit_BMI_8bit)
.globl ASM_SYM(asm_jit_BMI_8bit_END)
ASM_SYM(asm_jit_BMI_8bit):
  # Force short jump encoding for "js".
  .byte 0x78
  .byte 0x00

ASM_SYM(asm_jit_BMI_8bit_END):
  ret


.globl ASM_SYM(asm_jit_BNE)
.globl ASM_SYM(asm_jit_BNE_END)
ASM_SYM(asm_jit_BNE):
  jne ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BNE_END):
  ret


.globl ASM_SYM(asm_jit_BNE_8bit)
.globl ASM_SYM(asm_jit_BNE_8bit_END)
ASM_SYM(asm_jit_BNE_8bit):
  # Force short jump encoding for "jne".
  .byte 0x75
  .byte 0x00

ASM_SYM(asm_jit_BNE_8bit_END):
  ret


.globl ASM_SYM(asm_jit_BPL)
.globl ASM_SYM(asm_jit_BPL_END)
ASM_SYM(asm_jit_BPL):
  jns ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BPL_END):
  ret


.globl ASM_SYM(asm_jit_BPL_8bit)
.globl ASM_SYM(asm_jit_BPL_8bit_END)
ASM_SYM(asm_jit_BPL_8bit):
  # Force short jump encoding for "jns".
  .byte 0x79
  .byte 0x00

ASM_SYM(asm_jit_BPL_8bit_END):
  ret


.globl ASM_SYM(asm_jit_BVC)
.globl ASM_SYM(asm_jit_BVC_END)
ASM_SYM(asm_jit_BVC):
  jae ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BVC_END):
  ret


.globl ASM_SYM(asm_jit_BVC_8bit)
.globl ASM_SYM(asm_jit_BVC_8bit_END)
ASM_SYM(asm_jit_BVC_8bit):
  # Force short jump encoding for "jae".
  .byte 0x73
  .byte 0x00

ASM_SYM(asm_jit_BVC_8bit_END):
  ret


.globl ASM_SYM(asm_jit_BVS)
.globl ASM_SYM(asm_jit_BVS_END)
ASM_SYM(asm_jit_BVS):
  jb ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_BVS_END):
  ret


.globl ASM_SYM(asm_jit_BVS_8bit)
.globl ASM_SYM(asm_jit_BVS_8bit_END)
ASM_SYM(asm_jit_BVS_8bit):
  # Force short jump encoding for "jb".
  .byte 0x72
  .byte 0x00

ASM_SYM(asm_jit_BVS_8bit_END):
  ret


.globl ASM_SYM(asm_jit_CMP_ABS)
.globl ASM_SYM(asm_jit_CMP_ABS_END)
ASM_SYM(asm_jit_CMP_ABS):
  cmp REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_CMP_ABS_END):
  ret


.globl ASM_SYM(asm_jit_CMP_ABX)
.globl ASM_SYM(asm_jit_CMP_ABX_END)
ASM_SYM(asm_jit_CMP_ABX):
  cmp REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_CMP_ABX_END):
  ret


.globl ASM_SYM(asm_jit_CMP_ABY)
.globl ASM_SYM(asm_jit_CMP_ABY_END)
ASM_SYM(asm_jit_CMP_ABY):
  cmp REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_CMP_ABY_END):
  ret


.globl ASM_SYM(asm_jit_CMP_addr)
.globl ASM_SYM(asm_jit_CMP_addr_END)
ASM_SYM(asm_jit_CMP_addr):
  cmp REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_CMP_addr_END):
  ret


.globl ASM_SYM(asm_jit_CMP_addr_n)
.globl ASM_SYM(asm_jit_CMP_addr_n_END)
ASM_SYM(asm_jit_CMP_addr_n):
  cmp REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_CMP_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_CMP_addr_X)
.globl ASM_SYM(asm_jit_CMP_addr_X_END)
ASM_SYM(asm_jit_CMP_addr_X):
  cmp REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_CMP_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_CMP_addr_Y)
.globl ASM_SYM(asm_jit_CMP_addr_Y_END)
ASM_SYM(asm_jit_CMP_addr_Y):
  cmp REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_CMP_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_CMP_IMM)
.globl ASM_SYM(asm_jit_CMP_IMM_END)
ASM_SYM(asm_jit_CMP_IMM):
  cmp REG_6502_A, 0

ASM_SYM(asm_jit_CMP_IMM_END):
  ret


.globl ASM_SYM(asm_jit_CMP_ZPG)
.globl ASM_SYM(asm_jit_CMP_ZPG_END)
ASM_SYM(asm_jit_CMP_ZPG):
  cmp REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_CMP_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_CPX_ABS)
.globl ASM_SYM(asm_jit_CPX_ABS_END)
ASM_SYM(asm_jit_CPX_ABS):
  cmp REG_6502_X, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_CPX_ABS_END):
  ret


.globl ASM_SYM(asm_jit_CPX_IMM)
.globl ASM_SYM(asm_jit_CPX_IMM_END)
ASM_SYM(asm_jit_CPX_IMM):
  cmp REG_6502_X, 0

ASM_SYM(asm_jit_CPX_IMM_END):
  ret


.globl ASM_SYM(asm_jit_CPX_ZPG)
.globl ASM_SYM(asm_jit_CPX_ZPG_END)
ASM_SYM(asm_jit_CPX_ZPG):
  cmp REG_6502_X, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_CPX_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_CPY_ABS)
.globl ASM_SYM(asm_jit_CPY_ABS_END)
ASM_SYM(asm_jit_CPY_ABS):
  cmp REG_6502_Y, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_CPY_ABS_END):
  ret


.globl ASM_SYM(asm_jit_CPY_IMM)
.globl ASM_SYM(asm_jit_CPY_IMM_END)
ASM_SYM(asm_jit_CPY_IMM):
  cmp REG_6502_Y, 0

ASM_SYM(asm_jit_CPY_IMM_END):
  ret


.globl ASM_SYM(asm_jit_CPY_ZPG)
.globl ASM_SYM(asm_jit_CPY_ZPG_END)
ASM_SYM(asm_jit_CPY_ZPG):
  cmp REG_6502_Y, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_CPY_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_DEC_ABS)
.globl ASM_SYM(asm_jit_DEC_ABS_END)
ASM_SYM(asm_jit_DEC_ABS):
  dec BYTE PTR [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_DEC_ABS_END):
  ret


.globl ASM_SYM(asm_jit_DEC_value)
.globl ASM_SYM(asm_jit_DEC_value_END)
ASM_SYM(asm_jit_DEC_value):
  dec REG_SCRATCH2_8

ASM_SYM(asm_jit_DEC_value_END):
  ret


.globl ASM_SYM(asm_jit_DEC_ZPG)
.globl ASM_SYM(asm_jit_DEC_ZPG_END)
ASM_SYM(asm_jit_DEC_ZPG):
  dec BYTE PTR [REG_MEM + 0x7f]

ASM_SYM(asm_jit_DEC_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_DEX_n)
.globl ASM_SYM(asm_jit_DEX_n_END)
ASM_SYM(asm_jit_DEX_n):
  sub bl, 1

ASM_SYM(asm_jit_DEX_n_END):
  ret


.globl ASM_SYM(asm_jit_DEY_n)
.globl ASM_SYM(asm_jit_DEY_n_END)
ASM_SYM(asm_jit_DEY_n):
  sub cl, 1

ASM_SYM(asm_jit_DEY_n_END):
  ret


.globl ASM_SYM(asm_jit_EOR_ABS)
.globl ASM_SYM(asm_jit_EOR_ABS_END)
ASM_SYM(asm_jit_EOR_ABS):
  xor REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_EOR_ABS_END):
  ret


.globl ASM_SYM(asm_jit_EOR_ABX)
.globl ASM_SYM(asm_jit_EOR_ABX_END)
ASM_SYM(asm_jit_EOR_ABX):
  xor REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_EOR_ABX_END):
  ret


.globl ASM_SYM(asm_jit_EOR_ABY)
.globl ASM_SYM(asm_jit_EOR_ABY_END)
ASM_SYM(asm_jit_EOR_ABY):
  xor REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_EOR_ABY_END):
  ret


.globl ASM_SYM(asm_jit_EOR_addr)
.globl ASM_SYM(asm_jit_EOR_addr_END)
ASM_SYM(asm_jit_EOR_addr):
  xor REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_EOR_addr_END):
  ret


.globl ASM_SYM(asm_jit_EOR_addr_n)
.globl ASM_SYM(asm_jit_EOR_addr_n_END)
ASM_SYM(asm_jit_EOR_addr_n):
  xor REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_EOR_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_EOR_addr_X)
.globl ASM_SYM(asm_jit_EOR_addr_X_END)
ASM_SYM(asm_jit_EOR_addr_X):
  xor REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_EOR_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_EOR_addr_Y)
.globl ASM_SYM(asm_jit_EOR_addr_Y_END)
ASM_SYM(asm_jit_EOR_addr_Y):
  xor REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_EOR_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_EOR_IMM)
.globl ASM_SYM(asm_jit_EOR_IMM_END)
ASM_SYM(asm_jit_EOR_IMM):
  xor REG_6502_A, 0

ASM_SYM(asm_jit_EOR_IMM_END):
  ret


.globl ASM_SYM(asm_jit_EOR_ZPG)
.globl ASM_SYM(asm_jit_EOR_ZPG_END)
ASM_SYM(asm_jit_EOR_ZPG):
  xor REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_EOR_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_INC_ABS)
.globl ASM_SYM(asm_jit_INC_ABS_END)
ASM_SYM(asm_jit_INC_ABS):
  inc BYTE PTR [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_INC_ABS_END):
  ret


.globl ASM_SYM(asm_jit_INC_addr_zpx)
.globl ASM_SYM(asm_jit_INC_addr_zpx_END)
ASM_SYM(asm_jit_INC_addr_zpx):
  # NOTE: only used for mode zpx so it's safe to assume RAM.
  # TODO: shorter encoding with REG_MEM?
  inc BYTE PTR [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_INC_addr_zpx_END):
  ret


.globl ASM_SYM(asm_jit_INC_value)
.globl ASM_SYM(asm_jit_INC_value_END)
ASM_SYM(asm_jit_INC_value):
  inc REG_SCRATCH2_8

ASM_SYM(asm_jit_INC_value_END):
  ret


.globl ASM_SYM(asm_jit_INC_ZPG)
.globl ASM_SYM(asm_jit_INC_ZPG_END)
ASM_SYM(asm_jit_INC_ZPG):
  inc BYTE PTR [REG_MEM + 0x7f]

ASM_SYM(asm_jit_INC_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_INX_n)
.globl ASM_SYM(asm_jit_INX_n_END)
ASM_SYM(asm_jit_INX_n):
  add bl, 1

ASM_SYM(asm_jit_INX_n_END):
  ret


.globl ASM_SYM(asm_jit_INY_n)
.globl ASM_SYM(asm_jit_INY_n_END)
ASM_SYM(asm_jit_INY_n):
  add cl, 1

ASM_SYM(asm_jit_INY_n_END):
  ret


.globl ASM_SYM(asm_jit_JMP)
.globl ASM_SYM(asm_jit_JMP_END)
ASM_SYM(asm_jit_JMP):
  jmp ASM_SYM(asm_unpatched_branch_target)

ASM_SYM(asm_jit_JMP_END):
  ret

.globl ASM_SYM(asm_jit_JMP_8bit)
.globl ASM_SYM(asm_jit_JMP_8bit_END)
ASM_SYM(asm_jit_JMP_8bit):
  # Force short jump encoding for "jmp".
  .byte 0xeb
  .byte 0x00

ASM_SYM(asm_jit_JMP_8bit_END):
  ret


.globl ASM_SYM(asm_jit_LDA_ABS)
.globl ASM_SYM(asm_jit_LDA_ABS_END)
ASM_SYM(asm_jit_LDA_ABS):
  movzx REG_6502_A_32, BYTE PTR [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_LDA_ABS_END):
  ret


.globl ASM_SYM(asm_jit_LDA_addr)
.globl ASM_SYM(asm_jit_LDA_addr_END)
ASM_SYM(asm_jit_LDA_addr):
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_LDA_addr_END):
  ret


.globl ASM_SYM(asm_jit_LDA_addr_n)
.globl ASM_SYM(asm_jit_LDA_addr_n_END)
ASM_SYM(asm_jit_LDA_addr_n):
  # TODO: this is a 3-component lea. It's possible to fold REG_MEM and the
  # constant into one larger constant, resulting in a 2-component lea, but a
  # larger encoding. I don't really observe any timing difference in a
  # microbenchmark on my 11th gen Intel, but AMD is alleged to prefer
  # 2-component lea's so it may be worth revisiting in the future.
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_LDA_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_LDA_addr_X)
.globl ASM_SYM(asm_jit_LDA_addr_X_END)
ASM_SYM(asm_jit_LDA_addr_X):
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + \
                                 REG_6502_X_64 + \
                                 K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_LDA_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_LDA_addr_Y)
.globl ASM_SYM(asm_jit_LDA_addr_Y_END)
ASM_SYM(asm_jit_LDA_addr_Y):
  movzx REG_6502_A_32, BYTE PTR [REG_ADDR + \
                                 REG_6502_Y_64 + \
                                 K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_LDA_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_LDA_IMM)
.globl ASM_SYM(asm_jit_LDA_IMM_END)
ASM_SYM(asm_jit_LDA_IMM):
  mov REG_6502_A_32, 0x00000000

ASM_SYM(asm_jit_LDA_IMM_END):
  ret


.globl ASM_SYM(asm_jit_LDA_ABX)
.globl ASM_SYM(asm_jit_LDA_ABX_END)
ASM_SYM(asm_jit_LDA_ABX):
  movzx REG_6502_A_32, BYTE PTR [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_LDA_ABX_END):
  ret


.globl ASM_SYM(asm_jit_LDA_ABY)
.globl ASM_SYM(asm_jit_LDA_ABY_END)
ASM_SYM(asm_jit_LDA_ABY):
  movzx REG_6502_A_32, BYTE PTR [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_LDA_ABY_END):
  ret


.globl ASM_SYM(asm_jit_LDA_value)
.globl ASM_SYM(asm_jit_LDA_value_END)
ASM_SYM(asm_jit_LDA_value):
  mov REG_6502_A_32, REG_SCRATCH2_32

ASM_SYM(asm_jit_LDA_value_END):
  ret


.globl ASM_SYM(asm_jit_LDA_zero)
.globl ASM_SYM(asm_jit_LDA_zero_END)
ASM_SYM(asm_jit_LDA_zero):
  xor REG_6502_A_32, REG_6502_A_32

ASM_SYM(asm_jit_LDA_zero_END):
  ret


.globl ASM_SYM(asm_jit_LDA_ZPG)
.globl ASM_SYM(asm_jit_LDA_ZPG_END)
ASM_SYM(asm_jit_LDA_ZPG):
  movzx REG_6502_A_32, BYTE PTR [REG_MEM + 0x7f]

ASM_SYM(asm_jit_LDA_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_LDX_ABS)
.globl ASM_SYM(asm_jit_LDX_ABS_END)
ASM_SYM(asm_jit_LDX_ABS):
  mov REG_6502_X, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_LDX_ABS_END):
  ret


.globl ASM_SYM(asm_jit_LDX_ABY)
.globl ASM_SYM(asm_jit_LDX_ABY_END)
ASM_SYM(asm_jit_LDX_ABY):
  mov REG_6502_X, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_LDX_ABY_END):
  ret


.globl ASM_SYM(asm_jit_LDX_addr)
.globl ASM_SYM(asm_jit_LDX_addr_END)
ASM_SYM(asm_jit_LDX_addr):
  mov REG_6502_X, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_LDX_addr_END):
  ret


.globl ASM_SYM(asm_jit_LDX_addr_Y)
.globl ASM_SYM(asm_jit_LDX_addr_Y_END)
ASM_SYM(asm_jit_LDX_addr_Y):
  mov REG_6502_X, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_LDX_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_LDX_IMM)
.globl ASM_SYM(asm_jit_LDX_IMM_END)
ASM_SYM(asm_jit_LDX_IMM):
  mov REG_6502_X, 0

ASM_SYM(asm_jit_LDX_IMM_END):
  ret


.globl ASM_SYM(asm_jit_LDX_value)
.globl ASM_SYM(asm_jit_LDX_value_END)
ASM_SYM(asm_jit_LDX_value):
  mov REG_6502_X_32, REG_SCRATCH2_32

ASM_SYM(asm_jit_LDX_value_END):
  ret


.globl ASM_SYM(asm_jit_LDX_zero)
.globl ASM_SYM(asm_jit_LDX_zero_END)
ASM_SYM(asm_jit_LDX_zero):
  xor REG_6502_X_32, REG_6502_X_32

ASM_SYM(asm_jit_LDX_zero_END):
  ret


.globl ASM_SYM(asm_jit_LDX_ZPG)
.globl ASM_SYM(asm_jit_LDX_ZPG_END)
ASM_SYM(asm_jit_LDX_ZPG):
  mov REG_6502_X, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_LDX_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_LDY_ABS)
.globl ASM_SYM(asm_jit_LDY_ABS_END)
ASM_SYM(asm_jit_LDY_ABS):
  mov REG_6502_Y, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_LDY_ABS_END):
  ret


.globl ASM_SYM(asm_jit_LDY_ABX)
.globl ASM_SYM(asm_jit_LDY_ABX_END)
ASM_SYM(asm_jit_LDY_ABX):
  mov REG_6502_Y, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_LDY_ABX_END):
  ret


.globl ASM_SYM(asm_jit_LDY_addr)
.globl ASM_SYM(asm_jit_LDY_addr_END)
ASM_SYM(asm_jit_LDY_addr):
  mov REG_6502_Y, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_LDY_addr_END):
  ret


.globl ASM_SYM(asm_jit_LDY_addr_X)
.globl ASM_SYM(asm_jit_LDY_addr_X_END)
ASM_SYM(asm_jit_LDY_addr_X):
  mov REG_6502_Y, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_LDY_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_LDY_IMM)
.globl ASM_SYM(asm_jit_LDY_IMM_END)
ASM_SYM(asm_jit_LDY_IMM):
  mov REG_6502_Y, 0

ASM_SYM(asm_jit_LDY_IMM_END):
  ret


.globl ASM_SYM(asm_jit_LDY_value)
.globl ASM_SYM(asm_jit_LDY_value_END)
ASM_SYM(asm_jit_LDY_value):
  mov REG_6502_Y_32, REG_SCRATCH2_32

ASM_SYM(asm_jit_LDY_value_END):
  ret


.globl ASM_SYM(asm_jit_LDY_zero)
.globl ASM_SYM(asm_jit_LDY_zero_END)
ASM_SYM(asm_jit_LDY_zero):
  xor REG_6502_Y_32, REG_6502_Y_32

ASM_SYM(asm_jit_LDY_zero_END):
  ret


.globl ASM_SYM(asm_jit_LDY_ZPG)
.globl ASM_SYM(asm_jit_LDY_ZPG_END)
ASM_SYM(asm_jit_LDY_ZPG):
  mov REG_6502_Y, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_LDY_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_LSR_ABS)
.globl ASM_SYM(asm_jit_LSR_ABS_END)
ASM_SYM(asm_jit_LSR_ABS):
  shr BYTE PTR [REG_MEM + 0x7fffffff], 1

ASM_SYM(asm_jit_LSR_ABS_END):
  ret


.globl ASM_SYM(asm_jit_LSR_ACC)
.globl ASM_SYM(asm_jit_LSR_ACC_END)
ASM_SYM(asm_jit_LSR_ACC):
  shr REG_6502_A, 1

ASM_SYM(asm_jit_LSR_ACC_END):
  ret


.globl ASM_SYM(asm_jit_LSR_ACC_n)
.globl ASM_SYM(asm_jit_LSR_ACC_n_END)
ASM_SYM(asm_jit_LSR_ACC_n):
  shr REG_6502_A, 2

ASM_SYM(asm_jit_LSR_ACC_n_END):
  ret


.globl ASM_SYM(asm_jit_LSR_value)
.globl ASM_SYM(asm_jit_LSR_value_END)
ASM_SYM(asm_jit_LSR_value):
  shr REG_SCRATCH2_8, 1

ASM_SYM(asm_jit_LSR_value_END):
  ret


.globl ASM_SYM(asm_jit_LSR_ZPG)
.globl ASM_SYM(asm_jit_LSR_ZPG_END)
ASM_SYM(asm_jit_LSR_ZPG):
  shr BYTE PTR [REG_MEM + 0x7f], 1

ASM_SYM(asm_jit_LSR_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_ORA_ABS)
.globl ASM_SYM(asm_jit_ORA_ABS_END)
ASM_SYM(asm_jit_ORA_ABS):
  or REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_ORA_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ORA_ABX)
.globl ASM_SYM(asm_jit_ORA_ABX_END)
ASM_SYM(asm_jit_ORA_ABX):
  or REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_ORA_ABX_END):
  ret


.globl ASM_SYM(asm_jit_ORA_ABY)
.globl ASM_SYM(asm_jit_ORA_ABY_END)
ASM_SYM(asm_jit_ORA_ABY):
  or REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_ORA_ABY_END):
  ret


.globl ASM_SYM(asm_jit_ORA_addr)
.globl ASM_SYM(asm_jit_ORA_addr_END)
ASM_SYM(asm_jit_ORA_addr):
  or REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ORA_addr_END):
  ret


.globl ASM_SYM(asm_jit_ORA_addr_n)
.globl ASM_SYM(asm_jit_ORA_addr_n_END)
ASM_SYM(asm_jit_ORA_addr_n):
  or REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_ORA_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_ORA_addr_X)
.globl ASM_SYM(asm_jit_ORA_addr_X_END)
ASM_SYM(asm_jit_ORA_addr_X):
  or REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ORA_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_ORA_addr_Y)
.globl ASM_SYM(asm_jit_ORA_addr_Y_END)
ASM_SYM(asm_jit_ORA_addr_Y):
  or REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_ORA_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_ORA_IMM)
.globl ASM_SYM(asm_jit_ORA_IMM_END)
ASM_SYM(asm_jit_ORA_IMM):
  or REG_6502_A, 0

ASM_SYM(asm_jit_ORA_IMM_END):
  ret


.globl ASM_SYM(asm_jit_ORA_ZPG)
.globl ASM_SYM(asm_jit_ORA_ZPG_END)
ASM_SYM(asm_jit_ORA_ZPG):
  or REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_ORA_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_ROL_ABS)
.globl ASM_SYM(asm_jit_ROL_ABS_END)
ASM_SYM(asm_jit_ROL_ABS):
  rcl BYTE PTR [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_ROL_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ROL_ACC)
.globl ASM_SYM(asm_jit_ROL_ACC_END)
ASM_SYM(asm_jit_ROL_ACC):
  rcl REG_6502_A, 1

ASM_SYM(asm_jit_ROL_ACC_END):
  ret


.globl ASM_SYM(asm_jit_ROL_ACC_n)
.globl ASM_SYM(asm_jit_ROL_ACC_n_END)
ASM_SYM(asm_jit_ROL_ACC_n):
  rcl REG_6502_A, 2

ASM_SYM(asm_jit_ROL_ACC_n_END):
  ret


.globl ASM_SYM(asm_jit_ROL_value)
.globl ASM_SYM(asm_jit_ROL_value_END)
ASM_SYM(asm_jit_ROL_value):
  rcl REG_SCRATCH2_8, 1

ASM_SYM(asm_jit_ROL_value_END):
  ret


.globl ASM_SYM(asm_jit_ROL_ZPG)
.globl ASM_SYM(asm_jit_ROL_ZPG_END)
ASM_SYM(asm_jit_ROL_ZPG):
  rcl BYTE PTR [REG_MEM + 0x7f]

ASM_SYM(asm_jit_ROL_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_ROR_ABS)
.globl ASM_SYM(asm_jit_ROR_ABS_END)
ASM_SYM(asm_jit_ROR_ABS):
  rcr BYTE PTR [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_ROR_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ROR_ACC)
.globl ASM_SYM(asm_jit_ROR_ACC_END)
ASM_SYM(asm_jit_ROR_ACC):
  rcr REG_6502_A, 1

ASM_SYM(asm_jit_ROR_ACC_END):
  ret


.globl ASM_SYM(asm_jit_ROR_ACC_n)
.globl ASM_SYM(asm_jit_ROR_ACC_n_END)
ASM_SYM(asm_jit_ROR_ACC_n):
  rcr REG_6502_A, 2

ASM_SYM(asm_jit_ROR_ACC_n_END):
  ret


.globl ASM_SYM(asm_jit_ROR_value)
.globl ASM_SYM(asm_jit_ROR_value_END)
ASM_SYM(asm_jit_ROR_value):
  rcr REG_SCRATCH2_8, 1

ASM_SYM(asm_jit_ROR_value_END):
  ret


.globl ASM_SYM(asm_jit_ROR_ZPG)
.globl ASM_SYM(asm_jit_ROR_ZPG_END)
ASM_SYM(asm_jit_ROR_ZPG):
  rcr BYTE PTR [REG_MEM + 0x7f]

ASM_SYM(asm_jit_ROR_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_SAX_ABS)
.globl ASM_SYM(asm_jit_SAX_ABS_END)
ASM_SYM(asm_jit_SAX_ABS):
  lahf
  mov REG_SCRATCH2_8, REG_6502_X
  and REG_SCRATCH2_8, REG_6502_A
  sahf
  mov [REG_MEM + 0x7fffffff], REG_SCRATCH2_8

ASM_SYM(asm_jit_SAX_ABS_END):
  ret


.globl ASM_SYM(asm_jit_SBC_ABS)
.globl ASM_SYM(asm_jit_SBC_ABS_END)
ASM_SYM(asm_jit_SBC_ABS):
  sbb REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_SBC_ABS_END):
  ret


.globl ASM_SYM(asm_jit_SBC_ABX)
.globl ASM_SYM(asm_jit_SBC_ABX_END)
ASM_SYM(asm_jit_SBC_ABX):
  sbb REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_SBC_ABX_END):
  ret


.globl ASM_SYM(asm_jit_SBC_ABY)
.globl ASM_SYM(asm_jit_SBC_ABY_END)
ASM_SYM(asm_jit_SBC_ABY):
  sbb REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_SBC_ABY_END):
  ret


.globl ASM_SYM(asm_jit_SBC_addr)
.globl ASM_SYM(asm_jit_SBC_addr_END)
ASM_SYM(asm_jit_SBC_addr):
  sbb REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_SBC_addr_END):
  ret


.globl ASM_SYM(asm_jit_SBC_addr_n)
.globl ASM_SYM(asm_jit_SBC_addr_n_END)
ASM_SYM(asm_jit_SBC_addr_n):
  sbb REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_SBC_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_SBC_addr_X)
.globl ASM_SYM(asm_jit_SBC_addr_X_END)
ASM_SYM(asm_jit_SBC_addr_X):
  sbb REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_SBC_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_SBC_addr_Y)
.globl ASM_SYM(asm_jit_SBC_addr_Y_END)
ASM_SYM(asm_jit_SBC_addr_Y):
  sbb REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_SBC_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_SBC_IMM)
.globl ASM_SYM(asm_jit_SBC_IMM_END)
ASM_SYM(asm_jit_SBC_IMM):
  # NOTE: Intel has subtract with borrow, not subtract with carry, so
  # appropriate carry flag management also needs to be used.
  sbb REG_6502_A, 0

ASM_SYM(asm_jit_SBC_IMM_END):
  ret


.globl ASM_SYM(asm_jit_SBC_ZPG)
.globl ASM_SYM(asm_jit_SBC_ZPG_END)
ASM_SYM(asm_jit_SBC_ZPG):
  sbb REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_SBC_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_SLO_ABS)
.globl ASM_SYM(asm_jit_SLO_ABS_mov1_patch)
.globl ASM_SYM(asm_jit_SLO_ABS_mov2_patch)
.globl ASM_SYM(asm_jit_SLO_ABS_END)
ASM_SYM(asm_jit_SLO_ABS):
  mov REG_SCRATCH2_8, [REG_MEM + 0x7fffffff]
ASM_SYM(asm_jit_SLO_ABS_mov1_patch):
  mov REG_SCRATCH3, REG_SCRATCH2
  shl REG_SCRATCH2_8, 1
  mov [REG_MEM + 0x7fffffff], REG_SCRATCH2_8
ASM_SYM(asm_jit_SLO_ABS_mov2_patch):
  or REG_6502_A, REG_SCRATCH2_8
  bt REG_SCRATCH3_32, 7

ASM_SYM(asm_jit_SLO_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ST_IMM_ABS)
.globl ASM_SYM(asm_jit_ST_IMM_ABS_END)
ASM_SYM(asm_jit_ST_IMM_ABS):
  mov BYTE PTR [REG_MEM + 0x7fffffff], 0

ASM_SYM(asm_jit_ST_IMM_ABS_END):
  ret


.globl ASM_SYM(asm_jit_ST_IMM_ZPG)
.globl ASM_SYM(asm_jit_ST_IMM_ZPG_END)
ASM_SYM(asm_jit_ST_IMM_ZPG):
  mov BYTE PTR [REG_MEM + 0x7f], 0

ASM_SYM(asm_jit_ST_IMM_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_STA_ABS)
.globl ASM_SYM(asm_jit_STA_ABS_END)
ASM_SYM(asm_jit_STA_ABS):
  mov [REG_MEM + 0x7fffffff], REG_6502_A

ASM_SYM(asm_jit_STA_ABS_END):
  ret


.globl ASM_SYM(asm_jit_STA_ABX)
.globl ASM_SYM(asm_jit_STA_ABX_END)
ASM_SYM(asm_jit_STA_ABX):
  mov [REG_6502_X_64 + 0x7fffffff], REG_6502_A

ASM_SYM(asm_jit_STA_ABX_END):
  ret


.globl ASM_SYM(asm_jit_STA_ABY)
.globl ASM_SYM(asm_jit_STA_ABY_END)
ASM_SYM(asm_jit_STA_ABY):
  mov [REG_6502_Y_64 + 0x7fffffff], REG_6502_A

ASM_SYM(asm_jit_STA_ABY_END):
  ret


.globl ASM_SYM(asm_jit_STA_addr)
.globl ASM_SYM(asm_jit_STA_addr_END)
ASM_SYM(asm_jit_STA_addr):
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_A

ASM_SYM(asm_jit_STA_addr_END):
  ret


.globl ASM_SYM(asm_jit_STA_addr_n)
.globl ASM_SYM(asm_jit_STA_addr_n_END)
ASM_SYM(asm_jit_STA_addr_n):
  mov [REG_ADDR + 0x7fffffff], REG_6502_A

ASM_SYM(asm_jit_STA_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_STA_addr_X)
.globl ASM_SYM(asm_jit_STA_addr_X_END)
ASM_SYM(asm_jit_STA_addr_X):
  mov [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_A

ASM_SYM(asm_jit_STA_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_STA_addr_Y)
.globl ASM_SYM(asm_jit_STA_addr_Y_END)
ASM_SYM(asm_jit_STA_addr_Y):
  mov [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_A

ASM_SYM(asm_jit_STA_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_STA_ZPG)
.globl ASM_SYM(asm_jit_STA_ZPG_END)
ASM_SYM(asm_jit_STA_ZPG):
  mov [REG_MEM + 0x7f], REG_6502_A

ASM_SYM(asm_jit_STA_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_STX_ABS)
.globl ASM_SYM(asm_jit_STX_ABS_END)
ASM_SYM(asm_jit_STX_ABS):
  mov [REG_MEM + 0x7fffffff], REG_6502_X

ASM_SYM(asm_jit_STX_ABS_END):
  ret


.globl ASM_SYM(asm_jit_STX_addr)
.globl ASM_SYM(asm_jit_STX_addr_END)
ASM_SYM(asm_jit_STX_addr):
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_X

ASM_SYM(asm_jit_STX_addr_END):
  ret


.globl ASM_SYM(asm_jit_STX_ZPG)
.globl ASM_SYM(asm_jit_STX_ZPG_END)
ASM_SYM(asm_jit_STX_ZPG):
  mov [REG_MEM + 0x7f], REG_6502_X

ASM_SYM(asm_jit_STX_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_STY_ABS)
.globl ASM_SYM(asm_jit_STY_ABS_END)
ASM_SYM(asm_jit_STY_ABS):
  mov [REG_MEM + 0x7fffffff], REG_6502_Y

ASM_SYM(asm_jit_STY_ABS_END):
  ret


.globl ASM_SYM(asm_jit_STY_addr)
.globl ASM_SYM(asm_jit_STY_addr_END)
ASM_SYM(asm_jit_STY_addr):
  mov [REG_ADDR + K_BBC_MEM_WRITE_IND_ADDR], REG_6502_Y

ASM_SYM(asm_jit_STY_addr_END):
  ret


.globl ASM_SYM(asm_jit_STY_ZPG)
.globl ASM_SYM(asm_jit_STY_ZPG_END)
ASM_SYM(asm_jit_STY_ZPG):
  mov [REG_MEM + 0x7f], REG_6502_Y

ASM_SYM(asm_jit_STY_ZPG_END):
  ret


.globl ASM_SYM(asm_jit_SUB_ABS)
.globl ASM_SYM(asm_jit_SUB_ABS_END)
ASM_SYM(asm_jit_SUB_ABS):
  sub REG_6502_A, [REG_MEM + 0x7fffffff]

ASM_SYM(asm_jit_SUB_ABS_END):
  ret


.globl ASM_SYM(asm_jit_SUB_ABX)
.globl ASM_SYM(asm_jit_SUB_ABX_END)
ASM_SYM(asm_jit_SUB_ABX):
  sub REG_6502_A, [REG_6502_X_64 + 0x7fffffff]

ASM_SYM(asm_jit_SUB_ABX_END):
  ret


.globl ASM_SYM(asm_jit_SUB_ABY)
.globl ASM_SYM(asm_jit_SUB_ABY_END)
ASM_SYM(asm_jit_SUB_ABY):
  sub REG_6502_A, [REG_6502_Y_64 + 0x7fffffff]

ASM_SYM(asm_jit_SUB_ABY_END):
  ret


.globl ASM_SYM(asm_jit_SUB_addr)
.globl ASM_SYM(asm_jit_SUB_addr_END)
ASM_SYM(asm_jit_SUB_addr):
  sub REG_6502_A, [REG_ADDR + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_SUB_addr_END):
  ret


.globl ASM_SYM(asm_jit_SUB_addr_n)
.globl ASM_SYM(asm_jit_SUB_addr_n_END)
ASM_SYM(asm_jit_SUB_addr_n):
  sub REG_6502_A, [REG_ADDR + REG_MEM + 0x7f]

ASM_SYM(asm_jit_SUB_addr_n_END):
  ret


.globl ASM_SYM(asm_jit_SUB_addr_X)
.globl ASM_SYM(asm_jit_SUB_addr_X_END)
ASM_SYM(asm_jit_SUB_addr_X):
  sub REG_6502_A, [REG_ADDR + REG_6502_X_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_SUB_addr_X_END):
  ret


.globl ASM_SYM(asm_jit_SUB_addr_Y)
.globl ASM_SYM(asm_jit_SUB_addr_Y_END)
ASM_SYM(asm_jit_SUB_addr_Y):
  sub REG_6502_A, [REG_ADDR + REG_6502_Y_64 + K_BBC_MEM_READ_IND_ADDR]

ASM_SYM(asm_jit_SUB_addr_Y_END):
  ret


.globl ASM_SYM(asm_jit_SUB_IMM)
.globl ASM_SYM(asm_jit_SUB_IMM_END)
ASM_SYM(asm_jit_SUB_IMM):
  # NOTE: Intel has subtract with borrow, not subtract with carry, so
  # appropriate carry flag management also needs to be used.
  sub REG_6502_A, 0

ASM_SYM(asm_jit_SUB_IMM_END):
  ret


.globl ASM_SYM(asm_jit_SUB_ZPG)
.globl ASM_SYM(asm_jit_SUB_ZPG_END)
ASM_SYM(asm_jit_SUB_ZPG):
  sub REG_6502_A, [REG_MEM + 0x7f]

ASM_SYM(asm_jit_SUB_ZPG_END):
  ret


# Not called in the x64 model.
.globl ASM_SYM(asm_jit_interp_trampoline)
.globl ASM_SYM(asm_jit_interp_trampoline_END)
ASM_SYM(asm_jit_interp_trampoline):
  int 3
  ret

ASM_SYM(asm_jit_interp_trampoline_END):
  ret
