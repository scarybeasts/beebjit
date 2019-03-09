#include "asm_x64_jit.h"

#include "asm_x64_common.h"
#include "asm_x64_defs.h"
#include "util.h"

#include <assert.h>

static void
asm_x64_emit_jit_jump(struct util_buffer* p_buf,
                      int32_t value1,
                      int32_t value2,
                      void* p_jmp_32bit,
                      void* p_jmp_end_32bit,
                      void* p_jmp_8bit,
                      void* p_jmp_end_8bit) {
  int32_t len_x64;
  int32_t delta;

  size_t offset = util_buffer_get_pos(p_buf);

  len_x64 = (p_jmp_end_8bit - p_jmp_8bit);
  delta = (value2 - (value1 + len_x64));

  if (delta <= INT8_MAX && delta >= INT8_MIN) {
    asm_x64_copy(p_buf, p_jmp_8bit, p_jmp_end_8bit);
    asm_x64_patch_byte(p_buf, offset, p_jmp_8bit, p_jmp_end_8bit, delta);
  } else {
    len_x64 = (p_jmp_end_32bit - p_jmp_32bit);
    delta = (value2 - (value1 + len_x64));
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
asm_x64_emit_jit_BNE(struct util_buffer* p_buf,
                     int32_t value1,
                     int32_t value2) {
  asm_x64_emit_jit_jump(p_buf,
                        value1,
                        value2,
                        asm_x64_jit_BNE,
                        asm_x64_jit_BNE_END,
                        asm_x64_jit_BNE_8bit,
                        asm_x64_jit_BNE_8bit_END);
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
asm_x64_emit_jit_JMP(struct util_buffer* p_buf,
                     int32_t value1,
                     int32_t value2) {
  asm_x64_emit_jit_jump(p_buf,
                        value1,
                        value2,
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
asm_x64_emit_jit_STA_ABX(struct util_buffer* p_buf, uint16_t addr) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_STA_ABX, asm_x64_jit_STA_ABX_END);
  asm_x64_patch_int(p_buf,
                    offset,
                    asm_x64_jit_STA_ABX,
                    asm_x64_jit_STA_ABX_END,
                    (K_BBC_MEM_READ_TO_WRITE_OFFSET + addr));
}
