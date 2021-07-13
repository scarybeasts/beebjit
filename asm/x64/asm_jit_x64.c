#include "../asm_jit.h"

#include "../../defs_6502.h"
#include "../../util.h"
#include "../asm_common.h"
#include "../asm_defs_host.h"
#include "../asm_jit_defs.h"
/* For REG_MEM_OFFSET. */
#include "asm_defs_registers_x64.h"

#include <assert.h>

static void
asm_emit_jit_jump(struct util_buffer* p_buf,
                  void* p_target,
                  void* p_jmp_32bit,
                  void* p_jmp_end_32bit,
                  void* p_jmp_8bit,
                  void* p_jmp_end_8bit) {
  int32_t len_x64;
  int64_t delta;

  size_t offset = util_buffer_get_pos(p_buf);
  void* p_source = (util_buffer_get_base_address(p_buf) + offset);

  len_x64 = (p_jmp_end_8bit - p_jmp_8bit);
  delta = (p_target - (p_source + len_x64));

  if (delta <= INT8_MAX && delta >= INT8_MIN) {
    asm_copy(p_buf, p_jmp_8bit, p_jmp_end_8bit);
    asm_patch_byte(p_buf, offset, p_jmp_8bit, p_jmp_end_8bit, delta);
  } else {
    len_x64 = (p_jmp_end_32bit - p_jmp_32bit);
    delta = (p_target - (p_source + len_x64));
    assert(delta <= INT32_MAX && delta >= INT32_MIN);
    asm_copy(p_buf, p_jmp_32bit, p_jmp_end_32bit);
    asm_patch_int(p_buf, offset, p_jmp_32bit, p_jmp_end_32bit, delta);
  }
}

int
asm_jit_is_enabled(void) {
  return 1;
}

void
asm_jit_test_preconditions(void) {
  if ((asm_jit_BEQ_8bit_END - asm_jit_BEQ_8bit) != 2) {
    util_bail("JIT assembly miscompiled -- clang issue? try opt build.");
  }
}

int
asm_jit_supports_optimizer(void) {
  return 1;
}

int
asm_jit_supports_uopcode(int32_t uopcode) {
  (void) uopcode;
  return 1;
}

void
asm_jit_invalidate_code_at(void* p) {
  uint16_t* p_dst = (uint16_t*) p;
  /* call [rdi] */
  *p_dst = 0x17ff;
}

void
asm_emit_jit_invalidated(struct util_buffer* p_buf) {
  /* call [rdi] */
  util_buffer_add_2b(p_buf, 0xff, 0x17);
}

void
asm_emit_jit_call_compile_trampoline(struct util_buffer* p_buf) {
  /* To work correctly this sequence needs to be no more than 2 bytes. */
  assert((asm_jit_call_compile_trampoline_END -
          asm_jit_call_compile_trampoline) <= 2);

  asm_copy(p_buf,
           asm_jit_call_compile_trampoline,
           asm_jit_call_compile_trampoline_END);
}

void
asm_emit_jit_jump_interp_trampoline(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_jump_interp_trampoline,
           asm_jit_jump_interp_trampoline_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_jump_interp_trampoline,
                asm_jit_jump_interp_trampoline_pc_patch,
                addr);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_jump_interp_trampoline,
                 asm_jit_jump_interp_trampoline_jump_patch,
                 asm_jit_interp);
}

void
asm_emit_jit_check_countdown(struct util_buffer* p_buf,
                             struct util_buffer* p_buf_epilog,
                             uint32_t count,
                             uint16_t addr,
                             void* p_trampoline) {
  size_t offset = util_buffer_get_pos(p_buf);

  (void) p_buf_epilog;
  (void) addr;

  if (count <= 128) {
    asm_copy(p_buf,
             asm_jit_check_countdown_8bit,
             asm_jit_check_countdown_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_check_countdown_8bit,
                   asm_jit_check_countdown_8bit_count_patch,
                   -count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown_8bit,
                   asm_jit_check_countdown_8bit_jump_patch,
                   p_trampoline);
  } else {
    asm_copy(p_buf,
             asm_jit_check_countdown,
             asm_jit_check_countdown_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_check_countdown,
                  asm_jit_check_countdown_count_patch,
                  -count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown,
                   asm_jit_check_countdown_jump_patch,
                   p_trampoline);
  }
}

