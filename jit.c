#define _GNU_SOURCE /* For REG_RIP */

#include "jit.h"

#include "bbc.h"
#include "opdefs.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static const size_t k_guard_size = 4096;
static const int k_jit_bytes_per_byte = 256;
static const int k_jit_bytes_shift = 8;
static const int k_jit_bytes_mask = 0xff;
static void* k_jit_addr = (void*) 0x20000000;
static void* k_utils_addr = (void*) 0x80000000;
static const size_t k_utils_size = 4096;
static const size_t k_utils_debug_offset = 0;
static const size_t k_utils_regs_offset = 0x100;
static const size_t k_utils_jit_offset = 0x200;

static const int k_offset_util_jit = 0;
static const int k_offset_util_regs = 8;
static const int k_offset_util_debug = 16;

static const int k_offset_reg_rip = 24;
static const int k_offset_reg_a_eax = 28;
static const int k_offset_reg_x_ebx = 32;
static const int k_offset_reg_y_ecx = 36;
static const int k_offset_reg_s_esi = 40;
static const int k_offset_reg_pc = 44;
static const int k_offset_reg_x64_flags = 46;
static const int k_offset_reg_6502_flags = 47;
static const int k_offset_irq = 48;

static const int k_offset_debug_callback = 56;
static const int k_offset_jit_callback = 64;
static const int k_offset_read_callback = 72;
static const int k_offset_write_callback = 80;

static const int k_offset_debug = 88;
static const int k_offset_bbc = 96;
static const int k_offset_jit_ptrs = 104;

static const unsigned int k_jit_flag_debug = 1;
static const unsigned int k_jit_flag_merge_ops = 2;
static const unsigned int k_jit_flag_self_modifying = 4;
static const unsigned int k_jit_flag_dynamic_operand = 8;
static const unsigned int k_jit_flag_no_rom_fault = 16;

enum {
  k_a = 1,
  k_x = 2,
  k_y = 3,
  k_set = 4,
};

/* k_a: PLA, TXA, TYA, LDA */
/* k_x: LDX, TAX, TSX */
/* k_y: LDY, TAY */
static const unsigned char g_nz_flag_pending[58] = {
  0    , 0    , 0    , k_set, k_set, 0    , 0    , 0    ,
  0    , k_set, k_set, k_set, k_set, 0    , 0    , 0    ,
  k_set, k_set, 0    , 0    , 0    , 0    , 0    , k_set,
  k_a  , k_set, 0    , 0    , 0    , 0    , 0    , k_set,
  k_a  , 0    , k_a  , 0    , k_y  , k_a  , k_x  , k_y  ,
  k_x  , 0    , 0    , k_x  , k_set, k_set, k_set, k_set,
  k_set, k_set, 0    , 0    , k_set, k_set, 0    , k_set,
  0    , 0    ,
};

static const unsigned char g_nz_flags_needed[58] = {
  0, 0, 1, 0, 0, 1, 1, 0,
  1, 0, 0, 0, 0, 1, 0, 0,
  0, 0, 0, 1, 1, 1, 1, 0,
  0, 0, 1, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 0, 0, 0, 0, 0,
  1, 0,
};

struct jit_struct {
  /* Utilities called by JIT code. */
  /* Must be at 0. */
  unsigned char* p_util_jit;    /* 0   */
  unsigned char* p_util_regs;   /* 8   */
  unsigned char* p_util_debug;  /* 16  */

  /* Registers. */
  unsigned int reg_rip;         /* 24  */
  unsigned int reg_a_eax;       /* 28  */
  unsigned int reg_x_ebx;       /* 32  */
  unsigned int reg_y_ecx;       /* 36  */
  unsigned int reg_s_esi;       /* 40  */
  uint16_t reg_pc;              /* 44  */
  unsigned char reg_x64_flags;  /* 46  */
  unsigned char reg_6502_flags; /* 47  */
  unsigned char irq;            /* 48  */

  /* C callbacks called by JIT code. */
  void* p_debug_callback;       /* 56  */
  void* p_jit_callback;         /* 64  */
  void* p_read_callback;        /* 72  */
  void* p_write_callback;       /* 80  */

  /* Structures reeferenced by JIT code. */
  void* p_debug;                /* 88  */
  struct bbc_struct* p_bbc;     /* 96 */
  unsigned int jit_ptrs[k_bbc_addr_space_size]; /* 104 */

  /* Fields not referenced by JIT'ed code. */
  unsigned int jit_flags;
  unsigned char* p_mem;
  unsigned char* p_jit_base;
  unsigned char* p_utils_base;
  struct util_buffer* p_dest_buf;
  struct util_buffer* p_seq_buf;
  struct util_buffer* p_single_buf;
  unsigned char has_code[k_bbc_addr_space_size];
  unsigned char compiled_opcode[k_bbc_addr_space_size];
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
jit_emit_intel_to_6502_carry(unsigned char* p_jit, size_t index) {
  /* setb r14b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_intel_to_6502_sub_carry(unsigned char* p_jit, size_t index) {
  /* setae r14b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x93;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_intel_to_6502_overflow(unsigned char* p_jit, size_t index) {
  /* seto r12b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x90;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t
jit_emit_carry_to_6502_overflow(unsigned char* p_jit, size_t index) {
  /* setb r12b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t
jit_emit_do_zn_flags(unsigned char* p_jit, size_t index, int reg) {
  assert(reg >= 0 && reg <= 2);
  if (reg == 0) {
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

  return index;
}

static size_t
jit_emit_intel_to_6502_co(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_carry(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t
jit_emit_intel_to_6502_sub_co(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_sub_carry(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t
jit_emit_6502_carry_to_intel(unsigned char* p_jit, size_t index) {
  /* bt r14, 0 */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe6;
  p_jit[index++] = 0x00;

  return index;
}

static size_t
jit_emit_set_carry(unsigned char* p_jit, size_t index, unsigned char val) {
  /* mov r14b, val */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xb6;
  p_jit[index++] = val;

  return index;
}

static size_t
jit_emit_test_carry(unsigned char* p_jit, size_t index) {
  return jit_emit_6502_carry_to_intel(p_jit, index);
}

static size_t
jit_emit_test_overflow(unsigned char* p_jit, size_t index) {
  /* bt r12, 0 */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe4;
  p_jit[index++] = 0x00;

  return index;
}

static size_t
jit_emit_abs_x_to_scratch(unsigned char* p_jit,
                          size_t index,
                          uint16_t opcode_addr_6502) {
  /* Empty -- optimized for now but wrap-around will cause a fault. */
  return index;
}

static size_t
jit_emit_abs_y_to_scratch(unsigned char* p_jit,
                          size_t index,
                          uint16_t opcode_addr_6502) {
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
  unsigned char operand1_inc = operand1 + 1;
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* TODO: getting messy, rewrite. */
  /* Preserve rdi. */
  /* mov r8, rdi */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xf8;
  /* mov rdi, rbx */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xdf;
  /* lea edx, [rbx + operand1] */
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x93;
  index = jit_emit_int(p_jit_buf, index, operand1);
  /* lea r9, [rbx + operand1 + 1] */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x8b;
  index = jit_emit_int(p_jit_buf, index, operand1_inc);
  /* mov dil, dl */
  p_jit_buf[index++] = 0x40;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd7;
  /* movzx edx, BYTE PTR [rdi] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb6;
  p_jit_buf[index++] = 0x17;
  /* mov dil, r9b */
  p_jit_buf[index++] = 0x44;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xcf;
  /* mov dh, BYTE PTR [rdi] */
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x37;
  /* Restore rdi. */
  /* mov rdi, r8 */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xc7;

  return index;
}

static size_t
jit_emit_zp_x_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* lea r8, [rbx + operand1] */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x83;
  index = jit_emit_int(p_jit, index, operand1);
  /* movzx edx, r8b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd0;

  return index;
}

static size_t
jit_emit_zp_y_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* lea r8, [rcx + operand1] */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x81;
  index = jit_emit_int(p_jit, index, operand1);
  /* movzx edx, r8b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd0;

  return index;
}

static size_t
jit_emit_abs_x_dyn_to_scratch(struct jit_struct* p_jit,
                              unsigned char* p_jit_buf,
                              size_t index,
                              uint16_t addr_6502_plus_1) {
  /* movzx edx, WORD PTR [p_mem + addr_6502_plus_1] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x25;
  index = jit_emit_int(p_jit_buf,
                       index,
                       (size_t) p_jit->p_mem + addr_6502_plus_1);

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
  util_buffer_set_pos(p_buf, index);

  return index;
}

static size_t
jit_emit_jit_bytes_shift_scratch_left(unsigned char* p_jit, size_t index) {
  /* NOTE: uses BMI2 shlx instruction to avoid modifying flags. */
  /* mov r8b, k_jit_bytes_shift */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xb0;
  p_jit[index++] = k_jit_bytes_shift;
  /* shlx edx, edx, r8d */ /* BMI2 */
  p_jit[index++] = 0xc4;
  p_jit[index++] = 0xe2;
  p_jit[index++] = 0x39;
  p_jit[index++] = 0xf7;
  p_jit[index++] = 0xd2;

