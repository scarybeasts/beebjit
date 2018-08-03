#include "jit.h"

#include "bbc.h"
#include "debug.h"
#include "opdefs.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_addr_space_size = 0x10000;
static const size_t k_guard_size = 4096;
static const int k_jit_bytes_per_byte = 256;
static const int k_jit_bytes_shift = 8;
static void* k_jit_addr = (void*) 0x20000000;
static void* k_utils_addr = (void*) 0x80000000;
static const size_t k_utils_size = 4096;
static const size_t k_utils_debug_offset = 0;

static const int k_offset_util_debug = 24;
static const int k_offset_debug = 32;
static const int k_offset_debug_callback = 40;
static const int k_offset_bbc = 48;
static const int k_offset_read_callback = 56;
static const int k_offset_write_callback = 64;
static const int k_offset_interrupt = 72;

struct jit_struct {
  unsigned char* p_mem;        /* 0  */
  unsigned char* p_jit_base;   /* 8  */
  unsigned char* p_utils_base; /* 16 */
  unsigned char* p_util_debug; /* 24 */
  void* p_debug;               /* 32 */
  void* p_debug_callback;      /* 40 */
  struct bbc_struct* p_bbc;    /* 48 */
  void* p_read_callback;       /* 56 */
  void* p_write_callback;      /* 64 */
  uint64_t interrupt;          /* 72 */
};

static size_t
jit_emit_int(unsigned char* p_jit_buf, size_t index, ssize_t offset) {
  assert(offset >= INT_MIN);
  assert(offset <= INT_MAX);
  p_jit_buf[index++] = offset & 0xff;
  offset >>= 8;
  p_jit_buf[index++] = offset & 0xff;
  offset >>= 8;
  p_jit_buf[index++] = offset & 0xff;
  offset >>= 8;
  p_jit_buf[index++] = offset & 0xff;

  return index;
}

static size_t
jit_emit_intel_to_6502_zero(unsigned char* p_jit, size_t index) {
  /* sete r13b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x94;
  p_jit[index++] = 0xc5;

  return index;
}

static size_t
jit_emit_intel_to_6502_negative(unsigned char* p_jit, size_t index) {
  /* sets r14b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x98;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_intel_to_6502_carry(unsigned char* p_jit, size_t index) {
  /* setb r12b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t
jit_emit_intel_to_6502_sub_carry(unsigned char* p_jit, size_t index) {
  /* setae r12b */

  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x93;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t
jit_emit_intel_to_6502_overflow(unsigned char* p_jit, size_t index) {
  /* seto r15b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x90;
  p_jit[index++] = 0xc7;

  return index;
}

static size_t
jit_emit_carry_to_6502_zero(unsigned char* p_jit, size_t index) {
  /* setb r13b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc5;

  return index;
}

static size_t
jit_emit_carry_to_6502_negative(unsigned char* p_jit, size_t index) {
  /* setb r14b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_carry_to_6502_overflow(unsigned char* p_jit, size_t index) {
  /* setb r15b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc7;

  return index;
}

static size_t
jit_emit_do_zn_flags(unsigned char* p_jit, size_t index, int reg) {
  assert(index + 8 <= k_jit_bytes_per_byte);
  if (reg == -1) {
    /* Nothing -- flags already set. */
  } else if (reg == 0) {
    /* test al, al */
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xc0;
  } else if (reg == 1) {
    /* test bl, bl */
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xdb;
  } else if (reg == 2) {
    /* test cl, cl */
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xc9;
  }

  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);

  return index;
}

static size_t
jit_emit_intel_to_6502_znc(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);
  index = jit_emit_intel_to_6502_carry(p_jit, index);

  return index;
}

static size_t
jit_emit_intel_to_6502_sub_znc(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_zero(p_jit, index);
  index = jit_emit_intel_to_6502_negative(p_jit, index);
  index = jit_emit_intel_to_6502_sub_carry(p_jit, index);

  return index;
}

static size_t
jit_emit_intel_to_6502_znco(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_znc(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t
jit_emit_intel_to_6502_sub_znco(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_sub_znc(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t
jit_emit_6502_carry_to_intel(unsigned char* p_jit, size_t index) {
  /* Note: doesn't just check carry value but also trashes it. */
  /* shr r12b, 1 */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xd0;
  p_jit[index++] = 0xec;

  return index;
}

static size_t
jit_emit_set_carry(unsigned char* p_jit, size_t index, unsigned char val) {
  /* mov r12b, val */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xb4;
  p_jit[index++] = val;

  return index;
}

static size_t
jit_emit_test_carry(unsigned char* p_jit, size_t index) {
  /* test r12b, r12b */
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xe4;

  return index;
}

static size_t
jit_emit_test_zero(unsigned char* p_jit, size_t index) {
  /* test r13b, r13b */
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xed;

  return index;
}

static size_t
jit_emit_test_negative(unsigned char* p_jit, size_t index) {
  /* test r14b, r14b */
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xf6;

  return index;
}

static size_t
jit_emit_test_overflow(unsigned char* p_jit, size_t index) {
  /* test r15b, r15b */
  p_jit[index++] = 0x45;
  p_jit[index++] = 0x84;
  p_jit[index++] = 0xff;

  return index;
}

static size_t
jit_emit_abs_x_to_scratch(unsigned char* p_jit,
                          size_t index,
                          unsigned char operand1,
                          unsigned char operand2) {
  /* Empty -- optimized for now but wrap-around will cause a fault. */
  return index;
}

static size_t
jit_emit_abs_y_to_scratch(unsigned char* p_jit,
                          size_t index,
                          unsigned char operand1,
                          unsigned char operand2) {
  /* Empty -- optimized for now but wrap-around will cause a fault. */
  return index;
}

static size_t
jit_emit_ind_y_to_scratch(struct jit_struct* p_jit,
                          unsigned char* p_jit_buf,
                          size_t index,
                          unsigned char operand1) {
  if (operand1 == 0xff) {
    /* movzx edx, BYTE PTR [p_mem + 0xff] */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0x14;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + 0xff);
    /* mov dh, BYTE PTR [p_mem] */
    p_jit_buf[index++] = 0x8a;
    p_jit_buf[index++] = 0x34;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
  } else {
    /* movzx edx, WORD PTR [p_mem + op1] */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb7;
    p_jit_buf[index++] = 0x14;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + operand1);
  }

  return index;
}

static size_t
jit_emit_ind_x_to_scratch(struct jit_struct* p_jit,
                          unsigned char* p_jit_buf,
                          size_t index,
                          unsigned char operand1) {
  /* mov rdi, rbx */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xdf;
  /* add dil, operand1 */
  p_jit_buf[index++] = 0x40;
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xc7;
  p_jit_buf[index++] = operand1;
  /* movzx edx, BYTE PTR [rdi] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb6;
  p_jit_buf[index++] = 0x17;
  /* inc dil */
  p_jit_buf[index++] = 0x40;
  p_jit_buf[index++] = 0xfe;
  p_jit_buf[index++] = 0xc7;
  /* mov dh, BYTE PTR [rdi] */
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x37;

  return index;
}

static size_t
jit_emit_zp_x_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* movzx edx, bl */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd3;
  /* add dl, op1 */
  p_jit[index++] = 0x80;
  p_jit[index++] = 0xc2;
  p_jit[index++] = operand1;

  return index;
}

static size_t
jit_emit_zp_y_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* movzx edx, cl */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd1;
  /* add dl, op1 */
  p_jit[index++] = 0x80;
  p_jit[index++] = 0xc2;
  p_jit[index++] = operand1;

  return index;
}

