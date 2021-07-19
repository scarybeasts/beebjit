#include "../asm_jit.h"

#include "../../util.h"
#include "../asm_common.h"
#include "../asm_opcodes.h"
#include "asm_helper_arm64.h"

#include <assert.h>

static void
asm_emit_jit_SCRATCH_ADD_X(struct util_buffer* p_buf) {
  void asm_jit_SCRATCH_ADD_X(void);
  void asm_jit_SCRATCH_ADD_X_END(void);
  asm_copy(p_buf, asm_jit_SCRATCH_ADD_X, asm_jit_SCRATCH_ADD_X_END);
}

static void
asm_emit_jit_SCRATCH_ADD_Y(struct util_buffer* p_buf) {
  void asm_jit_SCRATCH_ADD_Y(void);
  void asm_jit_SCRATCH_ADD_Y_END(void);
  asm_copy(p_buf, asm_jit_SCRATCH_ADD_Y, asm_jit_SCRATCH_ADD_Y_END);
}

static void
asm_emit_jit_SCRATCH_SET(struct util_buffer* p_buf, uint32_t value) {
  void asm_jit_SCRATCH_SET(void);
  void asm_jit_SCRATCH_SET_END(void);
  void asm_jit_SCRATCH_SET_HI(void);
  void asm_jit_SCRATCH_SET_HI_END(void);
  if (value <= 0xFFFF) {
    asm_copy_patch_arm64_imm16(p_buf,
                               asm_jit_SCRATCH_SET,
                               asm_jit_SCRATCH_SET_END,
                               value);
  } else {
    assert((value & 0xFFFF) == 0);
    asm_copy_patch_arm64_imm16(p_buf,
                               asm_jit_SCRATCH_SET_HI,
                               asm_jit_SCRATCH_SET_HI_END,
                               (value >> 16));
  }
}

static void
asm_emit_jit_SCRATCH3_SET(struct util_buffer* p_buf, uint32_t value) {
  void asm_jit_SCRATCH3_SET(void);
  void asm_jit_SCRATCH3_SET_END(void);
  if (value <= 0xFFFF) {
    asm_copy_patch_arm64_imm16(p_buf,
                               asm_jit_SCRATCH3_SET,
                               asm_jit_SCRATCH3_SET_END,
                               value);
  } else {
    assert(0);
  }
}

static void
asm_emit_jit_SCRATCH_LOAD_SCRATCH(struct util_buffer* p_buf) {
  void asm_jit_SCRATCH_LOAD_SCRATCH(void);
  void asm_jit_SCRATCH_LOAD_SCRATCH_END(void);
  asm_copy(p_buf,
           asm_jit_SCRATCH_LOAD_SCRATCH,
           asm_jit_SCRATCH_LOAD_SCRATCH_END);
}

static void
asm_emit_jit_SCRATCH_LOAD(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_SCRATCH_LOAD_12bit(void);
  void asm_jit_SCRATCH_LOAD_12bit_END(void);
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_SCRATCH_LOAD_12bit,
                               asm_jit_SCRATCH_LOAD_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  }
}

static void
asm_emit_jit_SCRATCH2_LOAD(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_SCRATCH2_LOAD_12bit(void);
  void asm_jit_SCRATCH2_LOAD_12bit_END(void);
  void asm_jit_SCRATCH2_LOAD_SCRATCH3(void);
  void asm_jit_SCRATCH2_LOAD_SCRATCH3_END(void);
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_SCRATCH2_LOAD_12bit,
                               asm_jit_SCRATCH2_LOAD_12bit_END,
                               addr);
  } else {
    /* This uses SCRATCH3 as a temporary because in the case of LOAD_BYTE_PAIR,
     * SCRATCH1 needs to be preserved.
     */
    asm_emit_jit_SCRATCH3_SET(p_buf, addr);
    asm_copy(p_buf,
             asm_jit_SCRATCH2_LOAD_SCRATCH3,
             asm_jit_SCRATCH2_LOAD_SCRATCH3_END);
  }
}

static void
asm_emit_jit_SCRATCH2_LOAD_SCRATCH(struct util_buffer* p_buf) {
  void asm_jit_SCRATCH2_LOAD_SCRATCH(void);
  void asm_jit_SCRATCH2_LOAD_SCRATCH_END(void);
  asm_copy(p_buf,
           asm_jit_SCRATCH2_LOAD_SCRATCH,
           asm_jit_SCRATCH2_LOAD_SCRATCH_END);
}