  return index;
}

static size_t
jit_emit_stack_inc(unsigned char* p_jit, size_t index) {
  /* lea r8, [rsi + 1] */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x46;
  p_jit[index++] = 0x01;
  /* mov sil, r8b */
  p_jit[index++] = 0x44;
  p_jit[index++] = 0x88;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_stack_dec(unsigned char* p_jit, size_t index) {
  /* lea r8, [rsi - 1] */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x46;
  p_jit[index++] = 0xff;
  /* mov sil, r8b */
  p_jit[index++] = 0x44;
  p_jit[index++] = 0x88;
  p_jit[index++] = 0xc6;

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
jit_emit_flags_to_scratch(unsigned char* p_jit, size_t index, int is_brk) {
  unsigned char brk_and_set_bit = 0x20;
  if (is_brk) {
    brk_and_set_bit += 0x10;
  }

  /* TODO: getting messy, rewrite? */
  /* Preserve rdi. */
  /* mov r8, rdi */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xf8;

  /* lahf */
  p_jit[index++] = 0x9f;

  /* r13 is IF and DF */
  /* r14 is CF */
  /* lea rdx, [r13 + r14 + brk_and_set_bit] */
  p_jit[index++] = 0x4b;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x54;
  p_jit[index++] = 0x35;
  p_jit[index++] = brk_and_set_bit;

  /* r12 is OF */
  /* mov rdi, r12 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xe7;
  /* shl edi, 6 */
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x06;
  /* lea edx, [rdx + rdi] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3a;

  /* Intel flags bit 6 is ZF, 6502 flags bit 1 */
  /* movzx edi, ah */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xfc;
  /* and edi, 0x40 */
  p_jit[index++] = 0x83;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x40;
  /* shr edi, 5 */
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xef;
  p_jit[index++] = 0x05;
  /* lea edx, [rdx + rdi] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3a;

  /* Intel flags bit 7 is SF, 6502 flags bit 7 */
  /* movzx edi, ah */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xfc;
  /* and edi, 0x80 */
  p_jit[index++] = 0x83;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x80;
  /* lea edx, [rdx + rdi] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3a;

  /* sahf */
  p_jit[index++] = 0x9e;

  /* Restore rdi. */
  /* mov rdi, r8 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xc7;

  return index;
}

static size_t
jit_emit_set_flags(unsigned char* p_jit_buf, size_t index) {
  index = jit_emit_scratch_bit_test(p_jit_buf, index, 0);
  index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
  index = jit_emit_scratch_bit_test(p_jit_buf, index, 6);
  index = jit_emit_carry_to_6502_overflow(p_jit_buf, index);
  /* I and D flags */
  /* mov r13b, dl */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd5;
  /* and r13b, 0xc */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xe5;
  p_jit_buf[index++] = 0x0c;
  /* ZF */
  /* mov ah, dl */
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd4;
  /* and ah, 2 */
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xe4;
  p_jit_buf[index++] = 0x02;
  /* shl ah, 5 */
  p_jit_buf[index++] = 0xc0;
  p_jit_buf[index++] = 0xe4;
  p_jit_buf[index++] = 0x05;
  /* NF */
  /* and dl, 0x80 */
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xe2;
  p_jit_buf[index++] = 0x80;
  /* or ah, dl */
  p_jit_buf[index++] = 0x08;
  p_jit_buf[index++] = 0xd4;

  /* sahf */
  p_jit_buf[index++] = 0x9e;

  return index;
}

static size_t
jit_emit_jmp_from_6502_scratch(struct jit_struct* p_jit,
                               unsigned char* p_jit_buf,
                               size_t index) {
  index = jit_emit_jit_bytes_shift_scratch_left(p_jit_buf, index);
  /* lea edx, [rdx + p_jit_base] */
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x92;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_jit_base);
  index = jit_emit_jmp_scratch(p_jit_buf, index);

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
  index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);

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
  /* The flags we need to save are negative and zero, both covered by lahf. */
  /* lahf */
  p_jit[index++] = 0x9f;
  /* No need to push rdx because it is a scratch registers. */
  /* push rax / rcx / rsi / rdi */
  p_jit[index++] = 0x50;
  p_jit[index++] = 0x51;
  p_jit[index++] = 0x56;
  p_jit[index++] = 0x57;

  return index;
}

static size_t
jit_emit_restore_registers(unsigned char* p_jit, size_t index) {
  /* pop rdi / rsi / rcx / rax */
  p_jit[index++] = 0x5f;
  p_jit[index++] = 0x5e;
  p_jit[index++] = 0x59;
  p_jit[index++] = 0x58;
  /* sahf */
  p_jit[index++] = 0x9e;

  return index;
}

static void
jit_emit_debug_util(unsigned char* p_jit_buf) {
  size_t index = 0;

  /* Save rdi. */
  /* mov r15, rdi */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xff;

  /* 6502 IP */
  /* Must come first flags because other operations below trash dx. */
  /* mov [r15 + k_offset_reg_pc], dx */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_pc;

  /* Save Intel JIT address. */
  /* mov rdx, [rsp] */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x24;
  /* mov [r15 + k_offset_reg_rip], edx */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_rip;

  /* 6502 A */
  /* mov [r15 + k_offset_reg_a_eax], eax */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0x47;
  p_jit_buf[index++] = k_offset_reg_a_eax;

  /* 6502 X */
  /* mov [r15 + k_offset_reg_x_ebx], ebx */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0x5f;
  p_jit_buf[index++] = k_offset_reg_x_ebx;

  /* 6502 Y */
  /* mov [r15 + k_offset_reg_y_ecx], ecx */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0x4f;
  p_jit_buf[index++] = k_offset_reg_y_ecx;

  /* 6502 S */
  /* mov [r15 + k_offset_reg_s_esi], esi */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0x77;
  p_jit_buf[index++] = k_offset_reg_s_esi;

  /* 6502 flags */
  index = jit_emit_flags_to_scratch(p_jit_buf, index, 1);
  /* mov [r15 + k_offset_reg_6502_flags], dl */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_6502_flags;

  /* param1 */
  /* mov rdi, [r15 + k_offset_debug] */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7f;
  p_jit_buf[index++] = k_offset_debug;

  /* call [r15 + k_offset_debug_callback] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_debug_callback;

  /* call [r15 + k_offset_util_regs] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_util_regs;

  /* Restore rdi. */
  /* mov rdi, r15 */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xff;

  /* ret */
  p_jit_buf[index++] = 0xc3;
}

static void
jit_emit_regs_util(struct jit_struct* p_jit, unsigned char* p_jit_buf) {
  size_t index = 0;

  /* Set A. */
  /* mov eax, [r15 + k_offset_reg_a_eax] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x47;
  p_jit_buf[index++] = k_offset_reg_a_eax;

  /* Set X. */
  /* mov ebx, [r15 + k_offset_reg_x_ebx] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x5f;
  p_jit_buf[index++] = k_offset_reg_x_ebx;

  /* Set Y. */
  /* mov ecx, [r15 + k_offset_reg_y_ecx] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x4f;
  p_jit_buf[index++] = k_offset_reg_y_ecx;

  /* Set S. */
  /* mov esi, [r15 + k_offset_reg_s_esi] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x77;
  p_jit_buf[index++] = k_offset_reg_s_esi;

  /* Set flags. */
  /* mov dl, [r15 + k_offset_reg_6502_flags] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_6502_flags;
  index = jit_emit_set_flags(p_jit_buf, index);

  /* Load PC into edx. */
  /* movzx edx, WORD PTR [r15 + k_offset_reg_pc] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_pc;

  /* ret */
  p_jit_buf[index++] = 0xc3;
}

