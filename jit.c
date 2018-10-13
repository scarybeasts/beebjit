#define _GNU_SOURCE /* For REG_RIP */

#include "jit.h"

#include "bbc.h"
#include "debug.h"
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
static void* k_tables_addr = (void*) 0x0f000000;
static void* k_semaphores_addr = (void*) 0x0e000000;
static const size_t k_semaphore_block = 0;
static const size_t k_semaphore_cli = 4096;
static const size_t k_semaphore_cli_end_minus_4 = (4096 * 2) - 4;
static const size_t k_semaphore_cli_read_only = 4096 * 2;
static const size_t k_utils_size = 4096;
static const size_t k_utils_debug_offset = 0;
static const size_t k_utils_regs_offset = 0x100;
static const size_t k_utils_jit_offset = 0x200;
static const size_t k_utils_do_interrupt_offset = 0x300;
static const size_t k_tables_size = 4096;
static const size_t k_semaphore_size = 4096;
static const size_t k_semaphores_size = 4096 * 3;

static const int k_offset_util_jit = 0;
static const int k_offset_util_regs = 8;
static const int k_offset_util_debug = 16;
static const int k_offset_util_do_interrupt = 24;

static const int k_offset_reg_rip = 32;
static const int k_offset_reg_a_eax = 32 + 4;
static const int k_offset_reg_x_ebx = 32 + 8;
static const int k_offset_reg_y_ecx = 32 + 12;
static const int k_offset_reg_s_esi = 32 + 16;
static const int k_offset_reg_pc = 32 + 20;
static const int k_offset_reg_x64_flags = 32 + 22;
static const int k_offset_reg_6502_flags = 32 + 23;
static const int k_offset_irq = 32 + 24;

static const int k_offset_debug_callback = 64;
static const int k_offset_jit_callback = 64 + 8;
static const int k_offset_read_callback = 64 + 16;
static const int k_offset_write_callback = 64 + 24;

static const int k_offset_debug = 96;
static const int k_offset_bbc = 96 + 8;
static const int k_offset_counter = 96 + 16;
static const int k_offset_jit_ptrs = 96 + 24;

static const unsigned int k_log_flag_self_modify = 1;
static const unsigned int k_log_flag_compile = 2;

static const size_t k_max_6502_bytes = 8;

enum {
  /* k_a = 1 from opdefs.h */
  /* k_x = 2 from opdefs.h */
  /* k_y = 3 from opdefs.h */
  /* In host CPU flags. */
  k_flags = 4,
  /* In host CPU flags, but inverted (used for carry, think SBC vc. sbb). */
  k_finv = 5,
  k_reg = 6,
};

enum {
  k_flag_unknown = 1,
  k_flag_set = 2,
  k_flag_clear = 3,
};

/* k_a: PLA, TXA, TYA, LDA */
/* k_x: LDX, TAX, TSX */
/* k_y: LDY, TAY */
static const unsigned char g_nz_flags_location[k_6502_op_num_types] = {
  0      , 0      , 0      , k_flags, k_flags, 0      , 0      , 0      ,
  0      , k_flags, k_flags, k_flags, k_flags, 0      , 0      , k_flags,
  k_flags, k_flags, 0      , 0      , 0      , 0      , 0      , k_flags,
  k_a    , k_flags, 0      , 0      , 0      , 0      , 0      , k_flags,
  k_a    , 0      , k_a    , 0      , k_y    , k_a    , k_x    , k_y    ,
  k_x    , 0      , 0      , k_x    , k_flags, k_flags, k_flags, k_flags,
  k_flags, k_flags, 0      , 0      , k_flags, k_flags, 0      , k_flags,
  0      , 0      ,
};

static const unsigned char g_nz_flags_needed[k_6502_op_num_types] = {
  0, 0, 1, 0, 0, 1, 1, 0, /* BRK, PHP, BPL */
  1, 0, 0, 0, 0, 1, 0, 0, /* JSR, BMI */
  0, 0, 0, 1, 1, 1, 1, 0, /* JMP, BVC, CLI, RTS */
  0, 0, 1, 0, 0, 0, 0, 0, /* BVS */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCC */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCS */
  0, 0, 1, 0, 0, 0, 0, 0, /* BNE */
  1, 0,                   /* BEQ */
};

/* k_reg: CLC, PLP, ROL, SEC, RTI, ROR */
/* k_flags: ASL, LSR, ADC */
/* k_finv: CPY, CMP, CPX, SBC */
static const unsigned char g_carry_flag_location[k_6502_op_num_types] = {
  0      , 0      , 0      , 0      , k_flags, 0      , 0      , k_reg  ,
  0      , 0      , 0      , k_reg  , k_reg  , 0      , k_reg  , k_reg  ,
  0      , k_flags, 0      , 0      , 0      , 0      , 0      , k_flags,
  0      , k_reg  , 0      , 0      , 0      , 0      , 0      , 0      ,
  0      , 0      , 0      , 0      , 0      , 0      , 0      , 0      ,
  0      , 0      , 0      , 0      , k_finv , k_finv , k_finv , 0      ,
  0      , 0      , 0      , 0      , k_finv , 0      , 0      , 0      ,
  0      , 0      ,
};

/* k_reg: BIT, PLP, RTI, CLV */
/* k_flags: ADC, SBC */
static const unsigned char g_overflow_flag_location[k_6502_op_num_types] = {
  0      , 0      , 0      , 0      , 0      , 0      , 0      , 0      ,
  0      , 0      , k_reg  , k_reg  , 0      , 0      , 0      , k_reg  ,
  0      , 0      , 0      , 0      , 0      , 0      , 0      , k_flags,
  0      , 0      , 0      , 0      , 0      , 0      , 0      , 0      ,
  0      , 0      , 0      , 0      , 0      , 0      , 0      , 0      ,
  0      , 0      , k_reg  , 0      , 0      , 0      , 0      , 0      ,
  0      , 0      , 0      , 0      , k_flags, 0      , 0      , 0      ,
  0      , 0      ,
};

/* TODO: BIT trashing carry is just an internal wart to fix. */
/* This table tracks which 6502 opcodes need us to have the carry and overflow
 * flags safely stored in a register.
 * ORA / AND / EOR may be surprising at first but the underlying Intel
 * instructions do not preserve the Intel carry or overflow flag.
 */
static const unsigned char g_carry_flag_needed_in_reg[k_6502_op_num_types] = {
  0, 0, 1, 1, 0, 1, 1, 0, /* BRK, ORA, PHP, BPL */
  1, 1, 1, 0, 0, 1, 0, 0, /* JSR, AND, BIT, BMI */
  1, 0, 0, 1, 1, 1, 1, 0, /* EOR, JMP, BVC, CLI, RTS */
  0, 0, 1, 0, 0, 0, 0, 0, /* BVS */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCC */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCS */
  0, 0, 1, 0, 0, 0, 0, 0, /* BNE */
  1, 0,                   /* BEQ */
};

static const unsigned char g_overflow_flag_needed_in_reg[k_6502_op_num_types] =
{
  0, 0, 1, 1, 1, 1, 1, 0, /* BRK, ORA, ASL, PHP, BPL */
  1, 1, 0, 0, 1, 1, 0, 0, /* JSR, AND, ROL, BMI */
  1, 1, 0, 1, 1, 1, 1, 0, /* EOR, LSR, JMP, BVC, CLI, RTS */
  0, 1, 1, 0, 0, 0, 0, 0, /* ROR, BVS */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCC */
  0, 1, 0, 0, 1, 1, 1, 0, /* BCS, CPY, CMP, CPX */
  0, 0, 1, 0, 0, 0, 0, 0, /* BNE */
  1, 0,                   /* BEQ */
};

static const unsigned char g_inverted_carry_flag_used[k_6502_op_num_types] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 1, 1, 1, 0, /* CPY, CMP, CPX */
  0, 0, 0, 0, 1, 0, 0, 0, /* SBC */
  0, 0,
};

