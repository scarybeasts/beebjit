#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

static const size_t k_addr_space_size = 0x10000;
static const size_t k_guard_size = 4096;
static const size_t k_os_rom_offset = 0xc000;
static const size_t k_os_rom_len = 0x4000;
static const size_t k_lang_rom_offset = 0x8000;
static const size_t k_lang_rom_len = 0x4000;
static const size_t k_registers_offset = 0xfc00;
static const size_t k_registers_len = 0x300;
static const int k_jit_bytes_per_byte = 128;
static const int k_jit_bytes_shift = 7;
static const size_t k_vector_reset = 0xfffc;
static const size_t k_max_opcode_len = 16;

static char g_jit_debug_space[64];
static const unsigned int k_jit_debug = 1;

static const char* g_p_opcodes[256] =
{
  // 0x00
  "BRK", "ORA", "!!!", "???", "???", "ORA", "ASL", "???",
  "PHP", "ORA", "ASL", "???", "???", "ORA", "ASL", "???",
  // 0x10
  "BPL", "ORA", "!!!", "???", "???", "ORA", "ASL", "???",
  "CLC", "ORA", "???", "???", "???", "ORA", "ASL", "???",
  // 0x20
  "JSR", "AND", "!!!", "???", "BIT", "AND", "ROL", "???",
  "PLP", "AND", "ROL", "???", "BIT", "AND", "ROL", "???",
  // 0x30
  "BMI", "AND", "!!!", "???", "???", "AND", "ROL", "???",
  "SEC", "AND", "???", "???", "???", "AND", "ROL", "???",
  // 0x40
  "RTI", "EOR", "!!!", "???", "???", "EOR", "LSR", "???",
  "PHA", "EOR", "LSR", "???", "JMP", "EOR", "LSR", "???",
  // 0x50
  "BVC", "EOR", "!!!", "???", "???", "EOR", "LSR", "???",
  "CLI", "EOR", "???", "???", "???", "EOR", "LSR", "???",
  // 0x60
  "RTS", "ADC", "!!!", "???", "???", "ADC", "ROR", "???",
  "PLA", "ADC", "ROR", "???", "JMP", "ADC", "ROR", "???",
  // 0x70
  "BVS", "ADC", "!!!", "???", "???", "ADC", "ROR", "???",
  "SEI", "ADC", "???", "???", "???", "ADC", "ROR", "???",
  // 0x80
  "???", "STA", "???", "???", "STY", "STA", "STX", "???",
  "DEY", "???", "TXA", "???", "STY", "STA", "STX", "???",
  // 0x90
  "BCC", "STA", "!!!", "???", "STY", "STA", "STX", "???",
  "TYA", "STA", "TXS", "???", "???", "STA", "???", "???",
  // 0xa0
  "LDY", "LDA", "LDX", "???", "LDY", "LDA", "LDX", "???",
  "TAY", "LDA", "TAX", "???", "LDY", "LDA", "LDX", "???",
  // 0xb0
  "BCS", "LDA", "!!!", "???", "LDY", "LDA", "LDX", "???",
  "CLV", "LDA", "TSX", "???", "LDY", "LDA", "LDX", "???",
  // 0xc0
  "CPY", "CMP", "???", "???", "CPY", "CMP", "DEC", "???",
  "INY", "CMP", "DEX", "???", "CPY", "CMP", "DEC", "???",
  // 0xd0
  "BNE", "CMP", "!!!", "???", "???", "CMP", "DEC", "???",
  "CLD", "CMP", "???", "???", "???", "CMP", "DEC", "???",
  // 0xe0
  "CPX", "SBC", "???", "???", "CPX", "SBC", "INC", "???",
  "INX", "SBC", "NOP", "???", "CPX", "SBC", "INC", "???",
  // 0xf0
  "BEQ", "SBC", "!!!", "???", "???", "SBC", "INC", "???",
  "SED", "SBC", "???", "???", "???", "SBC", "INC", "???",
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

static unsigned char g_optypes[256] =
{
  // 0x00
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0x10
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0x20
  k_abs, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x30
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0x40
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0x50
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0x60
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_ind, k_abs, k_abs, 0    ,
  // 0x70
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0x80
  0    , k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, 0    , k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x90
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0xa0
  k_imm, k_idx, k_imm, 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0xb0
  k_imm, k_idy, 0    ,     0, k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil,     0, k_abx, k_abx, k_aby, 0    ,
  // 0xc0
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0xd0
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0xe0
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0xf0
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static void
mem_init(char* p_mem) {
  memset(p_mem, '\0', k_addr_space_size);
}

static void
jit_init(char* p_mem) {
  char* p_jit = p_mem + k_addr_space_size + k_guard_size;
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

static size_t jit_emit_int(char* p_jit, size_t index, ssize_t offset) {
  p_jit[index++] = offset & 0xff;
  offset >>= 8;
  p_jit[index++] = offset & 0xff;
  offset >>= 8;
  p_jit[index++] = offset & 0xff;
  offset >>= 8;
  p_jit[index++] = offset & 0xff;

  return index;
}

static size_t jit_emit_do_jmp_next(char* p_jit,
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

static size_t jit_emit_do_relative_jump(char* p_jit,
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
    unsigned int uint_offset = (unsigned int) offset;
    offset -= 4;
    assert(index + 6 <= k_jit_bytes_per_byte);
    p_jit[index++] = 0x0f;
    p_jit[index++] = intel_opcode + 0x10;
    index = jit_emit_int(p_jit, index, offset);
  }

  return index;
}

static size_t jit_emit_intel_to_6502_zero(char* p_jit, size_t index) {
  // sete r10b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x94;
  p_jit[index++] = 0xc2;

  return index;
}

static size_t jit_emit_intel_to_6502_negative(char* p_jit, size_t index) {
  // sets r11b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x98;
  p_jit[index++] = 0xc3;

  return index;
}

static size_t jit_emit_intel_to_6502_carry(char* p_jit, size_t index) {
  // setb r9b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc1;

  return index;
}

static size_t jit_emit_intel_to_6502_sub_carry(char* p_jit, size_t index) {
  // setae r9b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x93;
  p_jit[index++] = 0xc1;

  return index;
}

static size_t jit_emit_intel_to_6502_overflow(char* p_jit, size_t index) {
  // seto r12b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x90;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t jit_emit_carry_to_6502_zero(char* p_jit, size_t index) {
  // setb r10b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc2;

  return index;
}

static size_t jit_emit_carry_to_6502_negative(char* p_jit, size_t index) {
  // setb r11b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc3;

  return index;
}

static size_t jit_emit_carry_to_6502_overflow(char* p_jit, size_t index) {
  // setb r12b
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t jit_emit_do_zn_flags(char* p_jit, size_t index, int reg) {
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

static size_t jit_emit_intel_to_6502_znc(char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);
  index = jit_emit_intel_to_6502_carry(p_jit, index);

  return index;
}

static size_t jit_emit_intel_to_6502_sub_znc(char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);
  index = jit_emit_intel_to_6502_sub_carry(p_jit, index);

  return index;
}

static size_t jit_emit_intel_to_6502_znco(char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_znc(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t jit_emit_intel_to_6502_sub_znco(char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t jit_emit_6502_carry_to_intel(char* p_jit, size_t index) {
  // Note: doesn't just check carry value but also trashes it.
  // shr r9b, 1
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xd0;
  p_jit[index++] = 0xe9;

  return index;
}

static size_t jit_emit_set_carry(char* p_jit, size_t index, unsigned char val) {
  // mov r9b, val
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xb1;
  p_jit[index++] = val;

  return index;
}

static size_t jit_emit_test_carry(char* p_jit, size_t index) {
  // test r9b, r9b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xc9;

  return index;
}

static size_t jit_emit_test_zero(char* p_jit, size_t index) {
  // test r10b, r10b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xd2;

  return index;
}

static size_t jit_emit_test_negative(char* p_jit, size_t index) {
  // test r11b, r11b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xdb;

  return index;
}

static size_t jit_emit_test_overflow(char* p_jit, size_t index) {
  // test r12b, r12b
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xe4;

  return index;
}

static size_t jit_emit_abs_x_to_scratch(char* p_jit,
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

static size_t jit_emit_abs_y_to_scratch(char* p_jit,
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

static size_t jit_emit_ind_y_to_scratch(char* p_jit,
                                        size_t index,
                                        unsigned char operand1) {
  unsigned char operand1_inc = operand1 + 1;
  // movzx edx, BYTE PTR [rdi + op1]
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0x97;
  p_jit[index++] = operand1;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  // mov dh, BYTE PTR [rdi + op1 + 1]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0xb7;
  p_jit[index++] = operand1_inc;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  // add dx, cx
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x01;
  p_jit[index++] = 0xca;

  return index;
}

static size_t jit_emit_ind_x_to_scratch(char* p_jit,
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

size_t jit_emit_zp_x_to_scratch(char* p_jit,
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

static size_t jit_emit_scratch_bit_test(char* p_jit,
                                        size_t index,
                                        unsigned char bit) {
  // bt edx, bit
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe2;
  p_jit[index++] = bit;

  return index;
}

static size_t jit_emit_bit_common(char* p_jit, size_t index) {
  index = jit_emit_scratch_bit_test(p_jit, index, 7);
  index = jit_emit_carry_to_6502_negative(p_jit, index);
  index = jit_emit_scratch_bit_test(p_jit, index, 6);
  index = jit_emit_carry_to_6502_overflow(p_jit, index);
  // and dl, al
  p_jit[index++] = 0x20;
  p_jit[index++] = 0xc2;
  index = jit_emit_intel_to_6502_zero(p_jit, index);

  return index;
}

static size_t jit_emit_jmp_scratch(char* p_jit, size_t index) {
  // jmp rdx
  p_jit[index++] = 0xff;
  p_jit[index++] = 0xe2;

  return index;
}

static size_t jit_emit_jmp_op1_op2(char* p_jit,
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

static size_t jit_emit_jit_bytes_shift_scratch_left(char* p_jit, size_t index) {
  // shl edx, k_jit_bytes_shift
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe2;
  p_jit[index++] = k_jit_bytes_shift;

  return index;
}

static size_t jit_emit_jit_bytes_shift_scratch_right(char* p_jit,
                                                     size_t index) {
  // shr edx, k_jit_bytes_shift
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xea;
  p_jit[index++] = k_jit_bytes_shift;

  return index;
}

static size_t jit_emit_lda_scratch_offset(char* p_jit, size_t index) {
  // mov al, [rdi + rdx]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x04;
  p_jit[index++] = 0x17;

  return index;
}

static size_t jit_emit_cmp_scratch_offset(char* p_jit, size_t index) {
  // cmp al, [rdi + rdx]
  p_jit[index++] = 0x3a;
  p_jit[index++] = 0x04;
  p_jit[index++] = 0x17;

  return index;
}

static size_t jit_emit_sta_scratch_offset(char* p_jit, size_t index) {
  // mov [rdi + rdx], al
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x04;
  p_jit[index++] = 0x17;

  return index;
}

static size_t jit_emit_stack_inc(char* p_jit, size_t index) {
  // inc sil
  p_jit[index++] = 0x40;
  p_jit[index++] = 0xfe;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t jit_emit_stack_dec(char* p_jit, size_t index) {
  // dec sil
  p_jit[index++] = 0x40;
  p_jit[index++] = 0xfe;
  p_jit[index++] = 0xce;

  return index;
}

static size_t jit_emit_pull_to_a(char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  // mov al, [rsi]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x06;

  return index;
}

static size_t jit_emit_pull_to_scratch(char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  // mov dl, [rsi]
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x16;

  return index;
}

static size_t jit_emit_pull_to_scratch_word(char* p_jit, size_t index) {
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

static size_t jit_emit_push_from_a(char* p_jit, size_t index) {
  // mov [rsi], al
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x06;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t jit_emit_push_from_scratch(char* p_jit, size_t index) {
  // mov [rsi], dl
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t jit_emit_push_from_scratch_word(char* p_jit, size_t index) {
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

static size_t jit_emit_6502_ip_to_scratch(char* p_jit, size_t index) {
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

static void
jit_jit(char* p_mem,
        size_t jit_offset,
        size_t jit_len,
        unsigned int flags) {
  char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  size_t jit_end = jit_offset + jit_len;
  p_mem += jit_offset;
  p_jit += (jit_offset * k_jit_bytes_per_byte);
  while (jit_offset < jit_end) {
    unsigned char opcode = p_mem[0];
    unsigned char operand1 = 0;
    unsigned char operand2 = 0;
    unsigned char operand1_inc;
    unsigned char operand2_inc;
    size_t index = 0;
    if (jit_offset + 1 < jit_end) {
      operand1 = p_mem[1];
    }
    if (jit_offset + 2 < jit_end) {
      operand2 = p_mem[2];
    }
    operand1_inc = operand1 + 1;
    operand2_inc = operand2;
    if (operand1_inc == 0) {
      operand2_inc++;
    }

    if (flags & k_jit_debug) {
      index = jit_emit_6502_ip_to_scratch(p_jit, index);
      // mov [r14 + 16], rdx
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x89;
      p_jit[index++] = 0x56;
      p_jit[index++] = 0x10;
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
    }

    switch (opcode) {
    case 0x02:
      // Illegal opcode. Hangs a standard 6502.
      // Bounce out of JIT.
      // ret
      p_jit[index++] = 0xc3;
      break;
    case 0x05:
      // ORA zp
      // or al, [rdi + op1]
      p_jit[index++] = 0x0a;
      p_jit[index++] = 0x87;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x06:
      // ASL zp
      // shl BYTE PTR [rdi + op1]
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xa7;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x08:
      // PHP
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
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x09:
      // ORA #imm
      // or al, op1
      p_jit[index++] = 0x0c;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x0a:
      // ASL A
      // shl al, 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xe0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x0d:
      // ORA abs
      // or al, [rdi + op1,op2]
      p_jit[index++] = 0x0a;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x0e:
      // ASL abs
      // shl BYTE PTR [rdi + op1,op2], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xa7;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x10:
      // BPL
      index = jit_emit_test_negative(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x12:
      // Illegal opcode. Hangs a standard 6502.
      // Generate a debug trap and continue.
      // int 3
      p_jit[index++] = 0xcc;
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x18:
      // CLC
      index = jit_emit_set_carry(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x1d:
      // ORA abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // or al, [rdi + rdx]
      p_jit[index++] = 0x0a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x17;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x20:
      // JSR
      index = jit_emit_6502_ip_to_scratch(p_jit, index);
      // add edx, 2
      p_jit[index++] = 0x83;
      p_jit[index++] = 0xc2;
      p_jit[index++] = 0x02;
      index = jit_emit_push_from_scratch_word(p_jit, index);
      index = jit_emit_jmp_op1_op2(p_jit, index, operand1, operand2);
      break;
    case 0x24:
      // BIT zp
      // mov dl [rdi + op1]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x97;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_bit_common(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x25:
      // AND zp
      // and al, [rdi + op1]
      p_jit[index++] = 0x22;
      p_jit[index++] = 0x87;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x26:
      // ROL zp
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcl [rdi + op1], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x97;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x28:
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
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x29:
      // AND #imm
      // and al, op1
      p_jit[index++] = 0x24;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x2a:
      // ROL A
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcl al, 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xd0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x2c:
      // BIT abs
      // mov dl [rdi + op1,op2]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x97;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      index = jit_emit_bit_common(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x2d:
      // AND abs
      // and al, [rdi + op1,op2]
      p_jit[index++] = 0x22;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x2e:
      // ROL abs
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcl BYTE PTR [rdi + op1,op2], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x97;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x30:
      // BMI
      index = jit_emit_test_negative(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x38:
      // SEC
      index = jit_emit_set_carry(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x45:
      // EOR zp
      // xor al, [rdi + op1]
      p_jit[index++] = 0x32;
      p_jit[index++] = 0x87;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x46:
      // LSR zp
      // shr BYTE PTR [rdi + op1], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xaf;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x48:
      // PHA
      index = jit_emit_push_from_a(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x49:
      // EOR #imm
      // xor al, op1
      p_jit[index++] = 0x34;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x4a:
      // LSR A
      // shr al, 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xe8;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x4c:
      // JMP
      index = jit_emit_jmp_op1_op2(p_jit, index, operand1, operand2);
      break;
    case 0x4d:
      // EOR abs
      // xor al, [rdi + op1,op2]
      p_jit[index++] = 0x32;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x4e:
      // LSR abs
      // shr BYTE PTR [rdi + op1,op2], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xaf;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x50:
      // BVC
      index = jit_emit_test_overflow(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x58:
      // CLI
      // btr r8, 2
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xf0;
      p_jit[index++] = 0x02;
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x60:
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
    case 0x65:
      // ADC zp
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, [rdi + op1]
      p_jit[index++] = 0x12;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x66:
      // ROR zp
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcr BYTE PTR [rdi + op1], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x9f;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x68:
      // PLA
      index = jit_emit_pull_to_a(p_jit, index);
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x69:
      // ADC imm
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, op1
      p_jit[index++] = 0x14;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x6a:
      // ROR A
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcr al, 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xd8;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x6c:
      // JMP indirect
      // movzx edx, BYTE PTR [rdi + op1,op2]
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xb6;
      p_jit[index++] = 0x97;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      // mov dh, BYTE PTR [rdi + op1,op2 + 1]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0xb7;
      p_jit[index++] = operand1_inc;
      p_jit[index++] = operand2_inc;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_jit_bytes_shift_scratch_left(p_jit, index);
      // lea rdx, [rdi + rdx + k_addr_space_size + k_guard_size]
      p_jit[index++] = 0x48;
      p_jit[index++] = 0x8d;
      p_jit[index++] = 0x94;
      p_jit[index++] = 0x17;
      index = jit_emit_int(p_jit, index, k_addr_space_size + k_guard_size);
      index = jit_emit_jmp_scratch(p_jit, index);
      break;
    case 0x6d:
      // ADC abs
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, [rdi + op1,op2]
      p_jit[index++] = 0x12;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x6e:
      // ROR abs
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcr BYTE PTR [rdi + op1,op2], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x9f;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x70:
      // BVS
      index = jit_emit_test_overflow(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x78:
      // SEI
      // bts r8, 2
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xe8;
      p_jit[index++] = 0x02;
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x79:
      // ADC abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, [rdi + rdx]
      p_jit[index++] = 0x12;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x17;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x7d:
      // ADC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, [rdi + rdx]
      p_jit[index++] = 0x12;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x17;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x7e:
      // ROR abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcr BYTE PTR [rdi + rdx], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x1c;
      p_jit[index++] = 0x17;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x81:
      // STA (indirect, X)
      index = jit_emit_ind_x_to_scratch(p_jit, index, operand1);
      index = jit_emit_sta_scratch_offset(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x84:
      // STY zp
      // mov [rdi + op1], bh
      // TODO: can be optimized to 1-byte offset for 0-0x7f.
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x8f;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x85:
      // STA zp
      // mov [rdi + op1], al
      // TODO: can be optimized to 1-byte offset for 0-0x7f.
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x86:
      // STX zp
      // mov [rdi + op1], bl
      // TODO: can be optimized to 1-byte offset for 0-0x7f.
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x9f;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x88:
      // DEY
      // dec cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc9;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x8a:
      // TXA
      // mov al, bl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xd8;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x8d:
      // STA abs
      // mov [rdi + op1,op2], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x8c:
      // STY abs
      // mov [rdi + op1,op2], cl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x8f;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x8e:
      // STX abs
      // mov [rdi + op1,op2], bl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x9f;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x90:
      // BCC
      index = jit_emit_test_carry(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x91:
      // STA (indirect), Y
      index = jit_emit_ind_y_to_scratch(p_jit, index, operand1);
      index = jit_emit_sta_scratch_offset(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x95:
      // STA zp, X
      index = jit_emit_zp_x_to_scratch(p_jit, index, operand1);
      // mov [rdi + rdx], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x17;
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0x98:
      // TYA
      // mov al, cl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc8;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x99:
      // STA abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_sta_scratch_offset(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0x9a:
      // TXS
      // mov sil, bl
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xde;
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0x9d:
      // STA abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_sta_scratch_offset(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xa0:
      // LDY #imm
      // mov cl, op1
      p_jit[index++] = 0xb1;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xa2:
      // LDX #imm
      // mov bl, op1
      p_jit[index++] = 0xb3;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xa4:
      // LDY zp
      // mov cl, [rdi + op1]
      // TODO: can be optimized to 1-byte offset for 0-0x7f.
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x8f;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xa5:
      // LDA zp
      // mov al, [rdi + op1]
      // TODO: can be optimized to 1-byte offset for 0-0x7f.
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xa6:
      // LDX zp
      // mov bl, [rdi + op1]
      // TODO: can be optimized to 1-byte offset for 0-0x7f.
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x9f;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xa8:
      // TAY
      // mov cl, al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc1;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xa9:
      // LDA #imm
      // mov al, op1
      p_jit[index++] = 0xb0;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xaa:
      // TAX
      // mov bl, al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc3;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xac:
      // LDY abs
      // mov cl, [rdi + op1,op2]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x8f;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xad:
      // LDA abs
      // mov al, [rdi + op1,op2]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xae:
      // LDX abs
      // mov bl, [rdi + op1,op2]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x9f;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xb0:
      // BCS
      index = jit_emit_test_carry(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xb1:
      // LDA (indirect), Y
      index = jit_emit_ind_y_to_scratch(p_jit, index, operand1);
      index = jit_emit_lda_scratch_offset(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xb8:
      // CLV
      // mov r12b, 0
      p_jit[index++] = 0x41;
      p_jit[index++] = 0xb4;
      p_jit[index++] = 0x00;
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xb9:
      // LDA abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_lda_scratch_offset(p_jit, index);
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xba:
      // TSX
      // mov bl, sil
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xf3;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xbc:
      // LDY abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // mov cl, [rdi + rdx]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x0c;
      p_jit[index++] = 0x17;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xbd:
      // LDA abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_lda_scratch_offset(p_jit, index);
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xbe:
      // LDX abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      // mov bl, [rdi + rdx]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x1c;
      p_jit[index++] = 0x17;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xc0:
      // CPY #imm
      // cmp cl, op1
      p_jit[index++] = 0x80;
      p_jit[index++] = 0xf9;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xc5:
      // CMP zp
      // cmp al, [rdi + op1]
      p_jit[index++] = 0x3a;
      p_jit[index++] = 0x87;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xc6:
      // DEC zp
      // dec BYTE PTR [rdi + op1]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x8f;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xc8:
      // INY
      // inc cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xc9:
      // CMP #imm
      // cmp al, op1
      p_jit[index++] = 0x3c;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xca:
      // DEX
      // dec bl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xcb;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xcd:
      // CMP abs
      // cmp al, [rdi + op1,op2]
      p_jit[index++] = 0x3a;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xce:
      // DEC abs
      // dec BYTE PTR [rdi + op1,op2]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x8f;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xd0:
      // BNE
      index = jit_emit_test_zero(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit, index, 0x74, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xd1:
      // CMP (indirect), Y
      index = jit_emit_ind_y_to_scratch(p_jit, index, operand1);
      index = jit_emit_cmp_scratch_offset(p_jit, index);
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xd8:
      // CLD
      // btr r8, 3
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xf0;
      p_jit[index++] = 0x03;
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xd9:
      // CMP abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_cmp_scratch_offset(p_jit, index);
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xdd:
      // CMP abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_cmp_scratch_offset(p_jit, index);
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xde:
      // DEC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // dec BYTE PTR [rdi + rdx]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x0c;
      p_jit[index++] = 0x17;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xe0:
      // CPX #imm
      // cmp bl, op1
      p_jit[index++] = 0x80;
      p_jit[index++] = 0xfb;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xe6:
      // INC zp
      // inc BYTE PTR [rdi + op1]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x87;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xe8:
      // INX
      // inc bl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc3;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 1);
      break;
    case 0xe9:
      // SBC imm
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // cmc
      p_jit[index++] = 0xf5;
      // sbb al, op1
      p_jit[index++] = 0x1c;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_sub_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xec:
      // CPX abs
      // cmp bl, [rdi + op1,op2]
      p_jit[index++] = 0x3a;
      p_jit[index++] = 0x9f;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xee:
      // INC abs
      // inc BYTE PTR [rdi + op1,op2]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x87;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xf0:
      // BEQ
      index = jit_emit_test_zero(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit, index, 0x75, operand1);
      jit_emit_do_jmp_next(p_jit, index, 2);
      break;
    case 0xf2:
      // Illegal opcode. Hangs a standard 6502.
      // Generate a SEGV.
      // xor rdx, rdx
      p_jit[index++] = 0x31;
      p_jit[index++] = 0xd2;
      index = jit_emit_jmp_scratch(p_jit, index);
      break;
    case 0xfd:
      // SBC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // cmc
      p_jit[index++] = 0xf5;
      // sbb al, [rdi + rdx]
      p_jit[index++] = 0x1a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x17;
      index = jit_emit_intel_to_6502_sub_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    case 0xfe:
      // INC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // inc BYTE PTR [rdi + rdx]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x17;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, index, 3);
      break;
    default:
      // ud2
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0x0b;
      // Copy of unimplemented 6502 opcode.
      p_jit[index++] = opcode;
      // Virtual address of opcode, big endian.
      p_jit[index++] = jit_offset >> 8;
      p_jit[index++] = jit_offset & 0xff;
      break;
    }

    assert(index <= k_jit_bytes_per_byte);

    p_mem++;
    p_jit += k_jit_bytes_per_byte;
    jit_offset++;
  }
}

static void
print_opcode(char* buf,
             unsigned char opcode,
             unsigned char operand1,
             unsigned char operand2) {
  unsigned char optype = g_optypes[opcode];
  const char* opname = g_p_opcodes[opcode];
  switch (optype) {
  case k_nil:
    snprintf(buf, k_max_opcode_len, "%s", opname);
    break;
  case k_imm:
    snprintf(buf, k_max_opcode_len, "%s #$%.2x", opname, operand1);
    break;
  case k_zpg:
    snprintf(buf, k_max_opcode_len, "%s $%.2x", opname, operand1);
    break;
  case k_abs:
    snprintf(buf, k_max_opcode_len, "%s $%.2x%.2x", opname, operand2, operand1);
    break;
  case k_zpx:
    snprintf(buf, k_max_opcode_len, "%s $%.2x,X", opname, operand1);
    break;
  case k_zpy:
    snprintf(buf, k_max_opcode_len, "%s $%.2x,Y", opname, operand1);
    break;
  case k_abx:
    snprintf(buf,
             k_max_opcode_len,
             "%s $%.2x%.2x,X",
             opname,
             operand2,
             operand1);
    break;
  case k_aby:
    snprintf(buf,
             k_max_opcode_len,
             "%s $%.2x%.2x,Y",
             opname,
             operand2,
             operand1);
    break;
  case k_idx:
    snprintf(buf, k_max_opcode_len, "%s ($%.2x,X)", opname, operand1);
    break;
  case k_idy:
    snprintf(buf, k_max_opcode_len, "%s ($%.2x),Y", opname, operand1);
    break;
  case k_ind:
    snprintf(buf,
             k_max_opcode_len,
             "%s ($%.2x%.2x)",
             opname,
             operand2,
             operand1);
    break;
  default:
    snprintf(buf, k_max_opcode_len, "%s: %.2x", opname, opcode);
    break;
  }
}

static void
jit_debug_callback() {
  char opcode_buf[k_max_opcode_len];
  char** p_jit_debug_space = (char**) &g_jit_debug_space;
  char* p_mem = p_jit_debug_space[1];
  size_t ip_6502 = (size_t) p_jit_debug_space[2];

  unsigned char opcode = p_mem[ip_6502];
  unsigned char operand1 = p_mem[((ip_6502 + 1) & 0xffff)];
  unsigned char operand2 = p_mem[((ip_6502 + 2) & 0xffff)];
  print_opcode(opcode_buf, opcode, operand1, operand2);
  printf("%zx: %s\n", ip_6502, opcode_buf);
}

static void
jit_enter(char* p_mem, size_t vector_addr) {
  // The memory must be aligned to at least 0x100 so that our stack access
  // trick works.
  assert(((size_t) p_mem & 0xff) == 0);

  unsigned char addr_lsb = p_mem[vector_addr];
  unsigned char addr_msb = p_mem[vector_addr + 1];
  unsigned int addr = (addr_msb << 8) | addr_lsb;
  char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  char* p_entry = p_jit + (addr * k_jit_bytes_per_byte);
  char** p_jit_debug_space = (char**) &g_jit_debug_space;
  p_jit_debug_space[0] = (char*) jit_debug_callback;
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

int
main(int argc, const char* argv[]) {
  char* p_map;
  char* p_mem;
  int fd;
  ssize_t read_ret;
  const char* os_rom_name = "os12.rom";

  if (argc > 1) {
    os_rom_name = argv[1];
  }

  p_map = mmap(NULL,
               (k_addr_space_size * (k_jit_bytes_per_byte + 1)) +
                   (k_guard_size * 3),
               PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS,
               -1,
               0);
  p_mem = p_map + k_guard_size;

  mprotect(p_map,
           k_guard_size,
           PROT_NONE);
  mprotect(p_mem + k_addr_space_size,
           k_guard_size,
           PROT_NONE);
  mprotect(p_mem + (k_addr_space_size * (k_jit_bytes_per_byte + 1)) +
               k_guard_size,
           k_guard_size,
           PROT_NONE);

  mprotect(p_mem + k_addr_space_size + k_guard_size,
           k_addr_space_size * k_jit_bytes_per_byte,
           PROT_READ | PROT_WRITE | PROT_EXEC);

  p_mem = p_map + k_guard_size;

  mem_init(p_mem);

  fd = open(os_rom_name, O_RDONLY);
  if (fd < 0) {
    errx(1, "can't load OS rom");
  }
  read_ret = read(fd, p_mem + k_os_rom_offset, k_os_rom_len);
  if (read_ret != k_os_rom_len) {
    errx(1, "can't read OS rom");
  }
  close(fd);

  fd = open("basic.rom", O_RDONLY);
  if (fd < 0) {
    errx(1, "can't load language rom");
  }
  read_ret = read(fd, p_mem + k_lang_rom_offset, k_lang_rom_len);
  if (read_ret != k_lang_rom_len) {
    errx(1, "can't read lanuage rom");
  }
  close(fd);

  memset(p_mem + k_registers_offset, '\0', k_registers_len);

  jit_init(p_mem);
  jit_jit(p_mem, k_os_rom_offset, k_os_rom_len, k_jit_debug);
  jit_jit(p_mem, k_lang_rom_offset, k_lang_rom_len, k_jit_debug);
  jit_enter(p_mem, k_vector_reset);

  return 0;
}
