#include "asm_x64_jit.h"

#include "asm_x64_common.h"
#include "asm_x64_defs.h"
#include "asm_x64_jit_defs.h"
#include "util.h"

#include <assert.h>
#include <err.h>

static void
asm_x64_emit_jit_jump(struct util_buffer* p_buf,
                      void* p_target,
                      void* p_jmp_32bit,
                      void* p_jmp_end_32bit,
                      void* p_jmp_8bit,
                      void* p_jmp_end_8bit) {
  int32_t len_x64;
  ssize_t delta;

  size_t offset = util_buffer_get_pos(p_buf);
  void* p_source = (util_buffer_get_base_address(p_buf) + offset);

  len_x64 = (p_jmp_end_8bit - p_jmp_8bit);
  delta = (p_target - (p_source + len_x64));

  if (delta <= INT8_MAX && delta >= INT8_MIN) {
    asm_x64_copy(p_buf, p_jmp_8bit, p_jmp_end_8bit);
    asm_x64_patch_byte(p_buf, offset, p_jmp_8bit, p_jmp_end_8bit, delta);
  } else {
    len_x64 = (p_jmp_end_32bit - p_jmp_32bit);
    delta = (p_target - (p_source + len_x64));
    assert(delta <= INT32_MAX && delta >= INT32_MIN);
    asm_x64_copy(p_buf, p_jmp_32bit, p_jmp_end_32bit);
    asm_x64_patch_int(p_buf, offset, p_jmp_32bit, p_jmp_end_32bit, delta);
  }
}

void
asm_x64_emit_jit_call_compile_trampoline(struct util_buffer* p_buf) {
  /* To work correctly this sequence needs to be no more than 2 bytes. */
  assert((asm_x64_jit_call_compile_trampoline_END -
          asm_x64_jit_call_compile_trampoline) <= 2);

  asm_x64_copy(p_buf,
               asm_x64_jit_call_compile_trampoline,
               asm_x64_jit_call_compile_trampoline_END);
}

void
asm_x64_emit_jit_check_countdown(struct util_buffer* p_buf,
                                 uint16_t addr,
                                 uint32_t count) {
  size_t offset = util_buffer_get_pos(p_buf);

  if (count > 128) {
    errx(1, "check_countdown: countdown too large");
  }

  asm_x64_copy(p_buf,
               asm_x64_jit_check_countdown,
               asm_x64_jit_check_countdown_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_check_countdown,
                    asm_x64_jit_check_countdown_pc_patch,
                    addr);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_check_countdown,
                     asm_x64_jit_check_countdown_count_patch,
                     -count);
  asm_x64_patch_jump(p_buf,
                     offset,
                     asm_x64_jit_check_countdown,
                     asm_x64_jit_check_countdown_jump_patch,
                     asm_x64_jit_countdown_expired);
}

void
asm_x64_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_call_debug, asm_x64_jit_call_debug_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_call_debug,
                    asm_x64_jit_call_debug_pc_patch,
                    addr);
  asm_x64_patch_jump(p_buf,
                     offset,
                     asm_x64_jit_call_debug,
                     asm_x64_jit_call_debug_call_patch,
                     asm_x64_asm_debug);
}

void
asm_x64_emit_jit_jump_interp(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_jump_interp, asm_x64_jit_jump_interp_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_jump_interp,
                    asm_x64_jit_jump_interp_pc_patch,
                    addr);
  asm_x64_patch_jump(p_buf,
                     offset,
                     asm_x64_jit_jump_interp,
                     asm_x64_jit_jump_interp_jump_patch,
                     asm_x64_jit_interp);
}

void
asm_x64_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_ADD_IMM,
                          asm_x64_jit_ADD_IMM_END,
                          value);
}

void
asm_x64_emit_jit_CHECK_BCD(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_CHECK_BCD, asm_x64_jit_CHECK_BCD_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_CHECK_BCD,
                    asm_x64_jit_CHECK_BCD_pc_patch,
                    addr);
  /* TODO: usually calls interp with an overly subtracted countdown because of
   * the mid-block bail.
   */
  asm_x64_patch_jump(p_buf,
                     offset,
                     asm_x64_jit_CHECK_BCD,
                     asm_x64_jit_CHECK_BCD_jump_patch,
                     asm_x64_jit_interp);
}