struct jit_struct {
  /* Utilities called by JIT code via call [rdi + 0x??] */
  /* Must be at 0. */
  unsigned char* p_util_jit;    /* 0   */
  unsigned char* p_util_regs;
  unsigned char* p_util_debug;
  unsigned char* p_util_do_interrupt;

  /* Registers. */
  unsigned int reg_rip;         /* 32  */
  unsigned int reg_a_eax;
  unsigned int reg_x_ebx;
  unsigned int reg_y_ecx;
  unsigned int reg_s_esi;
  uint16_t reg_pc;
  unsigned char reg_x64_flags;
  unsigned char reg_6502_flags;
  unsigned char irq;

  /* C callbacks called by JIT code. */
  void* p_debug_callback;       /* 64  */
  void* p_jit_callback;
  void* p_read_callback;
  void* p_write_callback;

  /* Structures referenced by JIT code. */
  void* p_debug;                /* 96  */
  struct bbc_struct* p_bbc;
  size_t counter;
  unsigned int jit_ptrs[k_bbc_addr_space_size]; /* 120 */

  /* Fields not referenced by JIT'ed code. */
  unsigned int jit_flags;
  unsigned int log_flags;
  size_t max_num_ops;
  uint16_t debug_stop_addr;
  unsigned char* p_mem;
  size_t dummy_rom_offset;

  unsigned char* p_jit_base;
  unsigned char* p_utils_base;
  unsigned char* p_tables_base;
  unsigned char* p_semaphores;

  struct util_buffer* p_dest_buf;
  struct util_buffer* p_seq_buf;
  struct util_buffer* p_single_buf;

