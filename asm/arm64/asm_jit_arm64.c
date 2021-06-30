#include "../asm_jit.h"

#include "../asm_common.h"
#include "asm_helper_arm64.h"
#include "asm_opcodes.h"

#include <assert.h>

int
asm_jit_is_enabled(void) {
  return 0;
}

void
asm_jit_test_preconditions(void) {
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
asm_emit_jit_call_compile_trampoline(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_jump_interp_trampoline(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_check_countdown(struct util_buffer* p_buf,
                             uint32_t count,
                             void* p_trampoline) {
  (void) p_buf;
  (void) count;
  (void) p_trampoline;
}

void
asm_emit_jit_check_countdown_no_save_nz_flags(struct util_buffer* p_buf,
                                              uint32_t count,
                                              void* p_trampoline) {
  (void) p_buf;
  (void) count;
  (void) p_trampoline;
}

void
asm_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_call_debug_load_PC(void);
  void asm_jit_call_debug_load_PC_END(void);
  void asm_jit_call_debug_do_call(void);
  void asm_jit_call_debug_do_call_END(void);
  asm_copy_patch_arm64_imm16(p_buf,
                             asm_jit_call_debug_load_PC,
                             asm_jit_call_debug_load_PC_END,
                             addr);
  asm_copy(p_buf, asm_jit_call_debug_do_call, asm_jit_call_debug_do_call_END);
}

void
asm_emit_jit_jump_interp(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_jump_interp_load_PC(void);
  void asm_jit_jump_interp_load_PC_END(void);
  void asm_jit_jump_interp_do_jump(void);
  void asm_jit_jump_interp_do_jump_END(void);
  asm_copy_patch_arm64_imm16(p_buf,
                             asm_jit_jump_interp_load_PC,
                             asm_jit_jump_interp_load_PC_END,
                             addr);
  asm_copy(p_buf,
           asm_jit_jump_interp_do_jump,
           asm_jit_jump_interp_do_jump_END);
}

void
asm_emit_jit_call_inturbo(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_for_testing(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ADD_CYCLES(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_ADD_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ADD_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ADD_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_ADD_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_ADD_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
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
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_X(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_X_n(struct util_buffer* p_buf,
                                     uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_Y_n(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_CHECK_PENDING_IRQ(struct util_buffer* p_buf, void* p_trampoline) {
  (void) p_buf;
  (void) p_trampoline;
}

void
asm_emit_jit_CLEAR_CARRY(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_FLAGA(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_FLAGX(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_FLAGY(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_FLAG_MEM(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_INVERT_CARRY(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_JMP_SCRATCH_n(struct util_buffer* p_buf, uint16_t n) {
  (void) p_buf;
  (void) n;
}

void
asm_emit_jit_LDA_Z(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LDX_Z(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LDY_Z(struct util_buffer* p_buf) {
  (void) p_buf;
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
}

void
asm_emit_jit_LOAD_SCRATCH_16(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_MODE_ABX(struct util_buffer* p_buf, uint16_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_MODE_ABY(struct util_buffer* p_buf, uint16_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_MODE_IND_8(struct util_buffer* p_buf, uint8_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_MODE_IND_16(struct util_buffer* p_buf,
                         uint16_t addr,
                         uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_MODE_IND_SCRATCH_8(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_MODE_IND_SCRATCH_16(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_MODE_ZPX(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_MODE_ZPY(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_PULL_16(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_PUSH_16(struct util_buffer* p_buf, uint16_t value) {
  (void) p_buf;
  (void) value;
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
}

void
asm_emit_jit_STOA_IMM(struct util_buffer* p_buf, uint16_t addr, uint8_t value) {
  (void) p_buf;
  (void) addr;
  (void) value;
}

void
asm_emit_jit_SUB_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_SUB_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
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
}

void
asm_emit_jit_WRITE_INV_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ADC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ADC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ADC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_ADC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_ADC_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ALR_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_AND_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_AND_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_AND_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_AND_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_AND_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_AND_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ASL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ASL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ASL_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ASL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ASL_ACC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ASL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
}

void
asm_emit_jit_ASL_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_BCC(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_BCS(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_BEQ(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_BIT(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_BMI(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_BNE(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_BPL(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_BVC(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_BVS(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_CMP_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_CMP_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_CMP_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_CMP_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_CMP_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_CPX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_CPY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_DEC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_DEC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_DEC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_DEC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_DEC_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_EOR_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_EOR_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_EOR_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_EOR_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_EOR_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_EOR_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_INC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_INC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_INC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_INC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_INC_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_JMP(struct util_buffer* p_buf, void* p_target) {
  (void) p_buf;
  (void) p_target;
}

void
asm_emit_jit_LDA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_LDA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_LDA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
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
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_LDA_SCRATCH_X(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LDA_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LDX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_LDX_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
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
  (void) p_buf;
}

void
asm_emit_jit_LDY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_LDY_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_LDY_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LSR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_LSR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_LSR_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_LSR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_LSR_ACC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_LSR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
}

void
asm_emit_jit_LSR_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ORA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ORA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ORA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_ORA_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_ORA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_ORA_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ROL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ROL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ROL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ROL_ACC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ROL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
}

void
asm_emit_jit_ROL_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ROR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ROR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ROR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_ROR_ACC(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_ROR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  (void) p_buf;
  (void) n;
}

void
asm_emit_jit_ROR_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_SAX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_SBC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_SBC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_SBC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_SBC_IMM(struct util_buffer* p_buf, uint8_t value) {
  (void) p_buf;
  (void) value;
}

void
asm_emit_jit_SBC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_SBC_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_SHY_ABX(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
}

void
asm_emit_jit_SLO_ABS(struct util_buffer* p_buf, uint16_t addr) {
  (void) p_buf;
  (void) addr;
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
    assert(0);
  }
}

void
asm_emit_jit_STA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_STA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_STA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  (void) p_buf;
  (void) offset;
}

void
asm_emit_jit_STA_SCRATCH_Y(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_STX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_STX_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}

void
asm_emit_jit_STY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  (void) p_buf;
  (void) addr;
  (void) segment;
}

void
asm_emit_jit_STY_scratch(struct util_buffer* p_buf) {
  (void) p_buf;
}
