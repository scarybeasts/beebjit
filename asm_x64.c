#include "asm_x64.h"

#include "util.h"

size_t
asm_x64_copy(struct util_buffer* p_buf,
             void* p_start,
             void* p_end,
             size_t min_for_padding) {
  size_t size = (p_end - p_start);
  util_buffer_add_chunk(p_buf, p_start, size);
  if (size < min_for_padding) {
    size = (min_for_padding - size);
    while (size--) {
      /* nop */
      util_buffer_add_1b(p_buf, 0x90);
    }
  }

  return util_buffer_get_pos(p_buf);
}

void
asm_x64_emit_instruction_CRASH(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_CRASH,
               asm_x64_instruction_CRASH_END,
               2);
}

void
asm_x64_emit_instruction_TRAP(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_TRAP,
               asm_x64_instruction_TRAP_END,
               2);
}

void
asm_x64_emit_instruction_PHP(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_asm_emit_intel_flags_to_scratch,
               asm_x64_asm_emit_intel_flags_to_scratch_END,
               0);
  asm_x64_copy(p_buf,
               asm_x64_set_brk_flag_in_scratch,
               asm_x64_set_brk_flag_in_scratch_END,
               0);
  asm_x64_copy(p_buf,
               asm_x64_push_from_scratch,
               asm_x64_push_from_scratch_END,
               0);
}

void
asm_x64_emit_instruction_CMP_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_CMP_imm_interp,
               asm_x64_instruction_CMP_imm_interp_END,
               0);
}

void
asm_x64_emit_instruction_CMP_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_CMP_scratch_interp,
               asm_x64_instruction_CMP_scratch_interp_END,
               0);
}

void
asm_x64_emit_instruction_LDA_imm_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDA_imm_interp,
               asm_x64_instruction_LDA_imm_interp_END,
               0);
}

void
asm_x64_emit_instruction_LDA_scratch_interp(struct util_buffer* p_buf) {
  asm_x64_copy(p_buf,
               asm_x64_instruction_LDA_scratch_interp,
               asm_x64_instruction_LDA_scratch_interp_END,
               0);
}