static void
asm_emit_jit_SCRATCH2_STORE(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_SCRATCH2_STORE_12bit(void);
  void asm_jit_SCRATCH2_STORE_12bit_END(void);
  void asm_jit_SCRATCH2_STORE_SCRATCH3(void);
  void asm_jit_SCRATCH2_STORE_SCRATCH3_END(void);
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_SCRATCH2_STORE_12bit,
                               asm_jit_SCRATCH2_STORE_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH3_SET(p_buf, addr);
    asm_copy(p_buf,
             asm_jit_SCRATCH2_STORE_SCRATCH3,
             asm_jit_SCRATCH2_STORE_SCRATCH3_END);
  }
}

static void
asm_emit_jit_SCRATCH2_STORE_SCRATCH(struct util_buffer* p_buf) {
  void asm_jit_SCRATCH2_STORE_SCRATCH(void);
  void asm_jit_SCRATCH2_STORE_SCRATCH_END(void);
  asm_copy(p_buf,
           asm_jit_SCRATCH2_STORE_SCRATCH,
           asm_jit_SCRATCH2_STORE_SCRATCH_END);
}

static void
asm_emit_jit_ABS_RMW(struct util_buffer* p_buf,
                     uint16_t addr,
                     void* p_start,
                     void* p_end) {
  asm_emit_jit_SCRATCH2_LOAD(p_buf, addr);
  asm_copy(p_buf, p_start, p_end);
  /* TODO: this re-loads address into SCRATCH1, wasteful! */
  asm_emit_jit_SCRATCH2_STORE(p_buf, addr);
}

static void
asm_emit_jit_ABX_RMW(struct util_buffer* p_buf,
                     uint16_t addr,
                     void* p_start,
                     void* p_end) {
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_SCRATCH2_LOAD_SCRATCH(p_buf);
  asm_copy(p_buf, p_start, p_end);
  /* Re-uses the address in SCRACTH1 calculated by MODE_ABX above. */
  asm_emit_jit_SCRATCH2_STORE_SCRATCH(p_buf);
}

static void
asm_emit_jit_LOAD_BYTE_PAIR(struct util_buffer* p_buf,
                            uint16_t addr1,
                            uint16_t addr2) {
  void asm_jit_LOAD_BYTE_PAIR_or(void);
  void asm_jit_LOAD_BYTE_PAIR_or_END(void);
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr1);
  asm_emit_jit_SCRATCH2_LOAD(p_buf, addr2);
  asm_copy(p_buf, asm_jit_LOAD_BYTE_PAIR_or, asm_jit_LOAD_BYTE_PAIR_or_END);
}

static void
asm_emit_jit_RMW(struct util_buffer* p_buf, void* p_start, void* p_end) {
  asm_emit_jit_SCRATCH2_LOAD_SCRATCH(p_buf);
  asm_copy(p_buf, p_start, p_end);
  asm_emit_jit_SCRATCH2_STORE_SCRATCH(p_buf);
}

int
asm_jit_is_enabled(void) {
  return 1;
}

void
asm_jit_test_preconditions(void) {
}

int
asm_jit_supports_optimizer(void) {
  return 0;
}

int
asm_jit_supports_uopcode(int32_t uopcode) {
  int ret = 1;

  /* Some uopcodes don't make sense on ARM64 because they would be more likely
   * a pessimization than an optimization.
   * One example is STOA, which writes a constant to a memory location. ARM64
   * does not have a single instruction to do this, unlike x64. In the case of
   * STOA, keeping with the original load / store sequence avoids potentially
   * adding an extra constant load.
   */
  switch (uopcode) {
  case k_opcode_STOA_IMM:
    ret = 0;
    break;
  default:
    break;
  }

  return ret;
}

void
asm_jit_invalidate_code_at(void* p) {
  uint32_t* p_dst = (uint32_t*) p;
  /* blr x29 */
  *p_dst = 0xd63f03a0;
}

void
asm_emit_jit_invalidated(struct util_buffer* p_buf) {
  /* blr x29 */
  util_buffer_add_4b(p_buf, 0xa0, 0x03, 0x3f, 0xd6);
}

void
asm_emit_jit_call_compile_trampoline(struct util_buffer* p_buf) {
  /* TODO: get rid of this function. */
  (void) p_buf;
}