void
asm_x64_emit_jit_ADD_Y_SCRATCH(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ADD_Y_SCRATCH, asm_x64_jit_ADD_Y_SCRATCH_END);
}

void
asm_x64_emit_jit_FLAGA(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_FLAGA, asm_x64_jit_FLAGA_END);
}

void
asm_x64_emit_jit_FLAGX(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_FLAGX, asm_x64_jit_FLAGX_END);
}

void
asm_x64_emit_jit_FLAGY(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_FLAGY, asm_x64_jit_FLAGY_END);
}

void
asm_x64_emit_jit_INC_SCRATCH(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_INC_SCRATCH, asm_x64_jit_INC_SCRATCH_END);
}

void
asm_x64_emit_jit_JMP_SCRATCH(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_JMP_SCRATCH, asm_x64_jit_JMP_SCRATCH_END);
}

void
asm_x64_emit_jit_LOAD_CARRY(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LOAD_CARRY, asm_x64_jit_LOAD_CARRY_END);
}

void
asm_x64_emit_jit_LOAD_CARRY_INV(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_jit_LOAD_CARRY_INV,
               asm_x64_jit_LOAD_CARRY_INV_END);
}

void
asm_x64_emit_jit_LOAD_OVERFLOW(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LOAD_OVERFLOW, asm_x64_jit_LOAD_OVERFLOW_END);
}

void
asm_x64_emit_jit_MODE_ABX(struct util_buffer* p_buf, uint16_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_MODE_ABX, asm_x64_jit_MODE_ABX_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_MODE_ABX,
                    asm_x64_jit_MODE_ABX_lea_patch,
                    value);
}

void
asm_x64_emit_jit_MODE_ABY(struct util_buffer* p_buf, uint16_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_MODE_ABY, asm_x64_jit_MODE_ABY_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_MODE_ABY,
                    asm_x64_jit_MODE_ABY_lea_patch,
                    value);
}

void
asm_x64_emit_jit_MODE_IND(struct util_buffer* p_buf, uint16_t addr) {
  if ((addr & 0xFF) == 0xFF) {
    errx(1, "MODE_IND: page crossing");
  }

  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_MODE_IND,
                         asm_x64_jit_MODE_IND_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_MODE_IND_SCRATCH(struct util_buffer* p_buf) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf,
               asm_x64_jit_MODE_IND_SCRATCH,
               asm_x64_jit_MODE_IND_SCRATCH_END);
  asm_x64_patch_jump(p_buf,
                     offset,
                     asm_x64_jit_MODE_IND_SCRATCH,
                     asm_x64_jit_MODE_IND_SCRATCH_jump_patch,
                     asm_x64_instruction_ILLEGAL);
}

void
asm_x64_emit_jit_MODE_ZPX(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  if (value <= 0x7F) {
    asm_x64_copy(p_buf,
                 asm_x64_jit_MODE_ZPX_8bit,
                 asm_x64_jit_MODE_ZPX_8bit_END);
    asm_x64_patch_byte(p_buf,
                       offset,
                       asm_x64_jit_MODE_ZPX_8bit,
                       asm_x64_jit_MODE_ZPX_8bit_lea_patch,
                       value);
  } else {
    asm_x64_copy(p_buf, asm_x64_jit_MODE_ZPX, asm_x64_jit_MODE_ZPX_END);
    asm_x64_patch_int(p_buf,
                      offset,
                      asm_x64_jit_MODE_ZPX,
                      asm_x64_jit_MODE_ZPX_lea_patch,
                      value);
  }
}