static void
jit_emit_jit_util(struct jit_struct* p_jit, unsigned char* p_jit_buf) {
  size_t index = 0;

  /* Save calling rip. */
  /* mov rdx, [rsp] */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x24;

  index = jit_emit_save_registers(p_jit_buf, index);

  /* param1: jit_struct pointer. */
  /* It's already in rdi. */

  /* param2: x64 rip that call'ed here. */
  /* mov rsi, rdx */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xd6;

  /* call [rdi + k_offset_jit_callback] */
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_jit_callback;

  index = jit_emit_restore_registers(p_jit_buf, index);

  /* movzx edx, WORD PTR [rdi + k_offset_reg_pc] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_pc;

  /* We are jumping out of a call, so need to pop the return value. */
  /* pop r8 */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x58;

  index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
}

static void
jit_emit_debug_sequence(struct util_buffer* p_buf, uint16_t addr_6502) {
  /* mov dx, addr_6502 */
  util_buffer_add_2b_1w(p_buf, 0x66, 0xba, addr_6502);
  /* call [rdi + k_offset_util_debug] */
  util_buffer_add_3b(p_buf, 0xff, 0x57, k_offset_util_debug);
}

static int
jit_is_special_read_address(struct jit_struct* p_jit,
                            uint16_t opcode_addr_6502,
                            uint16_t opcode_addr_6502_upper_range) {
  struct bbc_struct* p_bbc = p_jit->p_bbc;
  /* NOTE: assumes contiguous ranges of BBC special addresses. */
  return bbc_is_special_read_address(p_bbc,
                                     opcode_addr_6502,
                                     opcode_addr_6502_upper_range);
}

static int
jit_is_special_write_address(struct jit_struct* p_jit,
                             uint16_t opcode_addr_6502,
                             uint16_t opcode_addr_6502_upper_range) {
  struct bbc_struct* p_bbc = p_jit->p_bbc;
  /* NOTE: assumes contiguous ranges of BBC special addresses. */
  return bbc_is_special_write_address(p_bbc,
                                      opcode_addr_6502,
                                      opcode_addr_6502_upper_range);
}