void
asm_emit_jit_jump_interp_trampoline(struct util_buffer* p_buf, uint16_t addr) {
  /* TODO: get rid of this function. */
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_check_countdown(struct util_buffer* p_buf,
                             struct util_buffer* p_buf_epilog,
                             uint32_t count,
                             uint16_t addr,
                             void* p_trampoline) {
  void asm_jit_countdown_sub(void);
  void asm_jit_countdown_sub_END(void);
  void asm_jit_countdown_tbnz(void);
  void asm_jit_countdown_tbnz_END(void);
  void* p_target = util_buffer_get_base_address(p_buf_epilog);
  (void) p_trampoline;
  asm_copy_patch_arm64_imm12(p_buf,
                             asm_jit_countdown_sub,
                             asm_jit_countdown_sub_END,
                             count);
  asm_copy_patch_arm64_imm14_pc_rel(p_buf,
                                    asm_jit_countdown_tbnz,
                                    asm_jit_countdown_tbnz_END,
                                    p_target);
  asm_emit_jit_jump_interp(p_buf_epilog, addr);
}

void
asm_emit_jit_check_countdown_no_save_nz_flags(struct util_buffer* p_buf,
                                              uint32_t count,
                                              void* p_trampoline) {
  (void) p_buf;
  (void) count;
  (void) p_trampoline;
  assert(0);
}

void
asm_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_load_PC(void);
  void asm_jit_load_PC_END(void);
  void asm_jit_call_debug(void);
  void asm_jit_call_debug_END(void);
  asm_copy_patch_arm64_imm16(p_buf, asm_jit_load_PC, asm_jit_load_PC_END, addr);
  asm_copy(p_buf, asm_jit_call_debug, asm_jit_call_debug_END);
}

void
asm_emit_jit_jump_interp(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_load_PC(void);
  void asm_jit_load_PC_END(void);
  void asm_jit_jump_interp(void);
  void asm_jit_jump_interp_END(void);
  asm_copy_patch_arm64_imm16(p_buf, asm_jit_load_PC, asm_jit_load_PC_END, addr);
  asm_copy(p_buf, asm_jit_jump_interp, asm_jit_jump_interp_END);
}

void
asm_emit_jit_call_inturbo(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_for_testing(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_ADD_CYCLES(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
  assert(0);
}

void
asm_emit_jit_ADD_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
  assert(0);
}

void
asm_emit_jit_ADD_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
  assert(0);
}

void
asm_emit_jit_ADD_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
  assert(0);
}

void
asm_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
  assert(0);
}

void
asm_emit_jit_ADD_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
  assert(0);
}