static size_t
jit_emit_scratch_bit_test(unsigned char* p_jit,
                          size_t index,
                          unsigned char bit) {
  /* bt edx, bit */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe2;
  p_jit[index++] = bit;

  return index;
}

static size_t
jit_emit_jmp_scratch(unsigned char* p_jit, size_t index) {
  /* jmp rdx */
  p_jit[index++] = 0xff;
  p_jit[index++] = 0xe2;

  return index;
}

static size_t
jit_emit_jmp_6502_addr(struct jit_struct* p_jit,
                       struct util_buffer* p_buf,
                       uint16_t new_addr_6502,
                       unsigned char opcode1,
                       unsigned char opcode2) {
  unsigned char* p_src_addr = util_buffer_get_base_address(p_buf) +
                              util_buffer_get_pos(p_buf);
  unsigned char* p_dst_addr = p_jit->p_jit_base +
                              (new_addr_6502 * k_jit_bytes_per_byte);
  ssize_t offset = p_dst_addr - p_src_addr;
  unsigned char* p_jit_buf = util_buffer_get_ptr(p_buf);
  size_t index = util_buffer_get_pos(p_buf);
  /* TODO: emit short opcode sequence if jump is in range. */
  /* Intel opcode length (5 or 6) counts against jump delta. */
  offset -= 5;
  p_jit_buf[index++] = opcode1;
  if (opcode2 != 0) {
    p_jit_buf[index++] = opcode2;
    offset--;
  }
  index = jit_emit_int(p_jit_buf, index, offset);

  return index;
}

static size_t
jit_emit_jit_bytes_shift_scratch_left(unsigned char* p_jit, size_t index) {
  /* shl edx, k_jit_bytes_shift */
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe2;
  p_jit[index++] = k_jit_bytes_shift;

  return index;
}