void
asm_emit_jit_check_countdown_no_save_nz_flags(struct util_buffer* p_buf,
                                              uint32_t count,
                                              void* p_trampoline) {
  void asm_jit_check_countdown_no_save_nz_flags_8bit(void);
  void asm_jit_check_countdown_no_save_nz_flags_8bit_count_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_8bit_jump_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_8bit_END(void);
  void asm_jit_check_countdown_no_save_nz_flags(void);
  void asm_jit_check_countdown_no_save_nz_flags_count_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_jump_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_END(void);

  size_t offset = util_buffer_get_pos(p_buf);

  if (count <= 127) {
    asm_copy(p_buf,
             asm_jit_check_countdown_no_save_nz_flags_8bit,
             asm_jit_check_countdown_no_save_nz_flags_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_check_countdown_no_save_nz_flags_8bit,
                   asm_jit_check_countdown_no_save_nz_flags_8bit_count_patch,
                   count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown_no_save_nz_flags_8bit,
                   asm_jit_check_countdown_no_save_nz_flags_8bit_jump_patch,
                   p_trampoline);
  } else {
    asm_copy(p_buf,
             asm_jit_check_countdown_no_save_nz_flags,
             asm_jit_check_countdown_no_save_nz_flags_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_check_countdown_no_save_nz_flags,
                  asm_jit_check_countdown_no_save_nz_flags_count_patch,
                  count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown_no_save_nz_flags,
                   asm_jit_check_countdown_no_save_nz_flags_jump_patch,
                   p_trampoline);
  }
}

void
asm_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_call_debug, asm_jit_call_debug_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_call_debug,
                asm_jit_call_debug_pc_patch,
                addr);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_call_debug,
                 asm_jit_call_debug_call_patch,
                 asm_debug);
}

void
asm_emit_jit_call_inturbo(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_call_inturbo(void);
  void asm_jit_call_inturbo_pc_patch(void);
  void asm_jit_call_inturbo_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_call_inturbo, asm_jit_call_inturbo_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_call_inturbo,
                asm_jit_call_inturbo_pc_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
}

void
asm_emit_jit_jump_interp(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  /* Require the trampolines to be hosted above the main JIT code in virtual
   * memory. This ensures that the (uncommon) jumps out of JIT are forward
   * jumps, which may help on some CPUs.
   */
  assert(K_BBC_JIT_TRAMPOLINES_ADDR > K_BBC_JIT_ADDR);

  asm_copy(p_buf, asm_jit_jump_interp, asm_jit_jump_interp_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_jump_interp,
                asm_jit_jump_interp_pc_patch,
                addr);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_jump_interp,
                 asm_jit_jump_interp_jump_patch,
                 asm_jit_interp);
}

void
asm_emit_jit_for_testing(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_for_testing, asm_jit_for_testing_END);
}

void
asm_emit_jit_ADD_CYCLES(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_ADD_CYCLES, asm_jit_ADD_CYCLES_END, value);
}

void
asm_emit_jit_ADD_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ADD_ZPG,
                        asm_jit_ADD_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ADD_ABS,
                       asm_jit_ADD_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_ADD_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADD_ABX,
                     asm_jit_ADD_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_ADD_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADD_ABY,
                     asm_jit_ADD_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_ADD_IMM, asm_jit_ADD_IMM_END, value);
}

void
asm_emit_jit_ADD_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADD_SCRATCH,
                     asm_jit_ADD_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_ADD_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ADD_SCRATCH_Y, asm_jit_ADD_SCRATCH_Y_END);
}

void
asm_emit_jit_CHECK_BCD(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_CHECK_BCD, asm_jit_CHECK_BCD_END);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_n(struct util_buffer* p_buf,
                                           uint8_t n) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n,
                asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n_mov_patch,
                (K_ASM_TABLE_PAGE_WRAP_CYCLE_INV + n));
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_X(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_X,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_X_END);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_Y,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_Y_END);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_X_n(struct util_buffer* p_buf,
                                     uint16_t addr) {
  uint32_t value;

  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_X_n,
           asm_jit_CHECK_PAGE_CROSSING_X_n_END);
  value = K_ASM_TABLE_PAGE_WRAP_CYCLE_INV;
  value += (addr & 0xFF);
  asm_patch_int(p_buf,
                offset,
                asm_jit_CHECK_PAGE_CROSSING_X_n,
                asm_jit_CHECK_PAGE_CROSSING_X_n_mov_patch,
                value);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_Y_n(struct util_buffer* p_buf, uint16_t addr) {
  uint32_t value;

  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_Y_n,
           asm_jit_CHECK_PAGE_CROSSING_Y_n_END);
  value = K_ASM_TABLE_PAGE_WRAP_CYCLE_INV;
  value += (addr & 0xFF);
  asm_patch_int(p_buf,
                offset,
                asm_jit_CHECK_PAGE_CROSSING_Y_n,
                asm_jit_CHECK_PAGE_CROSSING_Y_n_mov_patch,
                value);
}