void
asm_emit_jit_ADD_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_CHECK_BCD(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_n(struct util_buffer* p_buf,
                                           uint8_t n) {
  (void) p_buf;
  (void) n;
  assert(0);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_X(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_X_n(struct util_buffer* p_buf,
                                     uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_Y_n(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_CHECK_PENDING_IRQ(struct util_buffer* p_buf, void* p_trampoline) {
  (void) p_buf;
  (void) p_trampoline;
}

void
asm_emit_jit_CLEAR_CARRY(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_FLAG_MEM(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_INVERT_CARRY(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_JMP_SCRATCH_n(struct util_buffer* p_buf, uint16_t n) {
  void asm_jit_SCRATCH_ADD(void);
  void asm_jit_SCRATCH_ADD_END(void);
  void asm_jit_SCRATCH_JMP(void);
  void asm_jit_SCRATCH_JMP_END(void);
  assert(n < 4096);
  if (n != 0) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_SCRATCH_ADD,
                               asm_jit_SCRATCH_ADD_END,
                               n);
  }
  asm_copy(p_buf, asm_jit_SCRATCH_JMP, asm_jit_SCRATCH_JMP_END);
}

void
asm_emit_jit_LDA_Z(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_LDX_Z(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_LDY_Z(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_LOAD_CARRY_FOR_BRANCH(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LOAD_CARRY_FOR_CALC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LOAD_CARRY_INV_FOR_CALC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LOAD_OVERFLOW(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LOAD_SCRATCH_8(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_LOAD_SCRATCH_16(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_MODE_ABX(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_MODE_ABX_12bit(void);
  void asm_jit_MODE_ABX_12bit_END(void);
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_MODE_ABX_12bit,
                               asm_jit_MODE_ABX_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_SCRATCH_ADD_X(p_buf);
  }
}

void
asm_emit_jit_MODE_ABY(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_MODE_ABY_12bit(void);
  void asm_jit_MODE_ABY_12bit_END(void);
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_MODE_ABY_12bit,
                               asm_jit_MODE_ABY_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_SCRATCH_ADD_Y(p_buf);
  }
}

void
asm_emit_jit_MODE_IND_16(struct util_buffer* p_buf,
                         uint16_t addr,
                         uint32_t segment) {
  /* Wraps around 0x10FF -> 0x1000. */
  uint16_t next_addr;
  (void) segment;
  if ((addr & 0xFF) == 0xFF) {
    next_addr = (addr & 0xFF00);
  } else {
    next_addr = (addr + 1);
  }
  asm_emit_jit_LOAD_BYTE_PAIR(p_buf, addr, next_addr);
}

void
asm_emit_jit_MODE_IND_8(struct util_buffer* p_buf, uint8_t addr) {
  /* Wraps around 0xFF -> 0x00. */
  uint8_t next_addr = (addr + 1);
  asm_emit_jit_LOAD_BYTE_PAIR(p_buf, addr, next_addr);
}

void
asm_emit_jit_MODE_IND_SCRATCH_8(struct util_buffer* p_buf) {
  void asm_jit_MODE_IND_SCRATCH_8(void);
  void asm_jit_MODE_IND_SCRATCH_8_END(void);
  asm_copy(p_buf, asm_jit_MODE_IND_SCRATCH_8, asm_jit_MODE_IND_SCRATCH_8_END);
}

void
asm_emit_jit_MODE_IND_SCRATCH_16(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_MODE_ZPX(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_SCRATCH_TRUNC_8(void);
  void asm_jit_SCRATCH_TRUNC_8_END(void);
  asm_emit_jit_MODE_ABX(p_buf, value);
  asm_copy(p_buf, asm_jit_SCRATCH_TRUNC_8, asm_jit_SCRATCH_TRUNC_8_END);
}

void
asm_emit_jit_MODE_ZPY(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_SCRATCH_TRUNC_8(void);
  void asm_jit_SCRATCH_TRUNC_8_END(void);
  asm_emit_jit_MODE_ABY(p_buf, value);
  asm_copy(p_buf, asm_jit_SCRATCH_TRUNC_8, asm_jit_SCRATCH_TRUNC_8_END);
}

void
asm_emit_jit_PULL_16(struct util_buffer* p_buf) {
  void asm_jit_PULL_16(void);
  void asm_jit_PULL_16_END(void);
  asm_copy(p_buf, asm_jit_PULL_16, asm_jit_PULL_16_END);
}

void
asm_emit_jit_PUSH_16(struct util_buffer* p_buf, uint16_t value) {
  void asm_jit_PUSH_16_store_dec(void);
  void asm_jit_PUSH_16_store_dec_END(void);
  asm_emit_jit_SCRATCH_SET(p_buf, value);
  asm_copy(p_buf, asm_jit_PUSH_16_store_dec, asm_jit_PUSH_16_store_dec_END);
}

void
asm_emit_jit_SAVE_CARRY(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_SAVE_CARRY_INV(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_SAVE_OVERFLOW(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_SET_CARRY(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_STOA_IMM(struct util_buffer* p_buf, uint16_t addr, uint8_t value) {
  (void) p_buf;
  (void) addr;
  (void) value;
  assert(0);
}

void
asm_emit_jit_SUB_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
  assert(0);
}

void
asm_emit_jit_SUB_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
  assert(0);
}

void
asm_emit_jit_WRITE_INV_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_WRITE_INV_SCRATCH(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_WRITE_INV_SCRATCH_n(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
  assert(0);
}

void
asm_emit_jit_WRITE_INV_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ADC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_emit_jit_ADC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_ADC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_ADC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_ADC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABY(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_ADC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_ADC_IMM(void);
  void asm_jit_ADC_IMM_END(void);
  asm_emit_jit_SCRATCH_SET(p_buf, (value << 24));
  asm_copy(p_buf, asm_jit_ADC_IMM, asm_jit_ADC_IMM_END);
}

void
asm_emit_jit_ADC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_ADC_SCRATCH(void);
  void asm_jit_ADC_SCRATCH_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_ADC_SCRATCH, asm_jit_ADC_SCRATCH_END);
}

void
asm_emit_jit_ADC_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_emit_jit_SCRATCH_ADD_Y(p_buf);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_ADC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_ALR_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
  assert(0);
}

void
asm_emit_jit_AND_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_emit_jit_AND_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_AND_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
  assert(0);
}

void
asm_emit_jit_AND_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
  assert(0);
}

void
asm_emit_jit_AND_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_AND_IMM(void);
  void asm_jit_AND_IMM_END(void);
  uint8_t immr;
  uint8_t imms;
  if (asm_calculate_immr_imms(&immr, &imms, value)) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_AND_IMM,
                               asm_jit_AND_IMM_END,
                               ((immr << 6) | imms));
    asm_emit_instruction_A_NZ_flags(p_buf);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, value);
    asm_emit_jit_AND_SCRATCH(p_buf, 0);
  }
}

void
asm_emit_jit_AND_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_AND_SCRATCH(void);
  void asm_jit_AND_SCRATCH_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_AND_SCRATCH, asm_jit_AND_SCRATCH_END);
}

void
asm_emit_jit_AND_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_ASL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_ASL_ABS_RMW(p_buf, addr);
}

void
asm_emit_jit_ASL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ASL_SCRATCH2(void);
  void asm_jit_ASL_SCRATCH2_END(void);
  asm_emit_jit_ABS_RMW(p_buf,
                       addr,
                       asm_jit_ASL_SCRATCH2,
                       asm_jit_ASL_SCRATCH2_END);
}

void
asm_emit_jit_ASL_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_ASL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_ASL_ACC(struct util_buffer* p_buf) {
  void asm_jit_ASL_ACC(void);
  void asm_jit_ASL_ACC_END(void);
  asm_copy(p_buf, asm_jit_ASL_ACC, asm_jit_ASL_ACC_END);
}

void
asm_emit_jit_ASL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
  assert(0);
}

void
asm_emit_jit_ASL_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_BCC(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BCC(void);
  void asm_jit_BCC_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BCC,
                                    asm_jit_BCC_END,
                                    p_target);
}

