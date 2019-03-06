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
asm_x64_emit_jit_LODA_IMM(struct util_buffer* p_buf, uint8_t value) {
  size_t offset = util_buffer_get_pos(p_buf);

  asm_x64_copy(p_buf, asm_x64_jit_LODA_IMM, asm_x64_jit_LODA_IMM_END);
  asm_x64_patch_byte(p_buf,
                     offset,
                     asm_x64_jit_LODA_IMM,
                     asm_x64_jit_LODA_IMM_END,
                     value);
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
