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
emit_ADC(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x69, 0x65, 0x6d, 0x75, 0x00, 0x7d, 0x79, 0x61, 0x71,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_AND(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x29, 0x25, 0x2d, 0x35, 0x00, 0x3d, 0x39, 0x21, 0x31,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_ASL(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x0a, 0x00, 0x06, 0x0e, 0x16, 0x00, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_BCC(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x90, (unsigned char) offset);
}

void
emit_BCS(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0xb0, (unsigned char) offset);
}

void
emit_BEQ(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0xf0, (unsigned char) offset);
}

void
emit_BIT(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x24, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_BMI(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x30, (unsigned char) offset);
}

void
emit_BNE(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0xd0, (unsigned char) offset);
}

void
emit_BPL(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x10, (unsigned char) offset);
}

void
emit_BRK(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x00);
}

void
emit_BVC(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x50, (unsigned char) offset);
}

void
emit_BVS(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x70, (unsigned char) offset);
}

void
emit_CLC(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x18);
}

void
emit_CLI(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x58);
}

void
emit_CLV(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xb8);
}

void
emit_CMP(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xc9, 0xc5, 0xcd, 0xd5, 0x00, 0xdd, 0xd9, 0xc1, 0xd1,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_CPX(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xe0, 0xe4, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_CPY(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xc0, 0xc4, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_CRASH(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xf2);
}

void
emit_EXIT(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x02);
}

void
emit_DEC(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0xc6, 0xce, 0xd6, 0x00, 0xde, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_DEX(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xca);
}

void
emit_DEY(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x88);
}

void
emit_INC(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0xe6, 0xee, 0xf6, 0x00, 0xfe, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_INX(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xe8);
}

void
emit_INY(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xc8);
}

void
emit_JMP(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x6c, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_JSR(struct util_buffer* p_buf, uint16_t addr) {
  util_buffer_add_1b(p_buf, 0x20);
  emit_operand(p_buf, k_abs, addr);
}

void
emit_LDA(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xa9, 0xa5, 0xad, 0xb5, 0x00, 0xbd, 0xb9, 0xa1, 0xb1,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_LDX(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xa2, 0xa6, 0xae, 0x00, 0xb6, 0x00, 0xbe, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_LDY(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xa0, 0xa4, 0xac, 0xb4, 0x00, 0xbc, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_LSR(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x4a, 0x00, 0x46, 0x4e, 0x56, 0x00, 0x5e, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_NOP(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xea);
}

void
emit_PHA(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x48);
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
emit_ROL(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x2a, 0x00, 0x26, 0x2e, 0x36, 0x00, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_ROR(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x6a, 0x00, 0x66, 0x6e, 0x76, 0x00, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_RTI(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x40);
}

void
emit_RTS(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x60);
}

void
emit_SBC(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xe9, 0xe5, 0xed, 0xf5, 0x00, 0xfd, 0xfd, 0xe1, 0xf1,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_SEC(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x38);
}

void
emit_SEI(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x78);
}

void
emit_STA(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x85, 0x8d, 0x95, 0x00, 0x9d, 0x99, 0x81, 0x91,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_STX(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x86, 0x8e, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_TAX(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xAA);
}

void
emit_TAY(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xA8);
}

void
emit_TSX(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xba);
}

void
emit_TXS(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x9a);
}