void
asm_emit_jit_CHECK_PENDING_IRQ(struct util_buffer* p_buf, void* p_trampoline) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_CHECK_PENDING_IRQ, asm_jit_CHECK_PENDING_IRQ_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_CHECK_PENDING_IRQ,
                 asm_jit_CHECK_PENDING_IRQ_jump_patch,
                 p_trampoline);
}

void
asm_emit_jit_CLEAR_CARRY(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_CLEAR_CARRY, asm_jit_CLEAR_CARRY_END);
}

void
asm_emit_jit_FLAG_MEM(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_FLAG_MEM, asm_jit_FLAG_MEM_END);
  asm_patch_int(p_buf,
                (offset - 1),
                asm_jit_FLAG_MEM,
                asm_jit_FLAG_MEM_END,
                (addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_INVERT_CARRY(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_INVERT_CARRY, asm_jit_INVERT_CARRY_END);
}

void
asm_emit_jit_JMP_SCRATCH_n(struct util_buffer* p_buf, uint16_t n) {
  void asm_jit_JMP_SCRATCH_n(void);
  void asm_jit_JMP_SCRATCH_n_lea_patch(void);
  void asm_jit_JMP_SCRATCH_n_END(void);
  size_t offset = util_buffer_get_pos(p_buf);
  asm_copy(p_buf, asm_jit_JMP_SCRATCH_n, asm_jit_JMP_SCRATCH_n_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_JMP_SCRATCH_n,
                asm_jit_JMP_SCRATCH_n_lea_patch,
                ((K_BBC_JIT_ADDR >> K_BBC_JIT_BYTES_SHIFT) + n));
}

void
asm_emit_jit_LDA_Z(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LDA_Z, asm_jit_LDA_Z_END);
}

void
asm_emit_jit_LDX_Z(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LDX_Z, asm_jit_LDX_Z_END);
}

void
asm_emit_jit_LDY_Z(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LDY_Z, asm_jit_LDY_Z_END);
}

void
asm_emit_jit_LOAD_CARRY_FOR_BRANCH(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_jit_LOAD_CARRY_FOR_BRANCH,
           asm_jit_LOAD_CARRY_FOR_BRANCH_END);
}

void
asm_emit_jit_LOAD_CARRY_FOR_CALC(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_jit_LOAD_CARRY_FOR_CALC,
           asm_jit_LOAD_CARRY_FOR_CALC_END);
}

void
asm_emit_jit_LOAD_CARRY_INV_FOR_CALC(struct util_buffer* p_buf) {
  asm_copy(p_buf,
           asm_jit_LOAD_CARRY_INV_FOR_CALC,
           asm_jit_LOAD_CARRY_INV_FOR_CALC_END);
}

void
asm_emit_jit_LOAD_OVERFLOW(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LOAD_OVERFLOW, asm_jit_LOAD_OVERFLOW_END);
}

void
asm_emit_jit_LOAD_SCRATCH_8(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_LOAD_SCRATCH_8,
                     asm_jit_LOAD_SCRATCH_8_END,
                     (addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_LOAD_SCRATCH_16(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_MODE_IND_16, asm_jit_MODE_IND_16_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov1_patch,
                (addr - REG_MEM_OFFSET));
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov2_patch,
                ((addr + 1) - REG_MEM_OFFSET));
}

void
asm_emit_jit_MODE_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf, asm_jit_MODE_ABX, asm_jit_MODE_ABX_END, addr);
}

void
asm_emit_jit_MODE_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf, asm_jit_MODE_ABY, asm_jit_MODE_ABY_END, addr);
}