static size_t
jit_emit_stack_inc(unsigned char* p_jit, size_t index) {
  /* inc sil */
  p_jit[index++] = 0x40;
  p_jit[index++] = 0xfe;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_stack_dec(unsigned char* p_jit, size_t index) {
  /* dec sil */
  p_jit[index++] = 0x40;
  p_jit[index++] = 0xfe;
  p_jit[index++] = 0xce;

  return index;
}

static size_t
jit_emit_pull_to_a(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  /* mov al, [rsi] */
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x06;

  return index;
}

static size_t
jit_emit_pull_to_scratch(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  /* mov dl, [rsi] */
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x16;

  return index;
}

static size_t
jit_emit_pull_to_scratch_word(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  /* movzx edx, BYTE PTR [rsi] */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_inc(p_jit, index);
  /* mov dh, BYTE PTR [rsi] */

  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x36;

  return index;
}

static size_t
jit_emit_push_from_a(unsigned char* p_jit, size_t index) {
  /* mov [rsi], al */
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x06;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t
jit_emit_push_constant(unsigned char* p_jit_buf,
                       size_t index,
                       unsigned char val) {
  /* mov BYTE PTR [rsi], val */
  p_jit_buf[index++] = 0xc6;
  p_jit_buf[index++] = 0x06;
  p_jit_buf[index++] = val;
  index = jit_emit_stack_dec(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_push_from_scratch(unsigned char* p_jit, size_t index) {
  /* mov [rsi], dl */
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t
jit_emit_push_addr(unsigned char* p_jit_buf, size_t index, uint16_t addr_6502) {
  index = jit_emit_push_constant(p_jit_buf, index, (addr_6502 >> 8));
  index = jit_emit_push_constant(p_jit_buf, index, (addr_6502 & 0xff));

  return index;
}

static size_t
jit_emit_php(unsigned char* p_jit, size_t index, int is_brk) {
  /* mov rdx, r8 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xc2;
  if (is_brk) {
    /* or dl, 0x30 */
    p_jit[index++] = 0x80;
    p_jit[index++] = 0xca;
    p_jit[index++] = 0x30;
  } else {
    /* or dl, 0x20 */
    p_jit[index++] = 0x80;
    p_jit[index++] = 0xca;
    p_jit[index++] = 0x20;
  }

  /* or rdx, r12 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xe2;

  /* mov r9, r13 */
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xe9;
  /* shl r9, 1 */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0xd1;
  p_jit[index++] = 0xe1;
  /* or rdx, r9 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xca;

  /* mov r9, r14 */
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xf1;
  /* shl r9, 7 */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe1;
  p_jit[index++] = 0x07;
  /* or rdx, r9 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xca;

  /* mov r9, r15 */
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xf9;
  /* shl r9, 6 */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe1;
  p_jit[index++] = 0x06;
  /* or rdx, r9 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x09;
  p_jit[index++] = 0xca;

  index = jit_emit_push_from_scratch(p_jit, index);

  return index;
}

static size_t
jit_emit_plp(unsigned char* p_jit_buf, size_t index) {
  index = jit_emit_pull_to_scratch(p_jit_buf, index);

  index = jit_emit_scratch_bit_test(p_jit_buf, index, 0);
  index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
  index = jit_emit_scratch_bit_test(p_jit_buf, index, 1);
  index = jit_emit_carry_to_6502_zero(p_jit_buf, index);
  index = jit_emit_scratch_bit_test(p_jit_buf, index, 6);
  index = jit_emit_carry_to_6502_overflow(p_jit_buf, index);
  index = jit_emit_scratch_bit_test(p_jit_buf, index, 7);
  index = jit_emit_carry_to_6502_negative(p_jit_buf, index);
  /* and dl, 0xc */
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xe2;
  p_jit_buf[index++] = 0x0c;
  /* mov r8b, dl */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd0;

  return index;
}

static size_t
jit_emit_jmp_indirect(struct jit_struct* p_jit,
                      unsigned char* p_jit_buf,
                      size_t index,
                      uint16_t addr_6502) {
  assert((addr_6502 & 0xff) != 0xff);
  /* movzx edx, WORD PTR [p_mem + addr] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x25;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
  index = jit_emit_jit_bytes_shift_scratch_left(p_jit_buf, index);
  /* add edx, p_jit_base */
  p_jit_buf[index++] = 0x81;
  p_jit_buf[index++] = 0xc2;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_jit_base);
  index = jit_emit_jmp_scratch(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_undefined(unsigned char* p_jit,
                   size_t index,
                   unsigned char opcode,
                   size_t addr_6502) {
  /* ud2 */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x0b;
  /* Copy of unimplemented 6502 opcode. */
  p_jit[index++] = opcode;
  /* Virtual address of opcode, big endian. */
  p_jit[index++] = addr_6502 >> 8;
  p_jit[index++] = addr_6502 & 0xff;

  return index;
}

static size_t
jit_emit_save_registers(unsigned char* p_jit, size_t index) {
  /* No need to push rdx or rdi because they are scratch registers. */
  /* push rax / rcx / rsi */
  p_jit[index++] = 0x50;
  p_jit[index++] = 0x51;
  p_jit[index++] = 0x56;
  /* push r8 */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x50;

  return index;
}

static size_t
jit_emit_restore_registers(unsigned char* p_jit, size_t index) {
  /* pop r8 */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x58;
  /* pop rsi / rcx / rax */
  p_jit[index++] = 0x5e;
  p_jit[index++] = 0x59;
  p_jit[index++] = 0x58;

  return index;
}

static void
jit_emit_debug_util(unsigned char* p_jit) {
  size_t index = 0;
  index = jit_emit_save_registers(p_jit, index);

  /* param11: 6502 S */
  /* push rsi */
  p_jit[index++] = 0x56;

  /* param2: 6502 IP */
  /* mov esi, edx */
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xd6;

  /* param10: 6502 Y */
  /* push rcx */
  p_jit[index++] = 0x51;

  /* param9: 6502 X */
  /* push rbx */
  p_jit[index++] = 0x53;

  /* param8: 6502 A */
  /* push rax */
  p_jit[index++] = 0x50;

  /* param7: remaining flags */
  /* push r8 */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x50;

  /* param6: 6502 FO */
  /* mov r9, r15 */
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xf9;

  /* param5: 6502 FC */
  /* mov r8, r12 */
  p_jit[index++] = 0x4d;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xe0;

  /* param4: 6502 FN */
  /* mov rcx, r14 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xf1;

  /* param3: 6502 FZ */
  /* mov rdx, r13 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xea;

  /* param1 */
  /* mov rdi, [rbp + k_offset_debug] */
  p_jit[index++] = 0x48;
  p_jit[index++] = 0x8b;
  p_jit[index++] = 0x7d;
  p_jit[index++] = k_offset_debug;

  /* call [rbp + k_offset_debug_callback] */
  p_jit[index++] = 0xff;
  p_jit[index++] = 0x55;
  p_jit[index++] = k_offset_debug_callback;

  /* add rsp, 40 */
  p_jit[index++] = 0x48;
  p_jit[index++] = 0x83;
  p_jit[index++] = 0xc4;
  p_jit[index++] = 40;

  index = jit_emit_restore_registers(p_jit, index);

  /* ret */
  p_jit[index++] = 0xc3;
}

static void
jit_emit_debug_sequence(struct util_buffer* p_buf, uint16_t addr_6502) {
  /* mov dx, addr_6502 */
  util_buffer_add_2b_1w(p_buf, 0x66, 0xba, addr_6502);
  /* call [rbp + k_offset_util_debug] */
  util_buffer_add_3b(p_buf, 0xff, 0x55, k_offset_util_debug);
}

static size_t
jit_check_special_read(struct jit_struct* p_jit,
                       uint16_t addr_6502,
                       unsigned char* p_jit_buf,
                       size_t index) {
  if (!bbc_is_special_read_addr(p_jit->p_bbc, addr_6502)) {
    return index;
  }
  index = jit_emit_save_registers(p_jit_buf, index);

  /* mov rdi, [rbp + k_offset_bbc] */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7d;
  p_jit_buf[index++] = k_offset_bbc;
  /* mov si, addr_6502 */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0xbe;
  p_jit_buf[index++] = addr_6502 & 0xff;
  p_jit_buf[index++] = addr_6502 >> 8;
  /* call [rbp + k_offset_read_callback] */
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x55;
  p_jit_buf[index++] = k_offset_read_callback;
  /* mov rdx, rax */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xc2;

  index = jit_emit_restore_registers(p_jit_buf, index);
  /* mov [p_mem + addr_6502], dl */
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x25;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);

  return index;
}

static size_t
jit_check_special_write(struct jit_struct* p_jit,
                        uint16_t addr,
                        unsigned char* p_jit_buf,
                        size_t index) {
  if (!bbc_is_special_write_addr(p_jit->p_bbc, addr)) {
    return index;
  }
  index = jit_emit_save_registers(p_jit_buf, index);

  /* mov rdi, [rbp + k_offset_bbc] */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7d;
  p_jit_buf[index++] = k_offset_bbc;
  /* mov si, addr */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0xbe;
  p_jit_buf[index++] = addr & 0xff;
  p_jit_buf[index++] = addr >> 8;
  /* call [rbp + k_offset_write_callback] */
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x55;
  p_jit_buf[index++] = k_offset_write_callback;

  index = jit_emit_restore_registers(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_calc_op(struct jit_struct* p_jit,
                 unsigned char* p_jit_buf,
                 size_t index,
                 unsigned char opmode,
                 unsigned char operand1,
                 unsigned char operand2,
                 unsigned char intel_op_base) {
  uint16_t addr_6502 = (operand2 << 8) | operand1;
  switch (opmode) {
  case k_imm:
    /* OP al, op1 */
    p_jit_buf[index++] = intel_op_base + 2;
    p_jit_buf[index++] = operand1;
    break;
  case k_zpg:
  case k_abs:
    index = jit_check_special_read(p_jit, addr_6502, p_jit_buf, index);
    /* OP al, [p_mem + addr] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
    break;
  case k_idy:
    /* OP al, [rdx + rcx] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x0a;
    break;
  case k_abx:
    /* OP al, [rbx + addr_6502] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x83;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    break;
  case k_aby:
    /* OP al, [rcx + addr_6502] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x81;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    break;
  case k_zpx:
  case k_idx:
    /* OP al, [rdx + p_mem] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x82;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    break;
  default:
    assert(0);
    break;
  }

  return index;
}

static size_t
jit_emit_shift_op(struct jit_struct* p_jit,
                  unsigned char* p_jit_buf,
                  size_t index,
                  unsigned char opmode,
                  unsigned char operand1,
                  unsigned char operand2,
                  unsigned char intel_op_base,
                  size_t n_count) {
  assert(n_count < 8);
  uint16_t addr_6502 = (operand2 << 8) | operand1;
  unsigned char first_byte = 0xd0;
  if (n_count > 1) {
    first_byte = 0xc0;
  }
  switch (opmode) {
  case k_nil:
    /* OP al, n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base;
    break;
  case k_zpg:
  case k_abs:
    /* OP BYTE PTR [p_mem + addr], n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base - 0xbc;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
    break;
  case k_zpx:
    /* OP BYTE PTR [rdx + p_mem], n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base - 0x3e;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    break;
  case k_abx:
    /* OP BYTE PTR [rbx + addr_6502], n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base - 0x3d;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    break;
  default:
    assert(0);
    break;
  }

  if (n_count > 1) {
    p_jit_buf[index++] = n_count;
  }

  return index;
}

static size_t
jit_emit_post_rotate(struct jit_struct* p_jit,
                     unsigned char* p_jit_buf,
                     size_t index,
                     unsigned char opmode,
                     unsigned char operand1,
                     unsigned char operand2) {
  uint16_t addr_6502 = (operand2 << 8) | operand1;
  index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
  switch (opmode) {
  case k_nil:
    index = jit_emit_do_zn_flags(p_jit_buf, index, 0);
    break;
  case k_zpg:
  case k_abs:
    /* test BYTE PTR [p_mem + addr], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
    p_jit_buf[index++] = 0xff;
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_zpx:
    /* test BYTE PTR [rdx + p_mem], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x82;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    p_jit_buf[index++] = 0xff;
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_abx:
    /* test BYTE PTR [rbx + addr_6502], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x83;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    p_jit_buf[index++] = 0xff;
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  default:
    assert(0);
    break;
  }

  return index;
}

static size_t
jit_emit_sei(unsigned char* p_jit_buf, size_t index) {
  /* bts r8, 2 */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xba;
  p_jit_buf[index++] = 0xe8;
  p_jit_buf[index++] = 0x02;

  return index;
}

static size_t
jit_emit_do_interrupt(struct jit_struct* p_jit,
                      unsigned char* p_jit_buf,
                      size_t index,
                      uint16_t addr_6502,
                      int is_brk) {
  uint16_t vector = k_bbc_vector_irq;
  unsigned char n = 0;
  if (is_brk) {
    n = 2;
  }
  index = jit_emit_push_addr(p_jit_buf, index, addr_6502 + n);
  index = jit_emit_php(p_jit_buf, index, is_brk);
  index = jit_emit_sei(p_jit_buf, index);
  index = jit_emit_jmp_indirect(p_jit, p_jit_buf, index, vector);

  return index;
}

static size_t
jit_emit_check_interrupt(struct jit_struct* p_jit,
                         unsigned char* p_jit_buf,
                         size_t index,
                         uint16_t addr_6502,
                         int check_flag) {
  size_t index_jmp1 = 0;
  size_t index_jmp2 = 0;
  /* test BYTE PTR [rbp + k_offset_interrupt], 1 */
  p_jit_buf[index++] = 0xf6;
  p_jit_buf[index++] = 0x45;
  p_jit_buf[index++] = k_offset_interrupt;
  p_jit_buf[index++] = 0x01;
  /* je ... */
  p_jit_buf[index++] = 0x74;
  p_jit_buf[index++] = 0xfe;
  index_jmp1 = index;

  if (check_flag) {
    /* bt r8, 2 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xe0;
    p_jit_buf[index++] = 0x02;
    /* jb ... */
    p_jit_buf[index++] = 0x72;
    p_jit_buf[index++] = 0xfe;
    index_jmp2 = index;
  }

  index = jit_emit_do_interrupt(p_jit, p_jit_buf, index, addr_6502, 0);

  if (index_jmp1) {
    p_jit_buf[index_jmp1 - 1] = index - index_jmp1;
  }
  if (index_jmp2) {
    p_jit_buf[index_jmp2 - 1] = index - index_jmp2;
  }

  return index;
}

void
jit_set_interrupt(struct jit_struct* p_jit, int interrupt) {
  p_jit->interrupt = interrupt;
}

static size_t
jit_single(struct jit_struct* p_jit,
           struct util_buffer* p_buf,
           uint16_t addr_6502,
           unsigned int debug_flags,
           unsigned int jit_flags) {
  unsigned char* p_mem = p_jit->p_mem;
  unsigned char* p_jit_buf = util_buffer_get_ptr(p_buf);

  uint16_t addr_6502_plus_1 = addr_6502 + 1;
  uint16_t addr_6502_plus_2 = addr_6502 + 2;

  unsigned char opcode = p_mem[addr_6502];
  unsigned char operand1 = p_mem[addr_6502_plus_1];
  unsigned char operand2 = p_mem[addr_6502_plus_2];
  uint16_t addr_6502_relative_jump = (int) addr_6502 + 2 + (char) operand1;

  unsigned char opmode = g_opmodes[opcode];
  unsigned char optype = g_optypes[opcode];
  unsigned char oplen = 0;
  uint16_t opcode_addr_6502;
  size_t index = 0;
  size_t num_6502_bytes = 0;
  size_t n_count = 1;

  if (debug_flags) {
    jit_emit_debug_sequence(p_buf, addr_6502);
    index = util_buffer_get_pos(p_buf);
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
    index = jit_emit_zp_x_to_scratch(p_jit_buf, index, operand1);
    oplen = 2;
    break;
  case k_zpy:
    index = jit_emit_zp_y_to_scratch(p_jit_buf, index, operand1);
    oplen = 2;
    break;
  case k_abx:
    index = jit_emit_abs_x_to_scratch(p_jit_buf, index, operand1, operand2);
    oplen = 3;
    break;
  case k_aby:
    index = jit_emit_abs_y_to_scratch(p_jit_buf, index, operand1, operand2);
    oplen = 3;
    break;
  case k_idy:
    index = jit_emit_ind_y_to_scratch(p_jit, p_jit_buf, index, operand1);
    oplen = 2;
    break;
  case k_idx:
    index = jit_emit_ind_x_to_scratch(p_jit, p_jit_buf, index, operand1);
    oplen = 2;
    break;
  case k_ind:
    oplen = 3;
    break;
  case k_nil:
    oplen = 1;
    break;
  case 0:
    oplen = 1;
    break;
  default:
    assert(0);
    break;
  }

  num_6502_bytes += oplen;

  if (oplen < 3) { 
    /* Clear operand2 if we're not using it. This enables us to re-use the
     * same x64 opcode generation code for both k_zpg and k_abs.
     */
    operand2 = 0;
  }
  opcode_addr_6502 = (operand2 << 8) | operand1;

  /* Handle merging repeated shift / rotate instructions. */
  if (jit_flags) {
    if (optype == k_lsr ||
        optype == k_asl ||
        optype == k_rol ||
        optype == k_ror) {
      if (opmode == k_nil) {
        uint16_t next_addr_6502 = addr_6502 + 1;
        while (n_count < 7 && p_mem[next_addr_6502] == opcode) {
          n_count++;
          next_addr_6502++;
          num_6502_bytes++;
        }
      }
    }
  }

  switch (optype) {
  case k_kil:
    switch (opcode) {
    case 0x02:
      /* Illegal opcode. Hangs a standard 6502. */
      /* Bounce out of JIT. */
      /* ret */
      p_jit_buf[index++] = 0xc3;
      break;
    case 0x12:
      /* Illegal opcode. Hangs a standard 6502. */
      /* Generate a debug trap and continue. */
      /* int 3 */
      p_jit_buf[index++] = 0xcc;
      break;
    case 0xf2:
      /* Illegal opcode. Hangs a standard 6502. */
      /* Generate a SEGV. */
      /* xor rdx, rdx */
      p_jit_buf[index++] = 0x31;
      p_jit_buf[index++] = 0xd2;
      index = jit_emit_jmp_scratch(p_jit_buf, index);
      break;
    default:
      index = jit_emit_undefined(p_jit_buf, index, opcode, addr_6502);
      break;
    }
    break;
  case k_brk:
    /* BRK */
    index = jit_emit_do_interrupt(p_jit, p_jit_buf, index, addr_6502, 1);
    break;
  case k_ora:
    /* ORA */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x0a);
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_asl:
    /* ASL */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xe0,
                              n_count);
    index = jit_emit_intel_to_6502_znc(p_jit_buf, index);
    break;
  case k_php:
    /* PHP */
    index = jit_emit_php(p_jit_buf, index, 1);
    break;
  case k_bpl:
    /* BPL */
    index = jit_emit_test_negative(p_jit_buf, index);
    /* je */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x84);
    break;
  case k_clc:
    /* CLC */
    index = jit_emit_set_carry(p_jit_buf, index, 0);
    break;
  case k_jsr:
    /* JSR */
    index = jit_emit_push_addr(p_jit_buf, index, addr_6502 + 2);
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit, p_buf, opcode_addr_6502, 0xe9, 0);
    break;
  case k_bit:
    /* BIT */
    /* Only has zp and abs. */
    assert(opmode == k_zpg || opmode == k_abs);
    index = jit_check_special_read(p_jit, opcode_addr_6502, p_jit_buf, index);
    /* mov dl, [p_mem + addr] */
    p_jit_buf[index++] = 0x8a;
    p_jit_buf[index++] = 0x14;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf,
                         index,
                         (size_t) p_jit->p_mem + opcode_addr_6502);
    index = jit_emit_scratch_bit_test(p_jit_buf, index, 7);
    index = jit_emit_carry_to_6502_negative(p_jit_buf, index);
    index = jit_emit_scratch_bit_test(p_jit_buf, index, 6);
    index = jit_emit_carry_to_6502_overflow(p_jit_buf, index);
    /* and dl, al */
    p_jit_buf[index++] = 0x20;
    p_jit_buf[index++] = 0xc2;
    index = jit_emit_intel_to_6502_zero(p_jit_buf, index);
    break;
  case k_and:
    /* AND */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x22);
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_rol:
    /* ROL */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xd0,
                              n_count);
    index = jit_emit_post_rotate(p_jit,
                                 p_jit_buf,
                                 index,
                                 opmode,
                                 operand1,
                                 operand2);
    break;
  case k_plp:
    /* PLP */
    index = jit_emit_plp(p_jit_buf, index);
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502, 1);
    break;
  case k_bmi:
    /* BMI */
    index = jit_emit_test_negative(p_jit_buf, index);
    /* jne */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x85);
    break;
  case k_sec:
    /* SEC */
    index = jit_emit_set_carry(p_jit_buf, index, 1);
    break;
  case k_eor:
    /* EOR */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x32);
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_lsr:
    /* LSR */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xe8,
                              n_count);
    index = jit_emit_intel_to_6502_znc(p_jit_buf, index);
    break;
  case k_pha:
    /* PHA */
    index = jit_emit_push_from_a(p_jit_buf, index);
    break;
  case k_jmp:
    /* JMP */
    if (opmode == k_abs) {
      index = jit_emit_jmp_6502_addr(p_jit,
                                     p_buf,
                                     opcode_addr_6502,
                                     0xe9,
                                     0);
    } else {
      index = jit_emit_jmp_indirect(p_jit,
                                    p_jit_buf,
                                    index,
                                    opcode_addr_6502);
    }
    break;
  case k_bvc:
    /* BVC */
    index = jit_emit_test_overflow(p_jit_buf, index);
    /* je */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x84);
    break;
  case k_cli:
    /* CLI */
    /* btr r8, 2 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xf0;
    p_jit_buf[index++] = 0x02;
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502, 0);
    break;
  case k_rti:
    index = jit_emit_plp(p_jit_buf, index);
    /* Fall through to RTS. */
  case k_rts:
    /* RTS */
    index = jit_emit_pull_to_scratch_word(p_jit_buf, index);
    /* inc dx */
    p_jit_buf[index++] = 0x66;
    p_jit_buf[index++] = 0xff;
    p_jit_buf[index++] = 0xc2;
    index = jit_emit_jit_bytes_shift_scratch_left(p_jit_buf, index);
    /* add edx, k_jit_addr */
    p_jit_buf[index++] = 0x81;
    p_jit_buf[index++] = 0xc2;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_jit_base);
    index = jit_emit_jmp_scratch(p_jit_buf, index);
    break;
  case k_adc:
    /* ADC */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x12);
    index = jit_emit_intel_to_6502_znco(p_jit_buf, index);
    break;
  case k_ror:
    /* ROR */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xd8,
                              n_count);
    index = jit_emit_post_rotate(p_jit,
                                 p_jit_buf,
                                 index,
                                 opmode,
                                 operand1,
                                 operand2);
    break;
  case k_pla:
    /* PLA */
    index = jit_emit_pull_to_a(p_jit_buf, index);
    index = jit_emit_do_zn_flags(p_jit_buf, index, 0);
    break;
  case k_bvs:
    /* BVS */
    index = jit_emit_test_overflow(p_jit_buf, index);
    /* jne */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x85);
    break;
  case k_sei:
    /* SEI */
    index = jit_emit_sei(p_jit_buf, index);
    break;
  case k_sta:
    /* STA */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* mov [p_mem + addr], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      index = jit_check_special_write(p_jit,
                                      opcode_addr_6502,
                                      p_jit_buf,
                                      index);
      break;
    case k_idy:
      /* mov [rdx + rcx], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x0a;
      break;
    case k_abx:
      /* mov [rbx + addr_6502], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x83;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_aby:
      /* mov [rcx + addr_6502], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x81;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_zpx:
    case k_idx:
      /* mov [rdx + p_mem], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x82;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_sty:
    /* STY */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* mov [p_mem + addr], cl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      index = jit_check_special_write(p_jit,
                                      opcode_addr_6502,
                                      p_jit_buf,
                                      index);
      break;
    case k_zpx:
      /* mov [rdx + p_mem], cl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x8a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_stx:
    /* STX */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* mov [p_mem + addr], bl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      index = jit_check_special_write(p_jit,
                                      opcode_addr_6502,
                                      p_jit_buf,
                                      index);
      break;
    case k_zpy:
      /* mov [rdx + p_mem], bl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x9a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_dey:
    /* DEY */
    /* dec cl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc9;
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_txa:
    /* TXA */
    /* mov al, bl */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xd8;
    index = jit_emit_do_zn_flags(p_jit_buf, index, 0);
    break;
  case k_bcc:
    /* BCC */
    index = jit_emit_test_carry(p_jit_buf, index);
    /* je */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x84);
    break;
  case k_tya:
    /* TYA */
    /* mov al, cl */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc8;
    index = jit_emit_do_zn_flags(p_jit_buf, index, 0);
    break;
  case k_txs:
    /* TXS */
    /* mov sil, bl */
    p_jit_buf[index++] = 0x40;
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xde;
    break;
  case k_ldy:
    /* LDY */
    switch (opmode) {
    case k_imm:
      /* mov cl, op1 */
      p_jit_buf[index++] = 0xb1;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      index = jit_check_special_read(p_jit,
                                     opcode_addr_6502,
                                     p_jit_buf,
                                     index);
      /* mov cl, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpx:
      /* mov cl, [rdx + p_mem] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x8a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    case k_abx:
      /* mov cl, [rbx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x8b;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_do_zn_flags(p_jit_buf, index, 2);
    break;
  case k_ldx:
    /* LDX */
    switch (opmode) {
    case k_imm:
      /* mov bl, op1 */
      p_jit_buf[index++] = 0xb3;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      index = jit_check_special_read(p_jit,
                                     opcode_addr_6502,
                                     p_jit_buf,
                                     index);
      /* mov bl, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpy:
      /* mov bl, [rdx + p_mem] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x9a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    case k_aby:
      /* mov bl, [rcx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x9b;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_do_zn_flags(p_jit_buf, index, 1);
    break;
  case k_lda:
    /* LDA */
    switch (opmode) {
    case k_imm:
      /* mov al, op1 */
      p_jit_buf[index++] = 0xb0;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      index = jit_check_special_read(p_jit,
                                     opcode_addr_6502,
                                     p_jit_buf,
                                     index);
      /* mov al, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_idy:
      /* mov al, [rdx + rcx] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x0a;
      break;
    case k_abx:
      /* mov al, [rbx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x83;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_aby:
      /* mov al, [rcx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x81;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_zpx:
    case k_idx:
      /* mov al, [rdx + p_mem] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x82;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_do_zn_flags(p_jit_buf, index, 0);
    break;
  case k_tay:
    /* TAY */
    /* mov cl, al */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc1;
    index = jit_emit_do_zn_flags(p_jit_buf, index, 2);
    break;
  case k_tax:
    /* TAX */
    /* mov bl, al */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc3;
    index = jit_emit_do_zn_flags(p_jit_buf, index, 1);
    break;
  case k_bcs:
    /* BCS */
    index = jit_emit_test_carry(p_jit_buf, index);
    /* jne */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x85);
    break;
  case k_clv:
    /* CLV */
    /* mov r15b, 0 */
    p_jit_buf[index++] = 0x41;
    p_jit_buf[index++] = 0xb7;
    p_jit_buf[index++] = 0x00;
    break;
  case k_tsx:
    /* TSX */
    /* mov bl, sil */
    p_jit_buf[index++] = 0x40;
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xf3;
    index = jit_emit_do_zn_flags(p_jit_buf, index, 1);
    break;
  case k_cpy:
    /* CPY */
    switch (opmode) {
    case k_imm:
      /* cmp cl, op1 */
      p_jit_buf[index++] = 0x80;
      p_jit_buf[index++] = 0xf9;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      /* cmp cl, [p_mem + addr] */
      p_jit_buf[index++] = 0x3a;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_intel_to_6502_sub_znc(p_jit_buf, index);
    break;
  case k_cmp:
    /* CMP */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x3a);
    index = jit_emit_intel_to_6502_sub_znc(p_jit_buf, index);
    break;
  case k_dec:
    /* DEC */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* dec BYTE PTR [p_mem + addr] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpx: 
      /* dec BYTE PTR [rdx + p_mem] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x8a;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_abx:
      /* dec BYTE PTR [rbx + addr_6502] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x8b;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_iny:
    /* INY */
    /* inc cl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc1;
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_dex:
    /* DEX */
    /* dec bl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xcb;
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_bne:
    /* BNE */
    index = jit_emit_test_zero(p_jit_buf, index);
    /* je */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x84);
    break;
  case k_cld:
    /* CLD */
    /* btr r8, 3 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xf0;
    p_jit_buf[index++] = 0x03;
    break;
  case k_cpx:
    /* CPX */
    switch (opmode) {
    case k_imm:
      /* cmp bl, op1 */
      p_jit_buf[index++] = 0x80;
      p_jit_buf[index++] = 0xfb;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      /* cmp bl, [p_mem + addr] */
      p_jit_buf[index++] = 0x3a;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_intel_to_6502_sub_znc(p_jit_buf, index);
    break;
  case k_inc:
    /* INC */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* inc BYTE PTR [p_mem + addr] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpx: 
      /* inc BYTE PTR [rdx + p_mem] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x82;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_abx:
      /* inc BYTE PTR [rbx + addr_6502] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x83;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_inx:
    /* INX */
    /* inc bl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc3;
    index = jit_emit_do_zn_flags(p_jit_buf, index, -1);
    break;
  case k_sbc:
    /* SBC */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    /* cmc */
    p_jit_buf[index++] = 0xf5;
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x1a);
    index = jit_emit_intel_to_6502_sub_znco(p_jit_buf, index);
    break;
  case k_nop:
    /* NOP */
    break;
  case k_beq:
    /* BEQ */
    index = jit_emit_test_zero(p_jit_buf, index);
    /* jne */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x85);
    break;
  default:
    index = jit_emit_undefined(p_jit_buf, index, opcode, addr_6502);
    break;
  }

  util_buffer_set_pos(p_buf, index);

  return num_6502_bytes;
}

