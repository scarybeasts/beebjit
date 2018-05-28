#include "jit.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const size_t k_addr_space_size = 0x10000;
static const size_t k_guard_size = 4096;
static const int k_jit_bytes_per_byte = 256;
static const int k_jit_bytes_shift = 8;
static const size_t k_max_opcode_len = 16;
static const size_t k_max_extra_len = 32;

// 0:  debug_callback
// 8:  pointer to 6502 memory
// 16: 6502 ip
// 24: 6502 A
// 32: 6502 X
// 40: 6502 Y
// 48: 6502 S
static char g_jit_debug_space[64];

enum {
  k_kil = 0,
  k_unk = 1,
  k_brk = 2,
  k_ora = 3,
  k_asl = 4,
  k_php = 5,
  k_bpl = 6,
  k_clc = 7,
  k_jsr = 8,
  k_and = 9,
  k_bit = 10,
  k_plp = 11,
  k_rol = 12,
  k_bmi = 13,
  k_sec = 14,
  k_rti = 15,
  k_eor = 16,
  k_lsr = 17,
  k_pha = 18,
  k_jmp = 19,
  k_bvc = 20,
  k_cli = 21,
  k_rts = 22,
  k_adc = 23,
  k_pla = 24,
  k_ror = 25,
  k_bvs = 26,
  k_sei = 27,
  k_sty = 28,
  k_sta = 29,
  k_stx = 30,
  k_dey = 31,
  k_txa = 32,
  k_bcc = 33,
  k_tya = 34,
  k_txs = 35,
  k_ldy = 36,
  k_lda = 37,
  k_ldx = 38,
  k_tay = 39,
  k_tax = 40,
  k_bcs = 41,
  k_clv = 42,
  k_tsx = 43,
  k_cpy = 44,
  k_cmp = 45,
  k_cpx = 46,
  k_dec = 47,
  k_iny = 48,
  k_dex = 49,
  k_bne = 50,
  k_cld = 51,
  k_sbc = 52,
  k_inx = 53,
  k_nop = 54,
  k_inc = 55,
  k_beq = 56,
  k_sed = 57,
};

static const char* g_p_opnames[58] = {
  "!!!", "???", "BRK", "ORA", "ASL", "PHP", "BPL", "CLC",
  "JSR", "AND", "BIT", "PLP", "ROL", "BMI", "SEC", "RTI",
  "EOR", "LSR", "PHA", "JMP", "BVC", "CLI", "RTS", "ADC",
  "PLA", "ROR", "BVS", "SEI", "STY", "STA", "STX", "DEY",
  "TXA", "BCC", "TYA", "TXS", "LDY", "LDA", "LDX", "TAY",
  "TAX", "BCS", "CLV", "TSX", "CPY", "CMP", "CPX", "DEC",
  "INY", "DEX", "BNE", "CLD", "SBC", "INX", "NOP", "INC",
  "BEQ", "SED",
};


static unsigned char g_optypes[256] =
{
  // 0x00
  k_brk, k_ora, k_kil, k_unk, k_unk, k_ora, k_asl, k_unk,
  k_php, k_ora, k_asl, k_unk, k_unk, k_ora, k_asl, k_unk,
  // 0x10
  k_bpl, k_ora, k_kil, k_unk, k_unk, k_ora, k_asl, k_unk,
  k_clc, k_ora, k_unk, k_unk, k_unk, k_ora, k_asl, k_unk,
  // 0x20
  k_jsr, k_and, k_kil, k_unk, k_bit, k_and, k_rol, k_unk,
  k_plp, k_and, k_rol, k_unk, k_bit, k_and, k_rol, k_unk,
  // 0x30
  k_bmi, k_and, k_kil, k_unk, k_unk, k_and, k_rol, k_unk,
  k_sec, k_and, k_unk, k_unk, k_unk, k_and, k_rol, k_unk,
  // 0x40
  k_rti, k_eor, k_kil, k_unk, k_unk, k_eor, k_lsr, k_unk,
  k_pha, k_eor, k_lsr, k_unk, k_jmp, k_eor, k_lsr, k_unk,
  // 0x50
  k_bvc, k_eor, k_kil, k_unk, k_unk, k_eor, k_lsr, k_unk,
  k_cli, k_eor, k_unk, k_unk, k_unk, k_eor, k_lsr, k_unk,
  // 0x60
  k_rts, k_adc, k_kil, k_unk, k_unk, k_adc, k_ror, k_unk,
  k_pla, k_adc, k_ror, k_unk, k_jmp, k_adc, k_ror, k_unk,
  // 0x70
  k_bvs, k_adc, k_kil, k_unk, k_unk, k_adc, k_ror, k_unk,
  k_sei, k_adc, k_unk, k_unk, k_unk, k_adc, k_ror, k_unk,
  // 0x80
  k_unk, k_sta, k_unk, k_unk, k_sty, k_sta, k_stx, k_unk,
  k_dey, k_unk, k_txa, k_unk, k_sty, k_sta, k_stx, k_unk,
  // 0x90
  k_bcc, k_sta, k_kil, k_unk, k_sty, k_sta, k_stx, k_unk,
  k_tya, k_sta, k_txs, k_unk, k_unk, k_sta, k_unk, k_unk,
  // 0xa0
  k_ldy, k_lda, k_ldx, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  k_tay, k_lda, k_tax, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  // 0xb0
  k_bcs, k_lda, k_kil, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  k_clv, k_lda, k_tsx, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  // 0xc0
  k_cpy, k_cmp, k_unk, k_unk, k_cpy, k_cmp, k_dec, k_unk,
  k_iny, k_cmp, k_dex, k_unk, k_cpy, k_cmp, k_dec, k_unk,
  // 0xd0
  k_bne, k_cmp, k_kil, k_unk, k_unk, k_cmp, k_dec, k_unk,
  k_cld, k_cmp, k_unk, k_unk, k_unk, k_cmp, k_dec, k_unk,
  // 0xe0
  k_cpx, k_sbc, k_unk, k_unk, k_cpx, k_sbc, k_inc, k_unk,
  k_inx, k_sbc, k_nop, k_unk, k_cpx, k_sbc, k_inc, k_unk,
  // 0xf0
  k_beq, k_sbc, k_kil, k_unk, k_unk, k_sbc, k_inc, k_unk,
  k_sed, k_sbc, k_unk, k_unk, k_unk, k_sbc, k_inc, k_unk,
};