void
asm_emit_jit_BCS(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BCS(void);
  void asm_jit_BCS_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BCS,
                                    asm_jit_BCS_END,
                                    p_target);
}

void
asm_emit_jit_BEQ(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BEQ(void);
  void asm_jit_BEQ_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BEQ,
                                    asm_jit_BEQ_END,
                                    p_target);
}

void
asm_emit_jit_BIT(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_emit_instruction_BIT_common(p_buf);
}

void
asm_emit_jit_BMI(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BMI(void);
  void asm_jit_BMI_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BMI,
                                    asm_jit_BMI_END,
                                    p_target);
}

void
asm_emit_jit_BNE(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BNE(void);
  void asm_jit_BNE_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BNE,
                                    asm_jit_BNE_END,
                                    p_target);
}

void
asm_emit_jit_BPL(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BPL(void);
  void asm_jit_BPL_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BPL,
                                    asm_jit_BPL_END,
                                    p_target);
}

void
asm_emit_jit_BVC(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BVC(void);
  void asm_jit_BVC_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BVC,
                                    asm_jit_BVC_END,
                                    p_target);
}

void
asm_emit_jit_BVS(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BVS(void);
  void asm_jit_BVS_END(void);
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,
                                    asm_jit_BVS,
                                    asm_jit_BVS_END,
                                    p_target);
}

void
asm_emit_jit_CMP_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_emit_jit_CMP_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_CMP_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_CMP_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_CMP_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABY(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_CMP_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_CMP_IMM(void);
  void asm_jit_CMP_IMM_END(void);
  void asm_jit_SAVE_CARRY(void);
  void asm_jit_SAVE_CARRY_END(void);
  asm_emit_jit_SCRATCH_SET(p_buf, (value << 24));
  asm_copy(p_buf, asm_jit_CMP_IMM, asm_jit_CMP_IMM_END);
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_CMP_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_CMP_SCRATCH(void);
  void asm_jit_CMP_SCRATCH_END(void);
  void asm_jit_SAVE_CARRY(void);
  void asm_jit_SAVE_CARRY_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_CMP_SCRATCH, asm_jit_CMP_SCRATCH_END);
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_CMP_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_emit_jit_SCRATCH_ADD_Y(p_buf);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_CMP_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_CPX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_CPX_SCRATCH(void);
  void asm_jit_CPX_SCRATCH_END(void);
  void asm_jit_SAVE_CARRY(void);
  void asm_jit_SAVE_CARRY_END(void);
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_copy(p_buf, asm_jit_CPX_SCRATCH, asm_jit_CPX_SCRATCH_END);
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_CPX_IMM(void);
  void asm_jit_CPX_IMM_END(void);
  void asm_jit_SAVE_CARRY(void);
  void asm_jit_SAVE_CARRY_END(void);
  asm_emit_jit_SCRATCH_SET(p_buf, (value << 24));
  asm_copy(p_buf, asm_jit_CPX_IMM, asm_jit_CPX_IMM_END);
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_CPY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_CPY_SCRATCH(void);
  void asm_jit_CPY_SCRATCH_END(void);
  void asm_jit_SAVE_CARRY(void);
  void asm_jit_SAVE_CARRY_END(void);
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_copy(p_buf, asm_jit_CPY_SCRATCH, asm_jit_CPY_SCRATCH_END);
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_CPY_IMM(void);
  void asm_jit_CPY_IMM_END(void);
  void asm_jit_SAVE_CARRY(void);
  void asm_jit_SAVE_CARRY_END(void);
  asm_emit_jit_SCRATCH_SET(p_buf, (value << 24));
  asm_copy(p_buf, asm_jit_CPY_IMM, asm_jit_CPY_IMM_END);
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_DEC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_DEC_ABS_RMW(p_buf, addr);
}