static size_t
jit_emit_special_read(struct jit_struct* p_jit,
                      uint16_t addr_6502,
                      unsigned char opmode,
                      unsigned char* p_jit_buf,
                      size_t index) {
  index = jit_emit_save_registers(p_jit_buf, index);

  /* mov r15, rdi */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xff;
  /* mov rdi, [r15 + k_offset_bbc] */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7f;
  p_jit_buf[index++] = k_offset_bbc;
  if (opmode == k_abs) {
    /* xor esi, esi */
    p_jit_buf[index++] = 0x31;
    p_jit_buf[index++] = 0xf6;
  } else if (opmode == k_abx) {
    /* movzx esi, bl */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0xf3;
  } else if (opmode == k_aby) {
    /* movzx esi, cl */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0xf1;
  } else {
    assert(0);
  }
  /* add si, addr_6502 */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0x81;
  p_jit_buf[index++] = 0xc6;
  p_jit_buf[index++] = addr_6502 & 0xff;
  p_jit_buf[index++] = addr_6502 >> 8;
  /* call [r15 + k_offset_read_callback] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_read_callback;
  /* mov edx, eax */
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xc2;

  index = jit_emit_restore_registers(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_special_write(struct jit_struct* p_jit,
                       uint16_t addr_6502,
                       unsigned char opmode,
                       unsigned char* p_jit_buf,
                       size_t index) {
  index = jit_emit_save_registers(p_jit_buf, index);

  /* mov r15, rdi */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xff;
  /* mov rdi, [r15 + k_offset_bbc] */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7f;
  p_jit_buf[index++] = k_offset_bbc;
  if (opmode == k_abs) {
    /* xor esi, esi */
    p_jit_buf[index++] = 0x31;
    p_jit_buf[index++] = 0xf6;
  } else if (opmode == k_abx) {
    /* movzx esi, bl */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0xf3;
  } else if (opmode == k_aby) {
    /* movzx esi, cl */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0xf1;
  } else {
    assert(0);
  }
  /* add si, addr_6502 */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0x81;
  p_jit_buf[index++] = 0xc6;
  p_jit_buf[index++] = addr_6502 & 0xff;
  p_jit_buf[index++] = addr_6502 >> 8;
  /* rdx is third parameter, it's already set by the caller. */
  /* call [r15 + k_offset_write_callback] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_write_callback;

  index = jit_emit_restore_registers(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_calc_op(struct jit_struct* p_jit,
                 unsigned char* p_jit_buf,
                 size_t index,
                 unsigned char opmode,
                 uint16_t opcode_addr_6502,
                 int special,
                 unsigned char intel_op_base) {
  if (special) {
    /* OP al, dl */
    p_jit_buf[index++] = intel_op_base - 2;
    p_jit_buf[index++] = 0xd0;
    return index;
  }
  switch (opmode) {
  case k_imm:
    /* OP al, op1 */
    p_jit_buf[index++] = intel_op_base + 2;
    p_jit_buf[index++] = (unsigned char) opcode_addr_6502;
    break;
  case k_zpg:
  case k_abs:
    /* OP al, [p_mem + addr] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf,
                         index,
                         (size_t) p_jit->p_mem + opcode_addr_6502);
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
    index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
    break;
  case k_aby:
    /* OP al, [rcx + addr_6502] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x81;
    index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
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
                  uint16_t opcode_addr_6502,
                  int special,
                  unsigned char intel_op_base,
                  size_t n_count) {
  unsigned char first_byte;
  if (n_count == 0) {
    /* inc or dec */
    first_byte = 0xfe;
  } else if (n_count == 1) {
    first_byte = 0xd0;
  } else {
    assert(n_count < 8);
    first_byte = 0xc0;
  }
  if (special) {
    assert(n_count <= 1);
    /* OP dl */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base + 2;
    return index;
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
    index = jit_emit_int(p_jit_buf,
                         index,
                         (size_t) p_jit->p_mem + opcode_addr_6502);
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
    index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
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
                     int special,
                     uint16_t opcode_addr_6502) {
  index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
  if (special) {
    /* test dl, dl */
    p_jit_buf[index++] = 0x84;
    p_jit_buf[index++] = 0xd2;
    return index;
  }
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
    index = jit_emit_int(p_jit_buf,
                         index,
                         (size_t) p_jit->p_mem + opcode_addr_6502);
    p_jit_buf[index++] = 0xff;
    break;
  case k_zpx:
    /* test BYTE PTR [rdx + p_mem], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x82;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    p_jit_buf[index++] = 0xff;
    break;
  case k_abx:
    /* test BYTE PTR [rbx + addr_6502], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x83;
    index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
    p_jit_buf[index++] = 0xff;
    break;
  default:
    assert(0);
    break;
  }

  return index;
}

static size_t
jit_emit_sei(unsigned char* p_jit_buf, size_t index) {
  /* bts r13, 2 */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xba;
  p_jit_buf[index++] = 0xed;
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
  index = jit_emit_push_addr(p_jit_buf, index, addr_6502);
  index = jit_emit_flags_to_scratch(p_jit_buf, index, is_brk);
  index = jit_emit_push_from_scratch(p_jit_buf, index);
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
  /* bt DWORD PTR [rdi + k_offset_interrupt], 0 */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xba;
  p_jit_buf[index++] = 0x67;
  p_jit_buf[index++] = k_offset_irq;
  p_jit_buf[index++] = 0x00;
  /* jae / jnc ... */
  p_jit_buf[index++] = 0x73;
  p_jit_buf[index++] = 0xfe;
  index_jmp1 = index;

  if (check_flag) {
    /* bt r13, 2 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xe5;
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
  p_jit->irq = interrupt;
}

static unsigned char
jit_get_opcode(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_mem = p_jit->p_mem;
  return p_mem[addr_6502];
}

static size_t
jit_emit_do_jit(unsigned char* p_jit_buf, size_t index) {
  /* call [rdi] */
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x17;

  return index;
}

static size_t
jit_handle_invalidate(struct jit_struct* p_jit,
                      unsigned char opmem,
                      unsigned char opmode,
                      uint16_t opcode_addr_6502,
                      unsigned char* p_jit_buf,
                      size_t index) {
  /* TODO: it's most common that only the abs addressing mode needs to be
   * handled for self-modifying to work.
   * Should have a higher performance flag and mode just for this.
   */
  if (!(p_jit->jit_flags & k_jit_flag_self_modifying)) {
    return index;
  }
  if (opmem != k_write && opmem != k_rw) {
    return index;
  }
  if (opmode == k_nil ||
      opmode == k_zpg ||
      opmode == k_zpx ||
      opmode == k_zpy) {
    return index;
  }

  if (opmode == k_abs) {
    /* mov edx, [rdi + k_offset_jit_ptrs + (addr * 4)] */
    p_jit_buf[index++] = 0x8b;
    p_jit_buf[index++] = 0x97;
    index = jit_emit_int(p_jit_buf,
                         index,
                         k_offset_jit_ptrs +
                             (opcode_addr_6502 * sizeof(unsigned int)));
  } else if (opmode == k_abx) {
    /* movzx r8, bl */
    p_jit_buf[index++] = 0x4c;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0xc3;
    /* mov edx, [rdi + k_offset_jit_ptrs + r8*4 + (addr * 4)] */
    p_jit_buf[index++] = 0x42;
    p_jit_buf[index++] = 0x8b;
    p_jit_buf[index++] = 0x94;
    p_jit_buf[index++] = 0x87;
    index = jit_emit_int(p_jit_buf,
                         index,
                         k_offset_jit_ptrs +
                             (opcode_addr_6502 * sizeof(unsigned int)));
  } else if (opmode == k_aby) {
    /* movzx r8, cl */
    p_jit_buf[index++] = 0x4c;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0xc1;
    /* mov edx, [rdi + k_offset_jit_ptrs + r8*4 + (addr * 4)] */
    p_jit_buf[index++] = 0x42;
    p_jit_buf[index++] = 0x8b;
    p_jit_buf[index++] = 0x94;
    p_jit_buf[index++] = 0x87;
    index = jit_emit_int(p_jit_buf,
                         index,
                         k_offset_jit_ptrs +
                             (opcode_addr_6502 * sizeof(unsigned int)));
  } else if (opmode == k_idy || opmode == k_aby_dyn) {
    /* lea dx, [rdx + rcx] */
    p_jit_buf[index++] = 0x66;
    p_jit_buf[index++] = 0x8d;
    p_jit_buf[index++] = 0x14;
    p_jit_buf[index++] = 0x0a;
    /* mov edx, [rdi + k_offset_jit_ptrs + rdx*4] */
    p_jit_buf[index++] = 0x8b;
    p_jit_buf[index++] = 0x54;
    p_jit_buf[index++] = 0x97;
    p_jit_buf[index++] = k_offset_jit_ptrs;
  } else if (opmode == k_abx_dyn) {
    /* lea dx, [rdx + rbx] */
    p_jit_buf[index++] = 0x66;
    p_jit_buf[index++] = 0x8d;
    p_jit_buf[index++] = 0x14;
    p_jit_buf[index++] = 0x1a;
    /* mov edx, [rdi + k_offset_jit_ptrs + rdx*4] */
    p_jit_buf[index++] = 0x8b;
    p_jit_buf[index++] = 0x54;
    p_jit_buf[index++] = 0x97;
    p_jit_buf[index++] = k_offset_jit_ptrs;
  } else {
    assert(opmode == k_idx);
    /* mov edx, [rdi + k_offset_jit_ptrs + rdx*4] */
    p_jit_buf[index++] = 0x8b;
    p_jit_buf[index++] = 0x54;
    p_jit_buf[index++] = 0x97;
    p_jit_buf[index++] = k_offset_jit_ptrs;
  }

  /* mov WORD PTR [rdx], 0x17ff */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0xc7;
  p_jit_buf[index++] = 0x02;
  index = jit_emit_do_jit(p_jit_buf, index);

  return index;
}

static size_t
jit_single(struct jit_struct* p_jit,
           struct util_buffer* p_buf,
           uint16_t addr_6502,
           int dynamic_operand) {
  unsigned int jit_flags = p_jit->jit_flags;
  unsigned char* p_mem = p_jit->p_mem;
  unsigned char* p_jit_buf = util_buffer_get_ptr(p_buf);

  uint16_t addr_6502_plus_1 = addr_6502 + 1;
  uint16_t addr_6502_plus_2 = addr_6502 + 2;

  unsigned char opcode = jit_get_opcode(p_jit, addr_6502);
  unsigned char operand1 = p_mem[addr_6502_plus_1];
  unsigned char operand2 = p_mem[addr_6502_plus_2];
  uint16_t addr_6502_relative_jump = (int) addr_6502 + 2 + (char) operand1;

  unsigned char opmode = g_opmodes[opcode];
  unsigned char optype = g_optypes[opcode];
  unsigned char opmem = g_opmem[optype];
  unsigned char oplen = g_opmodelens[opmode];
  size_t index = util_buffer_get_pos(p_buf);
  size_t num_6502_bytes = oplen;
  size_t n_count = 1;

  uint16_t opcode_addr_6502;
  uint16_t opcode_addr_6502_upper_range;
  int special = 0;

  if (oplen < 3) {
    /* Clear operand2 if we're not using it. This enables us to re-use the
     * same x64 opcode generation code for both k_zpg and k_abs.
     */
    operand2 = 0;
  }

  opcode_addr_6502 = (operand2 << 8) | operand1;

  if (dynamic_operand) {
    switch (opmode) {
    case k_imm:
      opmode = k_imm_dyn;
      opcode_addr_6502 = addr_6502_plus_1;
      break;
    case k_abx:
      opmode = k_abx_dyn;
      break;
    case k_aby:
      opmode = k_aby_dyn;
      break;
    default:
      assert(0);
    }
  }

  switch (opmode) {
  case k_zpx:
    index = jit_emit_zp_x_to_scratch(p_jit_buf, index, opcode_addr_6502);
    break;
  case k_zpy:
    index = jit_emit_zp_y_to_scratch(p_jit_buf, index, opcode_addr_6502);
    break;
  case k_abx:
    index = jit_emit_abs_x_to_scratch(p_jit_buf, index, opcode_addr_6502);
    break;
  case k_aby:
    index = jit_emit_abs_y_to_scratch(p_jit_buf, index, opcode_addr_6502);
    break;
  case k_idy:
    index = jit_emit_ind_y_to_scratch(p_jit,
                                      p_jit_buf,
                                      index,
                                      opcode_addr_6502);
    break;
  case k_idx:
    index = jit_emit_ind_x_to_scratch(p_jit,
                                      p_jit_buf,
                                      index,
                                      opcode_addr_6502);
    break;
  case k_abx_dyn:
  case k_aby_dyn:
    index = jit_emit_abs_x_dyn_to_scratch(p_jit,
                                          p_jit_buf,
                                          index,
                                          addr_6502_plus_1);
    break;
  default:
    break;
  }

  opcode_addr_6502_upper_range = 0;
  if (opmode == k_abs) {
    opcode_addr_6502_upper_range = opcode_addr_6502;
  } else if ((opmode == k_abx || opmode == k_aby) &&
             opcode_addr_6502 <= 0xff00) {
    opcode_addr_6502_upper_range = opcode_addr_6502 + 0xff;
  }
  if ((opmem == k_read || opmem == k_rw) &&
      jit_is_special_read_address(p_jit,
                                  opcode_addr_6502,
                                  opcode_addr_6502_upper_range)) {
    special = 1;
  }
  if ((opmem == k_write || opmem == k_rw) &&
      jit_is_special_write_address(p_jit,
                                   opcode_addr_6502,
                                   opcode_addr_6502_upper_range)) {
    special = 1;
  }
  if (special && (opmem == k_read || opmem == k_rw)) {
    index = jit_emit_special_read(p_jit,
                                  opcode_addr_6502,
                                  opmode,
                                  p_jit_buf,
                                  index);
  }

  /* Handle merging repeated shift / rotate instructions. */
  if (jit_flags & k_jit_flag_merge_ops) {
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
      /* nop */ /* Opcodes byte length must be at least 2. */
      p_jit_buf[index++] = 0x90;
      /* ret */
      p_jit_buf[index++] = 0xc3;
      break;
    case 0x12:
      /* Illegal opcode. Hangs a standard 6502. */
      /* Generate a debug trap and continue. */
      /* nop */ /* Opcodes byte length must be at least 2. */
      p_jit_buf[index++] = 0x90;
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
  case k_unk:
    switch (opcode) {
    case 0x04:
      /* NOP zp */
      /* nop */ /* Opcodes byte length must be at least 2. */
      p_jit_buf[index++] = 0x90;
      /* nop */
      p_jit_buf[index++] = 0x90;
      break;
    default:
      index = jit_emit_undefined(p_jit_buf, index, opcode, addr_6502);
      break;
    }
    break;
  case k_brk:
    /* BRK */
    index = jit_emit_do_interrupt(p_jit, p_jit_buf, index, addr_6502 + 2, 1);
    break;
  case k_ora:
    /* ORA */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             opcode_addr_6502,
                             special,
                             0x0a);
    break;
  case k_asl:
    /* ASL */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              opcode_addr_6502,
                              special,
                              0xe0,
                              n_count);
    index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
    break;
  case k_php:
    /* PHP */
    index = jit_emit_flags_to_scratch(p_jit_buf, index, 1);
    index = jit_emit_push_from_scratch(p_jit_buf, index);
    break;
  case k_bpl:
    /* BPL */
    /* jns */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x89);
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
    if (special) {
      /* mov ah, dl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xd4;
    } else {
      /* mov ah, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x24;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
    }

    /* Bit 14 of eax is bit 6 of ah, where we get the OF from. */
    /* bt eax, 14 */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xe0;
    p_jit_buf[index++] = 14;
    index = jit_emit_carry_to_6502_overflow(p_jit_buf, index);

    /* Set ZF. */
    /* test ah, al */
    p_jit_buf[index++] = 0x84;
    p_jit_buf[index++] = 0xc4;
    /* sete dl */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0x94;
    p_jit_buf[index++] = 0xc2;
    /* x64 ZF is bit 6 in flags. */
    /* shl dl, 6 */
    p_jit_buf[index++] = 0xc0;
    p_jit_buf[index++] = 0xe2;
    p_jit_buf[index++] = 6;

    /* Set NF. With a trick: NF is bit 7 in both x64 and 6502 flags. */
    /* and ah, 0x80 */
    p_jit_buf[index++] = 0x80;
    p_jit_buf[index++] = 0xe4;
    p_jit_buf[index++] = 0x80;

    /* Put new flags into Intel flags. */
    /* or ah, dl */
    p_jit_buf[index++] = 0x08;
    p_jit_buf[index++] = 0xd4;
    /* sahf */
    p_jit_buf[index++] = 0x9e;
    break;
  case k_and:
    /* AND */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             opcode_addr_6502,
                             special,
                             0x22);
    break;
  case k_rol:
    /* ROL */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              opcode_addr_6502,
                              special,
                              0xd0,
                              n_count);
    index = jit_emit_post_rotate(p_jit,
                                 p_jit_buf,
                                 index,
                                 opmode,
                                 special,
                                 opcode_addr_6502);
    break;
  case k_plp:
    /* PLP */
    index = jit_emit_pull_to_scratch(p_jit_buf, index);
    index = jit_emit_set_flags(p_jit_buf, index);
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502 + 1, 1);
    break;
  case k_bmi:
    /* BMI */
    /* js */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x88);
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
                             opcode_addr_6502,
                             special,
                             0x32);
    break;
  case k_lsr:
    /* LSR */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              opcode_addr_6502,
                              special,
                              0xe8,
                              n_count);
    index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
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
    /* jae / jnc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x83);
    break;
  case k_cli:
    /* CLI */
    /* btr r13, 2 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xf5;
    p_jit_buf[index++] = 0x02;
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502 + 1, 0);
    break;
  case k_rti:
    index = jit_emit_pull_to_scratch(p_jit_buf, index);
    index = jit_emit_set_flags(p_jit_buf, index);
    index = jit_emit_pull_to_scratch_word(p_jit_buf, index);
    index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
    break;
  case k_rts:
    /* RTS */
    index = jit_emit_pull_to_scratch_word(p_jit_buf, index);
    /* lea dx, [rdx + 1] */
    p_jit_buf[index++] = 0x66;
    p_jit_buf[index++] = 0x8d;
    p_jit_buf[index++] = 0x52;
    p_jit_buf[index++] = 0x01;
    index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
    break;
  case k_adc:
    /* ADC */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             opcode_addr_6502,
                             special,
                             0x12);
    index = jit_emit_intel_to_6502_co(p_jit_buf, index);
    break;
  case k_ror:
    /* ROR */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              opcode_addr_6502,
                              special,
                              0xd8,
                              n_count);
    index = jit_emit_post_rotate(p_jit,
                                 p_jit_buf,
                                 index,
                                 opmode,
                                 special,
                                 opcode_addr_6502);
    break;
  case k_pla:
    /* PLA */
    index = jit_emit_pull_to_a(p_jit_buf, index);
    break;
  case k_bvs:
    /* BVS */
    index = jit_emit_test_overflow(p_jit_buf, index);
    /* jb / jc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x82);
    break;
  case k_sei:
    /* SEI */
    index = jit_emit_sei(p_jit_buf, index);
    break;
  case k_sta:
    /* STA */
    if (special) {
      /* mov dl, al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xc2;
      break;
    }
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
      break;
    case k_idy:
    case k_aby_dyn:
      if (jit_flags & k_jit_flag_no_rom_fault) {
        /* lea dx, [rdx + rcx] */
        p_jit_buf[index++] = 0x66;
        p_jit_buf[index++] = 0x8d;
        p_jit_buf[index++] = 0x14;
        p_jit_buf[index++] = 0x0a;
        /* bt edx, 15 */
        p_jit_buf[index++] = 0x0f;
        p_jit_buf[index++] = 0xba;
        p_jit_buf[index++] = 0xe2;
        p_jit_buf[index++] = 0x0f;
        /* jb / jc + 6 */
        p_jit_buf[index++] = 0x72;
        p_jit_buf[index++] = 0x06;
        /* mov [rdx + p_mem], al */
        p_jit_buf[index++] = 0x88;
        p_jit_buf[index++] = 0x82;
        index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      } else {
        /* mov [rdx + rcx], al */
        p_jit_buf[index++] = 0x88;
        p_jit_buf[index++] = 0x04;
        p_jit_buf[index++] = 0x0a;
      }
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
    case k_abx_dyn:
      /* mov [rdx + rbx], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x1a;
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_sty:
    /* STY */
    if (special) {
      /* mov dl, cl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xca;
      break;
    }
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
    if (special) {
      /* mov dl, bl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xda;
      break;
    }
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
    break;
  case k_txa:
    /* TXA */
    /* mov al, bl */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xd8;
    break;
  case k_bcc:
    /* BCC */
    index = jit_emit_test_carry(p_jit_buf, index);
    /* jae / jnc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x83);
    break;
  case k_tya:
    /* TYA */
    /* mov al, cl */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc8;
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
    if (special) {
      /* mov cl, dl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xd1;
      break;
    }
    switch (opmode) {
    case k_imm:
      /* mov cl, op1 */
      p_jit_buf[index++] = 0xb1;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
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
    break;
  case k_ldx:
    /* LDX */
    if (special) {
      /* mov bl, dl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xd3;
      break;
    }
    switch (opmode) {
    case k_imm:
      /* mov bl, op1 */
      p_jit_buf[index++] = 0xb3;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
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
      p_jit_buf[index++] = 0x99;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_lda:
    /* LDA */
    if (special) {
      /* mov al, dl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xd0;
      break;
    }
    switch (opmode) {
    case k_imm:
      /* mov al, op1 */
      p_jit_buf[index++] = 0xb0;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
    case k_imm_dyn:
      /* mov al, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_idy:
    case k_aby_dyn:
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
    case k_abx_dyn:
      /* mov al, [rdx + rbx] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x1a;
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_tay:
    /* TAY */
    /* mov cl, al */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc1;
    break;
  case k_tax:
    /* TAX */
    /* mov bl, al */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc3;
    break;
  case k_bcs:
    /* BCS */
    index = jit_emit_test_carry(p_jit_buf, index);
    /* jb / jc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x82);
    break;
  case k_clv:
    /* CLV */
    /* mov r12b, 0 */
    p_jit_buf[index++] = 0x41;
    p_jit_buf[index++] = 0xb4;
    p_jit_buf[index++] = 0x00;
    break;
  case k_tsx:
    /* TSX */
    /* mov bl, sil */
    p_jit_buf[index++] = 0x40;
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xf3;
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
    index = jit_emit_intel_to_6502_sub_carry(p_jit_buf, index);
    break;
  case k_cmp:
    /* CMP */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             opcode_addr_6502,
                             special,
                             0x3a);
    index = jit_emit_intel_to_6502_sub_carry(p_jit_buf, index);
    break;
  case k_dec:
    /* DEC */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              opcode_addr_6502,
                              special,
                              0xc8,
                              0);
    break;
  case k_iny:
    /* INY */
    /* inc cl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc1;
    break;
  case k_dex:
    /* DEX */
    /* dec bl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xcb;
    break;
  case k_bne:
    /* BNE */
    /* jne */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x85);
    break;
  case k_cld:
    /* CLD */
    /* btr r13, 3 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xf5;
    p_jit_buf[index++] = 0x03;
    break;
  case k_sed:
    printf("ignoring SED!\n");
    /* nop */ /* Opcodes byte length must be at least 2. */
    p_jit_buf[index++] = 0x90;
    /* nop */
    p_jit_buf[index++] = 0x90;
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
    index = jit_emit_intel_to_6502_sub_carry(p_jit_buf, index);
    break;
  case k_inc:
    /* INC */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              opcode_addr_6502,
                              special,
                              0xc0,
                              0);
    break;
  case k_inx:
    /* INX */
    /* inc bl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc3;
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
                             opcode_addr_6502,
                             special,
                             0x1a);
    index = jit_emit_intel_to_6502_sub_co(p_jit_buf, index);
    break;
  case k_nop:
    /* NOP */
    /* nop */ /* Opcodes byte length must be at least 2. */
    p_jit_buf[index++] = 0x90;
    /* nop */
    p_jit_buf[index++] = 0x90;
    break;
  case k_beq:
    /* BEQ */
    /* je */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x84);
    break;
  default:
    assert(0);
    break;
  }

  if (special && (opmem == k_write || opmem == k_rw)) {
    index = jit_emit_special_write(p_jit,
                                   opcode_addr_6502,
                                   opmode,
                                   p_jit_buf,
                                   index);
  }

  /* Writes to memory invalidate the JIT there. */
  index = jit_handle_invalidate(p_jit,
                                opmem,
                                opmode,
                                opcode_addr_6502,
                                p_jit_buf,
                                index);

  util_buffer_set_pos(p_buf, index);

  return num_6502_bytes;
}

