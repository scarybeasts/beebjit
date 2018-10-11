#include "emit_6502.h"

#include <assert.h>

#include "opdefs.h"
#include "util.h"

static void
emit_operand(struct util_buffer* p_buf, int mode, uint16_t addr) {
  size_t oplen = g_opmodelens[mode];
  switch (oplen) {
  case 1:
    break;
  case 2:
    util_buffer_add_1b(p_buf, (unsigned char) addr);
    break;
  case 3:
    util_buffer_add_2b(p_buf,
                       (unsigned char) addr,
                       (unsigned char) (addr >> 8));
    break;
  default:
    assert(0);
  }
}

static void
emit_from_array(struct util_buffer* p_buf,
                unsigned char* p_bytes,
                int mode,
                uint16_t addr) {
  util_buffer_add_1b(p_buf, p_bytes[mode]);
  emit_operand(p_buf, mode, addr);
}

void
emit_BEQ(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0xf0, offset);
}

void
emit_BMI(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x30, offset);
}

void
emit_CMP(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[13] =
  { 0x00,
    0x00, 0xc9, 0xc5, 0xcd, 0xd5, 0x00, 0xdd, 0xd9, 0xc1, 0xd1, 0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_CRASH(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xf2);
}

void
emit_JMP(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[13] =
  { 0x00,
    0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6c, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_LDA(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[13] =
  { 0x00,
    0x00, 0xa9, 0xa5, 0xad, 0xb5, 0x00, 0xbd, 0xb9, 0xa1, 0xb1, 0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_PHP(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x08);
}

void
emit_PLP(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x28);
}

void
emit_STA(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[13] =
  { 0x00,
    0x00, 0x00, 0x85, 0x8d, 0x95, 0x00, 0x9d, 0x99, 0x81, 0x91, 0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}