void
asm_x64_emit_jit_MODE_ZPY(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  if (value <= 0x7F) {
    asm_x64_copy(p_buf,
                 asm_x64_jit_MODE_ZPY_8bit,
                 asm_x64_jit_MODE_ZPY_8bit_END);
    asm_x64_patch_byte(p_buf,
                       offset,
                       asm_x64_jit_MODE_ZPY_8bit,
                       asm_x64_jit_MODE_ZPY_8bit_lea_patch,
                       value);
  } else {
    asm_x64_copy(p_buf, asm_x64_jit_MODE_ZPY, asm_x64_jit_MODE_ZPY_END);
    asm_x64_patch_int(p_buf,
                      offset,
                      asm_x64_jit_MODE_ZPY,
                      asm_x64_jit_MODE_ZPY_lea_patch,
                      value);
  }
}

void
asm_x64_emit_jit_PUSH_16(struct util_buffer* p_buf, uint16_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_PUSH_16, asm_x64_jit_PUSH_16_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_PUSH_16,
                     asm_x64_jit_PUSH_16_byte1_patch,
                     (value >> 8));
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_PUSH_16,
                     asm_x64_jit_PUSH_16_byte2_patch,
                     (value & 0xFF));
}

void
asm_x64_emit_jit_SAVE_CARRY(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_SAVE_CARRY, asm_x64_jit_SAVE_CARRY_END);
}

void
asm_x64_emit_jit_SAVE_CARRY_INV(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_jit_SAVE_CARRY_INV,
               asm_x64_jit_SAVE_CARRY_INV_END);
}

void
asm_x64_emit_jit_SAVE_OVERFLOW(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_SAVE_OVERFLOW, asm_x64_jit_SAVE_OVERFLOW_END);
}

void
asm_x64_emit_jit_STOA_IMM(struct util_buffer* p_buf,
                          uint16_t addr,
                          uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_STOA_IMM, asm_x64_jit_STOA_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_STOA_IMM,
                     asm_x64_jit_STOA_IMM_END,
                     value);
  asm_x64_patch_int(p_buf,
                    (offset - 1),
                    asm_x64_jit_STOA_IMM,
                    asm_x64_jit_STOA_IMM_END,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_SUB_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_SUB_IMM,
                          asm_x64_jit_SUB_IMM_END,
                          value);
}

void
asm_x64_emit_jit_WRITE_INV_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_WRITE_INV_ABS, asm_x64_jit_WRITE_INV_ABS_END);
  asm_x64_patch_int(
      p_buf,
      offset,
      asm_x64_jit_WRITE_INV_ABS,
      asm_x64_jit_WRITE_INV_ABS_offset_patch,
      (K_JIT_CONTEXT_OFFSET_JIT_PTRS + (addr * sizeof(uint32_t))));
}

void
asm_x64_emit_jit_WRITE_INV_SCRATCH(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_jit_WRITE_INV_SCRATCH,
               asm_x64_jit_WRITE_INV_SCRATCH_END);
}

void
asm_x64_emit_jit_ADC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_ADC_ABS,
                         asm_x64_jit_ADC_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_ADC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_ADC_ABX,
                         asm_x64_jit_ADC_ABX_END,
                         addr);
}

void
asm_x64_emit_jit_ADC_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_ADC_ABY,
                         asm_x64_jit_ADC_ABY_END,
                         addr);
}

void
asm_x64_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_ADC_IMM,
                          asm_x64_jit_ADC_IMM_END,
                          value);
}

void
asm_x64_emit_jit_ADC_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ADC_scratch, asm_x64_jit_ADC_scratch_END);
}

void
asm_x64_emit_jit_ALR_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ALR_IMM, asm_x64_jit_ALR_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_ALR_IMM,
                     asm_x64_jit_ALR_IMM_patch_byte,
                     value);
}

void
asm_x64_emit_jit_AND_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_AND_ABS,
                         asm_x64_jit_AND_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_AND_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_AND_IMM,
                          asm_x64_jit_AND_IMM_END,
                          value);
}

void
asm_x64_emit_jit_AND_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_AND_scratch, asm_x64_jit_AND_scratch_END);
}

void
asm_x64_emit_jit_ASL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_ASL_ABS,
                         asm_x64_jit_ASL_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_ASL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ASL_ABS_RMW, asm_x64_jit_ASL_ABS_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ASL_ABS_RMW,
                    asm_x64_jit_ASL_ABS_RMW_mov1_patch,
                    (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ASL_ABS_RMW,
                    asm_x64_jit_ASL_ABS_RMW_mov2_patch,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_ASL_ACC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ASL_ACC, asm_x64_jit_ASL_ACC_END);
}