static uint16_t
jit_6502_addr_from_intel(struct jit_struct* p_jit, unsigned char* intel_rip) {
  size_t block_addr_6502;
  size_t addr_6502;
  size_t intel_rip_masked;

  unsigned char* p_jit_base = p_jit->p_jit_base;

  /* -2 because of the call [rdi] opcode size. */
  intel_rip -= 2;

  block_addr_6502 = (intel_rip - p_jit_base);
  block_addr_6502 >>= k_jit_bytes_shift;

  assert(block_addr_6502 < k_bbc_addr_space_size);

  addr_6502 = block_addr_6502;
  intel_rip_masked = ((size_t) intel_rip) & k_jit_bytes_mask;

  if (intel_rip_masked) {
    int found = 0;
    while (addr_6502 < 0xffff) {
      if ((unsigned char*) (size_t) p_jit->jit_ptrs[addr_6502] == intel_rip) {
        found = 1;
        break;
      }
      addr_6502++;
    }
    assert(found);
  }

  return (uint16_t) addr_6502;
}

uint16_t
jit_block_from_6502(struct jit_struct* p_jit, uint16_t addr_6502) {
  size_t block_addr_6502;

  unsigned char* p_jit_ptr =
      (unsigned char*) (size_t) p_jit->jit_ptrs[addr_6502];
  size_t size_t_jit_ptr = (size_t) p_jit_ptr;

  size_t_jit_ptr -= (size_t) p_jit->p_jit_base;
  block_addr_6502 = size_t_jit_ptr >> k_jit_bytes_shift;
  assert(block_addr_6502 <= 0xffff);

  return block_addr_6502;
}