void
asm_emit_jit_MODE_IND_8(struct util_buffer* p_buf, uint8_t addr) {
  uint16_t next_addr;

  size_t offset = util_buffer_get_pos(p_buf);

  if (addr == 0xFF) {
    next_addr = 0;
  } else {
    next_addr = (addr + 1);
  }

  asm_copy(p_buf, asm_jit_MODE_IND_8, asm_jit_MODE_IND_8_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_MODE_IND_8,
                 asm_jit_MODE_IND_8_mov1_patch,
                 (addr - REG_MEM_OFFSET));
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_MODE_IND_8,
                 asm_jit_MODE_IND_8_mov2_patch,
                 (next_addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_MODE_IND_16(struct util_buffer* p_buf,
                         uint16_t addr,
                         uint32_t segment) {
  uint16_t next_addr;

  size_t offset = util_buffer_get_pos(p_buf);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);

  /* On the 6502, (e.g.) JMP (&10FF) does not fetch across the page boundary. */
  if ((addr & 0xFF) == 0xFF) {
    next_addr = (addr & 0xFF00);
  } else {
    next_addr = (addr + 1);
  }

  asm_copy(p_buf, asm_jit_MODE_IND_16, asm_jit_MODE_IND_16_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov1_patch,
                (addr - REG_MEM_OFFSET + delta));
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov2_patch,
                (next_addr - REG_MEM_OFFSET + delta));
}

void
asm_emit_jit_MODE_IND_SCRATCH_8(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_MODE_IND_SCRATCH_8, asm_jit_MODE_IND_SCRATCH_8_END);
}

void
asm_emit_jit_MODE_IND_SCRATCH_16(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_MODE_IND_SCRATCH_16, asm_jit_MODE_IND_SCRATCH_16_END);
}

void
asm_emit_jit_MODE_ZPX(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  if (value <= 0x7F) {
    asm_copy(p_buf, asm_jit_MODE_ZPX_8bit, asm_jit_MODE_ZPX_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_MODE_ZPX_8bit,
                   asm_jit_MODE_ZPX_8bit_lea_patch,
                   value);
  } else {
    asm_copy(p_buf, asm_jit_MODE_ZPX, asm_jit_MODE_ZPX_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_MODE_ZPX,
                  asm_jit_MODE_ZPX_lea_patch,
                  value);
  }
}

void
asm_emit_jit_MODE_ZPY(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  if (value <= 0x7F) {
    asm_copy(p_buf, asm_jit_MODE_ZPY_8bit, asm_jit_MODE_ZPY_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_MODE_ZPY_8bit,
                   asm_jit_MODE_ZPY_8bit_lea_patch,
                   value);
  } else {
    asm_copy(p_buf, asm_jit_MODE_ZPY, asm_jit_MODE_ZPY_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_MODE_ZPY,
                  asm_jit_MODE_ZPY_lea_patch,
                  value);
  }
}

void
asm_emit_jit_PULL_16(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_PULL_16, asm_jit_PULL_16_END);
}

void
asm_emit_jit_PUSH_16(struct util_buffer* p_buf, uint16_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_PUSH_16, asm_jit_PUSH_16_END);
  asm_patch_u16(p_buf,
                offset,
                asm_jit_PUSH_16,
                asm_jit_PUSH_16_word_patch,
                value);
}

void
asm_emit_jit_SAVE_CARRY(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_SAVE_CARRY_INV(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_SAVE_CARRY_INV, asm_jit_SAVE_CARRY_INV_END);
}

void
asm_emit_jit_SAVE_OVERFLOW(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_SAVE_OVERFLOW, asm_jit_SAVE_OVERFLOW_END);
}

void
asm_emit_jit_SET_CARRY(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_SET_CARRY, asm_jit_SET_CARRY_END);
}

void
asm_emit_jit_STOA_IMM(struct util_buffer* p_buf, uint16_t addr, uint8_t value) {
  void asm_jit_STOA_IMM(void);
  void asm_jit_STOA_IMM_END(void);
  void asm_jit_STOA_IMM_8bit(void);
  void asm_jit_STOA_IMM_8bit_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  if (addr < 0x100) {
    asm_copy(p_buf, asm_jit_STOA_IMM_8bit, asm_jit_STOA_IMM_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_STOA_IMM_8bit,
                   asm_jit_STOA_IMM_8bit_END,
                   value);
    asm_patch_byte(p_buf,
                   (offset - 1),
                   asm_jit_STOA_IMM_8bit,
                   asm_jit_STOA_IMM_8bit_END,
                   (addr - REG_MEM_OFFSET));
  } else {
    asm_copy(p_buf, asm_jit_STOA_IMM, asm_jit_STOA_IMM_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_STOA_IMM,
                   asm_jit_STOA_IMM_END,
                   value);
    asm_patch_int(p_buf,
                  (offset - 1),
                  asm_jit_STOA_IMM,
                  asm_jit_STOA_IMM_END,
                  (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
  }
}

void
asm_emit_jit_SUB_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_SUB_ZPG,
                        asm_jit_SUB_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_SUB_ABS,
                       asm_jit_SUB_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_SUB_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_SUB_IMM, asm_jit_SUB_IMM_END, value);
}