  unsigned char is_block_start[k_bbc_addr_space_size];
  unsigned char compiled_opcode[k_bbc_addr_space_size];
  unsigned char self_modify_optimize[k_bbc_addr_space_size];
  unsigned char compilation_pending[k_bbc_addr_space_size];
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
jit_emit_intel_to_6502_carry_oldapi(unsigned char* p_jit, size_t index) {
  /* setb r14b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc6;

  return index;
}

static void
jit_emit_intel_to_6502_carry(struct util_buffer* p_buf) {
  /* setb r14b */
  util_buffer_add_4b(p_buf, 0x41, 0x0f, 0x92, 0xc6);
}

static void
jit_emit_intel_to_6502_carry_inverted(struct util_buffer* p_buf) {
  /* setae r14b */
  util_buffer_add_4b(p_buf, 0x41, 0x0f, 0x93, 0xc6);
}

static void
jit_emit_intel_to_6502_overflow(struct util_buffer* p_buf) {
  /* seto r12b */
  util_buffer_add_4b(p_buf, 0x41, 0x0f, 0x90, 0xc4);
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

static void
jit_emit_do_zn_flags(struct util_buffer* p_buf, int reg) {
  assert(reg >= 0 && reg <= 2);
  if (reg == 0) {
    /* test al, al */
    util_buffer_add_2b(p_buf, 0x84, 0xc0);
  } else if (reg == 1) {
    /* test bl, bl */
    util_buffer_add_2b(p_buf, 0x84, 0xdb);
  } else if (reg == 2) {
    /* test cl, cl */
    util_buffer_add_2b(p_buf, 0x84, 0xc9);
  }
}

static void
jit_emit_6502_carry_to_intel(struct util_buffer* p_buf) {
  /* bt r14, 0 */
  util_buffer_add_5b(p_buf, 0x49, 0x0f, 0xba, 0xe6, 0x00);
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
  (void) p_jit;
  (void) opcode_addr_6502;

  /* Empty -- optimized for now but wrap-around will cause a fault. */
  return index;
}

static size_t
jit_emit_abs_y_to_scratch(unsigned char* p_jit,
                          size_t index,
                          uint16_t opcode_addr_6502) {
  (void) p_jit;
  (void) opcode_addr_6502;

  /* Empty -- optimized for now but wrap-around will cause a fault. */
  return index;
}

static size_t
jit_emit_ind_y_to_scratch(struct jit_struct* p_jit,
                          unsigned char* p_jit_buf,
                          size_t index,
                          uint16_t opcode_addr_6502) {
  if (opcode_addr_6502 == 0xff) {
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
    index = jit_emit_int(p_jit_buf,
                         index,
                         (size_t) p_jit->p_mem + opcode_addr_6502);
  }

  return index;
}

static size_t
jit_emit_zpg_dyn_to_scratch(struct jit_struct* p_jit,
                            unsigned char* p_jit_buf,
                            size_t index,
                            uint16_t opcode_addr_6502) {
  /* movzx edx, BYTE PTR [p_mem + addr] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb6;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x25;
  index = jit_emit_int(p_jit_buf,
                       index,
                       (size_t) p_jit->p_mem + opcode_addr_6502);

  return index;
}

static size_t
jit_emit_ind_x_to_scratch(struct jit_struct* p_jit,
                          unsigned char* p_jit_buf,
                          size_t index,
                          unsigned char operand1) {
  (void) p_jit;

  unsigned char operand1_inc = operand1 + 1;
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */

  /* mov r9, rbx */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xd9;
  /* lea r8, [rbx + operand1 + 1] */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x83;
  index = jit_emit_int(p_jit_buf, index, operand1_inc);
  /* mov r9b, r8b */
  p_jit_buf[index++] = 0x45;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xc1;
  /* movzx edx, BYTE PTR [r9] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb6;
  p_jit_buf[index++] = 0x11;
  /* mov dh, dl */
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd6;
  /* lea r8, [r8 - 1] */
  p_jit_buf[index++] = 0x4d;
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x40;
  p_jit_buf[index++] = 0xff;
  /* mov r9b, r8b */
  p_jit_buf[index++] = 0x45;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xc1;
  /* mov dl, BYTE PTR [r9] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x11;

  return index;
}

static size_t
jit_emit_zp_x_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* lea edx, [rbx + operand1] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x93;
  index = jit_emit_int(p_jit, index, operand1);
  /* movzx edx, dl */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd2;

  return index;
}

static size_t
jit_emit_zp_y_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* lea edx, [rcx + operand1] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x91;
  index = jit_emit_int(p_jit, index, operand1);
  /* movzx edx, dl */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd2;

  return index;
}

static size_t
jit_emit_ind_y_dyn_to_scratch(struct jit_struct* p_jit,
                              unsigned char* p_jit_buf,
                              size_t index,
                              uint16_t opcode_addr_6502) {
  /* movzx edx, BYTE PTR [p_mem + addr] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb6;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x25;
  index = jit_emit_int(p_jit_buf,
                       index,
                       (size_t) p_jit->p_mem + opcode_addr_6502);
  /* movzx edx, WORD PTR [rdx + p_mem] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x92;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);

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

unsigned char*
jit_get_jit_base_addr(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr = (p_jit->p_jit_base +
                              (addr_6502 * k_jit_bytes_per_byte));
  return p_jit_ptr;
}

static size_t
jit_emit_jmp_6502_addr(struct jit_struct* p_jit,
                       struct util_buffer* p_buf,
                       uint16_t new_addr_6502,
                       unsigned char opcode1,
                       unsigned char opcode2) {
  unsigned char* p_src_addr = util_buffer_get_base_address(p_buf) +
                              util_buffer_get_pos(p_buf);
  unsigned char* p_dst_addr = jit_get_jit_base_addr(p_jit, new_addr_6502);
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
  /* NOTE: uses BMI2 rorx instruction to avoid modifying flags. */
  /* Used to use shlx but that doesn't support immediates as the third operand,
   * and rorx does. So rorx can do it in one instruction.
   */
  /* rorx edx, edx, 24 */ /* BMI2 */
  p_jit[index++] = 0xc4;
  p_jit[index++] = 0xe3;
  p_jit[index++] = 0x7b;
  p_jit[index++] = 0xf0;
  p_jit[index++] = 0xd2;
  p_jit[index++] = 24;

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
jit_emit_push_word(unsigned char* p_jit_buf, size_t index, uint16_t addr_6502) {
  index = jit_emit_push_constant(p_jit_buf, index, (addr_6502 >> 8));
  index = jit_emit_push_constant(p_jit_buf, index, (addr_6502 & 0xff));

  return index;
}

static size_t
jit_emit_push_word_from_scratch(unsigned char* p_jit_buf, size_t index) {
  /* mov [rsi], dh */
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x36;
  index = jit_emit_stack_dec(p_jit_buf, index);
  index = jit_emit_push_from_scratch(p_jit_buf, index);

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
  index = jit_emit_intel_to_6502_carry_oldapi(p_jit_buf, index);
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
  /* The flags we need to save are negative, zero and carry, all covered by
   * lahf.
   */
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

  /* Save old PC. r8 preseved by next call. */
  /* mov r8, [rsp] */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x04;
  p_jit_buf[index++] = 0x24;

  /* Replace with new PC if there is one. */
  /* test rax, rax */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x85;
  p_jit_buf[index++] = 0xc0;
  /* cmovne r8, rax */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0x45;
  p_jit_buf[index++] = 0xc0;

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

  /* Pop the return value. */
  /* pop rdx */
  p_jit_buf[index++] = 0x5a;

  /* jmp r8 */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0xe0;
}

static void
jit_emit_regs_util(struct jit_struct* p_jit, unsigned char* p_jit_buf) {
  (void) p_jit;

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

static void
jit_emit_counter_sequence(struct util_buffer* p_buf) {
  /* mov rdx, [rdi + k_offset_counter] */
  util_buffer_add_4b(p_buf, 0x48, 0x8b, 0x57, k_offset_counter);
  /* lea rdx, [rdx - 1] */
  util_buffer_add_4b(p_buf, 0x48, 0x8d, 0x52, 0xff);
  /* mov [rdi + k_offset_counter], rdx */
  util_buffer_add_4b(p_buf, 0x48, 0x89, 0x57, k_offset_counter);
  /* bt rdx, 63 */
  util_buffer_add_5b(p_buf, 0x48, 0x0f, 0xba, 0xe2, 0x3f);
  /* jae / jnc + 1 */
  util_buffer_add_2b(p_buf, 0x73, 0x01);
  /* int 3 */
  util_buffer_add_1b(p_buf, 0xcc);
}

static int
jit_is_ram_address(struct jit_struct* p_jit, uint16_t addr_6502) {
  struct bbc_struct* p_bbc = p_jit->p_bbc;
  return bbc_is_ram_address(p_bbc, addr_6502);
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
  (void) p_jit;

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
  (void) p_jit;

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
  case k_imm_dyn:
    /* OP al, [p_mem + addr] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf,
                         index,
                         (size_t) p_jit->p_mem + opcode_addr_6502);
    break;
  case k_idy:
  case k_aby_dyn:
  case k_idy_dyn:
    /* OP al, [rcx + rdx] */
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
  case k_zpg_dyn:
  case k_abs_dyn:
    /* OP al, [rdx + p_mem] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x82;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    break;
  case k_abx_dyn:
    /* OP al, [rbx + rdx] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x13;
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
  case k_zpg_dyn:
  case k_abs_dyn:
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
  case k_abx_dyn:
    /* OP BYTE PTR [rbx + rdx], n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base - 0xbc;
    p_jit_buf[index++] = 0x13;
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
  if (opmode == k_nil) {
    /* Nothing. The NZ flags are now in the accumulator and we have the
     * machinery to pull them out if they are needed. The carry flag remains
     * in the processor carry flag.
     */
    return index;
  }
  index = jit_emit_intel_to_6502_carry_oldapi(p_jit_buf, index);
  if (special) {
    /* test dl, dl */
    p_jit_buf[index++] = 0x84;
    p_jit_buf[index++] = 0xd2;
    return index;
  }
  switch (opmode) {
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
  case k_zpg_dyn:
  case k_abs_dyn:
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
  case k_abx_dyn:
    /* test BYTE PTR [rbx + rdx], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x13;
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
  (void) p_jit;

  /* mov dx, addr_6502 */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0xba;
  p_jit_buf[index++] = (addr_6502 & 0xff);
  p_jit_buf[index++] = (addr_6502 >> 8);
  /* mov r9, is_brk */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0xc7;
  p_jit_buf[index++] = 0xc1;
  index = jit_emit_int(p_jit_buf, index, (is_brk * 0x10));
  /* jmp [rdi + k_offset_util_do_interrupt] */
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x67;
  p_jit_buf[index++] = k_offset_util_do_interrupt;

  return index;
}

static void
jit_emit_do_interrupt_util(struct jit_struct* p_jit, unsigned char* p_jit_buf) {
  uint16_t vector = k_bbc_vector_irq;
  size_t index = 0;

  index = jit_emit_push_word_from_scratch(p_jit_buf, index);
  index = jit_emit_flags_to_scratch(p_jit_buf, index, 0);
  /* Add in the BRK flag. */
  /* lea rdx, [rdx + r9] */
  p_jit_buf[index++] = 0x4a;
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x0a;
  index = jit_emit_push_from_scratch(p_jit_buf, index);
  index = jit_emit_sei(p_jit_buf, index);
  index = jit_emit_jmp_indirect(p_jit, p_jit_buf, index, vector);
}

static size_t
jit_emit_check_interrupt(struct jit_struct* p_jit,
                         unsigned char* p_jit_buf,
                         size_t index,
                         uint16_t addr_6502) {
  (void) p_jit;

  /* ABI in fault handler for the CLI semaphore is that rdx is set to the
   * 6502 interrupt return address.
   */
  /* mov edx, addr_6502 */
  p_jit_buf[index++] = 0xba;
  index = jit_emit_int(p_jit_buf, index, addr_6502);
  /* mov r8, 4 */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0xc7;
  p_jit_buf[index++] = 0xc0;
  index = jit_emit_int(p_jit_buf, index, 4);
  /* Exotic instruction alert! BMI2 */
  /* Transfers bit 2 (interrupt disable) to bit 0 without affecting flags. */
  /* pext r8d, r13d, r8d */
  p_jit_buf[index++] = 0xc4;
  p_jit_buf[index++] = 0x42;
  p_jit_buf[index++] = 0x12;
  p_jit_buf[index++] = 0xf5;
  p_jit_buf[index++] = 0xc0;
  /* Reads the very end of the semaphore page if interrupts are enabled,
   * i.e. fault if semaphore is raised and interrupts are enabled. If
   * interrupts are disabled, the next post-semaphore page is read and this
   * will never fault.
   */
  /* mov r8d, [r8*4 + k_semaphore_cli_end_minus_4] */
  p_jit_buf[index++] = 0x46;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x04;
  p_jit_buf[index++] = 0x85;
  index = jit_emit_int(p_jit_buf,
                       index,
                       (ssize_t) (k_semaphores_addr +
                                  k_semaphore_cli_end_minus_4));

  return index;
}

void
jit_set_interrupt(struct jit_struct* p_jit, int id, int set) {
  if (set) {
    p_jit->irq |= (1 << id);
  } else {
    p_jit->irq &= ~(1 << id);
  }
}

void
jit_set_counter(struct jit_struct* p_jit, size_t counter) {
  p_jit->counter = counter;
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

static void
jit_emit_block_prolog(struct jit_struct* p_jit, struct util_buffer* p_buf) {
  (void) p_jit;
  /* mov edx, [k_semaphore_block] */
  util_buffer_add_3b(p_buf, 0x8b, 0x14, 0x25);
  util_buffer_add_int(p_buf, (ssize_t) (k_semaphores_addr + k_semaphore_block));
}

static size_t
jit_handle_invalidate(struct jit_struct* p_jit,
                      unsigned char opmem,
                      unsigned char opmode,
                      uint16_t opcode_addr_6502,
                      unsigned char* p_jit_buf,
                      size_t index) {
  int abs_mode = (p_jit->jit_flags & k_jit_flag_self_modifying_abs);
  int all_mode = (p_jit->jit_flags & k_jit_flag_self_modifying_all);

  if (!abs_mode && !all_mode) {
    return index;
  }

  if (opmem != k_write && opmem != k_rw) {
    return index;
  }
  if (opmode == k_nil ||
      opmode == k_zpg ||
      opmode == k_zpx ||
      opmode == k_zpy ||
      opmode == k_zpg_dyn) {
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
    /* mov WORD PTR [rdx], 0x17ff */
    p_jit_buf[index++] = 0x66;
    p_jit_buf[index++] = 0xc7;
    p_jit_buf[index++] = 0x02;
    index = jit_emit_do_jit(p_jit_buf, index);

    return index;
  }

  if (!all_mode) {
    return index;
  }

  if (opmode == k_abx) {
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
  } else if (opmode == k_idy || opmode == k_aby_dyn || opmode == k_idy_dyn) {
    if (opmode == k_idy || opmode == k_idy_dyn) {
      /* lea dx, [rdx + rcx] */
      p_jit_buf[index++] = 0x66;
      p_jit_buf[index++] = 0x8d;
      p_jit_buf[index++] = 0x14;
      p_jit_buf[index++] = 0x0a;
    }
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
    assert(opmode == k_idx || opmode == k_abs_dyn);
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
           size_t max_6502_bytes,
           int dynamic_operand,
           int curr_carry_flag) {
  uint16_t opcode_addr_6502;
  uint16_t opcode_addr_6502_upper_range;

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
  uint16_t next_addr_6502 = addr_6502 + oplen;
  size_t index = util_buffer_get_pos(p_buf);
  size_t num_6502_bytes = oplen;
  size_t n_count = 1;
  int special = 0;
  unsigned char intel_opcode_base;

  if (oplen < 3) {
    /* Clear operand2 if we're not using it. This enables us to re-use the
     * same x64 opcode generation code for both k_zpg and k_abs.
     */
    operand2 = 0;
  }

  opcode_addr_6502 = (operand2 << 8) | operand1;

  if (dynamic_operand) {
    opcode_addr_6502 = addr_6502_plus_1;

    switch (opmode) {
    case k_nil:
      assert(0);
      break;
    case k_rel:
      assert(0);
      break;
    case k_imm:
      opmode = k_imm_dyn;
      break;
    case k_zpg:
      opmode = k_zpg_dyn;
      break;
    case k_abs:
      opmode = k_abs_dyn;
      break;
    case k_abx:
      opmode = k_abx_dyn;
      break;
    case k_aby:
      opmode = k_aby_dyn;
      break;
    case k_idy:
      opmode = k_idy_dyn;
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
  case k_abs_dyn:
  case k_abx_dyn:
  case k_aby_dyn:
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
  case k_zpg_dyn:
    index = jit_emit_zpg_dyn_to_scratch(p_jit,
                                        p_jit_buf,
                                        index,
                                        opcode_addr_6502);
    break;
  case k_idy_dyn:
    index = jit_emit_ind_y_dyn_to_scratch(p_jit,
                                          p_jit_buf,
                                          index,
                                          opcode_addr_6502);
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
        while (n_count < 7 &&
               num_6502_bytes < max_6502_bytes &&
               p_mem[next_addr_6502] == opcode) {
          n_count++;
          next_addr_6502++;
          num_6502_bytes++;
        }
      }
    }
  }

/* TODO: consider enabling this optimization, which eliminates a Galaforce
   delay loop.
   Need to make sure the opcode change from DEY to LDY doesn't confuse the
   lazy flag setting logic.
  if (optype == k_dey &&
      p_mem[next_addr_6502] == 0xd0 &&
      p_mem[(uint16_t) (next_addr_6502 + 1)] == 0xfd) {
printf("ooh\n");
    optype = k_ldy;
    opmode = k_imm;
    operand1 = 0;
    num_6502_bytes += 2;
  }
*/

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
      /* mov al, [0x00000042] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf, index, 0xdead);
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
    index = jit_emit_push_word(p_jit_buf, index, addr_6502 + 2);
    util_buffer_set_pos(p_buf, index);
    if (opmode == k_abs) {
      index = jit_emit_jmp_6502_addr(p_jit, p_buf, opcode_addr_6502, 0xe9, 0);
    } else {
      assert(opmode == k_abs_dyn);
      index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
    }
    break;
  case k_bit:
    /* BIT */
    /* Only has zp and abs. */
    assert(opmode == k_zpg ||
           opmode == k_abs ||
           opmode == k_zpg_dyn ||
           opmode == k_abs_dyn);
    if (special) {
      /* mov ah, dl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0xd4;
    } else if (opmode == k_abs_dyn || opmode == k_zpg_dyn) {
      /* mov ah, [rdx + p_mem] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0xa2;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
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
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502 + 1);
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
    } else if (opmode == k_ind) {
      index = jit_emit_jmp_indirect(p_jit,
                                    p_jit_buf,
                                    index,
                                    opcode_addr_6502);
    } else {
      assert(opmode == k_abs_dyn);
      index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
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
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502 + 1);
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
    if (curr_carry_flag != k_flag_clear) {
      /* adc */
      intel_opcode_base = 0x12;
    } else {
      /* add */
      intel_opcode_base = 0x02;
    }
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             opcode_addr_6502,
                             special,
                             intel_opcode_base);
    break;
  case k_ror:
    /* ROR */
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
    case k_idy_dyn:
      /* mov [rcx + rdx + dummy_rom_offset], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x84;
      p_jit_buf[index++] = 0x11;
      index = jit_emit_int(p_jit_buf,
                           index,
                           p_jit->dummy_rom_offset);
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
    case k_idx:
      /* mov [rdx + p_mem + offset], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x82;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + p_jit->dummy_rom_offset);
      break;
    case k_zpx:
    case k_abs_dyn:
    case k_zpg_dyn:
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
    case k_abs_dyn:
    case k_zpg_dyn:
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
    case k_abs_dyn:
    case k_zpg_dyn:
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
    case k_imm_dyn:
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
    case k_abx_dyn:
      /* mov cl, [rbx + rdx] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x13;
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
    case k_imm_dyn:
      /* mov bl, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpy:
    case k_abs_dyn:
    case k_zpg_dyn:
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
    case k_idy_dyn:
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
    case k_abs_dyn:
    case k_zpg_dyn:
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
    case k_imm_dyn:
      /* cmp bl, [p_mem + addr] */
      p_jit_buf[index++] = 0x3a;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpg_dyn:
      /* cmp bl, [rdx + p_mem] */
      p_jit_buf[index++] = 0x3a;
      p_jit_buf[index++] = 0x9a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
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
    if (curr_carry_flag != k_flag_set) {
      /* sbb */
      intel_opcode_base = 0x1a;
    } else {
      /* sub */
      intel_opcode_base = 0x2a;
    }
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             opcode_addr_6502,
                             special,
                             intel_opcode_base);
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

  block_addr_6502 = (intel_rip - p_jit_base);
  block_addr_6502 >>= k_jit_bytes_shift;

  assert(block_addr_6502 < k_bbc_addr_space_size);

  addr_6502 = block_addr_6502;
  intel_rip_masked = ((size_t) intel_rip) & k_jit_bytes_mask;

  if (intel_rip_masked) {
    int found = 0;
    while (addr_6502 < 0xffff) {
      unsigned char* p_jit_ptr = jit_get_code_ptr(p_jit, addr_6502);
      if (p_jit_ptr == intel_rip) {
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

  unsigned char* p_jit_ptr = jit_get_code_ptr(p_jit, addr_6502);
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

void
jit_init_addr(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);
  (void) jit_emit_do_jit(p_jit_ptr, 0);

  p_jit->jit_ptrs[addr_6502] = (unsigned int) (size_t) p_jit_ptr;
}

int
jit_has_code(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr = jit_get_code_ptr(p_jit, addr_6502);
  /* The "no code" state is the initial state, which is the code pointer
   * pointing at the block prolog of a block.
   */
  if (((size_t) p_jit_ptr) & k_jit_bytes_mask) {
    return 1;
  }
  assert(p_jit_ptr == jit_get_jit_base_addr(p_jit, addr_6502));
  return 0;
}

int
jit_is_block_start(struct jit_struct* p_jit, uint16_t addr_6502) {
  return p_jit->is_block_start[addr_6502];
}

unsigned char*
jit_get_code_ptr(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned int ptr = p_jit->jit_ptrs[addr_6502];

  return ((unsigned char*) (size_t) ptr);
}

int
jit_has_invalidated_code(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr = jit_get_code_ptr(p_jit, addr_6502);
  /* If there's no code at all, return false. */
  if (!(((size_t) p_jit_ptr) & k_jit_bytes_mask)) {
    return 0;
  }
  if (jit_is_invalidation_sequence(p_jit_ptr)) {
    return 1;
  }
  return 0;
}

int
jit_jump_target_is_invalidated(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);
  if (jit_is_invalidation_sequence(p_jit_ptr)) {
    return 1;
  }
  return 0;
}

int
jit_has_self_modify_optimize(struct jit_struct* p_jit, uint16_t addr_6502) {
  return p_jit->self_modify_optimize[addr_6502];
}

int
jit_is_compilation_pending(struct jit_struct* p_jit, uint16_t addr_6502) {
  return p_jit->compilation_pending[addr_6502];
}

static void
jit_invalidate_addr(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr = jit_get_code_ptr(p_jit, addr_6502);

  (void) jit_emit_do_jit(p_jit_ptr, 0);
}

static void
jit_invalidate_jump_target(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);

  (void) jit_emit_do_jit(p_jit_ptr, 0);
}

void
jit_memory_written(struct jit_struct* p_jit, uint16_t addr_6502) {
  jit_invalidate_addr(p_jit, addr_6502);
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
  int curr_nz_flags_location = k_flags;
  int curr_carry_flag_value = k_flag_unknown;
  int curr_carry_flag_location = k_reg;
  int curr_overflow_flag_location = k_reg;
  int jumps_always = 0;
  int emit_dynamic_operand = 0;
  int elim_nz_flag_tests = 0;
  int elim_co_flag_tests = 1;
  int log_self_modify = 0;
  int is_compilation_pending = 0;
  int has_code = 0;
  int is_invalidated_code = 0;
  int is_invalidated_block = 0;
  int is_new = 0;
  int is_split = 0;
  int is_size_stop = 0;
  int is_jump_stop = 0;
  int is_block_stop = 0;
  struct util_buffer* p_single_buf = p_jit->p_single_buf;
  unsigned int jit_flags = p_jit->jit_flags;
  unsigned int log_flags = p_jit->log_flags;
  size_t max_num_ops = p_jit->max_num_ops;

  if (jit_flags & k_jit_flag_dynamic_operand) {
    emit_dynamic_operand = 1;
  }
  if (jit_flags & k_jit_flag_elim_nz_flag_tests) {
    elim_nz_flag_tests = 1;
  }
  if (!(jit_flags & k_jit_flag_batch_ops)) {
    max_num_ops = 1;
  }

  if (log_flags & k_log_flag_self_modify) {
    log_self_modify = 1;
  }

  block_addr_6502 = jit_block_from_6502(p_jit, start_addr_6502);

  is_compilation_pending = jit_is_compilation_pending(p_jit, start_addr_6502);
  has_code = jit_has_code(p_jit, start_addr_6502);
  is_invalidated_code = jit_has_invalidated_code(p_jit, start_addr_6502);
  is_invalidated_block = jit_jump_target_is_invalidated(p_jit, start_addr_6502);

  if (!is_compilation_pending) {
    if (!has_code) {
      is_new = 1;
    } else if (!is_invalidated_code) {
      is_split = 1;
    }
  }

  if (is_new || is_split) {
    p_jit->is_block_start[start_addr_6502] = 1;
  }

  /* This opcode may be compiled into part of a previous block, so make sure to
   * invalidate that block.
   */
  if (block_addr_6502 != start_addr_6502 &&
      !jit_jump_target_is_invalidated(p_jit, block_addr_6502)) {
    jit_invalidate_jump_target(p_jit, block_addr_6502);
    p_jit->compilation_pending[block_addr_6502] = 1;
  }

  jit_emit_block_prolog(p_jit, p_buf);

  do {
    unsigned char opcode_6502;
    unsigned char optype;
    unsigned char opmode;
    size_t intel_opcodes_len;
    size_t buf_left;
    size_t num_6502_bytes;
    size_t i;
    size_t max_6502_bytes;
    int new_nz_flags_location;
    int new_carry_flag_location;
    int new_overflow_flag_location;
    int carry_flag_expectation;
    int effective_carry_flag_location;

    struct debug_struct* p_debug = p_jit->p_debug;
    unsigned char* p_dst = util_buffer_get_base_address(p_buf) +
                           util_buffer_get_pos(p_buf);
    int dynamic_operand = 0;
    int emit_debug = 0;
    int emit_counter = 0;

    assert((size_t) p_dst < 0xffffffff);

    if (debug_active_at_addr(p_debug, addr_6502)) {
      emit_debug = 1;
    }
    if (debug_counter_at_addr(p_debug, addr_6502)) {
      emit_counter = 1;
    }

    util_buffer_setup(p_single_buf, single_jit_buf, k_jit_bytes_per_byte);
    util_buffer_set_base_address(p_single_buf, p_dst);

    opcode_6502 = jit_get_opcode(p_jit, addr_6502);
    optype = g_optypes[opcode_6502];
    opmode = g_opmodes[opcode_6502];
    new_nz_flags_location = g_nz_flags_location[optype];
    new_carry_flag_location = g_carry_flag_location[optype];
    new_overflow_flag_location = g_overflow_flag_location[optype];
    effective_carry_flag_location = curr_carry_flag_location;

    /* Special case: the nil mode for ROL / ROR also doesn't test the
     * flags immediately as an optimization.
     */
    if ((optype == k_rol || optype == k_ror) && opmode == k_nil) {
      new_nz_flags_location = k_a;
      new_carry_flag_location = k_flags;
    }

    /* If we're compiling the same opcode on top of an existing invalidated
     * opcode, mark the location as self-modify optimize.
     */
    if (jit_has_invalidated_code(p_jit, addr_6502)) {
      unsigned char old_opcode_6502 = p_jit->compiled_opcode[addr_6502];
      if (opcode_6502 == old_opcode_6502) {
        p_jit->self_modify_optimize[addr_6502] = 1;
        if (log_self_modify) {
          printf("JIT: self-modified opcode match location %.4x, opcode %.2x\n",
                 addr_6502,
                 opcode_6502);
        }
      } else if (log_self_modify) {
        printf("JIT: self-modified opcode MISMATCH "
               "location %.4x, opcode %.2x, old %.2x\n",
               addr_6502,
               opcode_6502,
               old_opcode_6502);
      }
    }
    if (opcode_6502 != p_jit->compiled_opcode[addr_6502]) {
      p_jit->self_modify_optimize[addr_6502] = 0;
    }
    if (opmode == k_rel || opmode == k_nil) {
      p_jit->self_modify_optimize[addr_6502] = 0;
    }

    /* Try and emit a self-modifying optimization if appropriate. */
    if (emit_dynamic_operand &&
        jit_has_self_modify_optimize(p_jit, addr_6502)) {
      dynamic_operand = 1;
    }

    /* See if we need to sync the C flag from host flag to host register.
     * Must be done before the NZ flags sync because that trashes the carry
     * flag.
     */
    if ((g_carry_flag_needed_in_reg[optype] ||
         emit_debug ||
         !elim_co_flag_tests) &&
        effective_carry_flag_location != k_reg) {
      if (effective_carry_flag_location == k_flags) {
        jit_emit_intel_to_6502_carry(p_single_buf);
      } else if (effective_carry_flag_location == k_finv) {
        jit_emit_intel_to_6502_carry_inverted(p_single_buf);
      }
      if (new_carry_flag_location == 0) {
        new_carry_flag_location = k_reg;
      }
      effective_carry_flag_location = k_reg;
    }
    /* See if we need to sync the O flag from host flag to host register.
     * Must be done before the NZ flags sync because that trashes the overflow
     * flag.
     */
    if ((g_overflow_flag_needed_in_reg[optype] ||
         emit_debug ||
         !elim_co_flag_tests) &&
        curr_overflow_flag_location != k_reg) {
      jit_emit_intel_to_6502_overflow(p_single_buf);
      if (new_overflow_flag_location == 0) {
        new_overflow_flag_location = k_reg;
      }
    }

    /* See if we need to sync the NZ flags from 6502 register to host flags. */
    if ((g_nz_flags_needed[optype] || emit_debug || !elim_nz_flag_tests) &&
        curr_nz_flags_location != k_flags) {
      jit_emit_do_zn_flags(p_single_buf, curr_nz_flags_location - 1);
      if (new_nz_flags_location == 0) {
        new_nz_flags_location = k_flags;
      }
    }

    if (emit_debug) {
      jit_emit_debug_sequence(p_single_buf, addr_6502);
    }
    if (emit_counter) {
      jit_emit_counter_sequence(p_single_buf);
    }

    carry_flag_expectation = k_flags;
    if (g_inverted_carry_flag_used[optype]) {
      carry_flag_expectation = k_finv;
    }
    if (g_optype_uses_carry[optype] &&
        effective_carry_flag_location != carry_flag_expectation) {
      int is_inverted = 0;
      if (effective_carry_flag_location == k_finv) {
        is_inverted = 1;
      }
      if (curr_carry_flag_value != k_flag_unknown) {
        int value = 1;
        if (curr_carry_flag_value == k_flag_clear) {
          value = 0;
        }
        if (g_inverted_carry_flag_used[optype]) {
          value ^= 1;
        }
        if (value) {
          /* stc */
          util_buffer_add_1b(p_single_buf, 0xf9);
        } else {
          /* clc */
          util_buffer_add_1b(p_single_buf, 0xf8);
        }
      } else {
        if (effective_carry_flag_location == k_reg) {
          jit_emit_6502_carry_to_intel(p_single_buf);
        }
        if (is_inverted ^ g_inverted_carry_flag_used[optype]) {
          /* cmc */
          util_buffer_add_1b(p_single_buf, 0xf5);
        }
      }
    }

    /* This is a lookahead for block starts. It's used to stop instruction
     * coalescing in some cases to preserve block boundaries and prevent
     * continual recompilations.
     */
    for (max_6502_bytes = 1;
         max_6502_bytes < k_max_6502_bytes;
         ++max_6502_bytes) {
      if (jit_is_block_start(p_jit, addr_6502 + max_6502_bytes)) {
        break;
      }
    }

    num_6502_bytes = jit_single(p_jit,
                                p_single_buf,
                                addr_6502,
                                max_6502_bytes,
                                dynamic_operand,
                                curr_carry_flag_value);
    assert(num_6502_bytes > 0);
    assert(num_6502_bytes < k_max_6502_bytes);

    intel_opcodes_len = util_buffer_get_pos(p_single_buf);
    /* For us to be able to JIT invalidate correctly, all Intel sequences must
     * be at least 2 bytes because the invalidation sequence is 2 bytes.
     */
    assert(intel_opcodes_len >= 2);

    buf_left = util_buffer_remaining(p_buf);
    /* TODO: don't hardcode a guess at flag lazy load + jmp length. */
    /* 4 for carry flag sync, 4 for overflow flag sync, 2 for NZ flag sync,
     * 5 for jump.
     */
    if (buf_left < intel_opcodes_len + 4 + 4 + 2 + 5 ||
        total_num_ops == max_num_ops) {
      p_jit->compilation_pending[addr_6502] = 1;
      is_size_stop = 1;
      break;
    }

    if (g_opbranch[optype] == k_bra_y) {
      jumps_always = 1;
    }

    if (new_nz_flags_location == 0) {
      /* Nothing. nz flags status unaffected by opcode. */
    } else {
      curr_nz_flags_location = new_nz_flags_location;
    }
    if (new_carry_flag_location == 0) {
      /* Nothing. Carry flag status unaffected by opcode. */
    } else {
      curr_carry_flag_location = new_carry_flag_location;
    }
    if (new_overflow_flag_location == 0) {
      /* Nothing. Overflow flag status unaffected by opcode. */
    } else {
      curr_overflow_flag_location = new_overflow_flag_location;
    }

    if (optype == k_clc) {
      curr_carry_flag_value = k_flag_clear;
    } else if (optype == k_sec) {
      curr_carry_flag_value = k_flag_set;
    } else if (g_optype_changes_carry[optype]) {
      curr_carry_flag_value = k_flag_unknown;
    }

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
      p_jit->compilation_pending[addr_6502] = 0;

      jit_invalidate_jump_target(p_jit, addr_6502);

      if (i == 0) {
        p_jit->compiled_opcode[addr_6502] = opcode_6502;
      } else {
        p_jit->compiled_opcode[addr_6502] = 0;
        p_jit->self_modify_optimize[addr_6502] = 0;
        p_jit->is_block_start[addr_6502] = 0;
        if (dynamic_operand) {
          p_jit_ptr = jit_get_jit_base_addr(p_jit, 0xffff) + 7;
        }
      }
      p_jit->jit_ptrs[addr_6502] = (unsigned int) (size_t) p_jit_ptr;
      addr_6502++;
    }

    util_buffer_append(p_buf, p_single_buf);

    if (jumps_always) {
      is_jump_stop = 1;
      break;
    }
    if (jit_is_block_start(p_jit, addr_6502)) {
      is_block_stop = 1;
      break;
    }
  } while (1);

  assert(total_num_ops > 0);
  if (log_flags & k_log_flag_compile) {
    printf("JIT: compile address %.4x - %.4x, total_num_ops: %zu",
           start_addr_6502,
           addr_6502 - 1,
           total_num_ops);
    if (is_new) {
      printf(" [new]");
    }
    if (is_split) {
      printf(" [split]");
    }
    if (is_compilation_pending) {
      printf(" [pend]");
    }
    if (is_invalidated_code) {
      printf(" [inv:code]");
    }
    if (is_invalidated_block) {
      printf(" [inv:block]");
    }
    if (is_size_stop) {
      printf(" [s:size]");
    }
    if (is_jump_stop) {
      printf(" [s:jump]");
    }
    if (is_block_stop) {
      printf(" [s:block]");
    }
    printf("\n");
  }

  if (jumps_always) {
    return;
  }

  /* See if we need to lazy sync the 6502 carry flag. */
  if (curr_carry_flag_location == k_flags) {
    jit_emit_intel_to_6502_carry(p_buf);
  } else if (curr_carry_flag_location == k_finv) {
    jit_emit_intel_to_6502_carry_inverted(p_buf);
  }
  /* See if we need to lazy sync the 6502 overflow flag. */
  if (curr_overflow_flag_location == k_flags) {
    jit_emit_intel_to_6502_overflow(p_buf);
  }
  /* See if we need to lazy sync the 6502 NZ flags. */
  if (curr_nz_flags_location != k_flags) {
    jit_emit_do_zn_flags(p_buf, curr_nz_flags_location - 1);
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

  /* -2 because of the call [rdi] instruction sequence size. */
  intel_rip -= 2;

  assert(jit_is_invalidation_sequence(intel_rip));

  addr_6502 = jit_6502_addr_from_intel(p_jit, intel_rip);

  /* Executing within the zero page and stack page is trapped.
   * By default, for performance, writes to these pages do not invalidate
   * related JIT code.
   * Upon hitting this trap, we could re-JIT everything in a new mode that does
   * invalidate JIT upon writes to these addresses. This is unimplemented.
   */
/*  assert(block_addr_6502 >= 0x200);*/

  p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);

  util_buffer_setup(p_buf, &jit_bytes[0], k_jit_bytes_per_byte);
  util_buffer_set_base_address(p_buf, p_jit_ptr);

  jit_at_addr(p_jit, p_buf, addr_6502);

  util_buffer_setup(p_jit->p_dest_buf, p_jit_ptr, k_jit_bytes_per_byte);
  util_buffer_append(p_jit->p_dest_buf, p_buf);

  p_jit->reg_pc = addr_6502;
}

void
jit_async_timer_tick(struct jit_struct* p_jit) {
  util_make_mapping_none(p_jit->p_semaphores + k_semaphore_block,
                         k_semaphore_size);
}

static void
jit_sync_timer_tick(struct jit_struct* p_jit) {
  struct bbc_struct* p_bbc = p_jit->p_bbc;
  bbc_sync_timer_tick(p_bbc);
}

static void
jit_safe_hex_convert(unsigned char* p_buf, unsigned char* p_ptr) {
  size_t i;
  size_t val = (size_t) p_ptr;
  for (i = 0; i < 8; ++i) {
    unsigned char c1 = (val & 0x0f);
    unsigned char c2 = ((val & 0xf0) >> 4);

    if (c1 < 10) {
      c1 = '0' + c1;
    } else {
      c1 = 'a' + (c1 - 10);
    }
    if (c2 < 10) {
      c2 = '0' + c2;
    } else {
      c2 = 'a' + (c2 - 10);
    }

    p_buf[16 - 2 - (i * 2) + 1] = c1;
    p_buf[16 - 2 - (i * 2) ] = c2;

    val >>= 8;
  }
}

static void
sigsegv_reraise(unsigned char* p_rip, unsigned char* p_addr) {
  struct sigaction sa;
  unsigned char hex_buf[16];

  static const char* p_msg = "SIGSEGV: rip ";
  static const char* p_msg2 = ", addr ";
  static const char* p_msg3 = "\n";

  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = SIG_DFL;
  (void) sigaction(SIGSEGV, &sa, NULL);

  (void) write(2, p_msg, strlen(p_msg));
  jit_safe_hex_convert(&hex_buf[0], p_rip);
  (void) write(2, hex_buf, sizeof(hex_buf));
  (void) write(2, p_msg2, strlen(p_msg2));
  jit_safe_hex_convert(&hex_buf[0], p_addr);
  (void) write(2, hex_buf, sizeof(hex_buf));
  (void) write(2, p_msg3, strlen(p_msg3));

  (void) raise(SIGSEGV);
  _exit(1);
}

static void
handle_sigsegv_fire_interrupt(ucontext_t* p_context,
                              struct jit_struct* p_jit,
                              uint16_t addr_6502) {
  /* ABI is rdx for 6502 RTI address, r9 == 0 for BRK == 0. */
  p_context->uc_mcontext.gregs[REG_RDX] = addr_6502;
  p_context->uc_mcontext.gregs[REG_R9] = 0;
  p_context->uc_mcontext.gregs[REG_RIP] = (size_t) p_jit->p_util_do_interrupt;
}

static void
handle_block_semaphore_sigsegv(ucontext_t* p_context, unsigned char* p_rip) {
  struct jit_struct* p_jit =
      (struct jit_struct*) p_context->uc_mcontext.gregs[REG_RDI];
  size_t r13 = p_context->uc_mcontext.gregs[REG_R13];

  jit_sync_timer_tick(p_jit);
  /* Exit if neither of the VIAs are asserting an interrupt. */
  if (!p_jit->irq) {
    util_make_mapping_read_only(p_jit->p_semaphores + k_semaphore_block,
                                k_semaphore_size);
    return;
  }
  /* (r13 & 4) identifies the 6502 interrupt disable flag. */
  if (!(r13 & 4)) {
    /* Interrupts are enabled -- fire the 6502 interrupt logic. */
    uint16_t addr_6502 = jit_6502_addr_from_intel(p_jit, p_rip);

    /* Lower both the block and cli semaphores -- both could potentially be
     * raised.
     * Assumes / requires those semaphores are contiguous.
     */
    util_make_mapping_read_only(p_jit->p_semaphores + k_semaphore_block,
                                k_semaphore_size * 2);

    handle_sigsegv_fire_interrupt(p_context, p_jit, addr_6502);
  } else {
    /* Sadness, interrupts are disabled. Trigger the CLI semaphore so we
     * fault again when interrupts are enabled.
     */
    util_make_mapping_none(p_jit->p_semaphores + k_semaphore_cli,
                           k_semaphore_size);
    util_make_mapping_read_only(p_jit->p_semaphores + k_semaphore_block,
                                k_semaphore_size);
  }
}

static void
handle_cli_semaphore_sigsegv(ucontext_t* p_context, unsigned char* p_rip) {
  (void) p_rip;

  struct jit_struct* p_jit =
      (struct jit_struct*) p_context->uc_mcontext.gregs[REG_RDI];
  size_t r13 = p_context->uc_mcontext.gregs[REG_R13];
  uint16_t addr_6502 = (uint16_t) p_context->uc_mcontext.gregs[REG_RDX];
  /* Should only get here if 6502 interrupts are enabled. */
  /* assert() async safe...? Uhh yeah, ahem. */
  assert(!(r13 & 4));

  /* Lower the semaphore -- we'll either resolve the condition, or find there
   * is no longer a condition.
   */
  util_make_mapping_read_only(p_jit->p_semaphores + k_semaphore_cli,
                              k_semaphore_size);

  /* Exit if neither of the VIAs are asserting an interrupt. This can happen if
   * something else clears the interrupt since we raised the semaphore but
   * before the semaphore hit.
   */
  if (!p_jit->irq) {
    return;
  }

  handle_sigsegv_fire_interrupt(p_context, p_jit, addr_6502);
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

  /* Crash unless it's fault we clearly recognize. */
  if (signum != SIGSEGV || p_siginfo->si_code != SEGV_ACCERR) {
    sigsegv_reraise(p_rip, p_addr);
  }

  /* Crash unless the faulting instruction is in the JIT region. */
  if (p_rip < (unsigned char*) k_jit_addr ||
      p_rip >= (unsigned char*) k_jit_addr +
               (k_bbc_addr_space_size << k_jit_bytes_shift)) {
    sigsegv_reraise(p_rip, p_addr);
  }

  /* Handle the start-of-block semaphore fault. */
  if (p_addr == (k_semaphores_addr + k_semaphore_block)) {
    handle_block_semaphore_sigsegv(p_context, p_rip);
    return;
  }
  /* Handle the post-CLI-or-PLP semaphore fault. */
  if (p_addr == (k_semaphores_addr + k_semaphore_cli_end_minus_4)) {
    handle_cli_semaphore_sigsegv(p_context, p_rip);
    return;
  }

  /* Bail unless it's a fault writing the restricted ROM region. */
  if (p_addr < (unsigned char*) (size_t) k_bbc_mem_mmap_addr_dummy_rom_ro ||
      p_addr >= (unsigned char*) (size_t) k_bbc_mem_mmap_addr_dummy_rom +
                                          k_bbc_addr_space_size) {
    sigsegv_reraise(p_rip, p_addr);
  }

  /* Crash if it's in the registers region. */
  if (p_addr >= (unsigned char*) (size_t) k_bbc_mem_mmap_addr_dummy_rom +
                                          k_bbc_registers_start &&
      p_addr < (unsigned char*) (size_t) k_bbc_mem_mmap_addr_dummy_rom +
                                         k_bbc_registers_start +
                                         k_bbc_registers_len) {
    sigsegv_reraise(p_rip, p_addr);
  }

  /* Ok, it's a write fault in the ROM region. We can continue.
   * To continue, we need to bump rip along!
   */
  /* STA ($xx),Y */ /* mov [rcx + rdx + offset], al */
  if (p_rip[0] == 0x88 && p_rip[1] == 0x84 && p_rip[2] == 0x11) {
    rip_inc = 7;
  } else {
    sigsegv_reraise(p_rip, p_addr);
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

  /* TODO: get rid of local registers copy. */
  bbc_get_registers(p_jit->p_bbc,
                    (unsigned char*) &p_jit->reg_a_eax,
                    (unsigned char*) &p_jit->reg_x_ebx,
                    (unsigned char*) &p_jit->reg_y_ecx,
                    (unsigned char*) &p_jit->reg_s_esi,
                    (unsigned char*) &p_jit->reg_6502_flags,
                    (uint16_t*) &p_jit->reg_pc);

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
           const char* p_opt_flags,
           const char* p_log_flags) {
  unsigned int log_flags;
  unsigned char* p_jit_base;
  unsigned char* p_utils_base;
  unsigned char* p_tables_base;
  unsigned char* p_util_debug;
  unsigned char* p_util_regs;
  unsigned char* p_util_jit;
  unsigned char* p_util_do_interrupt;
  size_t i;

  unsigned char* p_mem = (unsigned char*) (size_t) k_bbc_mem_mmap_addr;
  struct jit_struct* p_jit = malloc(sizeof(struct jit_struct));
  if (p_jit == NULL) {
    errx(1, "cannot allocate jit_struct");
  }
  memset(p_jit, '\0', sizeof(struct jit_struct));

  /* This is the mapping that holds the dynamically JIT'ed code. */
  p_jit_base = util_get_guarded_mapping(
      k_jit_addr,
      k_bbc_addr_space_size * k_jit_bytes_per_byte);
  util_make_mapping_read_write_exec(
      p_jit_base,
      k_bbc_addr_space_size * k_jit_bytes_per_byte);

  p_jit->reg_x_ebx = (unsigned int) (size_t) p_mem;
  p_jit->reg_y_ecx = (unsigned int) (size_t) p_mem;
  p_jit->reg_s_esi = ((unsigned int) (size_t) p_mem) + 0x100;

  /* This is the mapping that holds static little runtime code gadgets. */
  p_utils_base = util_get_guarded_mapping(k_utils_addr, k_utils_size);
  util_make_mapping_read_write_exec(p_utils_base, k_utils_size);
  p_util_debug = p_utils_base + k_utils_debug_offset;
  p_util_regs = p_utils_base + k_utils_regs_offset;
  p_util_jit = p_utils_base + k_utils_jit_offset;
  p_util_do_interrupt = p_utils_base + k_utils_do_interrupt_offset;

  /* This is the mapping that holds runtime tables used by JIT code. */
  p_tables_base = util_get_guarded_mapping(k_tables_addr, k_tables_size);
  for (i = 0; i < 256; ++i) {
    uint16_t addr_6502 = (i << 8);
    unsigned int* p_table_entry = (unsigned int*) (p_tables_base + (i * 4));
    unsigned int table_value;
    if (jit_is_ram_address(p_jit, addr_6502)) {
      table_value = (unsigned int) (size_t) (p_mem + (i * 256));
    } else if (jit_is_special_read_address(p_jit, addr_6502, addr_6502)) {
      /* Make writes to register regions fault, so fixup can be done. */
      table_value = (unsigned int) (size_t) (k_tables_addr - 256);
    } else {
      /* ROM. Squash the useless write by directing the write to an otherwise
       * unused 256 bytes of RAM at the end of the table.
       */
      table_value = (unsigned int) (size_t) (k_tables_addr + (256 * 4));
    }
    *p_table_entry = table_value;
  }

  /* This is the mapping that is a semaphore to trigger JIT execution
   * interruption.
   */
  p_jit->p_semaphores = util_get_guarded_mapping(k_semaphores_addr,
                                                 k_semaphores_size);
  util_make_mapping_read_only(p_jit->p_semaphores, k_semaphores_size);

  p_jit->p_mem = p_mem;
  p_jit->dummy_rom_offset = k_bbc_mem_mmap_addr_dummy_rom - k_bbc_mem_mmap_addr;
  p_jit->p_jit_base = p_jit_base;
  p_jit->p_utils_base = p_utils_base;
  p_jit->p_tables_base = p_tables_base;
  p_jit->p_util_debug = p_util_debug;
  p_jit->p_util_regs = p_util_regs;
  p_jit->p_util_jit = p_util_jit;
  p_jit->p_util_do_interrupt = p_util_do_interrupt;
  p_jit->p_debug = p_debug;
  p_jit->p_debug_callback = p_debug_callback;
  p_jit->p_jit_callback = jit_callback;
  p_jit->p_bbc = p_bbc;
  p_jit->p_read_callback = p_read_callback;
  p_jit->p_write_callback = p_write_callback;

  p_jit->jit_flags = 0;
  jit_set_flag(p_jit, k_jit_flag_merge_ops);
  jit_set_flag(p_jit, k_jit_flag_self_modifying_abs);
  jit_set_flag(p_jit, k_jit_flag_dynamic_operand);
  jit_set_flag(p_jit, k_jit_flag_batch_ops);
  jit_set_flag(p_jit, k_jit_flag_elim_nz_flag_tests);
  if (strstr(p_opt_flags, "jit:self-mod-all")) {
    jit_set_flag(p_jit, k_jit_flag_self_modifying_all);
  }
  if (strstr(p_opt_flags, "jit:no-self-mod-abs")) {
    jit_clear_flag(p_jit, k_jit_flag_self_modifying_abs);
  }
  if (strstr(p_opt_flags, "jit:no-dynamic-operand")) {
    jit_clear_flag(p_jit, k_jit_flag_dynamic_operand);
  }
  if (strstr(p_opt_flags, "jit:no-merge-ops")) {
    jit_clear_flag(p_jit, k_jit_flag_merge_ops);
  }
  if (strstr(p_opt_flags, "jit:no-batch-ops")) {
    jit_clear_flag(p_jit, k_jit_flag_batch_ops);
  }
  if (strstr(p_opt_flags, "jit:no-elim-nz-flag-tests")) {
    jit_clear_flag(p_jit, k_jit_flag_elim_nz_flag_tests);
  }

  log_flags = 0;
  if (strstr(p_log_flags, "jit:self-modify")) {
    log_flags |= k_log_flag_self_modify;
  }
  if (strstr(p_log_flags, "jit:compile")) {
    log_flags |= k_log_flag_compile;
  }
  p_jit->log_flags = log_flags;

  jit_set_max_compile_ops(p_jit, 0);

  /* int3 */
  memset(p_jit_base, '\xcc', k_bbc_addr_space_size * k_jit_bytes_per_byte);
  for (i = 0; i < k_bbc_addr_space_size; ++i) {
    jit_init_addr(p_jit, i);
  }

  jit_emit_debug_util(p_util_debug);
  jit_emit_regs_util(p_jit, p_util_regs);
  jit_emit_jit_util(p_jit, p_util_jit);
  jit_emit_do_interrupt_util(p_jit, p_util_do_interrupt);

  p_jit->p_dest_buf = util_buffer_create();
  p_jit->p_seq_buf = util_buffer_create();
  p_jit->p_single_buf = util_buffer_create();

  return p_jit;
}

void
jit_set_flag(struct jit_struct* p_jit, unsigned int flag) {
  p_jit->jit_flags |= flag;
}

void
jit_clear_flag(struct jit_struct* p_jit, unsigned int flag) {
  p_jit->jit_flags &= ~flag;
}

void
jit_set_max_compile_ops(struct jit_struct* p_jit, size_t max_num_ops) {
  if (max_num_ops == 0) {
    max_num_ops = ~0;
  }
  p_jit->max_num_ops = max_num_ops;
}

void
jit_destroy(struct jit_struct* p_jit) {
  util_free_guarded_mapping(p_jit->p_semaphores, k_semaphores_size);
  util_free_guarded_mapping(p_jit->p_tables_base, k_tables_size);
  util_free_guarded_mapping(p_jit->p_jit_base,
                            k_bbc_addr_space_size * k_jit_bytes_per_byte);
  util_free_guarded_mapping(p_jit->p_utils_base, k_utils_size);
  util_buffer_destroy(p_jit->p_dest_buf);
  util_buffer_destroy(p_jit->p_seq_buf);
  util_buffer_destroy(p_jit->p_single_buf);
  free(p_jit);
}