static int
jit_is_invalidation_sequence(unsigned char* p_jit_ptr) {
  /* call [rdi] */
  if (p_jit_ptr[0] == 0xff && p_jit_ptr[1] == 0x17) {
    return 1;
  }
  return 0;
}

int
jit_has_code(struct jit_struct* p_jit, uint16_t addr_6502) {
  return p_jit->has_code[addr_6502];
}

static int
jit_is_valid_block_start(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr;
  uint16_t block_addr_6502 = jit_block_from_6502(p_jit, addr_6502);
  if (block_addr_6502 != addr_6502) {
    return 0;
  }
  p_jit_ptr = p_jit->p_jit_base;
  p_jit_ptr += (addr_6502 << k_jit_bytes_shift);
  if (jit_is_invalidation_sequence(p_jit_ptr)) {
    return 0;
  }
  return 1;
}

static int
jit_has_invalidated_code(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr =
      (unsigned char*) (size_t) p_jit->jit_ptrs[addr_6502];
  if (jit_is_invalidation_sequence(p_jit_ptr)) {
    return 1;
  }
  return 0;
}

static void
jit_addr_invalidate(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr =
      (unsigned char*) (size_t) p_jit->jit_ptrs[addr_6502];

  (void) jit_emit_do_jit(p_jit_ptr, 0);
}

void
jit_memory_written(struct jit_struct* p_jit, uint16_t addr_6502) {
  jit_addr_invalidate(p_jit, addr_6502);
}

