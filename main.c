#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

static const size_t k_addr_space_size = 0x10000;
static const size_t k_guard_size = 4096;
static const size_t k_os_rom_offset = 0xc000;
static const size_t k_os_rom_len = 0x4000;
static const int k_jit_bytes_per_byte = 64;
static const size_t k_vector_reset = 0xfffc;

static void
jit_init(char* p_mem, size_t jit_stride, size_t num_bytes) {
  char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  // nop
  memset(p_jit, '\x90', num_bytes * jit_stride);
  while (num_bytes--) {
    // ud2
    p_jit[0] = 0x0f;
    p_jit[1] = 0x0b;
    p_jit += jit_stride;
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
                                   size_t jit_stride,
                                   size_t index,
                                   size_t oplen) {
  assert(index + 2 <= jit_stride);
  size_t offset = (jit_stride * oplen) - (index + 2);
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
                                        size_t jit_stride,
                                        size_t index,
                                        unsigned char intel_opcode,
                                        unsigned char unsigned_jump_size) {
  char jump_size = (char) unsigned_jump_size;
  ssize_t offset = (jit_stride * (jump_size + 2)) - (index + 2);
  if (offset <= 0x7f && offset >= -0x80) {
    // Fits in a 1-byte offset.
    assert(index + 2 <= jit_stride);
    p_jit[index++] = intel_opcode;
    p_jit[index++] = (unsigned char) offset;
  } else {
    unsigned int uint_offset = (unsigned int) offset;
    offset -= 4;
    assert(index + 6 <= jit_stride);
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
    // test bh, bh
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xff;
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

static size_t jit_emit_intel_to_6502_znco(char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_znc(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t jit_emit_6502_carry_to_intel(char* p_jit, size_t index) {
  // cmp r9b, 1 
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x80;
  p_jit[index++] = 0xf9;
  p_jit[index++] = 0x01;

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

static size_t jit_emit_a_to_scratch(char* p_jit, size_t index) {
  // mov sil, al
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x88;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t jit_emit_x_to_scratch(char* p_jit, size_t index) {
  // mov sil, bl
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x88;
  p_jit[index++] = 0xde;

  return index;
}

static size_t jit_emit_y_to_scratch(char* p_jit, size_t index) {
  // mov si, bx
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xde;
  // shr si, 8
  p_jit[index++] = 0x66;
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xee;
  p_jit[index++] = 0x08;

  return index;
}

static size_t jit_emit_abs_x_to_scratch(char* p_jit,
                                        size_t index,
                                        unsigned char operand1,
                                        unsigned char operand2) {
  // mov esi, ebx
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xde;
  // and si, 0xff
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x81;
  p_jit[index++] = 0xe6;
  p_jit[index++] = 0xff;
  p_jit[index++] = 0x00;
  // add si, op1,op2
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x81;
  p_jit[index++] = 0xc6;
  p_jit[index++] = operand1;
  p_jit[index++] = operand2;

  return index;
}

static size_t jit_emit_abs_y_to_scratch(char* p_jit,
                                        size_t index,
                                        unsigned char operand1,
                                        unsigned char operand2) {
  // mov esi, ebx
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xde;
  // shr esi, 8
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xee;
  p_jit[index++] = 0x08;
  // add si, op1,op2
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x81;
  p_jit[index++] = 0xc6;
  p_jit[index++] = operand1;
  p_jit[index++] = operand2;

  return index;
}

static size_t jit_emit_ind_y_to_scratch(char* p_jit,
                                        size_t index,
                                        unsigned char operand1) {
  unsigned char operand1_inc = operand1 + 1;
  // movzx esi, BYTE PTR [rdi + op2]
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xb7;
  p_jit[index++] = operand1_inc;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  // shl esi, 8
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe6;
  p_jit[index++] = 0x08;
  // mov sil, BYTE PTR [rdi + op1]
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0xb7;
  p_jit[index++] = operand1;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  p_jit[index++] = 0;
  // mov r15, rbx
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xdf;
  // shr r15, 8
  p_jit[index++] = 0x49;
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xef;
  p_jit[index++] = 0x08;
  // add si, r15w
  p_jit[index++] = 0x66;
  p_jit[index++] = 0x44;
  p_jit[index++] = 0x01;
  p_jit[index++] = 0xfe;

  return index;
}

static size_t jit_emit_bit_common(char* p_jit, size_t index) {
  // bt esi, 7
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe6;
  p_jit[index++] = 0x07;
  index = jit_emit_carry_to_6502_negative(p_jit, index);
  // bt esi, 6
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe6;
  p_jit[index++] = 0x06;
  index = jit_emit_carry_to_6502_overflow(p_jit, index);
  // and sil, al
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x20;
  p_jit[index++] = 0xc6;
  index = jit_emit_intel_to_6502_zero(p_jit, index);

  return index;
}

static size_t jit_emit_jmp_scratch(char* p_jit, size_t index) {
  // jmp rsi
  p_jit[index++] = 0xff;
  p_jit[index++] = 0xe6;

  return index;
}

static size_t jit_emit_jmp_op1_op2(char* p_jit,
                                   size_t jit_stride,
                                   size_t index,
                                   unsigned char operand1,
                                   unsigned char operand2) {
  // lea rsi, [rdi + k_addr_space_size + k_guard_size +
  //               op1,op2 * jit_stride]
  p_jit[index++] = 0x48;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0xb7;
  index = jit_emit_int(p_jit,
                       index,
                       k_addr_space_size + k_guard_size +
                           ((operand1 + (operand2 << 8)) * jit_stride));
  index = jit_emit_jmp_scratch(p_jit, index);

  return index;
}

static void
jit_jit(char* p_mem,
        size_t jit_stride,
        size_t jit_offset,
        size_t jit_len) {
  char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  size_t jit_end = jit_offset + jit_len;
  p_mem += jit_offset;
  p_jit += (jit_offset * jit_stride);
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
    switch (opcode) {
    case 0x05:
      // ORA zp
      // or al, [rdi + op1]
      p_jit[index++] = 0x0a;
      p_jit[index++] = 0x87;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x06:
      // ASL zp
      // shl BYTE PTR [rdi + op1]
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xa7;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x08:
      // PHP
      // mov rsi, r8
      p_jit[index++] = 0x4c;
      p_jit[index++] = 0x89;
      p_jit[index++] = 0xc6;
      // or rsi, r9
      p_jit[index++] = 0x4c;
      p_jit[index++] = 0x09;
      p_jit[index++] = 0xce;
      // mov r15, rdx
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x89;
      p_jit[index++] = 0xd7;
      // and r15, 1
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x83;
      p_jit[index++] = 0xe7;
      p_jit[index++] = 0x01;
      // shl r15, 1
      p_jit[index++] = 0x49;
      p_jit[index++] = 0xd1;
      p_jit[index++] = 0xe7;
      // or rsi, r15
      p_jit[index++] = 0x4c;
      p_jit[index++] = 0x09;
      p_jit[index++] = 0xfe;
      // mov r15, rdx
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x89;
      p_jit[index++] = 0xd7;
      // shr r15, 8
      p_jit[index++] = 0x49;
      p_jit[index++] = 0xc1;
      p_jit[index++] = 0xef;
      p_jit[index++] = 0x08;
      // shl r15, 7
      p_jit[index++] = 0x49;
      p_jit[index++] = 0xc1;
      p_jit[index++] = 0xe7;
      p_jit[index++] = 0x07;
      // or rsi, r15
      p_jit[index++] = 0x4c;
      p_jit[index++] = 0x09;
      p_jit[index++] = 0xfe;

      // mov [rdi + rcx], sil
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x34;
      p_jit[index++] = 0x0f;
      // dec cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc9;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x09:
      // ORA #imm
      // or al, op1
      p_jit[index++] = 0x0c;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x0a:
      // ASL A
      // shl al, 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xe0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x10:
      // BPL
      index = jit_emit_test_negative(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit,
                                        jit_stride,
                                        index,
                                        0x74,
                                        operand1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x18:
      // CLC
      index = jit_emit_set_carry(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x1d:
      // ORA abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // or al, [rdi + rsi]
      p_jit[index++] = 0x0a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x20:
      // JSR
      // push rax
      p_jit[index++] = 0x50;
      // lea rax, [rip - (k_addr_space_size + k_guard_size)]
      p_jit[index++] = 0x48;
      p_jit[index++] = 0x8d;
      p_jit[index++] = 0x05;
      index = jit_emit_int(p_jit,
                           index,
                           -(ssize_t) (k_addr_space_size + k_guard_size));
      // sub rax, rdi
      p_jit[index++] = 0x48;
      p_jit[index++] = 0x29;
      p_jit[index++] = 0xf8;
      // shr eax, 6 (must be 2^6 == jit_stride)
      p_jit[index++] = 0xc1;
      p_jit[index++] = 0xe8;
      p_jit[index++] = 0x06;
      // add eax, 2
      p_jit[index++] = 0x83;
      p_jit[index++] = 0xc0;
      p_jit[index++] = 0x02;
      // mov [rdi + rcx], ah
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x24;
      p_jit[index++] = 0x0f;
      // dec cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc9;
      // mov [rdi + rcx], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x0f;
      // dec cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc9;
      // pop rax
      p_jit[index++] = 0x58;
      index = jit_emit_jmp_op1_op2(p_jit,
                                   jit_stride,
                                   index,
                                   operand1,
                                   operand2);
      break;
    case 0x24:
      // BIT zp
      // mov sil [rdi + op1]
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0xb7;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_bit_common(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x25:
      // AND zp
      // and al, [rdi + op1]
      p_jit[index++] = 0x22;
      p_jit[index++] = 0x87;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x26:
      // ROL zp
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcl [rdi + op1], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x97;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x28:
      // PLP
      // inc cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc1;
      // mov r8b, [rdi + rcx]
      p_jit[index++] = 0x44;
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x0f;

      // bt r8, 0
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xe0;
      p_jit[index++] = 0x00;
      index = jit_emit_intel_to_6502_carry(p_jit, index);
      // bt r8, 1
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xe0;
      p_jit[index++] = 0x01;
      index = jit_emit_carry_to_6502_zero(p_jit, index);
      // bt r8, 6
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xe0;
      p_jit[index++] = 0x06;
      index = jit_emit_carry_to_6502_overflow(p_jit, index);
      // bt r8, 7
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xe0;
      p_jit[index++] = 0x06;
      index = jit_emit_carry_to_6502_negative(p_jit, index);
      // and r8b, 0x3c
      p_jit[index++] = 0x41;
      p_jit[index++] = 0x80;
      p_jit[index++] = 0xe0;
      p_jit[index++] = 0x3c;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x29:
      // AND #imm
      // and al, op1
      p_jit[index++] = 0x24;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x2c:
      // BIT abs
      // mov sil [rdi + op1,op2]
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0xb7;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      index = jit_emit_bit_common(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x30:
      // BMI
      index = jit_emit_test_negative(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit,
                                        jit_stride,
                                        index,
                                        0x75,
                                        operand1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x38:
      // SEC
      index = jit_emit_set_carry(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x48:
      // PHA
      // mov [rdi + rcx], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x0f;
      // dec cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc9;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x49:
      // EOR #imm
      // xor al, op1
      p_jit[index++] = 0x34;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x4a:
      // LSR A
      // shr al, 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xe8;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x4c:
      // JMP
      index = jit_emit_jmp_op1_op2(p_jit,
                                   jit_stride,
                                   index,
                                   operand1,
                                   operand2);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x50:
      // BVC
      index = jit_emit_test_overflow(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit,
                                        jit_stride,
                                        index,
                                        0x74,
                                        operand1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x58:
      // CLI
      // btr r8, 2
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xf0;
      p_jit[index++] = 0x02;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x60:
      // RTS
      // push rax
      p_jit[index++] = 0x50;
      // inc cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc1;
      // mov al, [rdi + rcx]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x0f;
      // inc cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc1;
      // mov ah, [rdi + rcx]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x24;
      p_jit[index++] = 0x0f;
      // inc ax
      p_jit[index++] = 0x66;
      p_jit[index++] = 0xff;
      p_jit[index++] = 0xc0;
      // shl eax, 6
      p_jit[index++] = 0xc1;
      p_jit[index++] = 0xe0;
      p_jit[index++] = 0x06;
      // lea rsi, [rax + rdi + k_addr_space_size + k_guard_size]
      p_jit[index++] = 0x48;
      p_jit[index++] = 0x8d;
      p_jit[index++] = 0xb4;
      p_jit[index++] = 0x38;
      index = jit_emit_int(p_jit, index, k_addr_space_size + k_guard_size);
      // pop rax
      p_jit[index++] = 0x58;
      index = jit_emit_jmp_scratch(p_jit, index);
      break;
    case 0x66:
      // ROR zp
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcr [rdi + op1], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x9f;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x68:
      // PLA
      // inc cl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc1;
      // mov al, [rdi + rcx]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x0f;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x69:
      // ADC imm
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, op1
      p_jit[index++] = 0x14;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x6a:
      // ROR A
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcr al, 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0xd8;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x6c:
      // JMP indirect
      // movzx esi, BYTE PTR [rdi + op1,op2,1]
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xb6;
      p_jit[index++] = 0xb7;
      p_jit[index++] = operand1_inc;
      p_jit[index++] = operand2_inc;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      // shl esi, 8
      p_jit[index++] = 0xc1;
      p_jit[index++] = 0xe6;
      p_jit[index++] = 0x08;
      // mov sil, BYTE PTR [rdi + op1,op2]
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0xb7;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      // shl esi, 6
      p_jit[index++] = 0xc1;
      p_jit[index++] = 0xe6;
      p_jit[index++] = 0x06;
      // lea rsi, [rdi + rsi + k_addr_space_size + k_guard_size]
      p_jit[index++] = 0x48;
      p_jit[index++] = 0x8d;
      p_jit[index++] = 0xb4;
      p_jit[index++] = 0x37;
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x78:
      // SEI
      // bts r8, 2
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xe8;
      p_jit[index++] = 0x02;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x79:
      // ADC abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, [rdi + rsi]
      p_jit[index++] = 0x12;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x7d:
      // ADC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // adc al, [rdi + rsi]
      p_jit[index++] = 0x12;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x7e:
      // ROR abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // rcr BYTE PTR [rdi + rsi], 1
      p_jit[index++] = 0xd0;
      p_jit[index++] = 0x1c;
      p_jit[index++] = 0x37;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x88:
      // DEY
      // dec bh
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xcf;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x8a:
      // TXA
      // mov al, bl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xd8;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x8c:
      // STY abs
      // mov [rdi + op1,op2], bh
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xbf;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x90:
      // BCC
      index = jit_emit_test_carry(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit,
                                        jit_stride,
                                        index,
                                        0x74,
                                        operand1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x91:
      // STA (indirect), Y
      index = jit_emit_ind_y_to_scratch(p_jit, index, operand1);
      // mov [rdi + rsi], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x95:
      // STA zp, X
      // mov esi, ebx
      p_jit[index++] = 0x89;
      p_jit[index++] = 0xde;
      // add si, op1
      p_jit[index++] = 0x66;
      p_jit[index++] = 0x81;
      p_jit[index++] = 0xc6;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      // and si, 0xff
      p_jit[index++] = 0x66;
      p_jit[index++] = 0x81;
      p_jit[index++] = 0xe6;
      p_jit[index++] = 0xff;
      p_jit[index++] = 0x00;
      // mov [rdi + rsi], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0x98:
      // TYA
      // mov al, bh
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xf8;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x99:
      // STA abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      // mov [rdi + rsi], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0x9a:
      // TXS
      // mov cl, bl
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xd9;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0x9d:
      // STA abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // mov [rdi + rsi], al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xa0:
      // LDY #imm
      // mov bh, op1
      p_jit[index++] = 0xb7;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xa2:
      // LDX #imm
      // mov bl, op1
      p_jit[index++] = 0xb3;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xa4:
      // LDY zp
      // mov bh, [rdi + op1]
      // TODO: can be optimized to 1-byte offset for 0-0x7f.
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0xbf;
      p_jit[index++] = operand1;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      p_jit[index++] = 0x00;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xa8:
      // TAY
      // mov bh, al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc7;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0xa9:
      // LDA #imm
      // mov al, op1
      p_jit[index++] = 0xb0;
      p_jit[index++] = operand1;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xaa:
      // TAX
      // mov bl, al
      p_jit[index++] = 0x88;
      p_jit[index++] = 0xc3;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0xac:
      // LDY abs
      // mov bh, [rdi + op1,op2]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0xbf;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xb0:
      // BCS
      index = jit_emit_test_carry(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit,
                                        jit_stride,
                                        index,
                                        0x75,
                                        operand1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xb9:
      // LDA abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      // mov al, [rdi + rsi]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xbc:
      // LDY abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // mov bh, [rdi + rsi]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x3c;
      p_jit[index++] = 0x37;
      index = jit_emit_do_zn_flags(p_jit, index, 2);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xbd:
      // LDA abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // mov al, [rdi + rsi]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      index = jit_emit_do_zn_flags(p_jit, index, 0);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xbe:
      // LDX abs, Y
      index = jit_emit_abs_y_to_scratch(p_jit, index, operand1, operand2);
      // mov bl, [rdi + rsi]
      p_jit[index++] = 0x8a;
      p_jit[index++] = 0x1c;
      p_jit[index++] = 0x37;
      index = jit_emit_do_zn_flags(p_jit, index, 1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xc0:
      // CPY #imm
      index = jit_emit_y_to_scratch(p_jit, index);
      // sub sil, op1
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x80;
      p_jit[index++] = 0xee;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xc6:
      // DEC zp
      // dec BYTE PTR [rdi + op1]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x8f;
      index = jit_emit_int(p_jit, index, operand1);
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xc8:
      // INY
      // inc bh
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc7;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0xc9:
      // CMP #imm
      index = jit_emit_a_to_scratch(p_jit, index);
      // sub sil, op1
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x80;
      p_jit[index++] = 0xee;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xca:
      // DEX
      // dec bl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xcb;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0xcd:
      // CMP abs
      index = jit_emit_a_to_scratch(p_jit, index);
      // sub sil, [rdi + op1,op2]
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x2a;
      p_jit[index++] = 0xb7;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xd0:
      // BNE
      index = jit_emit_test_zero(p_jit, index);
      // je
      index = jit_emit_do_relative_jump(p_jit,
                                        jit_stride,
                                        index,
                                        0x74,
                                        operand1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xd8:
      // CLD
      // btr r8, 3
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x0f;
      p_jit[index++] = 0xba;
      p_jit[index++] = 0xf0;
      p_jit[index++] = 0x03;
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0xdd:
      // CMP abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // mov r15, rax
      p_jit[index++] = 0x49;
      p_jit[index++] = 0x89;
      p_jit[index++] = 0xc7;
      // sub r15b, [rdi + rsi]
      p_jit[index++] = 0x44;
      p_jit[index++] = 0x2a;
      p_jit[index++] = 0x3c;
      p_jit[index++] = 0x37;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xde:
      // DEC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // dec BYTE PTR [rdi + rsi]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x0c;
      p_jit[index++] = 0x37;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xe0:
      // CPX #imm
      index = jit_emit_x_to_scratch(p_jit, index);
      // sub sil, op1
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x80;
      p_jit[index++] = 0xee;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xe8:
      // INX
      // inc bl
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0xc3;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 1);
      break;
    case 0xe9:
      // SBC imm
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // sbb al, op1
      p_jit[index++] = 0x1c;
      p_jit[index++] = operand1;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xec:
      // CPX abs
      index = jit_emit_x_to_scratch(p_jit, index);
      // sub sil, [rdi + op1,op2]
      p_jit[index++] = 0x40;
      p_jit[index++] = 0x2a;
      p_jit[index++] = 0xb7;
      p_jit[index++] = operand1;
      p_jit[index++] = operand2;
      p_jit[index++] = 0;
      p_jit[index++] = 0;
      index = jit_emit_intel_to_6502_znc(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xf0:
      // BEQ
      index = jit_emit_test_zero(p_jit, index);
      // jne
      index = jit_emit_do_relative_jump(p_jit,
                                        jit_stride,
                                        index,
                                        0x75,
                                        operand1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 2);
      break;
    case 0xfd:
      // SBC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      index = jit_emit_6502_carry_to_intel(p_jit, index);
      // sbb al, [rdi + rsi]
      p_jit[index++] = 0x1a;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      index = jit_emit_intel_to_6502_znco(p_jit, index);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
      break;
    case 0xfe:
      // INC abs, X
      index = jit_emit_abs_x_to_scratch(p_jit, index, operand1, operand2);
      // inc BYTE PTR [rdi + rsi]
      p_jit[index++] = 0xfe;
      p_jit[index++] = 0x04;
      p_jit[index++] = 0x37;
      index = jit_emit_do_zn_flags(p_jit, index, -1);
      jit_emit_do_jmp_next(p_jit, jit_stride, index, 3);
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
    p_jit += jit_stride;
    jit_offset++;
  }
}

static void
jit_enter(const char* p_mem,
          size_t jit_stride,
          size_t vector_addr) {
  unsigned char addr_lsb = p_mem[vector_addr];
  unsigned char addr_msb = p_mem[vector_addr + 1];
  unsigned int addr = (addr_msb << 8) | addr_lsb;
  const char* p_jit = p_mem + k_addr_space_size + k_guard_size;
  const char* p_entry = p_jit + (addr * jit_stride);

  asm volatile (
    // al is 6502 A.
    "xor %%eax, %%eax;"
    // bl is 6502 X.
    // bh is 6502 Y.
    "xor %%ebx, %%ebx;"
    // cl is 6502 S.
    // ch is 0x01 so that cx is 0x1xx, an offset from virtual RAM base.
    "mov $0x00000100, %%ecx;"
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
    // Use rsi as a scratch register for jump location.
    "mov %0, %%rsi;"
    "call *%%rsi;"
    :
    : "r" (p_entry), "r" (p_mem)
    : "rax", "rbx", "rcx", "rdx", "rdi", "rsi",
      "r8", "r9", "r10", "r11", "r12", "r15"
  );
}

int
main(int argc, const char* argv[]) {
  char* p_map;
  char* p_mem;
  int fd;
  ssize_t read_ret;

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

  fd = open("os12.rom", O_RDONLY);
  if (fd < 0) {
    errx(1, "can't load rom");
  }
  read_ret = read(fd, p_mem + k_os_rom_offset, k_os_rom_len);
  if (read_ret != k_os_rom_len) {
    errx(1, "can't read rom");
  }
  close(fd);

  jit_init(p_mem, k_jit_bytes_per_byte, k_addr_space_size);
  jit_jit(p_mem, k_jit_bytes_per_byte, k_os_rom_offset, k_os_rom_len);
  jit_enter(p_mem, k_jit_bytes_per_byte, k_vector_reset);

  return 0;
}