void
asm_emit_jit_WRITE_INV_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_WRITE_INV_ABS, asm_jit_WRITE_INV_ABS_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_WRITE_INV_ABS,
                asm_jit_WRITE_INV_ABS_offset_patch,
                (K_JIT_CONTEXT_OFFSET_JIT_PTRS + (addr * sizeof(uint32_t))));
}

void
asm_emit_jit_WRITE_INV_SCRATCH(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_WRITE_INV_SCRATCH, asm_jit_WRITE_INV_SCRATCH_END);
}

void
asm_emit_jit_WRITE_INV_SCRATCH_n(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_WRITE_INV_SCRATCH_n_8bit(void);
  void asm_jit_WRITE_INV_SCRATCH_n_8bit_lea_patch(void);
  void asm_jit_WRITE_INV_SCRATCH_n_8bit_END(void);
  void asm_jit_WRITE_INV_SCRATCH_n_32bit(void);
  void asm_jit_WRITE_INV_SCRATCH_n_32bit_lea_patch(void);
  void asm_jit_WRITE_INV_SCRATCH_n_32bit_END(void);

  size_t offset = util_buffer_get_pos(p_buf);

  if (value < 0x80) {
    asm_copy(p_buf,
             asm_jit_WRITE_INV_SCRATCH_n_8bit,
             asm_jit_WRITE_INV_SCRATCH_n_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_WRITE_INV_SCRATCH_n_8bit,
                   asm_jit_WRITE_INV_SCRATCH_n_8bit_lea_patch,
                   value);
  } else {
    asm_copy(p_buf,
             asm_jit_WRITE_INV_SCRATCH_n_32bit,
             asm_jit_WRITE_INV_SCRATCH_n_32bit_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_WRITE_INV_SCRATCH_n_32bit,
                  asm_jit_WRITE_INV_SCRATCH_n_32bit_lea_patch,
                  value);
  }
}

void
asm_emit_jit_WRITE_INV_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_WRITE_INV_SCRATCH_Y, asm_jit_WRITE_INV_SCRATCH_Y_END);
}

void
asm_emit_jit_ADC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ADC_ZPG,
                        asm_jit_ADC_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ADC_ABS,
                       asm_jit_ADC_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_ADC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADC_ABX,
                     asm_jit_ADC_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_ADC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADC_ABY,
                     asm_jit_ADC_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_ADC_IMM, asm_jit_ADC_IMM_END, value);
}

void
asm_emit_jit_ADC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADC_SCRATCH,
                     asm_jit_ADC_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_ADC_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ADC_SCRATCH_Y, asm_jit_ADC_SCRATCH_Y_END);
}

void
asm_emit_jit_ALR_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ALR_IMM, asm_jit_ALR_IMM_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_ALR_IMM,
                 asm_jit_ALR_IMM_patch_byte,
                 value);
}

void
asm_emit_jit_AND_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_AND_ZPG,
                        asm_jit_AND_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_AND_ABS,
                       asm_jit_AND_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_AND_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_AND_ABX,
                     asm_jit_AND_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_AND_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_AND_ABY,
                     asm_jit_AND_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_AND_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_AND_IMM, asm_jit_AND_IMM_END, value);
}

void
asm_emit_jit_AND_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_AND_SCRATCH,
                     asm_jit_AND_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_AND_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_AND_SCRATCH_Y, asm_jit_AND_SCRATCH_Y_END);
}

void
asm_emit_jit_ASL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ASL_ZPG,
                        asm_jit_ASL_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ASL_ABS,
                       asm_jit_ASL_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_ASL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ASL_ABS_RMW, asm_jit_ASL_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABS_RMW,
                asm_jit_ASL_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABS_RMW,
                asm_jit_ASL_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_ASL_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ASL_ABX,
                     asm_jit_ASL_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_ASL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ASL_ABX_RMW, asm_jit_ASL_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABX_RMW,
                asm_jit_ASL_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABX_RMW,
                asm_jit_ASL_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_ASL_ACC(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ASL_ACC, asm_jit_ASL_ACC_END);
}