static void
jit_at_addr(struct jit_struct* p_jit,
            struct util_buffer* p_buf,
            uint16_t addr_6502) {
  unsigned char single_jit_buf[k_jit_bytes_per_byte];
  uint16_t block_addr_6502;

  size_t total_num_ops = 0;
  size_t total_6502_bytes = 0;
  uint16_t start_addr_6502 = addr_6502;
  unsigned char curr_nz_flags = 0;
  int jumps_always = 0;
  int emit_debug = 0;
  int emit_dynamic_operand = 0;
  unsigned char* p_jit_buf = util_buffer_get_ptr(p_buf);
  struct util_buffer* p_single_buf = p_jit->p_single_buf;
  unsigned int jit_flags = p_jit->jit_flags;

  if (jit_flags & k_jit_flag_debug) {
    emit_debug = 1;
  }
  if (jit_flags & k_jit_flag_dynamic_operand) {
    emit_dynamic_operand = 1;
  }

  /* This opcode may be compiled into part of a previous block, so make sure to
   * invalidate that block.
   */
  block_addr_6502 = jit_block_from_6502(p_jit, start_addr_6502);
  jit_addr_invalidate(p_jit, block_addr_6502);

  do {
    unsigned char opcode_6502;
    unsigned char optype;
    unsigned char opcode_6502_next;
    unsigned char optype_next;
    size_t intel_opcodes_len;
    size_t buf_left;
    unsigned char new_nz_flags;
    size_t num_6502_bytes;
    size_t i;

    unsigned char* p_dst = util_buffer_get_base_address(p_buf) +
                           util_buffer_get_pos(p_buf);
    int dynamic_operand = 0;

    assert((size_t) p_dst < 0xffffffff);

    if (jit_is_valid_block_start(p_jit, addr_6502)) {
      break;
    }

    util_buffer_setup(p_single_buf, single_jit_buf, k_jit_bytes_per_byte);
    util_buffer_set_base_address(p_single_buf, p_dst);

    opcode_6502 = jit_get_opcode(p_jit, addr_6502);
    optype = g_optypes[opcode_6502];
    new_nz_flags = g_nz_flag_pending[optype];

    /* If we're compiling the same opcode on top of an existing invalidated
     * opcode, apply a self-modifying optimization to try and avoid future
     * re-compilations.
     */
    if (emit_dynamic_operand &&
        jit_has_invalidated_code(p_jit, addr_6502) &&
        opcode_6502 == p_jit->compiled_opcode[addr_6502]) {
      unsigned char opmode = g_opmodes[opcode_6502];
      if ((optype == k_lda || optype == k_sta) &&
          (opmode == k_abx || opmode == k_aby || opmode == k_imm)) {
        dynamic_operand = 1;
      }
    }

    if (emit_debug) {
      jit_emit_debug_sequence(p_single_buf, addr_6502);
    }

    num_6502_bytes = jit_single(p_jit,
                                p_single_buf,
                                addr_6502,
                                dynamic_operand);

    opcode_6502_next = jit_get_opcode(p_jit, addr_6502 + num_6502_bytes);
    optype_next = g_optypes[opcode_6502_next];

    /* See if we need to load the 6502 NZ flags. */
    if ((emit_debug || g_nz_flags_needed[optype_next]) &&
        new_nz_flags != k_set) {
      unsigned char commit_nz_flags = 0;
      if (new_nz_flags != 0) {
        commit_nz_flags = new_nz_flags;
        new_nz_flags = 0;
      } else if (curr_nz_flags != 0) {
        commit_nz_flags = curr_nz_flags;
      }
      if (commit_nz_flags != 0) {
        size_t index = util_buffer_get_pos(p_single_buf);
        index = jit_emit_do_zn_flags(single_jit_buf,
                                     index,
                                     commit_nz_flags - 1);
        util_buffer_set_pos(p_single_buf, index);
      }
    }

    intel_opcodes_len = util_buffer_get_pos(p_single_buf);
    /* For us to be able to JIT invalidate correctly, all Intel sequences must
     * be at least 2 bytes because the invalidation sequence is 2 bytes.
     */
    assert(intel_opcodes_len >= 2);

    buf_left = util_buffer_remaining(p_buf);
    /* TODO: don't hardcode a guess at flag lazy load + jmp length. */
    if (buf_left < intel_opcodes_len + 2 + 4 + 4 + 5) {
      break;
    }

    if (g_opbranch[optype] == k_bra_y) {
      jumps_always = 1;
    }

    if (new_nz_flags == k_set) {
      new_nz_flags = 0;
    }
    curr_nz_flags = new_nz_flags;

    total_6502_bytes += num_6502_bytes;
    total_num_ops++;

    /* Store where the Intel code is for each 6502 opcode, so we can invalidate
     * Intel JIT on 6502 writes.
     * Note that invalidation for operands can be disabled if we're lifted
     * self-modification into inline resolution.
     * Also record the last compiled opcode at a given address.
     */
    for (i = 0; i < num_6502_bytes; ++i) {
      unsigned char* p_jit_ptr = p_dst;
      p_jit->has_code[addr_6502] = 1;
      if (i == 0) {
        p_jit->compiled_opcode[addr_6502] = opcode_6502;
      } else {
        p_jit->compiled_opcode[addr_6502] = 0;
        if (dynamic_operand) {
          p_jit_ptr = (p_jit->p_jit_base + (0xffff << k_jit_bytes_shift));
        }
      }
      jit_addr_invalidate(p_jit, addr_6502);
      p_jit->jit_ptrs[addr_6502] = (unsigned int) (size_t) p_jit_ptr;
      addr_6502++;
    }

    util_buffer_append(p_buf, p_single_buf);

    if (jumps_always) {
      break;
    }
  } while (1);

  assert(total_num_ops > 0);
/*printf("addr %x - %x, total_num_ops: %zu\n",
       start_addr_6502,
       addr_6502 - 1,
       total_num_ops);
fflush(stdout);*/

  if (jumps_always) {
    return;
  }

  /* See if we need to lazy load the 6502 NZ flags. */
  if (curr_nz_flags != 0) {
    size_t index = util_buffer_get_pos(p_buf);
    index = jit_emit_do_zn_flags(p_jit_buf, index, curr_nz_flags - 1);
    util_buffer_set_pos(p_buf, index);
  }

  (void) jit_emit_jmp_6502_addr(p_jit,
                                p_buf,
                                start_addr_6502 + total_6502_bytes,
                                0xe9,
                                0);
}

static void
jit_callback(struct jit_struct* p_jit, unsigned char* intel_rip) {
  unsigned char* p_jit_ptr;
  uint16_t addr_6502;
  unsigned char jit_bytes[k_jit_bytes_per_byte];

  struct util_buffer* p_buf = p_jit->p_seq_buf;

  addr_6502 = jit_6502_addr_from_intel(p_jit, intel_rip);

  /* Executing within the zero page and stack page is trapped.
   * By default, for performance, writes to these pages do not invalidate
   * related JIT code.
   * Upon hitting this trap, we could re-JIT everything in a new mode that does
   * invalidate JIT upon writes to these addresses. This is unimplemented.
   */
/*  assert(block_addr_6502 >= 0x200);*/

  p_jit_ptr = p_jit->p_jit_base + (addr_6502 << k_jit_bytes_shift);

  util_buffer_setup(p_buf, &jit_bytes[0], k_jit_bytes_per_byte);
  util_buffer_set_base_address(p_buf, p_jit_ptr);

  jit_at_addr(p_jit, p_buf, addr_6502);

  util_buffer_setup(p_jit->p_dest_buf, p_jit_ptr, k_jit_bytes_per_byte);
  util_buffer_append(p_jit->p_dest_buf, p_buf);

  p_jit->reg_pc = addr_6502;
}

static void
sigsegv_reraise(void) {
  struct sigaction sa;

  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = SIG_DFL;
  (void) sigaction(SIGSEGV, &sa, NULL);
  (void) raise(SIGSEGV);
  _exit(1);
}

static void
handle_sigsegv(int signum, siginfo_t* p_siginfo, void* p_void) {
  size_t rip_inc;

  unsigned char* p_addr = (unsigned char*) p_siginfo->si_addr;
  ucontext_t* p_context = (ucontext_t*) p_void;
  unsigned char* p_rip = (unsigned char*) p_context->uc_mcontext.gregs[REG_RIP];

  (void) signum;
  (void) p_siginfo;
  (void) p_void;

  /* Crash unless it's fault trying to write to the ROM region of BBC memory. */
  if (signum != SIGSEGV ||
      p_siginfo->si_code != SEGV_ACCERR ||
      p_addr < (unsigned char*) (size_t) k_bbc_mem_mmap_addr + k_bbc_ram_size ||
      p_addr >= (unsigned char*) (size_t) k_bbc_mem_mmap_addr +
               k_bbc_addr_space_size) {
    sigsegv_reraise();
  }

  /* Crash if it's in the registers region. */
  if (p_addr >= (unsigned char*) (size_t) k_bbc_mem_mmap_addr +
                k_bbc_registers_start &&
      p_addr < (unsigned char*) (size_t) k_bbc_mem_mmap_addr +
               k_bbc_registers_start +
               k_bbc_registers_len) {
    sigsegv_reraise();
  }

  /* Crash unless the faulting instruction in the JIT region. */
  if (p_rip < (unsigned char*) k_jit_addr ||
      p_rip >= (unsigned char*) k_jit_addr +
               (k_bbc_addr_space_size << k_jit_bytes_shift)) {
    sigsegv_reraise();
  }

  /* Ok, it's a write fault in the ROM region. We can continue.
   * To continue, we need to bump rip along!
   */
  /* STA ($xx),Y */
  if (p_rip[0] == 0x88 && p_rip[1] == 0x04 && p_rip[2] == 0x0a) {
    rip_inc = 3;
  } else {
    sigsegv_reraise();
  }

  p_context->uc_mcontext.gregs[REG_RIP] += rip_inc;
}