void
asm_x64_emit_jit_BCC(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BCC,
                        asm_x64_jit_BCC_END,
                        asm_x64_jit_BCC_8bit,
                        asm_x64_jit_BCC_8bit_END);
}

void
asm_x64_emit_jit_BCS(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BCS,
                        asm_x64_jit_BCS_END,
                        asm_x64_jit_BCS_8bit,
                        asm_x64_jit_BCS_8bit_END);
}

void
asm_x64_emit_jit_BEQ(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BEQ,
                        asm_x64_jit_BEQ_END,
                        asm_x64_jit_BEQ_8bit,
                        asm_x64_jit_BEQ_8bit_END);
}

void
asm_x64_emit_jit_BIT(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_BIT,
                         asm_x64_jit_BIT_END,
                         (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_emit_instruction_BIT_common(p_buf);
}

void
asm_x64_emit_jit_BMI(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BMI,
                        asm_x64_jit_BMI_END,
                        asm_x64_jit_BMI_8bit,
                        asm_x64_jit_BMI_8bit_END);
}

void
asm_x64_emit_jit_BNE(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BNE,
                        asm_x64_jit_BNE_END,
                        asm_x64_jit_BNE_8bit,
                        asm_x64_jit_BNE_8bit_END);
}

void
asm_x64_emit_jit_BPL(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BPL,
                        asm_x64_jit_BPL_END,
                        asm_x64_jit_BPL_8bit,
                        asm_x64_jit_BPL_8bit_END);
}

void
asm_x64_emit_jit_BVC(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BVC,
                        asm_x64_jit_BVC_END,
                        asm_x64_jit_BVC_8bit,
                        asm_x64_jit_BVC_8bit_END);
}

void
asm_x64_emit_jit_BVS(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_BVS,
                        asm_x64_jit_BVS_END,
                        asm_x64_jit_BVS_8bit,
                        asm_x64_jit_BVS_8bit_END);
}

void
asm_x64_emit_jit_CMP_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_CMP_ABS,
                         asm_x64_jit_CMP_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_CMP_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_CMP_ABX,
                         asm_x64_jit_CMP_ABX_END,
                         addr);
}

void
asm_x64_emit_jit_CMP_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_CMP_ABY,
                         asm_x64_jit_CMP_ABY_END,
                         addr);
}

void
asm_x64_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_CMP_IMM,
                          asm_x64_jit_CMP_IMM_END,
                          value);
}

void
asm_x64_emit_jit_CMP_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_CMP_scratch, asm_x64_jit_CMP_scratch_END);
}

void
asm_x64_emit_jit_CPX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_CPX_ABS,
                         asm_x64_jit_CPX_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_CPX_IMM,
                          asm_x64_jit_CPX_IMM_END,
                          value);
}

void
asm_x64_emit_jit_CPY_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_CPY_ABS,
                         asm_x64_jit_CPY_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_CPY_IMM,
                          asm_x64_jit_CPY_IMM_END,
                          value);
}

void
asm_x64_emit_jit_DEC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_DEC_ABS,
                         asm_x64_jit_DEC_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_DEC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_DEC_ABS_RMW, asm_x64_jit_DEC_ABS_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_DEC_ABS_RMW,
                    asm_x64_jit_DEC_ABS_RMW_mov1_patch,
                    (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_DEC_ABS_RMW,
                    asm_x64_jit_DEC_ABS_RMW_mov2_patch,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_DEC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_DEC_ABX_RMW, asm_x64_jit_DEC_ABX_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_DEC_ABX_RMW,
                    asm_x64_jit_DEC_ABX_RMW_mov1_patch,
                    addr);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_DEC_ABX_RMW,
                    asm_x64_jit_DEC_ABX_RMW_mov2_patch,
                    (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_EOR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_EOR_ABS,
                         asm_x64_jit_EOR_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_EOR_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_EOR_ABX,
                         asm_x64_jit_EOR_ABX_END,
                         addr);
}