void
asm_emit_jit_ASL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  asm_copy_patch_byte(p_buf, asm_jit_ASL_ACC_n, asm_jit_ASL_ACC_n_END, n);
}

void
asm_emit_jit_ASL_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ASL_scratch, asm_jit_ASL_scratch_END);
}

void
asm_emit_jit_BCC(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BCC,
                    asm_jit_BCC_END,
                    asm_jit_BCC_8bit,
                    asm_jit_BCC_8bit_END);
}

void
asm_emit_jit_BCS(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BCS,
                    asm_jit_BCS_END,
                    asm_jit_BCS_8bit,
                    asm_jit_BCS_8bit_END);
}

void
asm_emit_jit_BEQ(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BEQ,
                    asm_jit_BEQ_END,
                    asm_jit_BEQ_8bit,
                    asm_jit_BEQ_8bit_END);
}

void
asm_emit_jit_BIT(struct util_buffer* p_buf, uint16_t addr) {
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_BIT_ZPG,
                        asm_jit_BIT_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_BIT_ABS,
                       asm_jit_BIT_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
  asm_emit_instruction_BIT_common(p_buf);
}

void
asm_emit_jit_BMI(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BMI,
                    asm_jit_BMI_END,
                    asm_jit_BMI_8bit,
                    asm_jit_BMI_8bit_END);
}

void
asm_emit_jit_BNE(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BNE,
                    asm_jit_BNE_END,
                    asm_jit_BNE_8bit,
                    asm_jit_BNE_8bit_END);
}

void
asm_emit_jit_BPL(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BPL,
                    asm_jit_BPL_END,
                    asm_jit_BPL_8bit,
                    asm_jit_BPL_8bit_END);
}

void
asm_emit_jit_BVC(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BVC,
                    asm_jit_BVC_END,
                    asm_jit_BVC_8bit,
                    asm_jit_BVC_8bit_END);
}

void
asm_emit_jit_BVS(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BVS,
                    asm_jit_BVS_END,
                    asm_jit_BVS_8bit,
                    asm_jit_BVS_8bit_END);
}

void
asm_emit_jit_CMP_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_CMP_ZPG,
                        asm_jit_CMP_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_CMP_ABS,
                       asm_jit_CMP_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_CMP_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_CMP_ABX,
                     asm_jit_CMP_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_CMP_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_CMP_ABY,
                     asm_jit_CMP_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_CMP_IMM, asm_jit_CMP_IMM_END, value);
}

void
asm_emit_jit_CMP_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_CMP_SCRATCH,
                     asm_jit_CMP_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_CMP_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_CMP_SCRATCH_Y, asm_jit_CMP_SCRATCH_Y_END);
}

void
asm_emit_jit_CPX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_CPX_ZPG,
                        asm_jit_CPX_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_CPX_ABS,
                       asm_jit_CPX_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_CPX_IMM, asm_jit_CPX_IMM_END, value);
}

void
asm_emit_jit_CPY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_CPY_ZPG,
                        asm_jit_CPY_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_CPY_ABS,
                       asm_jit_CPY_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_CPY_IMM, asm_jit_CPY_IMM_END, value);
}

void
asm_emit_jit_DEC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_DEC_ZPG,
                        asm_jit_DEC_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_DEC_ABS,
                       asm_jit_DEC_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_DEC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_DEC_ABS_RMW, asm_jit_DEC_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABS_RMW,
                asm_jit_DEC_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABS_RMW,
                asm_jit_DEC_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_DEC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_DEC_ABX,
                     asm_jit_DEC_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_DEC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_DEC_ABX_RMW, asm_jit_DEC_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABX_RMW,
                asm_jit_DEC_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABX_RMW,
                asm_jit_DEC_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_DEC_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_DEC_scratch, asm_jit_DEC_scratch_END);
}

void
asm_emit_jit_EOR_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_EOR_ZPG,
                        asm_jit_EOR_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_EOR_ABS,
                       asm_jit_EOR_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_EOR_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_EOR_ABX,
                     asm_jit_EOR_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_EOR_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_EOR_ABY,
                     asm_jit_EOR_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_EOR_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_EOR_IMM, asm_jit_EOR_IMM_END, value);
}