void
jit_enter(struct jit_struct* p_jit) {
  int ret;
  struct sigaction sa;
  unsigned char* p_mem = p_jit->p_mem;

  /* Ah the horrors, a SIGSEGV handler! This actually enables a ton of
   * optimizations by using faults for very uncommon conditions, such that the
   * fast path doesn't need certain checks.
   */
  memset(&sa, '\0', sizeof(sa));
  sa.sa_sigaction = handle_sigsegv;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  ret = sigaction(SIGSEGV, &sa, NULL);
  if (ret != 0) {
    errx(1, "sigaction failed");
  }

  /* The memory must be aligned to at least 0x10000 so that our register access
   * tricks work.
   */
  assert(((size_t) p_mem & 0xffff) == 0);

  asm volatile (
    /* Pass a pointer to the jit_struct in rdi. */
    "mov %0, %%rdi;"
    /* al, bl, cl, sil are 6502 A, X, Y, S. */
    /* ebx, ecx, esi point to real Intel virtual RAM backing the 6502 RAM. */
    "xor %%eax, %%eax;"
    "xor %%ebx, %%ebx;"
    "xor %%ecx, %%ecx;"
    "xor %%esi, %%esi;"
    /* rdx, r8, r9 are scratch. */
    "xor %%edx, %%edx;"
    "xor %%r8, %%r8;"
    "xor %%r9, %%r9;"
    /* r12 is overflow flag. */
    "xor %%r12, %%r12;"
    /* r13 is the rest of the 6502 flags or'ed together. */
    /* Bit 2 is interrupt disable. */
    /* Bit 3 is decimal mode. */
    "xor %%r13, %%r13;"
    /* r14 is carry flag. */
    "xor %%r14, %%r14;"
    /* Call regs_util to set all the 6502 registers and flags.
     * 6502 start address left in edx.
     * Offset must match struct jit_struct layout.
     */
    "mov %%rdi, %%r15;"
    "call *8(%%rdi);"
    /* Calculate address of Intel JIT code for the 6502 execution address. */
    /* Constants here must match. */
    "mov $8, %%r8;"
    "shlx %%r8d, %%edx, %%edx;"
    "lea 0x20000000(%%edx), %%edx;"
    "call *%%rdx;"
    :
    : "g" (p_jit)
    : "rax", "rbx", "rcx", "rdx", "rdi", "rsi",
      "r8", "r9", "r12", "r13", "r14", "r15"
  );
}

struct jit_struct*
jit_create(void* p_debug_callback,
           struct debug_struct* p_debug,
           struct bbc_struct* p_bbc,
           void* p_read_callback,
           void* p_write_callback,
           const char* p_opt_flags) {
  unsigned int jit_flags;
  unsigned char* p_jit_base;
  unsigned char* p_utils_base;
  unsigned char* p_util_debug;
  unsigned char* p_util_regs;
  unsigned char* p_util_jit;

  unsigned char* p_mem = (unsigned char*) (size_t) k_bbc_mem_mmap_addr;
  struct jit_struct* p_jit = malloc(sizeof(struct jit_struct));
  if (p_jit == NULL) {
    errx(1, "cannot allocate jit_struct");
  }
  memset(p_jit, '\0', sizeof(struct jit_struct));

  p_jit_base = util_get_guarded_mapping(
      k_jit_addr,
      k_bbc_addr_space_size * k_jit_bytes_per_byte,
      1);

  p_jit->reg_x_ebx = (unsigned int) (size_t) p_mem;
  p_jit->reg_y_ecx = (unsigned int) (size_t) p_mem;
  p_jit->reg_s_esi = ((unsigned int) (size_t) p_mem) + 0x100;

  p_utils_base = util_get_guarded_mapping(k_utils_addr, k_utils_size, 1);
  p_util_debug = p_utils_base + k_utils_debug_offset;
  p_util_regs = p_utils_base + k_utils_regs_offset;
  p_util_jit = p_utils_base + k_utils_jit_offset;

  p_jit->p_mem = p_mem;
  p_jit->p_jit_base = p_jit_base;
  p_jit->p_utils_base = p_utils_base;
  p_jit->p_util_debug = p_util_debug;
  p_jit->p_util_regs = p_util_regs;
  p_jit->p_util_jit = p_util_jit;
  p_jit->p_debug = p_debug;
  p_jit->p_debug_callback = p_debug_callback;
  p_jit->p_jit_callback = jit_callback;
  p_jit->p_bbc = p_bbc;
  p_jit->p_read_callback = p_read_callback;
  p_jit->p_write_callback = p_write_callback;

  jit_flags = k_jit_flag_merge_ops |
              k_jit_flag_self_modifying |
              k_jit_flag_dynamic_operand;
  if (strstr(p_opt_flags, "jit:no-rom-fault")) {
    jit_flags |= k_jit_flag_no_rom_fault;
  }
  p_jit->jit_flags = jit_flags;

  /* int3 */
  memset(p_jit_base, '\xcc', k_bbc_addr_space_size * k_jit_bytes_per_byte);
  size_t addr_6502 = 0;
  while (addr_6502 < k_bbc_addr_space_size) {
    (void) jit_emit_do_jit(p_jit_base, 0);

    p_jit->jit_ptrs[addr_6502] = (unsigned int) (size_t) p_jit_base;
    jit_addr_invalidate(p_jit, addr_6502);

    p_jit_base += k_jit_bytes_per_byte;
    addr_6502++;
  }

  jit_emit_debug_util(p_util_debug);
  jit_emit_regs_util(p_jit, p_util_regs);
  jit_emit_jit_util(p_jit, p_util_jit);

  p_jit->p_dest_buf = util_buffer_create();
  p_jit->p_seq_buf = util_buffer_create();
  p_jit->p_single_buf = util_buffer_create();

  return p_jit;
}

void
jit_set_debug(struct jit_struct* p_jit, int debug) {
  p_jit->jit_flags &= ~k_jit_flag_debug;
  if (debug) {
    p_jit->jit_flags |= k_jit_flag_debug;
  }
}

void
jit_get_registers(struct jit_struct* p_jit,
                  unsigned char* a,
                  unsigned char* x,
                  unsigned char* y,
                  unsigned char* s,
                  unsigned char* flags,
                  uint16_t* pc) {
  *a = (unsigned char) p_jit->reg_a_eax;
  *x = (unsigned char) p_jit->reg_x_ebx;
  *y = (unsigned char) p_jit->reg_y_ecx;
  *s = (unsigned char) p_jit->reg_s_esi;
  *flags = p_jit->reg_6502_flags;
  *pc = p_jit->reg_pc;
}

void
jit_set_registers(struct jit_struct* p_jit,
                  unsigned char a,
                  unsigned char x,
                  unsigned char y,
                  unsigned char s,
                  unsigned char flags,
                  uint16_t pc) {
  *((unsigned char*) &p_jit->reg_a_eax) = a;
  *((unsigned char*) &p_jit->reg_x_ebx) = x;
  *((unsigned char*) &p_jit->reg_y_ecx) = y;
  *((unsigned char*) &p_jit->reg_s_esi) = s;
  p_jit->reg_6502_flags = flags;
  p_jit->reg_pc = pc;
}

void
jit_check_pc(struct jit_struct* p_jit) {
  /* Some consistency checks to make sure our basic block tracking is ok. */
  uint16_t reg_pc = p_jit->reg_pc;
  unsigned int reg_rip = p_jit->reg_rip;
  unsigned int jit_ptr = p_jit->jit_ptrs[reg_pc];

  /* -7 because the debug sequence call rip is +7 bytes from the start of
   * the 6502 instruction.
   */
  assert(reg_rip - 7 == jit_ptr);
}

void
jit_destroy(struct jit_struct* p_jit) {
  util_free_guarded_mapping(p_jit->p_jit_base,
                            k_bbc_addr_space_size * k_jit_bytes_per_byte);
  util_free_guarded_mapping(p_jit->p_utils_base, k_utils_size);
  util_buffer_destroy(p_jit->p_dest_buf);
  util_buffer_destroy(p_jit->p_seq_buf);
  util_buffer_destroy(p_jit->p_single_buf);
  free(p_jit);
}