void
jit_at_addr(struct jit_struct* p_jit,
            struct util_buffer* p_buf,
            uint16_t addr_6502,
            unsigned int debug_flags,
            unsigned int jit_flags) {
  unsigned char single_jit_buf[k_jit_bytes_per_byte];
  struct util_buffer* p_single_buf = util_buffer_create();
  size_t num_6502_bytes;
  size_t total_num_ops = 0;
  size_t total_6502_bytes = 0;
  uint16_t start_addr_6502 = addr_6502;
  unsigned char* p_dst;

  do {
    p_dst = util_buffer_get_ptr(p_buf) + util_buffer_get_pos(p_buf);
    util_buffer_setup(p_single_buf, single_jit_buf, k_jit_bytes_per_byte);
    util_buffer_set_base_address(p_single_buf, p_dst);
    num_6502_bytes = jit_single(p_jit, p_single_buf, addr_6502, debug_flags, 1);
    size_t opcodes_len = util_buffer_get_pos(p_single_buf);
    size_t buf_left = util_buffer_remaining(p_buf);
    /* TODO: don't hardcode jmp length. */
    if (buf_left >= opcodes_len + 5) {
      util_buffer_append(p_buf, p_single_buf);
      total_6502_bytes += num_6502_bytes;
      total_num_ops++;
    } else {
      break;
    }
    addr_6502 += num_6502_bytes;
  } while (1);

  assert(total_num_ops > 0);
//printf("addr %x, total_num_ops: %zu\n", start_addr_6502, total_num_ops);

  util_buffer_destroy(p_single_buf);

  (void) jit_emit_jmp_6502_addr(p_jit,
                                p_buf,
                                start_addr_6502 + total_6502_bytes,
                                0xe9,
                                0);
}