enum {
  k_nil = 1,
  k_imm = 2,
  k_zpg = 3,
  k_abs = 4,
  k_zpx = 5,
  k_zpy = 6,
  k_abx = 7,
  k_aby = 8,
  k_idx = 9,
  k_idy = 10,
  k_ind = 11,
};

static unsigned char g_opmodes[256] =
{
  // 0x00
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , 0    , k_abs, k_abs, 0    ,
  // 0x10
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x20
  k_abs, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x30
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x40
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x50
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x60
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_ind, k_abs, k_abs, 0    ,
  // 0x70
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x80
  0    , k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, 0    , k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x90
  k_imm, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , 0    , k_abx, 0    , 0    ,
  // 0xa0
  k_imm, k_idx, k_imm, 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0xb0
  k_imm, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_aby, 0    ,
  // 0xc0
  k_imm, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0xd0
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0xe0
  k_imm, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0xf0
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
};

void
jit_init(unsigned char* p_mem) {
  unsigned char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  // nop
  memset(p_jit, '\x90', k_addr_space_size * k_jit_bytes_per_byte);
  size_t num_bytes = k_addr_space_size;
  while (num_bytes--) {
    // ud2
    p_jit[0] = 0x0f;
    p_jit[1] = 0x0b;
    p_jit += k_jit_bytes_per_byte;
  }
}

static size_t jit_emit_int(unsigned char* p_jit, size_t index, ssize_t offset) {
  p_jit[index++] = offset & 0xff;
  offset >>= 8;
  p_jit[index++] = offset & 0xff;
  offset >>= 8;
  p_jit[index++] = offset & 0xff;
  offset >>= 8;
  p_jit[index++] = offset & 0xff;

  return index;
}

static size_t jit_emit_op1_op2(unsigned char* p_jit,
                               size_t index,
                               unsigned char operand1,
                               unsigned char operand2) {
  p_jit[index++] = operand1;
  p_jit[index++] = operand2;
  p_jit[index++] = 0;
  p_jit[index++] = 0;

  return index;
}

static size_t jit_emit_do_jmp_next(unsigned char* p_jit,
                                   size_t index,
                                   size_t oplen) {
  assert(index + 2 <= k_jit_bytes_per_byte);
  size_t offset = (k_jit_bytes_per_byte * oplen) - (index + 2);
  if (offset <= 0x7f) {
    // jmp
    p_jit[index++] = 0xeb;
    p_jit[index++] = offset;
  } else {
    offset -= 3;
    p_jit[index++] = 0xe9;
    index = jit_emit_int(p_jit, index, offset);
  }

  return index;
}

static size_t jit_emit_do_relative_jump(unsigned char* p_jit,
                                        size_t index,
                                        unsigned char intel_opcode,
                                        unsigned char unsigned_jump_size) {
  char jump_size = (char) unsigned_jump_size;
  ssize_t offset = (k_jit_bytes_per_byte * (jump_size + 2)) - (index + 2);
  if (offset <= 0x7f && offset >= -0x80) {
    // Fits in a 1-byte offset.
    assert(index + 2 <= k_jit_bytes_per_byte);
    p_jit[index++] = intel_opcode;
    p_jit[index++] = (unsigned char) offset;
  } else {
    offset -= 4;
    assert(index + 6 <= k_jit_bytes_per_byte);
    p_jit[index++] = 0x0f;
    p_jit[index++] = intel_opcode + 0x10;
    index = jit_emit_int(p_jit, index, offset);
  }

  return index;
}

static size_t jit_emit_intel_to_6502_zero(unsigned char* p_jit, size_t index) {
  // sete r10b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x94;
  p_jit[index++] = 0xc2;

  return index;
}

static size_t jit_emit_intel_to_6502_negative(unsigned char* p_jit,
                                              size_t index) {
  // sets r11b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x98;
  p_jit[index++] = 0xc3;

  return index;
}

static size_t jit_emit_intel_to_6502_carry(unsigned char* p_jit, size_t index) {
  // setb r9b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc1;

  return index;
}

static size_t jit_emit_intel_to_6502_sub_carry(unsigned char* p_jit,
                                               size_t index) {
  // setae r9b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x93;
  p_jit[index++] = 0xc1;

  return index;
}

static size_t jit_emit_intel_to_6502_overflow(unsigned char* p_jit,
                                              size_t index) {
  // seto r12b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x90;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t jit_emit_carry_to_6502_zero(unsigned char* p_jit, size_t index) {
  // setb r10b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc2;

  return index;
}

static size_t jit_emit_carry_to_6502_negative(unsigned char* p_jit,
                                              size_t index) {
  // setb r11b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc3;

  return index;
}

static size_t jit_emit_carry_to_6502_overflow(unsigned char* p_jit,
                                              size_t index) {
  // setb r12b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t jit_emit_do_zn_flags(unsigned char* p_jit,
                                   size_t index,
                                   int reg) {
  assert(index + 8 <= k_jit_bytes_per_byte);
  if (reg == -1) {
    // Nothing -- flags already set.
  } else if (reg == 0) {
    // test al, al
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xc0;
  } else if (reg == 1) {
    // test bl, bl
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xdb;
  } else if (reg == 2) {
    // test cl, cl
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xc9;
  }

  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);

  return index;
}

static size_t jit_emit_intel_to_6502_znc(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);
  index = jit_emit_intel_to_6502_carry(p_jit, index);

  return index;
}

static size_t jit_emit_intel_to_6502_sub_znc(unsigned char* p_jit,
                                             size_t index) {
  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);
  index = jit_emit_intel_to_6502_sub_carry(p_jit, index);

  return index;
}