void
asm_emit_jit_EOR_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_EOR_SCRATCH,
                     asm_jit_EOR_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_EOR_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_EOR_SCRATCH_Y, asm_jit_EOR_SCRATCH_Y_END);
}

void
asm_emit_jit_INC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_INC_ZPG,
                        asm_jit_INC_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_INC_ABS,
                       asm_jit_INC_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_INC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_INC_ABS_RMW, asm_jit_INC_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABS_RMW,
                asm_jit_INC_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABS_RMW,
                asm_jit_INC_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_INC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_INC_ABX,
                     asm_jit_INC_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_INC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_INC_ABX_RMW, asm_jit_INC_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABX_RMW,
                asm_jit_INC_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABX_RMW,
                asm_jit_INC_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_INC_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_INC_scratch, asm_jit_INC_scratch_END);
}

void
asm_emit_jit_JMP(struct util_buffer* p_buf, void* p_target) {
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_JMP,
                    asm_jit_JMP_END,
                    asm_jit_JMP_8bit,
                    asm_jit_JMP_8bit_END);
}

void
asm_emit_jit_LDA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LDA_ZPG,
                        asm_jit_LDA_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LDA_ABS,
                       asm_jit_LDA_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_LDA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDA_ABX,
                     asm_jit_LDA_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_LDA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDA_ABY,
                     asm_jit_LDA_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_u32(p_buf, asm_jit_LDA_IMM, asm_jit_LDA_IMM_END, value);
}

void
asm_emit_jit_LDA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_byte(p_buf,
                      asm_jit_LDA_SCRATCH,
                      asm_jit_LDA_SCRATCH_END,
                      (offset - REG_MEM_OFFSET));
}

void
asm_emit_jit_LDA_SCRATCH_X(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LDA_SCRATCH_X, asm_jit_LDA_SCRATCH_X_END);
}

void
asm_emit_jit_LDA_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LDA_SCRATCH_Y, asm_jit_LDA_SCRATCH_Y_END);
}

void
asm_emit_jit_LDX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LDX_ZPG,
                        asm_jit_LDX_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LDX_ABS,
                       asm_jit_LDX_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_LDX_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDX_ABY,
                     asm_jit_LDX_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_LDX_IMM, asm_jit_LDX_IMM_END, value);
}

void
asm_emit_jit_LDX_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LDX_scratch, asm_jit_LDX_scratch_END);
}

void
asm_emit_jit_LDY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LDY_ZPG,
                        asm_jit_LDY_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LDY_ABS,
                       asm_jit_LDY_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_LDY_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDY_ABX,
                     asm_jit_LDY_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_LDY_IMM, asm_jit_LDY_IMM_END, value);
}

void
asm_emit_jit_LDY_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LDY_scratch, asm_jit_LDY_scratch_END);
}

void
asm_emit_jit_LSR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LSR_ZPG,
                        asm_jit_LSR_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LSR_ABS,
                       asm_jit_LSR_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_LSR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_LSR_ABS_RMW, asm_jit_LSR_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABS_RMW,
                asm_jit_LSR_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABS_RMW,
                asm_jit_LSR_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_LSR_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_LSR_ABX,
                     asm_jit_LSR_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_LSR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_LSR_ABX_RMW, asm_jit_LSR_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABX_RMW,
                asm_jit_LSR_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABX_RMW,
                asm_jit_LSR_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_LSR_ACC(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LSR_ACC, asm_jit_LSR_ACC_END);
}

void
asm_emit_jit_LSR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  asm_copy_patch_byte(p_buf, asm_jit_LSR_ACC_n, asm_jit_LSR_ACC_n_END, n);
}

void
asm_emit_jit_LSR_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_LSR_scratch, asm_jit_LSR_scratch_END);
}

void
asm_emit_jit_ORA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ORA_ZPG,
                        asm_jit_ORA_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ORA_ABS,
                       asm_jit_ORA_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_ORA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ORA_ABX,
                     asm_jit_ORA_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_ORA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ORA_ABY,
                     asm_jit_ORA_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_ORA_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_ORA_IMM, asm_jit_ORA_IMM_END, value);
}

void
asm_emit_jit_ORA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_ORA_SCRATCH,
                     asm_jit_ORA_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_ORA_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ORA_SCRATCH_Y, asm_jit_ORA_SCRATCH_Y_END);
}