void
jit_jit(struct jit_struct* p_jit,
        size_t addr_6502,
        size_t num_opcodes,
        unsigned int debug_flags) {
  size_t jit_end = addr_6502 + num_opcodes;
  unsigned char* p_jit_base = p_jit->p_jit_base;
  while (addr_6502 < jit_end) {
    unsigned char* p_jit_buf = p_jit_base + (addr_6502 * k_jit_bytes_per_byte);
    struct util_buffer* p_buf = util_buffer_create();
    util_buffer_setup(p_buf, p_jit_buf, k_jit_bytes_per_byte);
    util_buffer_set_base_address(p_buf, p_jit_buf);

    jit_at_addr(p_jit, p_buf, addr_6502, debug_flags, 1);

    util_buffer_destroy(p_buf);
    addr_6502++;
  }
}

void
jit_enter(struct jit_struct* p_jit, size_t vector_addr) {
  unsigned char* p_mem = p_jit->p_mem;
  unsigned char addr_lsb = p_mem[vector_addr];
  unsigned char addr_msb = p_mem[vector_addr + 1];
  unsigned int addr = (addr_msb << 8) | addr_lsb;
  unsigned char* p_jit_base = p_jit->p_jit_base;
  unsigned char* p_entry = p_jit_base + (addr * k_jit_bytes_per_byte);

  /* The memory must be aligned to at least 0x100 so that our register access
   * tricks work.
   */
  assert(((size_t) p_mem & 0xff) == 0);

  asm volatile (
    /* al is 6502 A. */
    "xor %%eax, %%eax;"
    /* bl is 6502 X */
    "mov %1, %%rbx;"
    /* cl is 6502 Y. */
    "mov %1, %%rcx;"
    /* rdx, rdi are scratch. */
    "xor %%edx, %%edx;"
    "xor %%edi, %%edi;"
    /* r8 is the rest of the 6502 flags or'ed together. */
    /* Bit 2 is interrupt disable. */
    /* Bit 3 is decimal mode. */
    "xor %%r8, %%r8;"
    /* r12 is carry flag. */
    "xor %%r12, %%r12;"
    /* r13 is zero flag. */
    "xor %%r13, %%r13;"
    /* r14 is negative flag. */
    "xor %%r14, %%r14;"
    /* r15 is overflow flag. */
    "xor %%r15, %%r15;"
    /* sil is 6502 S. */
    /* rsi is a pointer to the real (aligned) backing memory. */
    "lea 0x100(%%rbx), %%rsi;"
    /* Use scratch register for jump location. */
    "mov %0, %%rdx;"
    /* Pass a pointer to the jit_struct in rbp. */
    "mov %2, %%r9;"
    "push %%rbp;"
    "mov %%r9, %%rbp;"
    "call *%%rdx;"
    "pop %%rbp;"
    :
    : "g" (p_entry), "g" (p_mem), "g" (p_jit)
    : "rax", "rbx", "rcx", "rdx", "rdi", "rsi",
      "r8", "r9", "r12", "r13", "r14", "r15"
  );
}

