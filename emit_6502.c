#include "emit_6502.h"

#include <assert.h>

#include "defs_6502.h"
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
    0x00, 0x00, 0x69, 0x65, 0x6D, 0x75, 0x00, 0x7D, 0x79, 0x61, 0x71,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_AND(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x29, 0x25, 0x2D, 0x35, 0x00, 0x3D, 0x39, 0x21, 0x31,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_ASL(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x0A, 0x00, 0x06, 0x0E, 0x16, 0x00, 0x1E, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_BCC(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x90, (unsigned char) offset);
}

void
emit_BCS(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0xB0, (unsigned char) offset);
}

void
emit_BEQ(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0xF0, (unsigned char) offset);
}

void
emit_BIT(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x24, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_BMI(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0x30, (unsigned char) offset);
}

void
emit_BNE(struct util_buffer* p_buf, char offset) {
  util_buffer_add_2b(p_buf, 0xD0, (unsigned char) offset);
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
emit_CLD(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xD8);
}

void
emit_CLI(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x58);
}

void
emit_CLV(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xB8);
}

void
emit_CMP(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xC9, 0xC5, 0xCD, 0xD5, 0x00, 0xDD, 0xD9, 0xC1, 0xD1,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_CPX(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xE0, 0xE4, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_CPY(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xC0, 0xC4, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_CRASH(struct util_buffer* p_buf) {
  emit_STA(p_buf, k_abs, 0xFEE0);
}

void
emit_CYCLES(struct util_buffer* p_buf) {
  emit_LDA(p_buf, k_abs, 0xFEE1);
}

void
emit_CYCLES_RESET(struct util_buffer* p_buf) {
  emit_STA(p_buf, k_abs, 0xFEE1);
}

void
emit_EXIT(struct util_buffer* p_buf) {
  emit_LDA(p_buf, k_imm, 0xA5);
  emit_STA(p_buf, k_abs, 0xFEE2);
}

void
emit_DEC(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0xC6, 0xCE, 0xD6, 0x00, 0xDE, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_DEX(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xCA);
}

void
emit_DEY(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x88);
}

void
emit_EOR(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4D, 0x55, 0x00, 0x5D, 0x59, 0x41, 0x51,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_INC(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0xE6, 0xEE, 0xF6, 0x00, 0xFE, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_INX(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xE8);
}

void
emit_INY(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xC8);
}

void
emit_JMP(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x6C, 0x00 };
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
    0x00, 0x00, 0xA9, 0xA5, 0xAD, 0xB5, 0x00, 0xBD, 0xB9, 0xA1, 0xB1,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_LDX(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xA2, 0xA6, 0xAE, 0x00, 0xB6, 0x00, 0xBE, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_LDY(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0xA0, 0xA4, 0xAC, 0xB4, 0x00, 0xBC, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_LSR(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x4A, 0x00, 0x46, 0x4E, 0x56, 0x00, 0x5E, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_NOP(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xEA);
}

void
emit_NOP1(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x03);
}

void
emit_ORA(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x09, 0x05, 0x0D, 0x15, 0x00, 0x1D, 0x19, 0x01, 0x11,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
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
emit_PLA(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x68);
}

void
emit_PLP(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x28);
}

void
emit_ROL(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x2A, 0x00, 0x26, 0x2E, 0x36, 0x00, 0x3E, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_ROR(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x6A, 0x00, 0x66, 0x6E, 0x76, 0x00, 0x7E, 0x00, 0x00, 0x00,
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
    0x00, 0x00, 0xE9, 0xE5, 0xED, 0xF5, 0x00, 0xFD, 0xFD, 0xE1, 0xF1,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_SEC(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x38);
}

void
emit_SED(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0xF8);
}

void
emit_SEI(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x78);
}

void
emit_STA(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x85, 0x8D, 0x95, 0x00, 0x9D, 0x99, 0x81, 0x91,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_STX(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x86, 0x8E, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_STY(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x84, 0x8C, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
  emit_from_array(p_buf, &s_bytes[0], mode, addr);
}

void
emit_STZ(struct util_buffer* p_buf, int mode, uint16_t addr) {
  static unsigned char s_bytes[k_6502_op_num_modes] =
  { 0x00,
    0x00, 0x00, 0x00, 0x64, 0x9C, 0x74, 0x00, 0x9E, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x00 };
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
  util_buffer_add_1b(p_buf, 0xBA);
}

void
emit_TXA(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x8A);
}

void
emit_TXS(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x9A);
}

void
emit_TYA(struct util_buffer* p_buf) {
  util_buffer_add_1b(p_buf, 0x98);
}