void
asm_emit_jit_ROL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ROL_ZPG,
                        asm_jit_ROL_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ROL_ABS,
                       asm_jit_ROL_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_ROL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROL_ABS_RMW, asm_jit_ROL_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABS_RMW,
                asm_jit_ROL_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABS_RMW,
                asm_jit_ROL_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_ROL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROL_ABX_RMW, asm_jit_ROL_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABX_RMW,
                asm_jit_ROL_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABX_RMW,
                asm_jit_ROL_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_ROL_ACC(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ROL_ACC, asm_jit_ROL_ACC_END);
}

void
asm_emit_jit_ROL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  asm_copy_patch_byte(p_buf,
                          asm_jit_ROL_ACC_n,
                          asm_jit_ROL_ACC_n_END,
                          n);
}

void
asm_emit_jit_ROL_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ROL_scratch, asm_jit_ROL_scratch_END);
}

void
asm_emit_jit_ROR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ROR_ZPG,
                        asm_jit_ROR_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ROR_ABS,
                       asm_jit_ROR_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_ROR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROR_ABS_RMW, asm_jit_ROR_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABS_RMW,
                asm_jit_ROR_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABS_RMW,
                asm_jit_ROR_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_ROR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROR_ABX_RMW, asm_jit_ROR_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABX_RMW,
                asm_jit_ROR_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABX_RMW,
                asm_jit_ROR_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_ROR_ACC(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ROR_ACC, asm_jit_ROR_ACC_END);
}

void
asm_emit_jit_ROR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  asm_copy_patch_byte(p_buf, asm_jit_ROR_ACC_n, asm_jit_ROR_ACC_n_END, n);
}

void
asm_emit_jit_ROR_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_ROR_scratch, asm_jit_ROR_scratch_END);
}

void
asm_emit_jit_SAX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_SAX_ABS,
                     asm_jit_SAX_ABS_END,
                     (addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_SBC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_SBC_ZPG,
                        asm_jit_SBC_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_SBC_ABS,
                       asm_jit_SBC_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_SBC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_SBC_ABX,
                     asm_jit_SBC_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_SBC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_SBC_ABY,
                     asm_jit_SBC_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_SBC_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_copy_patch_byte(p_buf, asm_jit_SBC_IMM, asm_jit_SBC_IMM_END, value);
}

void
asm_emit_jit_SBC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_SBC_SCRATCH,
                     asm_jit_SBC_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_SBC_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_SBC_SCRATCH_Y, asm_jit_SBC_SCRATCH_Y_END);
}

void
asm_emit_jit_SHY_ABX(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);
  uint8_t value = ((addr >> 8) + 1);

  asm_copy(p_buf, asm_jit_SHY_ABX, asm_jit_SHY_ABX_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_SHY_ABX,
                 asm_jit_SHY_ABX_byte_patch,
                 value);
  asm_patch_int(p_buf,
                offset,
                asm_jit_SHY_ABX,
                asm_jit_SHY_ABX_mov_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_SLO_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_SLO_ABS, asm_jit_SLO_ABS_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_SLO_ABS,
                asm_jit_SLO_ABS_mov1_patch,
                (addr - REG_MEM_OFFSET));
  asm_patch_int(p_buf,
                offset,
                asm_jit_SLO_ABS,
                asm_jit_SLO_ABS_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_STA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_STA_ZPG,
                        asm_jit_STA_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(
        p_buf,
        asm_jit_STA_ABS,
        asm_jit_STA_ABS_END,
        (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_STA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_STA_ABX,
                     asm_jit_STA_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_STA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_STA_ABY,
                     asm_jit_STA_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_STA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  asm_copy_patch_u32(p_buf,
                     asm_jit_STA_SCRATCH,
                     asm_jit_STA_SCRATCH_END,
                     (K_BBC_MEM_WRITE_IND_ADDR + offset));
}

void
asm_emit_jit_STA_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_STA_SCRATCH_Y, asm_jit_STA_SCRATCH_Y_END);
}

void
asm_emit_jit_STX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_STX_ZPG,
                        asm_jit_STX_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_STX_ABS,
                       asm_jit_STX_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_STX_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_STX_scratch, asm_jit_STX_scratch_END);
}

void
asm_emit_jit_STY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_STY_ZPG,
                        asm_jit_STY_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_STY_ABS,
                       asm_jit_STY_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_STY_scratch(struct util_buffer* p_buf) {
  asm_copy(p_buf, asm_jit_STY_scratch, asm_jit_STY_scratch_END);
}