struct jit_struct*
jit_create(unsigned char* p_mem,
           void* p_debug_callback,
           struct debug_struct* p_debug,
           struct bbc_struct* p_bbc,
           void* p_read_callback,
           void* p_write_callback) {
  unsigned char* p_jit_base;
  unsigned char* p_utils_base;
  unsigned char* p_util_debug;
  struct jit_struct* p_jit = malloc(sizeof(struct jit_struct));
  if (p_jit == NULL) {
    errx(1, "cannot allocate jit_struct");
  }
  memset(p_jit, '\0', sizeof(struct jit_struct));

  p_jit_base = util_get_guarded_mapping(
      k_jit_addr,
      k_addr_space_size * k_jit_bytes_per_byte,
      1);

  p_utils_base = util_get_guarded_mapping(k_utils_addr, k_utils_size, 1);
  p_util_debug = p_utils_base + k_utils_debug_offset;
  jit_emit_debug_util(p_util_debug);

  p_jit->p_mem = p_mem;
  p_jit->p_jit_base = p_jit_base;
  p_jit->p_utils_base = p_utils_base;
  p_jit->p_util_debug = p_util_debug;
  p_jit->p_debug = p_debug;
  p_jit->p_debug_callback = p_debug_callback;
  p_jit->p_bbc = p_bbc;
  p_jit->p_read_callback = p_read_callback;
  p_jit->p_write_callback = p_write_callback;

  /* nop */
  memset(p_jit_base, '\x90', k_addr_space_size * k_jit_bytes_per_byte);
  size_t num_bytes = k_addr_space_size;
  while (num_bytes--) {
    /* ud2 */
    p_jit_base[0] = 0x0f;
    p_jit_base[1] = 0x0b;
    p_jit_base += k_jit_bytes_per_byte;
  }

  return p_jit;
}

void
jit_destroy(struct jit_struct* p_jit) {
  util_free_guarded_mapping(p_jit->p_jit_base,
                            k_addr_space_size * k_jit_bytes_per_byte);
  util_free_guarded_mapping(p_jit->p_utils_base, k_utils_size);
  free(p_jit);
}
