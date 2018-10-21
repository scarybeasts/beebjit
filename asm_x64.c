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
