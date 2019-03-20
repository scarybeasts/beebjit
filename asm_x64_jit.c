#include "asm_x64_jit.h"

#include "asm_x64_common.h"
#include "asm_x64_defs.h"
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
asm_x64_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf,
               asm_x64_jit_call_debug,
               asm_x64_jit_call_debug_END);

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
asm_x64_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ADD_IMM, asm_x64_jit_ADD_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_ADD_IMM,
                     asm_x64_jit_ADD_IMM_END,
                     value);
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
asm_x64_emit_jit_MODE_IND(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  if ((addr & 0xFF) == 0xFF) {
    errx(1, "MODE_IND: page crossing");
  }

  asm_x64_copy(p_buf, asm_x64_jit_MODE_IND, asm_x64_jit_MODE_IND_END);
  asm_x64_patch_int(p_buf,
                    offset,
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
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_SUB_IMM, asm_x64_jit_SUB_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_SUB_IMM,
                     asm_x64_jit_SUB_IMM_END,
                     value);
}

void
asm_x64_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_ADC_IMM, asm_x64_jit_ADC_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_ADC_IMM,
                     asm_x64_jit_ADC_IMM_END,
                     value);
}

void
asm_x64_emit_jit_AND_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_AND_IMM, asm_x64_jit_AND_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_AND_IMM,
                     asm_x64_jit_AND_IMM_END,
                     value);
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
asm_x64_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_CMP_IMM, asm_x64_jit_CMP_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_CMP_IMM,
                     asm_x64_jit_CMP_IMM_END,
                     value);
}

void
asm_x64_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_CPX_IMM, asm_x64_jit_CPX_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_CPX_IMM,
                     asm_x64_jit_CPX_IMM_END,
                     value);
}

void
asm_x64_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_CPY_IMM, asm_x64_jit_CPY_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_CPY_IMM,
                     asm_x64_jit_CPY_IMM_END,
                     value);
}

void
asm_x64_emit_jit_INC_ZPG(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_INC_ZPG, asm_x64_jit_INC_ZPG_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_INC_ZPG,
                    asm_x64_jit_INC_ZPG_END,
                    (K_BBC_MEM_READ_ADDR + value));
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
asm_x64_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LDA_IMM, asm_x64_jit_LDA_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_LDA_IMM,
                     asm_x64_jit_LDA_IMM_END,
                     value);
}

void
asm_x64_emit_jit_LDA_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LDA_ABS, asm_x64_jit_LDA_ABS_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_LDA_ABS,
                    asm_x64_jit_LDA_ABS_END,
                    (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_LDA_ABX(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LDA_ABX, asm_x64_jit_LDA_ABX_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_LDA_ABX,
                    asm_x64_jit_LDA_ABX_END,
                    addr);
}

void
asm_x64_emit_jit_LDA_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LDA_scratch, asm_x64_jit_LDA_scratch_END);
}

void
asm_x64_emit_jit_LDX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LDX_ABS, asm_x64_jit_LDX_ABS_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_LDX_ABS,
                    asm_x64_jit_LDX_ABS_END,
                    (K_BBC_MEM_READ_ADDR + addr));
}

void
asm_x64_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LDX_IMM, asm_x64_jit_LDX_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_LDX_IMM,
                     asm_x64_jit_LDX_IMM_END,
                     value);
}

void
asm_x64_emit_jit_LDX_scratch(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LDX_scratch, asm_x64_jit_LDX_scratch_END);
}

void
asm_x64_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LDY_IMM, asm_x64_jit_LDY_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_LDY_IMM,
                     asm_x64_jit_LDY_IMM_END,
                     value);
}

void
asm_x64_emit_jit_LSR_ACC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_LSR_ACC, asm_x64_jit_LSR_ACC_END);
}

void
asm_x64_emit_jit_ROL_ACC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ROL_ACC, asm_x64_jit_ROL_ACC_END);
}

void
asm_x64_emit_jit_ROR_ACC(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf, asm_x64_jit_ROR_ACC, asm_x64_jit_ROR_ACC_END);
}

void
asm_x64_emit_jit_SBC_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_SBC_IMM, asm_x64_jit_SBC_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_SBC_IMM,
                     asm_x64_jit_SBC_IMM_END,
                     value);
}

void
asm_x64_emit_jit_STA_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_STA_ABS, asm_x64_jit_STA_ABS_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_STA_ABS,
                    asm_x64_jit_STA_ABS_END,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_STA_ABX(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_STA_ABX, asm_x64_jit_STA_ABX_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_STA_ABX,
                    asm_x64_jit_STA_ABX_END,
                    (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}

void
asm_x64_emit_jit_STX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_STX_ABS, asm_x64_jit_STX_ABS_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_STX_ABS,
                    asm_x64_jit_STX_ABS_END,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}

void
asm_x64_emit_jit_STY_ABS(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_STY_ABS, asm_x64_jit_STY_ABS_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_STY_ABS,
                    asm_x64_jit_STY_ABS_END,
                    (K_BBC_MEM_WRITE_ADDR + addr));
}