void
asm_x64_emit_jit_EOR_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_EOR_ABY,
                         asm_x64_jit_EOR_ABY_END,
                         addr);
}

void
asm_x64_emit_jit_EOR_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_EOR_IMM,
                          asm_x64_jit_EOR_IMM_END,
                          value);
}

void
asm_x64_emit_jit_EOR_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_EOR_scratch, asm_x64_jit_EOR_scratch_END);
}

void
asm_x64_emit_jit_INC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_INC_ABS,
                         asm_x64_jit_INC_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_INC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_INC_ABS_RMW, asm_x64_jit_INC_ABS_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_INC_ABS_RMW,
                    asm_x64_jit_INC_ABS_RMW_mov1_patch,
                    (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_INC_ABS_RMW,
                    asm_x64_jit_INC_ABS_RMW_mov2_patch,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_INC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_INC_ABX_RMW, asm_x64_jit_INC_ABX_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_INC_ABX_RMW,
                    asm_x64_jit_INC_ABX_RMW_mov1_patch,
                    addr);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_INC_ABX_RMW,
                    asm_x64_jit_INC_ABX_RMW_mov2_patch,
                    (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_JMP(struct util_buffer* p_buf, void* p_target) {
  asm_x64_emit_jit_jump(p_buf,
                        p_target,
                        asm_x64_jit_JMP,
                        asm_x64_jit_JMP_END,
                        asm_x64_jit_JMP_8bit,
                        asm_x64_jit_JMP_8bit_END);
}

void
asm_x64_emit_jit_LDA_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LDA_ABS,
                         asm_x64_jit_LDA_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_LDA_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LDA_ABX,
                         asm_x64_jit_LDA_ABX_END,
                         addr);
}

void
asm_x64_emit_jit_LDA_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LDA_ABY,
                         asm_x64_jit_LDA_ABY_END,
                         addr);
}

void
asm_x64_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_LDA_IMM,
                          asm_x64_jit_LDA_IMM_END,
                          value);
}

void
asm_x64_emit_jit_LDA_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LDA_scratch, asm_x64_jit_LDA_scratch_END);
}

void
asm_x64_emit_jit_LDX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LDX_ABS,
                         asm_x64_jit_LDX_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_LDX_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LDX_ABY,
                         asm_x64_jit_LDX_ABY_END,
                         addr);
}

void
asm_x64_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_LDX_IMM,
                          asm_x64_jit_LDX_IMM_END,
                          value);
}

void
asm_x64_emit_jit_LDX_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LDX_scratch, asm_x64_jit_LDX_scratch_END);
}

void
asm_x64_emit_jit_LDY_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LDY_ABS,
                         asm_x64_jit_LDY_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_LDY_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LDY_ABX,
                         asm_x64_jit_LDY_ABX_END,
                         addr);
}

void
asm_x64_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_LDY_IMM,
                          asm_x64_jit_LDY_IMM_END,
                          value);
}

void
asm_x64_emit_jit_LSR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_LSR_ABS,
                         asm_x64_jit_LSR_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_LSR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LSR_ABS_RMW, asm_x64_jit_LSR_ABS_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_LSR_ABS_RMW,
                    asm_x64_jit_LSR_ABS_RMW_mov1_patch,
                    (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_LSR_ABS_RMW,
                    asm_x64_jit_LSR_ABS_RMW_mov2_patch,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_LSR_ACC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LSR_ACC, asm_x64_jit_LSR_ACC_END);
}

void
asm_x64_emit_jit_ORA_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_ORA_ABS,
                         asm_x64_jit_ORA_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_ORA_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_ORA_ABX,
                         asm_x64_jit_ORA_ABX_END,
                         addr);
}

void
asm_x64_emit_jit_ORA_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_ORA_IMM,
                          asm_x64_jit_ORA_IMM_END,
                          value);
}

void
asm_x64_emit_jit_ORA_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ORA_scratch, asm_x64_jit_ORA_scratch_END);
}