void
asm_emit_jit_DEC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_DEC_SCRATCH2(void);
  void asm_jit_DEC_SCRATCH2_END(void);
  asm_emit_jit_ABS_RMW(p_buf,
                       addr,
                       asm_jit_DEC_SCRATCH2,
                       asm_jit_DEC_SCRATCH2_END);
}

void
asm_emit_jit_DEC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_DEC_ABX_RMW(p_buf, addr);
}

void
asm_emit_jit_DEC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_DEC_SCRATCH2(void);
  void asm_jit_DEC_SCRATCH2_END(void);
  asm_emit_jit_ABX_RMW(p_buf,
                       addr,
                       asm_jit_DEC_SCRATCH2,
                       asm_jit_DEC_SCRATCH2_END);
}

void
asm_emit_jit_DEC_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_EOR_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_emit_jit_EOR_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_EOR_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_EOR_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_EOR_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABY(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_EOR_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_EOR_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_EOR_IMM(void);
  void asm_jit_EOR_IMM_END(void);
  uint8_t immr;
  uint8_t imms;
  if (asm_calculate_immr_imms(&immr, &imms, value)) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_EOR_IMM,
                               asm_jit_EOR_IMM_END,
                               ((immr << 6) | imms));
    asm_emit_instruction_A_NZ_flags(p_buf);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, value);
    asm_emit_jit_EOR_SCRATCH(p_buf, 0);
  }
}

void
asm_emit_jit_EOR_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_EOR_SCRATCH(void);
  void asm_jit_EOR_SCRATCH_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_EOR_SCRATCH, asm_jit_EOR_SCRATCH_END);
}

void
asm_emit_jit_EOR_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_emit_jit_SCRATCH_ADD_Y(p_buf);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_EOR_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_INC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_INC_ABS_RMW(p_buf, addr);
}

void
asm_emit_jit_INC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_INC_SCRATCH2(void);
  void asm_jit_INC_SCRATCH2_END(void);
  asm_emit_jit_ABS_RMW(p_buf,
                       addr,
                       asm_jit_INC_SCRATCH2,
                       asm_jit_INC_SCRATCH2_END);
}

void
asm_emit_jit_INC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_INC_ABX_RMW(p_buf, addr);
}

void
asm_emit_jit_INC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_INC_SCRATCH2(void);
  void asm_jit_INC_SCRATCH2_END(void);
  asm_emit_jit_ABX_RMW(p_buf,
                       addr,
                       asm_jit_INC_SCRATCH2,
                       asm_jit_INC_SCRATCH2_END);
}

void
asm_emit_jit_INC_scratch(struct util_buffer* p_buf) {
  void asm_jit_INC_SCRATCH2(void);
  void asm_jit_INC_SCRATCH2_END(void);
  asm_emit_jit_RMW(p_buf, asm_jit_INC_SCRATCH2, asm_jit_INC_SCRATCH2_END);
}

void
asm_emit_jit_JMP(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_JMP(void);
  void asm_jit_JMP_END(void);
  asm_copy_patch_arm64_imm26_pc_rel(p_buf,
                                    asm_jit_JMP,
                                    asm_jit_JMP_END,
                                    p_target);
}

void
asm_emit_jit_LDA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDA_ABS_12bit(void);
  void asm_jit_LDA_ABS_12bit_END(void);
  (void) segment;
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_LDA_ABS_12bit,
                               asm_jit_LDA_ABS_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_LDA_SCRATCH(p_buf, 0);
  }
}

void
asm_emit_jit_LDA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_LDA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_LDA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABY(p_buf, addr);
  asm_emit_jit_LDA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_LDA_IMM(void);
  void asm_jit_LDA_IMM_END(void);
  asm_copy_patch_arm64_imm16(p_buf,
                             asm_jit_LDA_IMM,
                             asm_jit_LDA_IMM_END,
                             value);
}

void
asm_emit_jit_LDA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_LDA_SCRATCH(void);
  void asm_jit_LDA_SCRATCH_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_LDA_SCRATCH, asm_jit_LDA_SCRATCH_END);
}