static size_t jit_emit_intel_to_6502_znco(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_znc(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t jit_emit_intel_to_6502_sub_znco(unsigned char* p_jit,
                                              size_t index) {
  index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t jit_emit_6502_carry_to_intel(unsigned char* p_jit, size_t index) {
  // Note: doesn't just check carry value but also trashes it.
  // shr r9b, 1
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xd0;
  p_jit[index++] = 0xe9;

  return index;
}

static size_t jit_emit_set_carry(unsigned char* p_jit,
                                 size_t index,
                                 unsigned char val) {
  // mov r9b, val
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xb1;
  p_jit[index++] = val;

  return index;
}

static size_t jit_emit_test_carry(unsigned char* p_jit, size_t index) {
  // test r9b, r9b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xc9;

  return index;
}

static size_t jit_emit_test_zero(unsigned char* p_jit, size_t index) {
  // test r10b, r10b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xd2;

  return index;
}

static size_t jit_emit_test_negative(unsigned char* p_jit, size_t index) {
  // test r11b, r11b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xdb;

  return index;
}

static size_t jit_emit_test_overflow(unsigned char* p_jit, size_t index) {
  // test r12b, r12b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xe4;

  return index;
}

static size_t jit_emit_abs_x_to_scratch(unsigned char* p_jit,
                                        size_t index,
                                        unsigned char operand1,
                                        unsigned char operand2) {
  // mov edx, ebx
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xda;
  // add dx, op1,op2
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x81;
  p_jit[index++] = 0xc2;
  p_jit[index++] = operand1;
  p_jit[index++] = operand2;

  return index;
}

static size_t jit_emit_abs_y_to_scratch(unsigned char* p_jit,
                                        size_t index,
                                        unsigned char operand1,
                                        unsigned char operand2) {
  // mov edx, ecx
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xca;
  // add dx, op1,op2
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x81;
  p_jit[index++] = 0xc2;
  p_jit[index++] = operand1;
  p_jit[index++] = operand2;

  return index;
}

static size_t jit_emit_ind_y_to_scratch(unsigned char* p_jit,
                                        size_t index,
                                        unsigned char operand1) {
  if (operand1 == 0xff) {
    // movzx edx, BYTE PTR [rdi + 0xff]
    p_jit[index++] = 0x0f;
    p_jit[index++] = 0xb6;
    p_jit[index++] = 0x97;
    index = jit_emit_op1_op2(p_jit, index, 0xff, 0);
    // mov dh, BYTE PTR [rdi]
    p_jit[index++] = 0x8a;
    p_jit[index++] = 0x37;
  } else {
    // movzx edx, WORD PTR [rdi + op1]
    p_jit[index++] = 0x0f;
    p_jit[index++] = 0xb7;
    p_jit[index++] = 0x97;
    index = jit_emit_op1_op2(p_jit, index, operand1, 0);
  }
  // add dx, cx
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x01;
  p_jit[index++] = 0xca;

  return index;
}

static size_t jit_emit_ind_x_to_scratch(unsigned char* p_jit,
                                        size_t index,
                                        unsigned char operand1) {
  unsigned char operand1_inc = operand1 + 1;
  // mov r15, rbx
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xdf;
  // add r15b, operand1_inc
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x80;
  p_jit[index++] = 0xc7;
  p_jit[index++] = operand1_inc;
  // movzx rdx, BYTE PTR [rdi + r15]
  p_jit[index++] = 0x4a;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3f;
  // shl edx, 8
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe2;
  p_jit[index++] = 0x08;
  // dec r15b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xfe;
  p_jit[index++] = 0xcf;
  // mov dl, BYTE PTR [rdi + r15]
  p_jit[index++] = 0x42;
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3f;

  return index;
}

size_t jit_emit_zp_x_to_scratch(unsigned char* p_jit,
                                size_t index,
                                unsigned char operand1) {
  // mov edx, ebx
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xda;
  // add dl, op1
  p_jit[index++] = 0x80;
  p_jit[index++] = 0xc2;
  p_jit[index++] = operand1;

  return index;
}

size_t jit_emit_zp_y_to_scratch(unsigned char* p_jit,
                                size_t index,
                                unsigned char operand1) {
  // mov edx, ecx
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xca;
  // add dl, op1
  p_jit[index++] = 0x80;
  p_jit[index++] = 0xc2;
  p_jit[index++] = operand1;

  return index;
}

static size_t jit_emit_scratch_bit_test(unsigned char* p_jit,
                                        size_t index,
                                        unsigned char bit) {
  // bt edx, bit
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe2;
  p_jit[index++] = bit;

  return index;
}

static size_t jit_emit_jmp_scratch(unsigned char* p_jit, size_t index) {
  // jmp rdx
  p_jit[index++] = 0xff;
  p_jit[index++] = 0xe2;

  return index;
}

static size_t jit_emit_jmp_op1_op2(unsigned char* p_jit,
                                   size_t index,
                                   unsigned char operand1,
                                   unsigned char operand2) {
  // lea rdx, [rdi + k_addr_space_size + k_guard_size +
  //               op1,op2 * k_jit_bytes_per_byte]
  p_jit[index++] = 0x48;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x97;
  index = jit_emit_int(p_jit,
                       index,
                       k_addr_space_size + k_guard_size +
                           ((operand1 + (operand2 << 8)) *
                               k_jit_bytes_per_byte));
  index = jit_emit_jmp_scratch(p_jit, index);

  return index;
}

static size_t jit_emit_jit_bytes_shift_scratch_left(unsigned char* p_jit,
                                                    size_t index) {
  // shl edx, k_jit_bytes_shift
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe2;
  p_jit[index++] = k_jit_bytes_shift;

  return index;
}

static size_t jit_emit_jit_bytes_shift_scratch_right(unsigned char* p_jit,
                                                     size_t index) {
  // shr edx, k_jit_bytes_shift
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xea;
  p_jit[index++] = k_jit_bytes_shift;

  return index;
}

static size_t jit_emit_stack_inc(unsigned char* p_jit, size_t index) {
  // inc sil
  p_jit[index++] = 0x40;
  p_jit[index++] = 0xfe;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t jit_emit_stack_dec(unsigned char* p_jit, size_t index) {
  // dec sil
  p_jit[index++] = 0x40;
  p_jit[index++] = 0xfe;
  p_jit[index++] = 0xce;

  return index;
}

static size_t jit_emit_pull_to_a(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  // mov al, [rsi]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x06;

  return index;
}

static size_t jit_emit_pull_to_scratch(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  // mov dl, [rsi]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x16;

  return index;
}

static size_t jit_emit_pull_to_scratch_word(unsigned char* p_jit,
                                            size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  // movzx edx, BYTE PTR [rsi]
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_inc(p_jit, index);
  // mov dh, BYTE PTR [rsi]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x36;

  return index;
}

static size_t jit_emit_push_from_a(unsigned char* p_jit, size_t index) {
  // mov [rsi], al
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x06;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t jit_emit_push_from_scratch(unsigned char* p_jit, size_t index) {
  // mov [rsi], dl
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t jit_emit_push_from_scratch_word(unsigned char* p_jit,
                                              size_t index) {
  // mov [rsi], dh
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x36;
  index = jit_emit_stack_dec(p_jit, index);
  // mov [rsi], dl
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t jit_emit_6502_ip_to_scratch(unsigned char* p_jit, size_t index) {
  // lea rdx, [rip - (k_addr_space_size + k_guard_size)]
  p_jit[index++] = 0x48;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x15;
  index = jit_emit_int(p_jit,
                       index,
                       -(ssize_t) (k_addr_space_size + k_guard_size));
  // sub rdx, rdi
  p_jit[index++] = 0x48;
  p_jit[index++] = 0x29;
  p_jit[index++] = 0xfa;
  index = jit_emit_jit_bytes_shift_scratch_right(p_jit, index);

  return index;
}

static size_t jit_emit_push_ip_plus_two(unsigned char* p_jit, size_t index) {
  index = jit_emit_6502_ip_to_scratch(p_jit, index);
  // add edx, 2
  p_jit[index++] = 0x83;
  p_jit[index++] = 0xc2;
  p_jit[index++] = 0x02;
  index = jit_emit_push_from_scratch_word(p_jit, index);

  return index;
}

static size_t jit_emit_php(unsigned char* p_jit, size_t index) {
  // mov rdx, r8
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xc2;
  // or rdx, r9
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xca;

  // mov r15, r10
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xd7;
  // shl r15, 1
  p_jit[index++] = 0x49;
  p_jit[index++] = 0xd1;
  p_jit[index++] = 0xe7;
  // or rdx, r15
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xfa;

  // mov r15, r11
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xdf;
  // shl r15, 7
  p_jit[index++] = 0x49;
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x07;
  // or rdx, r15
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xfa;

  // mov r15, r12
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xe7;
  // shl r15, 6
  p_jit[index++] = 0x49;
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x06;
  // or rdx, r15
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xfa;

  index = jit_emit_push_from_scratch(p_jit, index);

  return index;
}

static size_t jit_emit_jmp_indirect(unsigned char* p_jit,
                                    size_t index,
                                    unsigned char addr_low,
                                    unsigned char addr_high) {
  unsigned char next_addr_high = addr_high;
  unsigned char next_addr_low = addr_low + 1;
  if (next_addr_low == 0) {
    next_addr_high++;
  }
  // movzx edx, BYTE PTR [rdi + low,high]
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0x97;
  index = jit_emit_op1_op2(p_jit, index, addr_low, addr_high);
  // mov dh, BYTE PTR [rdi + low,high + 1]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0xb7;
  index = jit_emit_op1_op2(p_jit, index, next_addr_low, next_addr_high);
  index = jit_emit_jit_bytes_shift_scratch_left(p_jit, index);
  // lea rdx, [rdi + rdx + k_addr_space_size + k_guard_size]
  p_jit[index++] = 0x48;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x94;
  p_jit[index++] = 0x17;
  index = jit_emit_int(p_jit, index, k_addr_space_size + k_guard_size);
  index = jit_emit_jmp_scratch(p_jit, index);

  return index;
}

size_t jit_emit_undefined(unsigned char* p_jit,
                          size_t index,
                          unsigned char opcode,
                          size_t jit_offset) {
  // ud2
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x0b;
  // Copy of unimplemented 6502 opcode.
  p_jit[index++] = opcode;
  // Virtual address of opcode, big endian.
  p_jit[index++] = jit_offset >> 8;
  p_jit[index++] = jit_offset & 0xff;

  return index;
}

static size_t jit_emit_debug_sequence(unsigned char* p_jit, size_t index) {
  index = jit_emit_6502_ip_to_scratch(p_jit, index);
  // Save 6502 IP
  // mov [r14 + 16], rdx
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0x56;
  p_jit[index++] = 0x10;
  // Save 6502 A
  // mov [r14 + 24], rax
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0x46;
  p_jit[index++] = 0x18;
  // Save 6502 X
  // mov [r14 + 32], rbx
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0x5e;
  p_jit[index++] = 0x20;
  // Save 6502 Y
  // mov [r14 + 40], rcx
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0x4e;
  p_jit[index++] = 0x28;
  // Save 6502 S
  // mov [r14 + 48], rsi
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0x76;
  p_jit[index++] = 0x30;
  // push rax / rcx / rdx / rsi / rdi
  p_jit[index++] = 0x50;
  p_jit[index++] = 0x51;
  p_jit[index++] = 0x52;
  p_jit[index++] = 0x56;
  p_jit[index++] = 0x57;
  // push r8 / r9 / r10 / r11
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x50;
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x51;
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x52;
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x53;
  // call [r14]
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xff;
  p_jit[index++] = 0x16;
  // pop r11 / r10 / r9 / r8
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x5b;
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x5a;
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x59;
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x58;
  // pop rdi / rsi / rdx / rcx / rax
  p_jit[index++] = 0x5f;
  p_jit[index++] = 0x5e;
  p_jit[index++] = 0x5a;
  p_jit[index++] = 0x59;
  p_jit[index++] = 0x58;

  return index;
}

static size_t jit_emit_calc_op(unsigned char* p_jit,
                               size_t index,
                               unsigned char opmode,
                               unsigned char operand1,
                               unsigned char operand2,
                               unsigned char intel_op_base) {
  switch (opmode) {
  case k_imm:
    // OP al, op1
    p_jit[index++] = intel_op_base + 2;
    p_jit[index++] = operand1;
    break;
  case k_zpg:
  case k_abs:
    // OP al, [rdi + op1,op2?]
    p_jit[index++] = intel_op_base;
    p_jit[index++] = 0x87;
    index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
    break;
  default:
    // OP al, [rdi + rdx]
    p_jit[index++] = intel_op_base;
    p_jit[index++] = 0x04;
    p_jit[index++] = 0x17;
    break;
  }

  return index;
}

static size_t jit_emit_shift_op(unsigned char* p_jit,
                                size_t index,
                                unsigned char opmode,
                                unsigned char operand1,
                                unsigned char operand2,
                                unsigned char intel_op_base) {
  switch (opmode) {
  case k_nil:
    // OP al, 1
    p_jit[index++] = 0xd0;
    p_jit[index++] = intel_op_base;
    break;
  case k_zpg:
  case k_abs:
    // OP BYTE PTR [rdi + op1,op2?], 1
    p_jit[index++] = 0xd0;
    p_jit[index++] = intel_op_base - 0x39;
    index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
    break;
  default:
    // OP BYTE PTR [rdi + rdx], 1
    p_jit[index++] = 0xd0;
    p_jit[index++] = intel_op_base - 0xbc;
    p_jit[index++] = 0x17;
    break;
  }

  return index;
}

static size_t jit_emit_post_rotate(unsigned char* p_jit,
                                   size_t index,
                                   unsigned char opmode,
                                   unsigned char operand1,
                                   unsigned char operand2) {
  index = jit_emit_intel_to_6502_carry(p_jit, index);
  switch (opmode) {
  case k_nil:
    index = jit_emit_do_zn_flags(p_jit, index, 0);
    break;
  case k_zpg:
  case k_abs:
    // test BYTE PTR [rdi + op1,op2?], 0xff
    p_jit[index++] = 0xf6;
    p_jit[index++] = 0x87;
    index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
    p_jit[index++] = 0xff;
    index = jit_emit_do_zn_flags(p_jit, index, -1);
    break;
  default:
    // test BYTE PTR [rdi + rdx], 0xff
    p_jit[index++] = 0xf6;
    p_jit[index++] = 0x04;
    p_jit[index++] = 0x17;
    p_jit[index++] = 0xff;
    index = jit_emit_do_zn_flags(p_jit, index, -1);
    break;
  }

  return index;
}

void
jit_jit(unsigned char* p_mem,
        size_t jit_offset,
        size_t jit_len,
        unsigned int debug_flags) {
  unsigned char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  size_t jit_end = jit_offset + jit_len;
  p_mem += jit_offset;
  p_jit += (jit_offset * k_jit_bytes_per_byte);
  while (jit_offset < jit_end) {
    unsigned char opcode = p_mem[0];
    unsigned char opmode = g_opmodes[opcode];
    unsigned char optype = g_optypes[opcode];
    unsigned char oplen = 1;
    unsigned char operand1 = 0;
    unsigned char operand2 = 0;
    size_t index = 0;

    // Note: not correct if JIT code wraps the address space but that shouldn't
    // happen in normal operation: the end of address space contains IRQ / reset
    // etc. vectors.
    if (jit_offset + 1 < jit_end) {
      operand1 = p_mem[1];
    }
    if (jit_offset + 2 < jit_end) {
      operand2 = p_mem[2];
    }

    if (debug_flags) {
      index = jit_emit_debug_sequence(p_jit, index);
    }

    switch (opmode) {
    case k_imm:
      oplen = 2;
      break;
    case k_zpg:
      oplen = 2;
      break;
    case k_abs:
      oplen = 3;
      break;
    case k_zpx:
      index = jit_emit_zp_x_to_scratch(p_jit, index, operand1);
      oplen = 2;
      break;
    case k_zpy:
      index = jit_emit_zp_y_to_scratch(p_jit, index, operand1);
      oplen = 2;
      break;
    case k_abx:
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      oplen = 3;
      break;
    case k_aby:
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      oplen = 3;
      break;
    case k_idy:
      index = jit_emit_ind_y_to_scratch(p_jit, index, operand1);
      oplen = 2;
      break;
    case k_idx:
      index = jit_emit_ind_x_to_scratch(p_jit, index, operand1);
      oplen = 2;
      break;
    case k_ind:
      oplen = 3;
      break;
    default:
      break;
    }

    if (oplen < 3) { 
      // Clear operand2 if we're not using it. This enables us to re-use the
      // same x64 opcode generation code for both k_zpg and k_abs.
      operand2 = 0;
    }

    switch (optype) {
    case k_kil:
      switch (opcode) {
      case 0x02:
        // Illegal opcode. Hangs a standard 6502.
        // Bounce out of JIT.
        // ret
        p_jit[index++] = 0xc3;
        break;
      case 0x12:
        // Illegal opcode. Hangs a standard 6502.
        // Generate a debug trap and continue.
        // int 3
        p_jit[index++] = 0xcc;
        break;
      case 0xf2:
        // Illegal opcode. Hangs a standard 6502.
        // Generate a SEGV.
        // xor rdx, rdx
        p_jit[index++] = 0x31;
        p_jit[index++] = 0xd2;
        index = jit_emit_jmp_scratch(p_jit, index);
        break;
      default:
        index = jit_emit_undefined(p_jit, index, opcode, jit_offset);
        break;
      }
      break;
    case k_brk:
      // BRK
      index = jit_emit_push_ip_plus_two(p_jit, index);
      index = jit_emit_php(p_jit, index);
      index = jit_emit_jmp_indirect(p_jit, index, 0xfe, 0xff);
      break;
    case k_ora:
      // ORA
      index = jit_emit_calc_op(p_jit, index, opmode, operand1, operand2, 0x0a);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_asl:
      // ASL
      index = jit_emit_shift_op(p_jit, index, opmode, operand1, operand2, 0xe0);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      break;
    case k_php:
      // PHP
      index = jit_emit_php(p_jit, index);
      break;
    case k_bpl:
      // BPL
      index = jit_emit_test_negative(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      break;
    case k_clc:
      // CLC
      index = jit_emit_set_carry(p_jit, index, 0);
      break;
    case k_jsr:
      // JSR
      index = jit_emit_push_ip_plus_two(p_jit, index);
      index = jit_emit_jmp_op1_op2(p_jit, index, operand1, operand2);
      break;
    case k_bit:
      // BIT
      // Only has zp and abs
      // mov dl [rdi + op1,op2?]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x97;
      index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
      index = jit_emit_scratch_bit_test(p_jit, index, 7);
      index = jit_emit_carry_to_6502_negative(p_jit, index);
      index = jit_emit_scratch_bit_test(p_jit, index, 6);
      index = jit_emit_carry_to_6502_overflow(p_jit, index);
      // and dl, al
      p_jit[index++] = 0x20;
      p_jit[index++] = 0xc2;
      index = jit_emit_intel_to_6502_zero(p_jit, index);
      break;
    case k_and:
      // AND
      index = jit_emit_calc_op(p_jit, index, opmode, operand1, operand2, 0x22);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_rol:
      // ROL
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      index = jit_emit_shift_op(p_jit, index, opmode, operand1, operand2, 0xd0);
      index = jit_emit_post_rotate(p_jit, index, opmode, operand1, operand2);
      break;
    case k_plp:
      // PLP
      index = jit_emit_pull_to_scratch(p_jit, index);

      index = jit_emit_scratch_bit_test(p_jit, index, 0);
      index = jit_emit_intel_to_6502_carry(p_jit, index);
      index = jit_emit_scratch_bit_test(p_jit, index, 1);
      index = jit_emit_carry_to_6502_zero(p_jit, index);
      index = jit_emit_scratch_bit_test(p_jit, index, 6);
      index = jit_emit_carry_to_6502_overflow(p_jit, index);
      index = jit_emit_scratch_bit_test(p_jit, index, 7);
      index = jit_emit_carry_to_6502_negative(p_jit, index);
      // mov r8b, dl
      p_jit[index++] = 0x41;
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xd0;
      // and r8b, 0x3c
      p_jit[index++] = 0x41;
      p_jit[index++] = 0x80;
      p_jit[index++] = 0xe0;
      p_jit[index++] = 0x3c;
      break;
    case k_bmi:
      // BMI
      index = jit_emit_test_negative(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      break;
    case k_sec:
      // SEC
      index = jit_emit_set_carry(p_jit, index, 1);
      break;
    case k_eor:
      // EOR
      index = jit_emit_calc_op(p_jit, index, opmode, operand1, operand2, 0x32);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_lsr:
      // LSR
      index = jit_emit_shift_op(p_jit, index, opmode, operand1, operand2, 0xe8);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      break;
    case k_pha:
      // PHA
      index = jit_emit_push_from_a(p_jit, index);
      break;
    case k_jmp:
      // JMP
      if (opmode == k_abs) {
        index = jit_emit_jmp_op1_op2(p_jit, index, operand1, operand2);
      } else {
        index = jit_emit_jmp_indirect(p_jit, index, operand1, operand2);
      }
      break;
    case k_bvc:
      // BVC
      index = jit_emit_test_overflow(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      break;
    case k_cli:
      // CLI
      // btr r8, 2
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xf0;
      p_jit[index++] = 0x02;
      break;
    case k_rts:
      // RTS
      index = jit_emit_pull_to_scratch_word(p_jit, index);
      // inc dx
      p_jit[index++] = 0x66;
      p_jit[index++] = 0xff;
      p_jit[index++] = 0xc2;
      index = jit_emit_jit_bytes_shift_scratch_left(p_jit, index);
      // lea rdx, [rdi + rdx + k_addr_space_size + k_guard_size]
      p_jit[index++] = 0x48;
      p_jit[index++] = 0x8d;
      p_jit[index++] = 0x94;
      p_jit[index++] = 0x17;
      index = jit_emit_int(p_jit, index, k_addr_space_size + k_guard_size);
      index = jit_emit_jmp_scratch(p_jit, index);
      break;
    case k_adc:
      // ADC
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      index = jit_emit_calc_op(p_jit, index, opmode, operand1, operand2, 0x12);
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      break;
    case k_ror:
      // ROR
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      index = jit_emit_shift_op(p_jit, index, opmode, operand1, operand2, 0xd8);
      index = jit_emit_post_rotate(p_jit, index, opmode, operand1, operand2);
      break;
    case k_pla:
      // PLA
      index = jit_emit_pull_to_a(p_jit, index);
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      break;
    case k_bvs:
      // BVS
      index = jit_emit_test_overflow(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      break;
    case k_sei:
      // SEI
      // bts r8, 2
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xe8;
      p_jit[index++] = 0x02;
      break;
    case k_sta:
      // STA
      switch (opmode) {
      case k_zpg:
      case k_abs:
        // mov [rdi + op1,op2?], al
        p_jit[index++] = 0x88;
        p_jit[index++] = 0x87;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default:
        // mov [rdi + rdx], al
        p_jit[index++] = 0x88;
        p_jit[index++] = 0x04;
        p_jit[index++] = 0x17;
        break;
      }
      break;
    case k_sty:
      // STY
      switch (opmode) {
      case k_zpg:
      case k_abs:
        // mov [rdi + op1,op2?], cl
        p_jit[index++] = 0x88;
        p_jit[index++] = 0x8f;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default:
        // mov [rdi + rdx], cl
        p_jit[index++] = 0x88;
        p_jit[index++] = 0x0c;
        p_jit[index++] = 0x17;
        break;
      }
      break;
    case k_stx:
      // STX
      switch (opmode) {
      case k_zpg:
      case k_abs:
        // mov [rdi + op1,op2?], bl
        p_jit[index++] = 0x88;
        p_jit[index++] = 0x9f;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default:
        // mov [rdi + rdx], bl
        p_jit[index++] = 0x88;
        p_jit[index++] = 0x1c;
        p_jit[index++] = 0x17;
        break;
      }
      break;
    case k_dey:
      // DEY
      // dec cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc9;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_txa:
      // TXA
      // mov al, bl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xd8;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      break;
    case k_bcc:
      // BCC
      index = jit_emit_test_carry(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      break;
    case k_tya:
      // TYA
      // mov al, cl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc8;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      break;
    case k_txs:
      // TXS
      // mov sil, bl
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xde;
      break;
    case k_ldy:
      // LDY
      switch (opmode) {
      case k_imm:
        // mov cl, op1
        p_jit[index++] = 0xb1;
        p_jit[index++] = operand1;
        break;
      case k_zpg:
      case k_abs:
        // mov cl, [rdi + op1,op2?]
        p_jit[index++] = 0x8a;
        p_jit[index++] = 0x8f;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default:
        // mov cl, [rdi + rdx]
        p_jit[index++] = 0x8a;
        p_jit[index++] = 0x0c;
        p_jit[index++] = 0x17;
        break;
      }
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      break;
    case k_ldx:
      // LDX
      switch (opmode) {
      case k_imm:
        // mov bl, op1
        p_jit[index++] = 0xb3;
        p_jit[index++] = operand1;
        break;
      case k_zpg:
      case k_abs:
        // mov bl, [rdi + op1,op2?]
        p_jit[index++] = 0x8a;
        p_jit[index++] = 0x9f;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default:
        // mov bl, [rdi + rdx]
        p_jit[index++] = 0x8a;
        p_jit[index++] = 0x1c;
        p_jit[index++] = 0x17;
        break;
      }
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      break;
    case k_lda:
      // LDA
      switch (opmode) {
      case k_imm:
        // mov al, op1
        p_jit[index++] = 0xb0;
        p_jit[index++] = operand1;
        break;
      case k_zpg:
      case k_abs:
        // mov al, [rdi + op1,op2?]
        p_jit[index++] = 0x8a;
        p_jit[index++] = 0x87;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default:
        // mov al, [rdi + rdx]
        p_jit[index++] = 0x8a;
        p_jit[index++] = 0x04;
        p_jit[index++] = 0x17;
        break;
      }
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      break;
    case k_tay:
      // TAY
      // mov cl, al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc1;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      break;
    case k_tax:
      // TAX
      // mov bl, al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc3;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      break;
    case k_bcs:
      // BCS
      index = jit_emit_test_carry(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      break;
    case k_clv:
      // CLV
      // mov r12b, 0
      p_jit[index++] = 0x41;
      p_jit[index++] = 0xb4;
      p_jit[index++] = 0x00;
      break;
    case k_tsx:
      // TSX
      // mov bl, sil
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xf3;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      break;
    case k_cpy:
      // CPY
      switch (opmode) {
      case k_imm:
        // cmp cl, op1
        p_jit[index++] = 0x80;
        p_jit[index++] = 0xf9;
        p_jit[index++] = operand1;
        break;
      case k_zpg:
      case k_abs:
        // cmp cl, [rdi + op1,op2?]
        p_jit[index++] = 0x3a;
        p_jit[index++] = 0x8f;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      }
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      break;
    case k_cmp:
      // CMP
      index = jit_emit_calc_op(p_jit, index, opmode, operand1, operand2, 0x3a);
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      break;
    case k_dec:
      // DEC
      switch (opmode) {
      case k_zpg:
      case k_abs:
        // dec BYTE PTR [rdi + op1,op2?]
        p_jit[index++] = 0xfe;
        p_jit[index++] = 0x8f;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default: 
        // dec BYTE PTR [rdi + rdx]
        p_jit[index++] = 0xfe;
        p_jit[index++] = 0x0c;
        p_jit[index++] = 0x17;
        break;
      }
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_iny:
      // INY
      // inc cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_dex:
      // DEX
      // dec bl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xcb;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_bne:
      // BNE
      index = jit_emit_test_zero(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      break;
    case k_cld:
      // CLD
      // btr r8, 3
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xf0;
      p_jit[index++] = 0x03;
      break;
    case k_cpx:
      // CPX
      switch (opmode) {
      case k_imm:
        // cmp bl, op1
        p_jit[index++] = 0x80;
        p_jit[index++] = 0xfb;
        p_jit[index++] = operand1;
        break;
      case k_zpg:
      case k_abs:
        // cmp bl, [rdi + op1,op2?]
        p_jit[index++] = 0x3a;
        p_jit[index++] = 0x9f;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      }
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      break;
    case k_inc:
      // INC
      switch (opmode) {
      case k_zpg:
      case k_abs:
        p_jit[index++] = 0xfe;
        p_jit[index++] = 0x87;
        index = jit_emit_op1_op2(p_jit, index, operand1, operand2);
        break;
      default: 
        // inc BYTE PTR [rdi + rdx]
        p_jit[index++] = 0xfe;
        p_jit[index++] = 0x04;
        p_jit[index++] = 0x17;
        break;
      }
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_inx:
      // INX
      // inc bl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc3;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      break;
    case k_sbc:
      // SBC
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // cmc
      p_jit[index++] = 0xf5;
      index = jit_emit_calc_op(p_jit, index, opmode, operand1, operand2, 0x1a);
      index = jit_emit_intel_to_6502_sub_znco(p_jit, index);
      break;
    case k_nop:
      // NOP
      break;
    case k_beq:
      // BEQ
      index = jit_emit_test_zero(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      break;
    default:
      index = jit_emit_undefined(p_jit, index, opcode, jit_offset);
      break;
    }

    index = jit_emit_do_jmp_next(p_jit, index, oplen);

    assert(index <= k_jit_bytes_per_byte);

    p_mem++;
    p_jit += k_jit_bytes_per_byte;
    jit_offset++;
  }
}

static void
print_opcode(char* buf,
             size_t buf_len,
             unsigned char opcode,
             unsigned char operand1,
             unsigned char operand2) {
  unsigned char opmode = g_opmodes[opcode];
  const char* opname = g_p_opnames[g_optypes[opcode]];
  switch (opmode) {
  case k_nil:
    snprintf(buf, buf_len, "%s", opname);
    break;
  case k_imm:
    snprintf(buf, buf_len, "%s #$%.2x", opname, operand1);
    break;
  case k_zpg:
    snprintf(buf, buf_len, "%s $%.2x", opname, operand1);
    break;
  case k_abs:
    snprintf(buf, buf_len, "%s $%.2x%.2x", opname, operand2, operand1);
    break;
  case k_zpx:
    snprintf(buf, buf_len, "%s $%.2x,X", opname, operand1);
    break;
  case k_zpy:
    snprintf(buf, buf_len, "%s $%.2x,Y", opname, operand1);
    break;
  case k_abx:
    snprintf(buf, buf_len, "%s $%.2x%.2x,X", opname, operand2, operand1);
    break;
  case k_aby:
    snprintf(buf, buf_len, "%s $%.2x%.2x,Y", opname, operand2, operand1);
    break;
  case k_idx:
    snprintf(buf, buf_len, "%s ($%.2x,X)", opname, operand1);
    break;
  case k_idy:
    snprintf(buf, buf_len, "%s ($%.2x),Y", opname, operand1);
    break;
  case k_ind:
    snprintf(buf, buf_len, "%s ($%.2x%.2x)", opname, operand2, operand1);
    break;
  default:
    snprintf(buf, buf_len, "%s: %.2x", opname, opcode);
    break;
  }
}

static void
jit_debug_get_addr(char* p_buf,
                   size_t buf_len,
                   unsigned char opcode,
                   unsigned char operand1,
                   unsigned char operand2,
                   unsigned char x_6502,
                   unsigned char y_6502,
                   unsigned char* p_mem) {
  unsigned char opmode = g_opmodes[opcode];
  uint16_t addr;
  switch (opmode) {
  case k_zpg:
    addr = operand1;
    break;
  case k_zpx:
    addr = (unsigned char) (operand1 + x_6502);
    break;
  case k_zpy:
    addr = (unsigned char) (operand1 + y_6502);
    break;
  case k_abs:
    addr = (uint16_t) (operand1 + (operand2 << 8));
    break;
  case k_abx:
    addr = (uint16_t) (operand1 + (operand2 << 8) + x_6502);
    break;
  case k_aby:
    addr = (uint16_t) (operand1 + (operand2 << 8) + y_6502);
    break;
  case k_idx:
    addr = p_mem[(unsigned char) (operand1 + x_6502 + 1)];
    addr <<= 8;
    addr |= p_mem[(unsigned char) (operand1 + x_6502)];
    break;
  case k_idy:
    addr = p_mem[(unsigned char) (operand1 + 1)];
    addr <<= 8;
    addr |= p_mem[operand1];
    addr = (uint16_t) (addr + y_6502);
    break;
  default:
    return;
    break;
  }
  snprintf(p_buf, buf_len, "[addr=%.4x val=%.2x]", addr, p_mem[addr]);
}

static void
jit_debug_callback() {
  char opcode_buf[k_max_opcode_len];
  char extra_buf[k_max_extra_len];
  unsigned char** p_jit_debug_space = (unsigned char**) &g_jit_debug_space;
  unsigned char* p_mem = p_jit_debug_space[1];
  uint16_t ip_6502 = (size_t) p_jit_debug_space[2];
  unsigned char a_6502 = (size_t) p_jit_debug_space[3];
  unsigned char x_6502 = (size_t) p_jit_debug_space[4];
  unsigned char y_6502 = (size_t) p_jit_debug_space[5];
  unsigned char s_6502 = (size_t) p_jit_debug_space[6];
  unsigned char opcode = p_mem[ip_6502];
  unsigned char operand1 = p_mem[((ip_6502 + 1) & 0xffff)];
  unsigned char operand2 = p_mem[((ip_6502 + 2) & 0xffff)];
  extra_buf[0] = '\0';
  jit_debug_get_addr(extra_buf,
                     sizeof(extra_buf),
                     opcode,
                     operand1,
                     operand2,
                     x_6502,
                     y_6502,
                     p_mem);

  print_opcode(opcode_buf, sizeof(opcode_buf), opcode, operand1, operand2);
  printf("%.4x: %-16s [A=%.2x X=%.2x Y=%.2x S=%.2x] %s\n",
         ip_6502,
         opcode_buf,
         a_6502,
         x_6502,
         y_6502,
         s_6502,
         extra_buf);
  fflush(stdout);
}

void
jit_enter(unsigned char* p_mem, size_t vector_addr) {
  // The memory must be aligned to at least 0x100 so that our stack access
  // trick works.
  assert(((size_t) p_mem & 0xff) == 0);

  unsigned char addr_lsb = p_mem[vector_addr];
  unsigned char addr_msb = p_mem[vector_addr + 1];
  unsigned int addr = (addr_msb << 8) | addr_lsb;
  unsigned char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  unsigned char* p_entry = p_jit + (addr * k_jit_bytes_per_byte);
  unsigned char** p_jit_debug_space = (unsigned char**) &g_jit_debug_space;
  p_jit_debug_space[0] = (unsigned char*) jit_debug_callback;
  p_jit_debug_space[1] = p_mem;

  asm volatile (
    // al is 6502 A.
    "xor %%eax, %%eax;"
    // bl is 6502 X.
    "xor %%ebx, %%ebx;"
    // cl is 6502 Y.
    "xor %%ecx, %%ecx;"
    // rdx is scratch.
    "xor %%edx, %%edx;"
    // r8 is the rest of the 6502 flags or'ed together.
    // Bit 2 is interrupt disable.
    // Bit 3 is decimal mode.
    // Bit 4 is set for BRK and PHP.
    // Bit 5 is always set.
    "xor %%r8, %%r8;"
    "bts $4, %%r8;"
    "bts $5, %%r8;"
    // r9 is carry flag.
    "xor %%r9, %%r9;"
    // r10 is zero flag.
    "xor %%r10, %%r10;"
    // r11 is negative flag.
    "xor %%r11, %%r11;"
    // r12 is overflow flag.
    "xor %%r12, %%r12;"
    // rdi points to the virtual RAM, guard page, JIT space.
    "mov %1, %%rdi;"
    // sil is 6502 S.
    // rsi is a pointer to the real (aligned) backing memory.
    "lea 0x100(%%rdi), %%rsi;"
    // Pass a pointer to a debug area in r14.
    "mov %2, %%r14;"
    // Use scratch register for jump location.
    "mov %0, %%rdx;"
    "call *%%rdx;"
    :
    : "g" (p_entry), "g" (p_mem), "g" (p_jit_debug_space)
    : "rax", "rbx", "rcx", "rdx", "rdi", "rsi",
      "r8", "r9", "r10", "r11", "r12", "r14", "r15"
  );
}
