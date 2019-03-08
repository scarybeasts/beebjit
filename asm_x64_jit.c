#include "asm_x64_jit.h"

#include "asm_x64_common.h"
#include "asm_x64_defs.h"
#include "util.h"

#include <assert.h>

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
  int32_t len_x64;
  int32_t delta;

  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_BNE, asm_x64_jit_BNE_END);
  len_x64 = (asm_x64_jit_BNE_END - asm_x64_jit_BNE);
  delta = (value2 - (value1 + len_x64));
  asm_x64_patch_int(p_buf, offset, asm_x64_jit_BNE, asm_x64_jit_BNE_END, delta);
}

void
asm_x64_emit_jit_JMP(struct util_buffer* p_buf,
                     int32_t value1,
                     int32_t value2) {
  int32_t len_x64;
  int32_t delta;

  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_JMP, asm_x64_jit_JMP_END);
  len_x64 = (asm_x64_jit_JMP_END - asm_x64_jit_JMP);
  delta = (value2 - (value1 + len_x64));
  asm_x64_patch_int(p_buf, offset, asm_x64_jit_JMP, asm_x64_jit_JMP_END, delta);
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