void
asm_emit_jit_LDA_SCRATCH_X(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_LDA_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_emit_jit_SCRATCH_ADD_Y(p_buf);
  asm_emit_jit_LDA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_LDX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDX_ABS_12bit(void);
  void asm_jit_LDX_ABS_12bit_END(void);
  (void) segment;
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_LDX_ABS_12bit,
                               asm_jit_LDX_ABS_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_LDX_scratch(p_buf);
  }
}

void
asm_emit_jit_LDX_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABY(p_buf, addr);
  asm_emit_jit_LDX_scratch(p_buf);
}

void
asm_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_LDX_IMM(void);
  void asm_jit_LDX_IMM_END(void);
  asm_copy_patch_arm64_imm16(p_buf,
                             asm_jit_LDX_IMM,
                             asm_jit_LDX_IMM_END,
                             value);
}

void
asm_emit_jit_LDX_scratch(struct util_buffer* p_buf) {
  void asm_jit_LDX_SCRATCH(void);
  void asm_jit_LDX_SCRATCH_END(void);
  asm_copy(p_buf, asm_jit_LDX_SCRATCH, asm_jit_LDX_SCRATCH_END);
}

void
asm_emit_jit_LDY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDY_ABS_12bit(void);
  void asm_jit_LDY_ABS_12bit_END(void);
  (void) segment;
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_LDY_ABS_12bit,
                               asm_jit_LDY_ABS_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_LDY_scratch(p_buf);
  }
}

void
asm_emit_jit_LDY_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_LDY_scratch(p_buf);
}

void
asm_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_LDY_IMM(void);
  void asm_jit_LDY_IMM_END(void);
  asm_copy_patch_arm64_imm16(p_buf,
                             asm_jit_LDY_IMM,
                             asm_jit_LDY_IMM_END,
                             value);
}

void
asm_emit_jit_LDY_scratch(struct util_buffer* p_buf) {
  void asm_jit_LDY_SCRATCH(void);
  void asm_jit_LDY_SCRATCH_END(void);
  asm_copy(p_buf, asm_jit_LDY_SCRATCH, asm_jit_LDY_SCRATCH_END);
}

void
asm_emit_jit_LSR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_LSR_ABS_RMW(p_buf, addr);
}

void
asm_emit_jit_LSR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_LSR_SCRATCH2(void);
  void asm_jit_LSR_SCRATCH2_END(void);
  asm_emit_jit_ABS_RMW(p_buf,
                       addr,
                       asm_jit_LSR_SCRATCH2,
                       asm_jit_LSR_SCRATCH2_END);
}

void
asm_emit_jit_LSR_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_LSR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_LSR_ACC(struct util_buffer* p_buf) {
  void asm_jit_LSR_ACC(void);
  void asm_jit_LSR_ACC_END(void);
  asm_copy(p_buf, asm_jit_LSR_ACC, asm_jit_LSR_ACC_END);
}

void
asm_emit_jit_LSR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
  assert(0);
}

void
asm_emit_jit_LSR_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_ORA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_emit_jit_ORA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_ORA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_ORA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_ORA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
  assert(0);
}

void
asm_emit_jit_ORA_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_ORA_IMM(void);
  void asm_jit_ORA_IMM_END(void);
  uint8_t immr;
  uint8_t imms;
  if (asm_calculate_immr_imms(&immr, &imms, value)) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_ORA_IMM,
                               asm_jit_ORA_IMM_END,
                               ((immr << 6) | imms));
    asm_emit_instruction_A_NZ_flags(p_buf);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, value);
    asm_emit_jit_ORA_SCRATCH(p_buf, 0);
  }
}

void
asm_emit_jit_ORA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_ORA_SCRATCH(void);
  void asm_jit_ORA_SCRATCH_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_ORA_SCRATCH, asm_jit_ORA_SCRATCH_END);
}

void
asm_emit_jit_ORA_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_ROL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_ROL_ABS_RMW(p_buf, addr);
}

void
asm_emit_jit_ROL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROL_SCRATCH2(void);
  void asm_jit_ROL_SCRATCH2_END(void);
  asm_emit_jit_ABS_RMW(p_buf,
                       addr,
                       asm_jit_ROL_SCRATCH2,
                       asm_jit_ROL_SCRATCH2_END);
}

void
asm_emit_jit_ROL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_ROL_ACC(struct util_buffer* p_buf) {
  void asm_jit_ROL_ACC(void);
  void asm_jit_ROL_ACC_END(void);
  asm_copy(p_buf, asm_jit_ROL_ACC, asm_jit_ROL_ACC_END);
}

void
asm_emit_jit_ROL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
  assert(0);
}

void
asm_emit_jit_ROL_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_ROR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_emit_jit_ROR_ABS_RMW(p_buf, addr);
}