void
asm_x64_emit_jit_ROL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ROL_ABS_RMW, asm_x64_jit_ROL_ABS_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROL_ABS_RMW,
                    asm_x64_jit_ROL_ABS_RMW_mov1_patch,
                    (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROL_ABS_RMW,
                    asm_x64_jit_ROL_ABS_RMW_mov2_patch,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_ROL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ROL_ABX_RMW, asm_x64_jit_ROL_ABX_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROL_ABX_RMW,
                    asm_x64_jit_ROL_ABX_RMW_mov1_patch,
                    addr);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROL_ABX_RMW,
                    asm_x64_jit_ROL_ABX_RMW_mov2_patch,
                    (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_ROL_ACC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ROL_ACC, asm_x64_jit_ROL_ACC_END);
}

void
asm_x64_emit_jit_ROR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ROR_ABS_RMW, asm_x64_jit_ROR_ABS_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROR_ABS_RMW,
                    asm_x64_jit_ROR_ABS_RMW_mov1_patch,
                    (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROR_ABS_RMW,
                    asm_x64_jit_ROR_ABS_RMW_mov2_patch,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_ROR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ROR_ABX_RMW, asm_x64_jit_ROR_ABX_RMW_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROR_ABX_RMW,
                    asm_x64_jit_ROR_ABX_RMW_mov1_patch,
                    addr);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_ROR_ABX_RMW,
                    asm_x64_jit_ROR_ABX_RMW_mov2_patch,
                    (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_ROR_ACC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ROR_ACC, asm_x64_jit_ROR_ACC_END);
}

void
asm_x64_emit_jit_SAX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_SAX_ABS,
                         asm_x64_jit_SAX_ABS_END,
                         (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_SBC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_SBC_ABS,
                         asm_x64_jit_SBC_ABS_END,
                         (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_SBC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_SBC_ABX,
                         asm_x64_jit_SBC_ABX_END,
                         addr);
}

void
asm_x64_emit_jit_SBC_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_SBC_ABY,
                         asm_x64_jit_SBC_ABY_END,
                         addr);
}

void
asm_x64_emit_jit_SBC_IMM(struct util_buffer* p_buf, uint8_t value) {
  asm_x86_copy_patch_byte(p_buf,
                          asm_x64_jit_SBC_IMM,
                          asm_x64_jit_SBC_IMM_END,
                          value);
}

void
asm_x64_emit_jit_SBC_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_SBC_scratch, asm_x64_jit_SBC_scratch_END);
}

void
asm_x64_emit_jit_SHY_ABX(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);
  uint8_t value = ((addr >> 8) + 1);

  asm_x64_copy(p_buf, asm_x64_jit_SHY_ABX, asm_x64_jit_SHY_ABX_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_SHY_ABX,
                     asm_x64_jit_SHY_ABX_byte_patch,
                     value);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_SHY_ABX,
                    asm_x64_jit_SHY_ABX_mov_patch,
                    (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_SLO_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_SLO_ABS, asm_x64_jit_SLO_ABS_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_SLO_ABS,
                    asm_x64_jit_SLO_ABS_mov1_patch,
                    (K_BBC_MEM_READ_ADDR + addr));
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_SLO_ABS,
                    asm_x64_jit_SLO_ABS_mov2_patch,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_STA_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_STA_ABS,
                         asm_x64_jit_STA_ABS_END,
                         (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_STA_ABX(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_STA_ABX,
                         asm_x64_jit_STA_ABX_END,
                         (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_STA_ABY(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_STA_ABY,
                         asm_x64_jit_STA_ABY_END,
                         (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_STA_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_STA_scratch, asm_x64_jit_STA_scratch_END);
}

void
asm_x64_emit_jit_STX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_STX_ABS,
                         asm_x64_jit_STX_ABS_END,
                         (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_STY_ABS(struct util_buffer* p_buf, uint16_t addr) {
  asm_x86_copy_patch_u32(p_buf,
                         asm_x64_jit_STY_ABS,
                         asm_x64_jit_STY_ABS_END,
                         (K_BBC_MEM_WRITE_ADDR + addr));
}