void
asm_emit_jit_ROR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROR_SCRATCH2(void);
  void asm_jit_ROR_SCRATCH2_END(void);
  asm_emit_jit_ABS_RMW(p_buf,
                       addr,
                       asm_jit_ROR_SCRATCH2,
                       asm_jit_ROR_SCRATCH2_END);
}

void
asm_emit_jit_ROR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROR_SCRATCH2(void);
  void asm_jit_ROR_SCRATCH2_END(void);
  asm_emit_jit_ABX_RMW(p_buf,
                       addr,
                       asm_jit_ROR_SCRATCH2,
                       asm_jit_ROR_SCRATCH2_END);
}

void
asm_emit_jit_ROR_ACC(struct util_buffer* p_buf) {
  void asm_jit_ROR_ACC(void);
  void asm_jit_ROR_ACC_END(void);
  asm_copy(p_buf, asm_jit_ROR_ACC, asm_jit_ROR_ACC_END);
}

void
asm_emit_jit_ROR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
  assert(0);
}

void
asm_emit_jit_ROR_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
  assert(0);
}

void
asm_emit_jit_SAX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_SBC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_SCRATCH_LOAD(p_buf, addr);
  asm_emit_jit_SBC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_SBC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_SBC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_SBC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABY(p_buf, addr);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_SBC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_SBC_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_SBC_IMM(void);
  void asm_jit_SBC_IMM_END(void);
  asm_emit_jit_SCRATCH_SET(p_buf, (value << 24));
  asm_copy(p_buf, asm_jit_SBC_IMM, asm_jit_SBC_IMM_END);
}

void
asm_emit_jit_SBC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_SBC_SCRATCH(void);
  void asm_jit_SBC_SCRATCH_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_SBC_SCRATCH, asm_jit_SBC_SCRATCH_END);
}

void
asm_emit_jit_SBC_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_emit_jit_SCRATCH_ADD_Y(p_buf);
  asm_emit_jit_SCRATCH_LOAD_SCRATCH(p_buf);
  asm_emit_jit_SBC_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_SHY_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_SLO_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
  assert(0);
}

void
asm_emit_jit_STA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STA_ABS_12bit(void);
  void asm_jit_STA_ABS_12bit_END(void);
  (void) segment;
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_STA_ABS_12bit,
                               asm_jit_STA_ABS_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_STA_SCRATCH(p_buf, 0);
  }
}

void
asm_emit_jit_STA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABX(p_buf, addr);
  asm_emit_jit_STA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_STA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) segment;
  asm_emit_jit_MODE_ABY(p_buf, addr);
  asm_emit_jit_STA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_STA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_STA_SCRATCH(void);
  void asm_jit_STA_SCRATCH_END(void);
  (void) offset;
  asm_copy(p_buf, asm_jit_STA_SCRATCH, asm_jit_STA_SCRATCH_END);
}

void
asm_emit_jit_STA_SCRATCH_Y(struct util_buffer* p_buf) {
  asm_emit_jit_SCRATCH_ADD_Y(p_buf);
  asm_emit_jit_STA_SCRATCH(p_buf, 0);
}

void
asm_emit_jit_STX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STX_ABS_12bit(void);
  void asm_jit_STX_ABS_12bit_END(void);
  (void) segment;
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_STX_ABS_12bit,
                               asm_jit_STX_ABS_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_STX_scratch(p_buf);
  }
}

void
asm_emit_jit_STX_scratch(struct util_buffer* p_buf) {
  void asm_jit_STX_SCRATCH(void);
  void asm_jit_STX_SCRATCH_END(void);
  asm_copy(p_buf, asm_jit_STX_SCRATCH, asm_jit_STX_SCRATCH_END);
}

void
asm_emit_jit_STY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STY_ABS_12bit(void);
  void asm_jit_STY_ABS_12bit_END(void);
  (void) segment;
  if (addr < 0x1000) {
    asm_copy_patch_arm64_imm12(p_buf,
                               asm_jit_STY_ABS_12bit,
                               asm_jit_STY_ABS_12bit_END,
                               addr);
  } else {
    asm_emit_jit_SCRATCH_SET(p_buf, addr);
    asm_emit_jit_STY_scratch(p_buf);
  }
}

void
asm_emit_jit_STY_scratch(struct util_buffer* p_buf) {
  void asm_jit_STY_SCRATCH(void);
  void asm_jit_STY_SCRATCH_END(void);
  asm_copy(p_buf, asm_jit_STY_SCRATCH, asm_jit_STY_SCRATCH_END);
}
