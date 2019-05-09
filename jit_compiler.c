#include "jit_compiler.h"

#include "asm_x64_common.h"
#include "asm_x64_jit.h"
#include "asm_x64_jit_defs.h"
#include "bbc_options.h"
#include "defs_6502.h"
#include "memory_access.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

struct jit_compiler {
  struct memory_access* p_memory_access;
  uint8_t* p_mem_read;
  void* (*get_block_host_address)(void* p, uint16_t addr);
  void* (*get_trampoline_host_address)(void* p, uint16_t addr);
  uint16_t (*get_jit_ptr_block)(void* p, uint32_t jit_ptr);
  void* p_host_address_object;
  uint32_t* p_jit_ptrs;
  int debug;
  int option_accurate_timings;
  uint32_t max_6502_opcodes_per_block;
  uint16_t needs_callback_above;

  struct util_buffer* p_single_opcode_buf;
  struct util_buffer* p_tmp_buf;
  uint32_t no_code_jit_ptr;

  uint32_t len_x64_jmp;
  uint32_t len_x64_countdown;
  uint32_t len_x64_FLAGA;
  uint32_t len_x64_FLAGX;
  uint32_t len_x64_FLAGY;
  uint32_t len_x64_LDA_Z;
  uint32_t len_x64_0xA9;
  uint32_t len_x64_LDX_Z;
  uint32_t len_x64_0xA2;
  uint32_t len_x64_LDY_Z;
  uint32_t len_x64_0xA0;

  int compile_for_code_in_zero_page;

  int32_t addr_opcode[k_6502_addr_space_size];
  uint8_t addr_is_block_start[k_6502_addr_space_size];
  uint8_t addr_is_block_continuation[k_6502_addr_space_size];

  int32_t addr_cycles_fixup[k_6502_addr_space_size];
  uint8_t addr_nz_fixup[k_6502_addr_space_size];
  int32_t addr_a_fixup[k_6502_addr_space_size];
  int32_t addr_x_fixup[k_6502_addr_space_size];
  int32_t addr_y_fixup[k_6502_addr_space_size];
};

struct jit_uop {
  /* Static details. */
  int32_t uopcode;
  int32_t uoptype;
  int32_t value1;
  int32_t value2;

  /* Dynamic details that are calculated as compilation proceeds. */
  uint32_t len_x64;
  int eliminated;
};

enum {
  k_max_opcodes_per_compile = 256,
  k_max_uops_per_opcode = 16,
};

struct jit_opcode_details {
  /* Static details. */
  uint16_t addr_6502;
  uint8_t opcode_6502;
  uint16_t operand_6502;
  uint8_t len_bytes_6502_orig;
  uint8_t max_cycles_orig;
  int branches;

  /* Partially dynamic details that may be changed by optimization. */
  uint8_t num_uops;
  struct jit_uop uops[k_max_uops_per_opcode];

  /* Dynamic details that are calculated as compilation proceeds. */
  int ends_block;
  void* p_host_address;
  int32_t cycles_run_start;
  int32_t reg_a;
  int32_t reg_x;
  int32_t reg_y;
  int32_t flag_carry;
  int32_t flag_decimal;
  uint8_t num_fixup_uops;
  struct jit_uop* fixup_uops[k_max_uops_per_opcode];
  uint8_t len_bytes_6502_merged;
  uint8_t max_cycles_merged;
  int eliminated;
};

static const int32_t k_value_unknown = -1;

enum {
  k_opcode_countdown = 0x100,
  k_opcode_debug,
  k_opcode_interp,
  k_opcode_ABX_CHECK_PAGE_CROSSING,
  k_opcode_ABY_CHECK_PAGE_CROSSING,
  k_opcode_ADD_CYCLES,
  k_opcode_ADD_ABS,
  k_opcode_ADD_ABX,
  k_opcode_ADD_ABY,
  k_opcode_ADD_IMM,
  k_opcode_ADD_scratch,
  k_opcode_ADD_scratch_Y,
  k_opcode_ASL_ACC_n,
  k_opcode_CHECK_BCD,
  k_opcode_CHECK_PENDING_IRQ,
  k_opcode_FLAGA,
  k_opcode_FLAGX,
  k_opcode_FLAGY,
  k_opcode_IDY_CHECK_PAGE_CROSSING,
  k_opcode_INC_SCRATCH,
  k_opcode_JMP_SCRATCH,
  k_opcode_LDA_Z,
  k_opcode_LDX_Z,
  k_opcode_LDY_Z,
  k_opcode_LOAD_CARRY,
  k_opcode_LOAD_CARRY_INV,
  k_opcode_LOAD_OVERFLOW,
  k_opcode_LSR_ACC_n,
  k_opcode_MODE_ABX,
  k_opcode_MODE_ABY,
  k_opcode_MODE_IND,
  k_opcode_MODE_IND_SCRATCH,
  k_opcode_MODE_ZPX,
  k_opcode_MODE_ZPY,
  k_opcode_PULL_16,
  k_opcode_PUSH_16,
  k_opcode_ROL_ACC_n,
  k_opcode_ROR_ACC_n,
  k_opcode_SAVE_CARRY,
  k_opcode_SAVE_CARRY_INV,
  k_opcode_SAVE_OVERFLOW,
  k_opcode_STOA_IMM,
  k_opcode_SUB_IMM,
  k_opcode_WRITE_INV_ABS,
  k_opcode_WRITE_INV_SCRATCH,
};

static void
jit_set_jit_ptr_no_code(struct jit_compiler* p_compiler, uint16_t addr) {
  p_compiler->p_jit_ptrs[addr] = p_compiler->no_code_jit_ptr;
}

static void
jit_invalidate_jump_target(struct jit_compiler* p_compiler, uint16_t addr) {
  void* p_host_ptr =
      p_compiler->get_block_host_address(p_compiler->p_host_address_object,
                                         addr);
  util_buffer_setup(p_compiler->p_tmp_buf, p_host_ptr, 2);
  asm_x64_emit_jit_call_compile_trampoline(p_compiler->p_tmp_buf);
}

static void
jit_invalidate_block_with_addr(struct jit_compiler* p_compiler, uint16_t addr) {
  uint32_t jit_ptr = p_compiler->p_jit_ptrs[addr];
  uint16_t block_addr_6502 =
      p_compiler->get_jit_ptr_block(p_compiler->p_host_address_object, jit_ptr);
  jit_invalidate_jump_target(p_compiler, block_addr_6502);
}

struct jit_compiler*
jit_compiler_create(struct memory_access* p_memory_access,
                    void* (*get_block_host_address)(void*, uint16_t),
                    void* (*get_trampoline_host_address)(void*, uint16_t),
                    uint16_t (*get_jit_ptr_block)(void*, uint32_t),
                    void* p_host_address_object,
                    uint32_t* p_jit_ptrs,
                    struct bbc_options* p_options,
                    int debug,
                    int option_accurate_timings) {
  size_t i;
  struct util_buffer* p_tmp_buf;
  uint16_t needs_callback_above;
  uint16_t temp_u16;

  void* p_memory_object = p_memory_access->p_callback_obj;
  int max_6502_opcodes_per_block = 65536;

  /* Check invariants required for compact code generation. */
  assert(K_JIT_CONTEXT_OFFSET_JIT_PTRS < 0x80);

  struct jit_compiler* p_compiler = malloc(sizeof(struct jit_compiler));
  if (p_compiler == NULL) {
    errx(1, "cannot alloc jit_compiler");
  }
  (void) memset(p_compiler, '\0', sizeof(struct jit_compiler));

  p_compiler->p_memory_access = p_memory_access;
  p_compiler->p_mem_read = p_memory_access->p_mem_read;
  p_compiler->get_block_host_address = get_block_host_address;
  p_compiler->get_trampoline_host_address = get_trampoline_host_address;
  p_compiler->get_jit_ptr_block = get_jit_ptr_block;
  p_compiler->p_host_address_object = p_host_address_object;
  p_compiler->p_jit_ptrs = p_jit_ptrs;
  p_compiler->debug = debug;
  p_compiler->option_accurate_timings = option_accurate_timings;

  (void) util_get_int_option(&max_6502_opcodes_per_block,
                             p_options->p_opt_flags,
                             "jit:max-ops");
  if (max_6502_opcodes_per_block < 1) {
    max_6502_opcodes_per_block = 1;
  }
  p_compiler->max_6502_opcodes_per_block = max_6502_opcodes_per_block;

  needs_callback_above = p_memory_access->memory_read_needs_callback_above(
      p_memory_object);
  temp_u16 = p_memory_access->memory_write_needs_callback_above(
      p_memory_object);
  if (temp_u16 < needs_callback_above) {
    needs_callback_above = temp_u16;
  }
  p_compiler->needs_callback_above = needs_callback_above;

  p_compiler->compile_for_code_in_zero_page = 0;

  p_compiler->p_single_opcode_buf = util_buffer_create();
  p_tmp_buf = util_buffer_create();
  p_compiler->p_tmp_buf = p_tmp_buf;

  p_compiler->no_code_jit_ptr = 
      (uint32_t) (size_t) get_block_host_address(p_host_address_object,
                                                 (k_6502_addr_space_size - 1));

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    jit_set_jit_ptr_no_code(p_compiler, i);
  }

  jit_compiler_memory_range_invalidate(p_compiler,
                                       0,
                                       (k_6502_addr_space_size - 1));

  /* Calculate lengths of sequences we need to know. */
  p_compiler->len_x64_jmp = (asm_x64_jit_JMP_END - asm_x64_jit_JMP);
  p_compiler->len_x64_countdown = (asm_x64_jit_check_countdown_END -
                                   asm_x64_jit_check_countdown);
  p_compiler->len_x64_FLAGA = (asm_x64_jit_FLAGA_END - asm_x64_jit_FLAGA);
  p_compiler->len_x64_FLAGX = (asm_x64_jit_FLAGX_END - asm_x64_jit_FLAGX);
  p_compiler->len_x64_FLAGY = (asm_x64_jit_FLAGY_END - asm_x64_jit_FLAGY);
  p_compiler->len_x64_LDA_Z = (asm_x64_jit_LDA_Z_END - asm_x64_jit_LDA_Z);
  p_compiler->len_x64_0xA9 = (asm_x64_jit_LDA_IMM_END - asm_x64_jit_LDA_IMM);
  p_compiler->len_x64_LDX_Z = (asm_x64_jit_LDX_Z_END - asm_x64_jit_LDX_Z);
  p_compiler->len_x64_0xA2 = (asm_x64_jit_LDX_IMM_END - asm_x64_jit_LDX_IMM);
  p_compiler->len_x64_LDY_Z = (asm_x64_jit_LDY_Z_END - asm_x64_jit_LDY_Z);
  p_compiler->len_x64_0xA0 = (asm_x64_jit_LDY_IMM_END - asm_x64_jit_LDY_IMM);

  return p_compiler;
}

void
jit_compiler_destroy(struct jit_compiler* p_compiler) {
  util_buffer_destroy(p_compiler->p_single_opcode_buf);
  util_buffer_destroy(p_compiler->p_tmp_buf);
  free(p_compiler);
}

static void
jit_compiler_get_opcode_details(struct jit_compiler* p_compiler,
                                struct jit_opcode_details* p_details,
                                uint16_t addr_6502) {
  uint8_t opcode_6502;
  uint16_t operand_6502;
  uint8_t optype;
  uint8_t opmode;
  uint8_t opmem;
  uint16_t addr_range_start;
  uint16_t addr_range_end;
  int main_written;

  struct memory_access* p_memory_access = p_compiler->p_memory_access;
  uint8_t* p_mem_read = p_compiler->p_mem_read;
  void* p_memory_callback = p_memory_access->p_callback_obj;
  uint16_t addr_plus_1 = (addr_6502 + 1);
  uint16_t addr_plus_2 = (addr_6502 + 2);
  struct jit_uop* p_uop = &p_details->uops[0];
  struct jit_uop* p_first_post_debug_uop = p_uop;
  int use_interp = 0;
  int could_page_cross = 1;
  int emit_flag_load = 1;
  uint16_t rel_target_6502 = 0;

  p_details->addr_6502 = addr_6502;
  p_details->num_uops = 0;

  opcode_6502 = p_mem_read[addr_6502];
  optype = g_optypes[opcode_6502];
  opmode = g_opmodes[opcode_6502];
  opmem = g_opmem[optype];

  p_details->opcode_6502 = opcode_6502;
  p_details->len_bytes_6502_orig = g_opmodelens[opmode];
  p_details->branches = g_opbranch[optype];
  p_details->ends_block = 0;
  if (p_details->branches == k_bra_y) {
    p_details->ends_block = 1;
  }

  p_details->p_host_address = NULL;
  p_details->cycles_run_start = -1;

  if (p_compiler->debug) {
    p_uop->uopcode = k_opcode_debug;
    p_uop->uoptype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    p_first_post_debug_uop = p_uop;
  }

  /* Mode resolution and possibly per-mode uops. */
  switch (opmode) {
  case 0:
    operand_6502 = addr_6502;
    break;
  case k_nil:
  case k_acc:
    operand_6502 = 0;
    break;
  case k_imm:
  case k_zpg:
    operand_6502 = p_mem_read[addr_plus_1];
    break;
  case k_zpx:
    operand_6502 = p_mem_read[addr_plus_1];
    p_uop->uopcode = k_opcode_MODE_ZPX;
    p_uop->uoptype = -1;
    p_uop->value1 = operand_6502;
    p_uop++;
    break;
  case k_zpy:
    operand_6502 = p_mem_read[addr_plus_1];
    p_uop->uopcode = k_opcode_MODE_ZPY;
    p_uop->uoptype = -1;
    p_uop->value1 = operand_6502;
    p_uop++;
    break;
  case k_rel:
    operand_6502 = p_mem_read[addr_plus_1];
    rel_target_6502 = ((int) addr_6502 + 2 + (int8_t) operand_6502);
    break;
  case k_abs:
  case k_abx:
  case k_aby:
    operand_6502 = ((p_mem_read[addr_plus_2] << 8) | p_mem_read[addr_plus_1]);
    if ((operand_6502 & 0xFF) == 0x00) {
      could_page_cross = 0;
    }
    addr_range_start = operand_6502;
    addr_range_end = operand_6502;
    if (opmode == k_abx || opmode == k_aby) {
      addr_range_end += 0xFF;
    }
    if (p_compiler->option_accurate_timings &&
        (opmem == k_read) &&
        could_page_cross) {
      if (opmode == k_abx) {
        p_uop->uopcode = k_opcode_ABX_CHECK_PAGE_CROSSING;
        p_uop->uoptype = -1;
        p_uop->value1 = operand_6502;
        p_uop++;
      } else if (opmode == k_aby) {
        p_uop->uopcode = k_opcode_ABY_CHECK_PAGE_CROSSING;
        p_uop->uoptype = -1;
        p_uop->value1 = operand_6502;
        p_uop++;
      }
    }

    /* Use the interpreter for address space wraps. Otherwise the JIT code
     * will do an out-of-bounds access. Longer term, this could be addressed,
     * with performance maintained, by mapping a wrap-around page.
     */
    if (addr_range_start > addr_range_end) {
      use_interp = 1;
    }
    if (opmem == k_read || opmem == k_rw) {
      if (p_memory_access->memory_read_needs_callback(p_memory_callback,
                                                      addr_range_start)) {
        use_interp = 1;
      }
      if (p_memory_access->memory_read_needs_callback(p_memory_callback,
                                                      addr_range_end)) {
        use_interp = 1;
      }
    }
    if (opmem == k_write || opmem == k_rw) {
      if (p_memory_access->memory_write_needs_callback(p_memory_callback,
                                                       addr_range_start)) {
        use_interp = 1;
      }
      if (p_memory_access->memory_write_needs_callback(p_memory_callback,
                                                       addr_range_end)) {
        use_interp = 1;
      }
    }
    break;
  case k_ind:
    operand_6502 = ((p_mem_read[addr_plus_2] << 8) | p_mem_read[addr_plus_1]);
    p_uop->uopcode = k_opcode_MODE_IND;
    p_uop->uoptype = -1;
    p_uop->value1 = operand_6502;
    p_uop++;
    break;
  case k_idx:
    operand_6502 = p_mem_read[addr_plus_1];
    p_uop->uopcode = k_opcode_MODE_ZPX;
    p_uop->uoptype = -1;
    p_uop->value1 = operand_6502;
    p_uop++;
    p_uop->uopcode = k_opcode_MODE_IND_SCRATCH;
    p_uop->uoptype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    break;
  case k_idy:
    operand_6502 = p_mem_read[addr_plus_1];
    p_uop->uopcode = k_opcode_MODE_IND;
    p_uop->uoptype = -1;
    p_uop->value1 = (uint16_t) operand_6502;
    p_uop++;
    /* NOTE: we run the check for special addresses before the check for page
     * crossings, otherwise we might account for a page crossing only to jump
     * into the interpreter to handle a special address.
     * This ordering also means we haven't added Y to the indirect base
     * address yet, so we need to subtract the maximum value of Y (0xFF) from
     * the special address base.
     */
    /* TODO: this happens too soon if we hit a register page fault? */
    if (p_compiler->option_accurate_timings && (opmem == k_read)) {
      p_uop->uopcode = k_opcode_IDY_CHECK_PAGE_CROSSING;
      p_uop->uoptype = -1;
      p_uop++;
    }
    break;
  default:
    assert(0);
    operand_6502 = 0;
    break;
  }

  p_details->operand_6502 = operand_6502;

  p_details->max_cycles_orig = g_opcycles[opcode_6502];
  if (p_compiler->option_accurate_timings) {
    if ((opmem == k_read) &&
        (opmode == k_abx || opmode == k_aby || opmode == k_idy) &&
        could_page_cross) {
      p_details->max_cycles_orig++;
    } else if (opmode == k_rel) {
      /* Taken branches take 1 cycles longer, or 2 cycles longer if there's
       * also a page crossing.
       */
      if (((addr_6502 + 2) >> 8) ^ (rel_target_6502 >> 8)) {
        p_details->max_cycles_orig += 2;
      } else {
        p_details->max_cycles_orig++;
      }
    }
  }

  if (optype == k_rti) {
    /* Bounce to the interpreter for RTI. The problem with RTI is that it
     * might jump all over the place without any particular pattern, because
     * interrupts will happen all over the place. If we are not careful, over
     * time, RTI will split all of the JIT blocks into 1-instruction blocks,
     * which will be super slow.
     */
    use_interp = 1;
  }

  if (use_interp) {
    p_uop = p_first_post_debug_uop;

    p_uop->uopcode = k_opcode_interp;
    p_uop->uoptype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    p_details->ends_block = 1;

    p_details->num_uops = (p_uop - &p_details->uops[0]);
    assert(p_details->num_uops <= k_max_uops_per_opcode);
    return;
  }

  /* Code invalidation for writes, aka. self-modifying code. */
  /* TODO: stack page invalidations. */
  if (opmem == k_write || opmem == k_rw) {
    switch (opmode) {
    case k_abs:
      p_uop->uopcode = k_opcode_WRITE_INV_ABS;
      p_uop->uoptype = -1;
      p_uop->value1 = operand_6502;
      p_uop++;
      break;
    case k_abx:
      p_uop->uopcode = k_opcode_MODE_ABX;
      p_uop->uoptype = -1;
      p_uop->value1 = operand_6502;
      p_uop++;
      p_uop->uopcode = k_opcode_WRITE_INV_SCRATCH;
      p_uop->uoptype = -1;
      p_uop++;
      break;
    case k_aby:
      p_uop->uopcode = k_opcode_MODE_ABY;
      p_uop->uoptype = -1;
      p_uop->value1 = operand_6502;
      p_uop++;
      p_uop->uopcode = k_opcode_WRITE_INV_SCRATCH;
      p_uop->uoptype = -1;
      p_uop++;
      break;
    case k_idx:
    case k_idy:
      p_uop->uopcode = k_opcode_WRITE_INV_SCRATCH;
      p_uop->uoptype = -1;
      p_uop++;
      break;
    case k_zpg:
      if (p_compiler->compile_for_code_in_zero_page) {
        p_uop->uopcode = k_opcode_WRITE_INV_ABS;
        p_uop->uoptype = -1;
        p_uop->value1 = operand_6502;
        p_uop++;
      }
      break;
    case k_zpx:
    case k_zpy:
      if (p_compiler->compile_for_code_in_zero_page) {
        p_uop->uopcode = k_opcode_WRITE_INV_SCRATCH;
        p_uop->uoptype = -1;
        p_uop++;
      }
      break;
    default:
      break;
    }
  }

  /* Pre-main uops. */
  switch (optype) {
  case k_adc:
    p_uop->uopcode = k_opcode_CHECK_BCD;
    p_uop->uoptype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    p_uop->uopcode = k_opcode_LOAD_CARRY;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_bcc:
  case k_bcs:
  case k_rol:
  case k_ror:
    p_uop->uopcode = k_opcode_LOAD_CARRY;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_bvc:
  case k_bvs:
    p_uop->uopcode = k_opcode_LOAD_OVERFLOW;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_cli:
  case k_plp:
    p_uop->uopcode = k_opcode_CHECK_PENDING_IRQ;
    p_uop->uoptype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    break;
  case k_jsr:
    p_uop->uopcode = k_opcode_PUSH_16;
    p_uop->uoptype = -1;
    p_uop->value1 = (uint16_t) (addr_6502 + 2);
    p_uop++;
    break;
  case k_rti:
    p_uop->uopcode = k_opcode_CHECK_PENDING_IRQ;
    p_uop->uoptype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    /* PLP */
    p_uop->uopcode = 0x28;
    p_uop->uoptype = k_plp;
    p_uop++;
    p_uop->uopcode = k_opcode_PULL_16;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_rts:
    p_uop->uopcode = k_opcode_PULL_16;
    p_uop->uoptype = -1;
    p_uop++;
    /* TODO: may increment 0xFFFF -> 0x10000, which may crash. */
    p_uop->uopcode = k_opcode_INC_SCRATCH;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_sbc:
    p_uop->uopcode = k_opcode_CHECK_BCD;
    p_uop->uoptype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    p_uop->uopcode = k_opcode_LOAD_CARRY_INV;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  default:
    break;
  }

  /* Main uop, or a replacement thereof. */
  main_written = 1;
  switch (optype) {
  case k_bcc:
  case k_bcs:
  case k_beq:
  case k_bne:
  case k_bmi:
  case k_bpl:
  case k_bvc:
  case k_bvs:
    p_uop->uopcode = opcode_6502;
    p_uop->uoptype = optype;
    p_uop->value1 = rel_target_6502;
    p_uop++;
    break;
  case k_brk:
    p_uop->uopcode = k_opcode_PUSH_16;
    p_uop->uoptype = -1;
    p_uop->value1 = (uint16_t) (addr_6502 + 2);
    p_uop++;
    /* PHP */
    p_uop->uopcode = 0x08;
    p_uop->uoptype = k_php;
    p_uop++;
    /* SEI */
    p_uop->uopcode = 0x78;
    p_uop->uoptype = k_sei;
    p_uop++;
    /* MODE_IND */
    p_uop->uopcode = k_opcode_MODE_IND;
    p_uop->uoptype = -1;
    p_uop->value1 = (uint16_t) k_6502_vector_irq;
    p_uop++;
    /* JMP_SCRATCH */
    p_uop->uopcode = k_opcode_JMP_SCRATCH;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_jmp:
    if (opmode == k_ind) {
      p_uop->uopcode = k_opcode_JMP_SCRATCH;
      p_uop->uoptype = -1;
      p_uop++;
    } else {
      main_written = 0;
    }
    break;
  case k_jsr:
    p_uop->uopcode = 0x4C; /* JMP abs */
    p_uop->uoptype = k_jmp;
    p_uop->value1 = operand_6502;
    p_uop++;
    break;
  case k_lda:
    if ((opmode == k_imm) && (operand_6502 == 0x00)) {
      p_uop->uopcode = k_opcode_LDA_Z;
      p_uop->uoptype = -1;
      p_uop++;
      emit_flag_load = 0;
    } else {
      main_written = 0;
    }
    break;
  case k_ldx:
    if ((opmode == k_imm) && (operand_6502 == 0x00)) {
      p_uop->uopcode = k_opcode_LDX_Z;
      p_uop->uoptype = -1;
      p_uop++;
      emit_flag_load = 0;
    } else {
      main_written = 0;
    }
    break;
  case k_ldy:
    if ((opmode == k_imm) && (operand_6502 == 0x00)) {
      p_uop->uopcode = k_opcode_LDY_Z;
      p_uop->uoptype = -1;
      p_uop++;
      emit_flag_load = 0;
    } else {
      main_written = 0;
    }
    break;
  case k_rti:
  case k_rts:
    p_uop->uopcode = k_opcode_JMP_SCRATCH;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  default:
    main_written = 0;
    break;
  }
  if (!main_written) {
    p_uop->uopcode = opcode_6502;
    p_uop->uoptype = optype;
    p_uop->value1 = operand_6502;
    p_uop++;
  }

  /* Post-main uops. */
  switch (optype) {
  case k_adc:
    p_uop->uopcode = k_opcode_SAVE_CARRY;
    p_uop->uoptype = -1;
    p_uop++;
    p_uop->uopcode = k_opcode_SAVE_OVERFLOW;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_alr:
  case k_asl:
  case k_lsr:
  case k_slo:
    p_uop->uopcode = k_opcode_SAVE_CARRY;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_bcc:
  case k_bcs:
  case k_beq:
  case k_bmi:
  case k_bne:
  case k_bpl:
  case k_bvc:
  case k_bvs:
    if (p_compiler->option_accurate_timings) {
      /* Fixup countdown if a branch wasn't taken. */
      p_uop->uopcode = k_opcode_ADD_CYCLES;
      p_uop->uoptype = -1;
      p_uop->value1 = (uint8_t) (p_details->max_cycles_orig - 2);
      p_uop++;
    }
    break;
  case k_cmp:
  case k_cpx:
  case k_cpy:
    p_uop->uopcode = k_opcode_SAVE_CARRY_INV;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  case k_lda:
  case k_txa:
  case k_tya:
  case k_pla:
    if (emit_flag_load) {
      p_uop->uopcode = k_opcode_FLAGA;
      p_uop->uoptype = -1;
      p_uop++;
    }
    break;
  case k_ldx:
  case k_tax:
  case k_tsx:
    if (emit_flag_load) {
      p_uop->uopcode = k_opcode_FLAGX;
      p_uop->uoptype = -1;
      p_uop++;
    }
    break;
  case k_ldy:
  case k_tay:
    if (emit_flag_load) {
      p_uop->uopcode = k_opcode_FLAGY;
      p_uop->uoptype = -1;
      p_uop++;
    }
    break;
  case k_rol:
  case k_ror:
    p_uop->uopcode = k_opcode_SAVE_CARRY;
    p_uop->uoptype = -1;
    p_uop++;
    /* TODO: come up with viable optimization for non-acc modes. */
    if (opmode == k_acc) {
      p_uop->uopcode = k_opcode_FLAGA;
      p_uop->uoptype = -1;
      p_uop++;
    }
    break;
  case k_sbc:
    p_uop->uopcode = k_opcode_SAVE_CARRY_INV;
    p_uop->uoptype = -1;
    p_uop++;
    p_uop->uopcode = k_opcode_SAVE_OVERFLOW;
    p_uop->uoptype = -1;
    p_uop++;
    break;
  default:
    break;
  }

  p_details->num_uops = (p_uop - &p_details->uops[0]);
  assert(p_details->num_uops <= k_max_uops_per_opcode);
}

static void
jit_compiler_emit_uop(struct jit_compiler* p_compiler,
                      struct util_buffer* p_dest_buf,
                      struct jit_uop* p_uop) {
  int uopcode = p_uop->uopcode;
  int32_t value1 = p_uop->value1;
  int32_t value2 = p_uop->value2;
  struct memory_access* p_memory_access = p_compiler->p_memory_access;
  void* p_memory_object = p_memory_access->p_callback_obj;
  void* p_host_address_object = p_compiler->p_host_address_object;

  /* Resolve any addresses to real pointers. */
  switch (uopcode) {
  case k_opcode_countdown:
  case k_opcode_CHECK_BCD:
  case k_opcode_CHECK_PENDING_IRQ:
    value1 = (uint32_t) (size_t) p_compiler->get_trampoline_host_address(
        p_host_address_object, (uint16_t) value1);
    break;
  case 0x4C: /* JMP abs */
  case 0x10: /* All of the conditional branches. */
  case 0x30:
  case 0x50:
  case 0x70:
  case 0x90:
  case 0xB0:
  case 0xD0:
  case 0xF0:
    value1 = (uint32_t) (size_t) p_compiler->get_block_host_address(
        p_host_address_object, (uint16_t) value1);
    break;
  default:
    break;
  }

  /* Emit the opcode. */
  switch (uopcode) {
  case k_opcode_countdown:
    asm_x64_emit_jit_check_countdown(p_dest_buf,
                                     (uint32_t) value2,
                                     (void*) (size_t) value1);
    break;
  case k_opcode_debug:
    asm_x64_emit_jit_call_debug(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_interp:
    asm_x64_emit_jit_jump_interp(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ABX_CHECK_PAGE_CROSSING:
    asm_x64_emit_jit_ABX_CHECK_PAGE_CROSSING(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ABY_CHECK_PAGE_CROSSING:
    asm_x64_emit_jit_ABY_CHECK_PAGE_CROSSING(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ADD_CYCLES:
    asm_x64_emit_jit_ADD_CYCLES(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_ADD_ABS:
    asm_x64_emit_jit_ADD_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ADD_ABX:
    asm_x64_emit_jit_ADD_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ADD_ABY:
    asm_x64_emit_jit_ADD_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ADD_IMM:
    asm_x64_emit_jit_ADD_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_ADD_scratch:
    asm_x64_emit_jit_ADD_scratch(p_dest_buf);
    break;
  case k_opcode_ADD_scratch_Y:
    asm_x64_emit_jit_ADD_scratch_Y(p_dest_buf);
    break;
  case k_opcode_ASL_ACC_n:
    asm_x64_emit_jit_ASL_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_CHECK_BCD:
    asm_x64_emit_jit_CHECK_BCD(p_dest_buf, (void*) (size_t) value1);
    break;
  case k_opcode_CHECK_PENDING_IRQ:
    asm_x64_emit_jit_CHECK_PENDING_IRQ(p_dest_buf, (void*) (size_t) value1);
    break;
  case k_opcode_FLAGA:
    asm_x64_emit_jit_FLAGA(p_dest_buf);
    break;
  case k_opcode_FLAGX:
    asm_x64_emit_jit_FLAGX(p_dest_buf);
    break;
  case k_opcode_FLAGY:
    asm_x64_emit_jit_FLAGY(p_dest_buf);
    break;
  case k_opcode_IDY_CHECK_PAGE_CROSSING:
    asm_x64_emit_jit_IDY_CHECK_PAGE_CROSSING(p_dest_buf);
    break;
  case k_opcode_INC_SCRATCH:
    asm_x64_emit_jit_INC_SCRATCH(p_dest_buf);
    break;
  case k_opcode_JMP_SCRATCH:
    asm_x64_emit_jit_JMP_SCRATCH(p_dest_buf);
    break;
  case k_opcode_LDA_Z:
    asm_x64_emit_jit_LDA_Z(p_dest_buf);
    break;
  case k_opcode_LDX_Z:
    asm_x64_emit_jit_LDX_Z(p_dest_buf);
    break;
  case k_opcode_LDY_Z:
    asm_x64_emit_jit_LDY_Z(p_dest_buf);
    break;
  case k_opcode_LOAD_CARRY:
    asm_x64_emit_jit_LOAD_CARRY(p_dest_buf);
    break;
  case k_opcode_LOAD_CARRY_INV:
    asm_x64_emit_jit_LOAD_CARRY_INV(p_dest_buf);
    break;
  case k_opcode_LOAD_OVERFLOW:
    asm_x64_emit_jit_LOAD_OVERFLOW(p_dest_buf);
    break;
  case k_opcode_LSR_ACC_n:
    asm_x64_emit_jit_LSR_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_MODE_ABX:
    asm_x64_emit_jit_MODE_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_ABY:
    asm_x64_emit_jit_MODE_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_IND:
    asm_x64_emit_jit_MODE_IND(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_IND_SCRATCH:
    asm_x64_emit_jit_MODE_IND_SCRATCH(p_dest_buf);
    break;
  case k_opcode_MODE_ZPX:
    asm_x64_emit_jit_MODE_ZPX(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_MODE_ZPY:
    asm_x64_emit_jit_MODE_ZPY(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_PULL_16:
    asm_x64_emit_pull_word_to_scratch(p_dest_buf);
    break;
  case k_opcode_PUSH_16:
    asm_x64_emit_jit_PUSH_16(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ROL_ACC_n:
    asm_x64_emit_jit_ROL_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_ROR_ACC_n:
    asm_x64_emit_jit_ROR_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_SAVE_CARRY:
    asm_x64_emit_jit_SAVE_CARRY(p_dest_buf);
    break;
  case k_opcode_SAVE_CARRY_INV:
    asm_x64_emit_jit_SAVE_CARRY_INV(p_dest_buf);
    break;
  case k_opcode_SAVE_OVERFLOW:
    asm_x64_emit_jit_SAVE_OVERFLOW(p_dest_buf);
    break;
  case k_opcode_STOA_IMM:
    asm_x64_emit_jit_STOA_IMM(p_dest_buf, (uint16_t) value1, (uint8_t) value2);
    break;
  case k_opcode_SUB_IMM:
    asm_x64_emit_jit_SUB_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_WRITE_INV_ABS:
    asm_x64_emit_jit_WRITE_INV_ABS(p_dest_buf, (uint32_t) value1);
    break;
  case k_opcode_WRITE_INV_SCRATCH:
    asm_x64_emit_jit_WRITE_INV_SCRATCH(p_dest_buf);
    break;
  case 0x01: /* ORA idx */
  case 0x15: /* ORA zpx */
    asm_x64_emit_jit_ORA_scratch(p_dest_buf);
    break;
  case 0x04: /* NOP zpg */ /* Undocumented. */
  case 0xDC: /* NOP abx */ /* Undocumented. */
  case 0xEA: /* NOP */
  case 0xF4: /* NOP zpx */ /* Undocumented. */
    /* We don't really have to emit anything for a NOP, but for now and for
     * good readability, we'll emit a host NOP.
     * (The correct place to change if we wanted to not emit anything would be
     * to eliminate the 6502 opcode in the optimized.)
     * We have a minimum of 2 bytes of x64 code per uop because that's the size
     * of the self-modified marker, so we'll need 2 1-byte x64 nops.
     */
    asm_x64_emit_instruction_REAL_NOP(p_dest_buf);
    asm_x64_emit_instruction_REAL_NOP(p_dest_buf);
    break;
  case 0x05: /* ORA zpg */
  case 0x0D: /* ORA abs */
    asm_x64_emit_jit_ORA_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x06: /* ASL zpg */
    asm_x64_emit_jit_ASL_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x07: /* SLO zpg */ /* Undocumented. */
    asm_x64_emit_jit_SLO_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x08:
    asm_x64_emit_instruction_PHP(p_dest_buf);
    break;
  case 0x09:
    asm_x64_emit_jit_ORA_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x0A:
    asm_x64_emit_jit_ASL_ACC(p_dest_buf);
    break;
  case 0x0E: /* ASL abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_ASL_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_ASL_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x10:
    asm_x64_emit_jit_BPL(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x11: /* ORA idy */
    asm_x64_emit_jit_ORA_scratch_Y(p_dest_buf);
    break;
  case 0x16: /* ASL zpx */
    asm_x64_emit_jit_ASL_scratch(p_dest_buf);
    break;
  case 0x18:
    asm_x64_emit_instruction_CLC(p_dest_buf);
    break;
  case 0x19:
    asm_x64_emit_jit_ORA_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x1D:
    asm_x64_emit_jit_ORA_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x1E: /* ASL abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_ASL_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_ASL_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x21: /* AND idx */
  case 0x35: /* AND zpx */
    asm_x64_emit_jit_AND_scratch(p_dest_buf);
    break;
  case 0x24: /* BIT zpg */
  case 0x2C: /* BIT abs */
    asm_x64_emit_jit_BIT(p_dest_buf, (uint16_t) value1);
    break;
  case 0x25: /* AND zpg */
  case 0x2D: /* AND abs */
    asm_x64_emit_jit_AND_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x26: /* ROL zpg */
  case 0x2E: /* ROL abs */
    asm_x64_emit_jit_ROL_ABS_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x28:
    asm_x64_emit_instruction_PLP(p_dest_buf);
    break;
  case 0x29:
    asm_x64_emit_jit_AND_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x2A:
    asm_x64_emit_jit_ROL_ACC(p_dest_buf);
    break;
  case 0x30:
    asm_x64_emit_jit_BMI(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x31: /* AND idy */
    asm_x64_emit_jit_AND_scratch_Y(p_dest_buf);
    break;
  case 0x36: /* ROL zpx */
    asm_x64_emit_jit_ROL_scratch(p_dest_buf);
    break;
  case 0x38:
    asm_x64_emit_instruction_SEC(p_dest_buf);
    break;
  case 0x39:
    asm_x64_emit_jit_AND_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x3D:
    asm_x64_emit_jit_AND_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x3E:
    asm_x64_emit_jit_ROL_ABX_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x41: /* EOR idx */
  case 0x55: /* EOR zpx */
    asm_x64_emit_jit_EOR_scratch(p_dest_buf);
    break;
  case 0x45: /* EOR zpg */
  case 0x4D: /* EOR abs */
    asm_x64_emit_jit_EOR_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x46: /* LSR zpg */
    asm_x64_emit_jit_LSR_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x48:
    asm_x64_emit_instruction_PHA(p_dest_buf);
    break;
  case 0x49:
    asm_x64_emit_jit_EOR_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x4A:
    asm_x64_emit_jit_LSR_ACC(p_dest_buf);
    break;
  case 0x4B: /* ALR imm */ /* Undocumented. */
    asm_x64_emit_jit_ALR_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x4C:
    asm_x64_emit_jit_JMP(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x4E: /* LSR abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_LSR_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_LSR_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x50:
    asm_x64_emit_jit_BVC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x51: /* EOR idy */
    asm_x64_emit_jit_EOR_scratch_Y(p_dest_buf);
    break;
  case 0x56: /* LSR zpx */
    asm_x64_emit_jit_LSR_scratch(p_dest_buf);
    break;
  case 0x58:
    asm_x64_emit_instruction_CLI(p_dest_buf);
    break;
  case 0x59:
    asm_x64_emit_jit_EOR_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x5D:
    asm_x64_emit_jit_EOR_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x5E: /* LSR abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_LSR_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_LSR_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x61: /* ADC idx */
  case 0x75: /* ADC zpx */
    asm_x64_emit_jit_ADC_scratch(p_dest_buf);
    break;
  case 0x65: /* ADC zpg */
  case 0x6D: /* ADC abs */
    asm_x64_emit_jit_ADC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x66: /* ROR zpg */
  case 0x6E: /* ROR abs */
    asm_x64_emit_jit_ROR_ABS_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x68:
    asm_x64_emit_instruction_PLA(p_dest_buf);
    break;
  case 0x69:
    asm_x64_emit_jit_ADC_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x6A:
    asm_x64_emit_jit_ROR_ACC(p_dest_buf);
    break;
  case 0x70:
    asm_x64_emit_jit_BVS(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x71: /* ADC idy */
    asm_x64_emit_jit_ADC_scratch_Y(p_dest_buf);
    break;
  case 0x76: /* ROR zpx */
    asm_x64_emit_jit_ROR_scratch(p_dest_buf);
    break;
  case 0x78:
    asm_x64_emit_instruction_SEI(p_dest_buf);
    break;
  case 0x79:
    asm_x64_emit_jit_ADC_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x7D:
    asm_x64_emit_jit_ADC_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x7E:
    asm_x64_emit_jit_ROR_ABX_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x81: /* STA idx */
  case 0x95: /* STA zpx */
    asm_x64_emit_jit_STA_scratch(p_dest_buf);
    break;
  case 0x98:
    asm_x64_emit_instruction_TYA(p_dest_buf);
    break;
  case 0x84: /* STY zpg */
  case 0x8C: /* STY abs */
    asm_x64_emit_jit_STY_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x85: /* STA zpg */
  case 0x8D: /* STA abs */
    asm_x64_emit_jit_STA_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x86: /* STX zpg */
  case 0x8E: /* STX abs */
    asm_x64_emit_jit_STX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x87: /* SAX zpg */ /* Undocumented. */
    asm_x64_emit_jit_SAX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x88:
    asm_x64_emit_instruction_DEY(p_dest_buf);
    break;
  case 0x8A:
    asm_x64_emit_instruction_TXA(p_dest_buf);
    break;
  case 0x90:
    asm_x64_emit_jit_BCC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x91: /* STA idy */
    asm_x64_emit_jit_STA_scratch_Y(p_dest_buf);
    break;
  case 0x94: /* STY zpx */
    asm_x64_emit_jit_STY_scratch(p_dest_buf);
    break;
  case 0x96: /* STX zpy */
    asm_x64_emit_jit_STX_scratch(p_dest_buf);
    break;
  case 0x99:
    asm_x64_emit_jit_STA_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x9A:
    asm_x64_emit_instruction_TXS(p_dest_buf);
    break;
  case 0x9C:
    asm_x64_emit_jit_SHY_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x9D:
    asm_x64_emit_jit_STA_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA0:
    asm_x64_emit_jit_LDY_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xA1: /* LDA idx */
  case 0xB5: /* LDA zpx */
    asm_x64_emit_jit_LDA_scratch(p_dest_buf);
    break;
  case 0xA2:
    asm_x64_emit_jit_LDX_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xA4: /* LDY zpg */
  case 0xAC: /* LDY abs */
    asm_x64_emit_jit_LDY_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA5: /* LDA zpg */
  case 0xAD: /* LDA abs */
    asm_x64_emit_jit_LDA_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA6: /* LDX zpg */
  case 0xAE: /* LDX abs */
    asm_x64_emit_jit_LDX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA8:
    asm_x64_emit_instruction_TAY(p_dest_buf);
    break;
  case 0xA9:
    asm_x64_emit_jit_LDA_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xAA:
    asm_x64_emit_instruction_TAX(p_dest_buf);
    break;
  case 0xB0:
    asm_x64_emit_jit_BCS(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xB1: /* LDA idy */
    asm_x64_emit_jit_LDA_scratch_Y(p_dest_buf);
    break;
  case 0xB4: /* LDY zpx */
    asm_x64_emit_jit_LDY_scratch(p_dest_buf);
    break;
  case 0xB6: /* LDX zpy */
    asm_x64_emit_jit_LDX_scratch(p_dest_buf);
    break;
  case 0xB8:
    asm_x64_emit_instruction_CLV(p_dest_buf);
    break;
  case 0xB9:
    asm_x64_emit_jit_LDA_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xBA:
    asm_x64_emit_instruction_TSX(p_dest_buf);
    break;
  case 0xBC:
    asm_x64_emit_jit_LDY_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xBD:
    asm_x64_emit_jit_LDA_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xBE:
    asm_x64_emit_jit_LDX_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC0:
    asm_x64_emit_jit_CPY_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xC1: /* CMP idx */
  case 0xD5: /* CMP zpx */
    asm_x64_emit_jit_CMP_scratch(p_dest_buf);
    break;
  case 0xC4: /* CPY zpg */
  case 0xCC: /* CPY abs */
    asm_x64_emit_jit_CPY_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC5: /* CMP zpg */
  case 0xCD: /* CMP abs */
    asm_x64_emit_jit_CMP_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC6: /* DEC zpg */
    asm_x64_emit_jit_DEC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC8:
    asm_x64_emit_instruction_INY(p_dest_buf);
    break;
  case 0xC9:
    asm_x64_emit_jit_CMP_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xCA:
    asm_x64_emit_instruction_DEX(p_dest_buf);
    break;
  case 0xCE: /* DEC abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_DEC_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_DEC_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xD0:
    asm_x64_emit_jit_BNE(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xD1: /* CMP idy */
    asm_x64_emit_jit_CMP_scratch_Y(p_dest_buf);
    break;
  case 0xD6: /* DEC zpx */
    asm_x64_emit_jit_DEC_scratch(p_dest_buf);
    break;
  case 0xD8:
    asm_x64_emit_instruction_CLD(p_dest_buf);
    break;
  case 0xD9:
    asm_x64_emit_jit_CMP_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xDD:
    asm_x64_emit_jit_CMP_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xDE: /* DEC abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_DEC_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_DEC_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xE0:
    asm_x64_emit_jit_CPX_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xE1: /* SBC idx */
  case 0xF5: /* SBC zpx */
    asm_x64_emit_jit_SBC_scratch(p_dest_buf);
    break;
  case 0xE4: /* CPX zpg */
  case 0xEC: /* CPX abs */
    asm_x64_emit_jit_CPX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xE5: /* SBC zpg */
  case 0xED: /* SBC abs */
    asm_x64_emit_jit_SBC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xE6: /* INC zpg */
    asm_x64_emit_jit_INC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xE8:
    asm_x64_emit_instruction_INX(p_dest_buf);
    break;
  case 0xE9:
    asm_x64_emit_jit_SBC_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xEE: /* INC abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_INC_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_INC_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xF0:
    asm_x64_emit_jit_BEQ(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xF1: /* SBC idy */
    asm_x64_emit_jit_SBC_scratch_Y(p_dest_buf);
    break;
  case 0xF2:
    asm_x64_emit_instruction_CRASH(p_dest_buf);
    break;
  case 0xF6: /* INC zpx */
    asm_x64_emit_jit_INC_scratch(p_dest_buf);
    break;
  case 0xF8:
    asm_x64_emit_instruction_SED(p_dest_buf);
    break;
  case 0xF9:
    asm_x64_emit_jit_SBC_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xFD:
    asm_x64_emit_jit_SBC_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xFE: /* INC abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_INC_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_INC_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  default:
    /* Use the interpreter for unknown opcodes. These could be either the
     * special re-purposed opcodes (e.g. CYCLES) or genuinely unused opcodes.
     */
    asm_x64_emit_jit_jump_interp(p_dest_buf, (uint16_t) value1);
    break;
  }
}

static struct jit_uop*
jit_compiler_find_uop(struct jit_compiler* p_compiler,
                      struct jit_opcode_details* p_opcode,
                      int32_t uopcode) {
  uint32_t i_uops;

  (void) p_compiler;

  for (i_uops = 0; i_uops < p_opcode->num_uops; ++i_uops) {
    struct jit_uop* p_uop = &p_opcode->uops[i_uops];
    if (p_uop->uopcode == uopcode) {
      return p_uop;
    }
  }

  return NULL;
}

static void
jit_compiler_eliminate(struct jit_compiler* p_compiler,
                       struct jit_opcode_details** pp_elim_opcode,
                       struct jit_uop** pp_elim_uop,
                       struct jit_opcode_details* p_curr_opcode) {
  struct jit_opcode_details* p_elim_opcode = *pp_elim_opcode;
  struct jit_uop* p_elim_uop = *pp_elim_uop;

  (void) p_compiler;

  *pp_elim_opcode = NULL;
  *pp_elim_uop = NULL;

  p_elim_uop->eliminated = 1;

  while (p_elim_opcode != p_curr_opcode) {
    assert(p_elim_opcode->num_fixup_uops < k_max_uops_per_opcode);
    p_elim_opcode->fixup_uops[p_elim_opcode->num_fixup_uops++] = p_elim_uop;
    p_elim_opcode++;
  }
}

static void
jit_compiler_optimize(struct jit_compiler* p_compiler,
                      struct jit_opcode_details* p_opcodes,
                      uint32_t num_opcodes) {
  uint32_t i_opcodes;

  int32_t reg_a = k_value_unknown;
  int32_t reg_x = k_value_unknown;
  int32_t reg_y = k_value_unknown;
  int32_t flag_carry = k_value_unknown;
  int32_t flag_decimal = k_value_unknown;

  struct jit_opcode_details* p_prev_opcode = NULL;

  struct jit_opcode_details* p_flags_opcode = NULL;
  struct jit_uop* p_flags_uop = NULL;
  struct jit_opcode_details* p_lda_opcode = NULL;
  struct jit_uop* p_lda_uop = NULL;
  struct jit_opcode_details* p_ldx_opcode = NULL;
  struct jit_uop* p_ldx_uop = NULL;
  struct jit_opcode_details* p_ldy_opcode = NULL;
  struct jit_uop* p_ldy_uop = NULL;

  (void) p_compiler;
  (void) flag_decimal;

  for (i_opcodes = 0; i_opcodes < num_opcodes; ++i_opcodes) {
    uint32_t i_uops;
    uint32_t num_uops;
    uint8_t opreg;

    struct jit_opcode_details* p_opcode = &p_opcodes[i_opcodes];
    uint8_t opcode_6502 = p_opcode->opcode_6502;
    uint16_t operand_6502 = p_opcode->operand_6502;
    uint8_t optype = g_optypes[opcode_6502];
    uint8_t opmode = g_opmodes[opcode_6502];
    uint8_t opbranch = g_opbranch[optype];
    int changes_nz = g_optype_changes_nz_flags[optype];
    int changes_carry = g_optype_changes_carry[optype];

    p_opcode->reg_a = reg_a;
    p_opcode->reg_x = reg_x;
    p_opcode->reg_y = reg_y;
    p_opcode->flag_carry = flag_carry;
    p_opcode->flag_decimal = flag_carry;
    p_opcode->num_fixup_uops = 0;
    p_opcode->len_bytes_6502_merged = p_opcode->len_bytes_6502_orig;
    p_opcode->max_cycles_merged = p_opcode->max_cycles_orig;
    p_opcode->eliminated = 0;

    /* TODO: seems hacky, should g_optype_sets_register just be per-opcode? */
    opreg = g_optype_sets_register[optype];
    if (opmode == k_acc) {
      opreg = k_a;
    }

    /* Merge opcode into previous if supported. */
    if ((p_prev_opcode != NULL) &&
        (opcode_6502 == p_prev_opcode->opcode_6502)) {
      int32_t old_uopcode = -1;
      int32_t new_uopcode = -1;
      switch (opcode_6502) {
      case 0x0A: /* ASL A */
        old_uopcode = 0x0A;
        new_uopcode = k_opcode_ASL_ACC_n;
        break;
      case 0x2A: /* ROL A */
        old_uopcode = 0x2A;
        new_uopcode = k_opcode_ROL_ACC_n;
        break;
      case 0x4A: /* LSR A */
        old_uopcode = 0x4A;
        new_uopcode = k_opcode_LSR_ACC_n;
        break;
      case 0x6A: /* ROR A */
        old_uopcode = 0x6A;
        new_uopcode = k_opcode_ROR_ACC_n;
        break;
      default:
        break;
      }

      if (old_uopcode != -1) {
        struct jit_uop* p_modify_uop = jit_compiler_find_uop(p_compiler,
                                                             p_prev_opcode,
                                                             old_uopcode);
        if (p_modify_uop != NULL) {
          p_modify_uop->uopcode = new_uopcode;
          p_modify_uop->value1 = 1;
        } else {
          p_modify_uop = jit_compiler_find_uop(p_compiler,
                                               p_prev_opcode,
                                               new_uopcode);
        }
        assert(p_modify_uop != NULL);
        p_opcode->eliminated = 1;
        p_modify_uop->value1++;
        p_prev_opcode->len_bytes_6502_merged += p_opcode->len_bytes_6502_orig;
        p_prev_opcode->max_cycles_merged += p_opcode->max_cycles_orig;

        continue;
      }
    }

    /* Cancel pending optimizations that can't cross this opcode. */
    if (opbranch != k_bra_n) {
      p_flags_opcode = NULL;
      p_flags_uop = NULL;
    }
    if (optype == k_php) {
      p_flags_opcode = NULL;
      p_flags_uop = NULL;
    }
    if ((optype != k_sta) &&
        (optype != k_stx) &&
        (optype != k_sty) &&
        (optype != k_lda) &&
        (optype != k_ldx) &&
        (optype != k_ldy)) {
      /* NOTE: can widen the optypes above if we wanted to. */
      p_lda_opcode = NULL;
      p_lda_uop = NULL;
      p_ldx_opcode = NULL;
      p_ldx_uop = NULL;
      p_ldy_opcode = NULL;
      p_ldy_uop = NULL;
    }
    /* NOTE: currently don't optimize immediate stores other than zpg and abs
     * modes.
     */
    if ((optype == k_sta) && (opmode != k_zpg) && (opmode != k_abs)) {
      p_lda_opcode = NULL;
      p_lda_uop = NULL;
    }
    if ((optype == k_stx) && (opmode != k_zpg) && (opmode != k_abs)) {
      p_ldx_opcode = NULL;
      p_ldx_uop = NULL;
    }
    if ((optype == k_sty) && (opmode != k_zpg) && (opmode != k_abs)) {
      p_ldy_opcode = NULL;
      p_ldy_uop = NULL;
    }
    if (opmode == k_idx || opmode == k_zpx || opmode == k_abx) {
      p_ldx_opcode = NULL;
      p_ldx_uop = NULL;
    }
    if (opmode == k_idy || opmode == k_zpy || opmode == k_aby) {
      p_ldy_opcode = NULL;
      p_ldy_uop = NULL;
    }

    /* Eliminate useless NZ flag updates. */
    if ((p_flags_opcode != NULL) && changes_nz) {
      jit_compiler_eliminate(p_compiler,
                             &p_flags_opcode,
                             &p_flags_uop,
                             p_opcode);
    }
    /* Eliminate constant loads that are folded into other opcodes. */
    if ((p_lda_opcode != NULL) && ((optype == k_lda) ||
                                   (optype == k_pla) ||
                                   (optype == k_txa) ||
                                   (optype == k_tya))) {
      jit_compiler_eliminate(p_compiler, &p_lda_opcode, &p_lda_uop, p_opcode);
    }
    if ((p_ldx_opcode != NULL) && ((optype == k_ldx) ||
                                   (optype == k_tax) ||
                                   (optype == k_tsx))) {
      jit_compiler_eliminate(p_compiler, &p_ldx_opcode, &p_ldx_uop, p_opcode);
    }
    if ((p_ldy_opcode != NULL) && ((optype == k_ldy) || (optype == k_tay))) {
      jit_compiler_eliminate(p_compiler, &p_ldy_opcode, &p_ldy_uop, p_opcode);
    }

    num_uops = p_opcode->num_uops;
    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct jit_uop* p_uop = &p_opcode->uops[i_uops];
      int32_t uopcode = p_uop->uopcode;

      p_uop->eliminated = 0;

      /* Keep track of uops we may be able to eliminate. */
      switch (uopcode) {
      case k_opcode_FLAGA:
      case k_opcode_FLAGX:
      case k_opcode_FLAGY:
        p_flags_opcode = p_opcode;
        p_flags_uop = p_uop;
        break;
      case k_opcode_LDA_Z:
      case 0xA9:
        p_lda_opcode = p_opcode;
        p_lda_uop = p_uop;
        break;
      case k_opcode_LDX_Z:
      case 0xA2:
        p_ldx_opcode = p_opcode;
        p_ldx_uop = p_uop;
        break;
      case k_opcode_LDY_Z:
      case 0xA0:
        p_ldy_opcode = p_opcode;
        p_ldy_uop = p_uop;
        break;
      default:
        break;
      }

      /* Optimize uopcodes that can be directly replaced based on knowledge of
       * current register / flag values.
       */
      switch (uopcode) {
      case 0x61: /* ADC idx */
      case 0x75: /* ADC zpx */
        if (flag_carry == 0) {
          p_uop->uopcode = k_opcode_ADD_scratch;
        }
        break;
      case 0x65: /* ADC zpg */
      case 0x6D: /* ADC abs */
        if (flag_carry == 0) {
          p_uop->uopcode = k_opcode_ADD_ABS;
        }
        break;
      case 0x69: /* ADC imm */
        if (flag_carry == 0) {
          p_uop->uopcode = k_opcode_ADD_IMM;
        } else if (flag_carry == 1) {
          /* NOTE: if this is common, we can optimize this case. */
          printf("LOG:JIT:optimizer sees ADC #$imm with C==1\n");
        }
        break;
      case 0x71: /* ADC idy */
        if (flag_carry == 0) {
          p_uop->uopcode = k_opcode_ADD_scratch_Y;
        }
        break;
      case 0x79: /* ADC aby */
        if (flag_carry == 0) {
          p_uop->uopcode = k_opcode_ADD_ABY;
        }
        break;
      case 0x7D: /* ADC abx */
        if (flag_carry == 0) {
          p_uop->uopcode = k_opcode_ADD_ABX;
        }
        break;
      case 0x84: /* STY zpg */
      case 0x8C: /* STY abs */
        if (reg_y != k_value_unknown) {
          p_uop->uopcode = k_opcode_STOA_IMM;
          p_uop->value2 = reg_y;
        }
        break;
      case 0x85: /* STA zpg */
      case 0x8D: /* STA abs */
        if (reg_a != k_value_unknown) {
          p_uop->uopcode = k_opcode_STOA_IMM;
          p_uop->value2 = reg_a;
        }
        break;
      case 0x86: /* STX zpg */
      case 0x8E: /* STX abs */
        if (reg_x != k_value_unknown) {
          p_uop->uopcode = k_opcode_STOA_IMM;
          p_uop->value2 = reg_x;
        }
        break;
      case 0xE9: /* SBC imm */
        if (flag_carry == 1) {
          p_uop->uopcode = k_opcode_SUB_IMM;
        }
        break;
      default:
        break;
      }
    }

    /* Update known state of registers, flags, etc. for next opcode. */
    switch (opreg) {
    case k_a:
      reg_a = k_value_unknown;
      break;
    case k_x:
      reg_x = k_value_unknown;
      break;
    case k_y:
      reg_y = k_value_unknown;
      break;
    default:
      break;
    }

    if (changes_carry) {
      flag_carry = k_value_unknown;
    }

    switch (opcode_6502) {
    case 0x18: /* CLC */
      flag_carry = 0;
      break;
    case 0x38: /* SEC */
      flag_carry = 1;
      break;
    case 0xA0: /* LDY imm */
      reg_y = operand_6502;
      break;
    case 0xA2: /* LDX imm */
      reg_x = operand_6502;
      break;
    case 0xA9: /* LDA imm */
      reg_a = operand_6502;
      break;
    case 0xD8: /* CLD */
      flag_decimal = 0;
      break;
    case 0xF8: /* SED */
      flag_decimal = 1;
      break;
    default:
      break;
    }

    p_prev_opcode = p_opcode;
  }
}

static void
jit_compiler_prepend_uop(struct jit_opcode_details* p_details,
                         int uopcode,
                         int32_t value1,
                         int32_t value2) {
  uint8_t num_uops = p_details->num_uops;
  struct jit_uop* p_uop = &p_details->uops[0];
  assert(num_uops < k_max_uops_per_opcode);

  (void) memmove(&p_details->uops[1],
                 p_uop,
                 (sizeof(struct jit_uop) * num_uops));
  p_uop->uopcode = uopcode;
  p_uop->uoptype = -1;
  p_uop->value1 = value1;
  p_uop->value2 = value2;

  p_details->num_uops++;
}

static void
jit_compiler_set_internal_opcode(struct jit_opcode_details* p_details,
                                 int32_t uopcode,
                                 int32_t value1) {
  (void) memset(p_details, '\0', sizeof(struct jit_opcode_details));
  p_details->cycles_run_start = -1;
  p_details->num_uops = 1;
  p_details->uops[0].uopcode = uopcode;
  p_details->uops[0].uoptype = 1;
  p_details->uops[0].value1 = value1;
  p_details->uops[0].value2 = 0;
}

void
jit_compiler_compile_block(struct jit_compiler* p_compiler,
                           struct util_buffer* p_buf,
                           uint16_t start_addr_6502) {
  struct jit_opcode_details opcode_details[k_max_opcodes_per_compile];
  uint8_t single_opcode_buffer[128];
  struct jit_uop temp_uop;

  uint32_t i_opcodes;
  uint32_t i_uops;
  uint16_t addr_6502;
  uint32_t cycles;
  int needs_countdown;
  struct jit_opcode_details* p_details;
  struct jit_opcode_details* p_details_fixup;
  struct jit_opcode_details* p_prev_opcode;
  struct jit_uop* p_uop;

  struct util_buffer* p_single_opcode_buf = p_compiler->p_single_opcode_buf;
  /* total_num_opcodes includes internally generated opcodes such as jumping
   * from the end of a block to the start of the next. total_num_6502_opcodes
   * is a count of real 6502 opcodes consumed only.
   */
  uint32_t total_num_opcodes = 0;
  uint32_t total_num_6502_opcodes = 0;
  int block_ended = 0;

  assert(!util_buffer_get_pos(p_buf));

  jit_invalidate_block_with_addr(p_compiler, start_addr_6502);

  if (!p_compiler->addr_is_block_continuation[start_addr_6502]) {
    p_compiler->addr_is_block_start[start_addr_6502] = 1;
  } else {
    p_compiler->addr_is_block_start[start_addr_6502] = 0;
  }

  /* First break all the opcodes for this run into uops.
   * This defines maximum possible bounds for the block and respects existing
   * known block boundaries.
   */
  addr_6502 = start_addr_6502;
  needs_countdown = 1;
  while (1) {
    p_details = &opcode_details[total_num_opcodes];

    jit_compiler_get_opcode_details(p_compiler, p_details, addr_6502);

    if (needs_countdown) {
      p_details->cycles_run_start = 0;
      /* TODO: use a separate internal opcode, not a prepend. */
      jit_compiler_prepend_uop(p_details, k_opcode_countdown, addr_6502, 0);
      needs_countdown = 0;
    }

    addr_6502 += p_details->len_bytes_6502_orig;
    total_num_opcodes++;
    total_num_6502_opcodes++;

    if (p_details->ends_block) {
      block_ended = 1;
      break;
    }

    if (p_compiler->addr_is_block_start[addr_6502]) {
      break;
    }

    if ((total_num_opcodes == (k_max_opcodes_per_compile - 1)) ||
        (total_num_6502_opcodes == p_compiler->max_6502_opcodes_per_block)) {
      break;
    }

    if (p_details->branches == k_bra_m) {
      needs_countdown = 1;
    }
  }

  assert(addr_6502 > start_addr_6502);

  if (!block_ended) {
    p_details = &opcode_details[total_num_opcodes];
    total_num_opcodes++;

    jit_compiler_set_internal_opcode(p_details, 0x4C, addr_6502);
    p_details->ends_block = 1;
  }

  /* Second, walk the opcode list and apply any fixups or adjustments. */
  p_uop = NULL;
  p_details_fixup = NULL;
  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    p_details = &opcode_details[i_opcodes];
    if (p_details->cycles_run_start != -1) {
      p_details_fixup = p_details;
      assert(p_details_fixup->num_uops >= 2);
      assert(p_details_fixup->cycles_run_start == 0);
      p_uop = &p_details_fixup->uops[0];
      assert(p_uop->uopcode == k_opcode_countdown);
      assert(p_uop->value2 == 0);
    }
    p_details_fixup->cycles_run_start += p_details->max_cycles_orig;
    p_uop->value2 = p_details_fixup->cycles_run_start;
  }

  /* Third, run the optimizer across the list of opcodes. */
  jit_compiler_optimize(p_compiler, &opcode_details[0], total_num_opcodes);

  /* Fourth, emit the uop stream to the output buffer. This finalizes the number
   * of opcodes compiled, which may get smaller if we run out of space in the
   * binary output buffer.
   */
  p_prev_opcode = NULL;
  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    uint8_t num_uops;
    size_t buf_needed;
    void* p_host_address;

    p_details = &opcode_details[i_opcodes];
    if (p_details->eliminated) {
      continue;
    }
    addr_6502 = p_details->addr_6502;

    util_buffer_setup(p_single_opcode_buf,
                      &single_opcode_buffer[0],
                      sizeof(single_opcode_buffer));

    p_host_address = (util_buffer_get_base_address(p_buf) +
                      util_buffer_get_pos(p_buf));
    util_buffer_set_base_address(p_single_opcode_buf, p_host_address);

    num_uops = p_details->num_uops;
    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      size_t len_x64 = util_buffer_get_pos(p_single_opcode_buf);
      p_uop = &p_details->uops[i_uops];
      if (p_uop->eliminated) {
        continue;
      }
      jit_compiler_emit_uop(p_compiler, p_single_opcode_buf, p_uop);
      len_x64 = (util_buffer_get_pos(p_single_opcode_buf) - len_x64);
      p_uop->len_x64 = len_x64;

      /* Need at least 2 bytes because that the length of the self-modification
       * overwrite.
       */
      assert(len_x64 >= 2);
    }

    /* Calculate if this opcode fits. In order to fit, not only must the opcode
     * itself fit, but there must be space to commit any eliminated uops plus
     * space for a possible final jump to the block continuation.
     */
    buf_needed = util_buffer_get_pos(p_single_opcode_buf);
    if (!p_details->ends_block) {
      buf_needed += p_compiler->len_x64_jmp;
    }
    for (i_uops = 0; i_uops < p_details->num_fixup_uops; ++i_uops) {
      p_uop = p_details->fixup_uops[i_uops];
      assert(p_uop->eliminated);
      switch (p_uop->uopcode) {
      case k_opcode_FLAGA:
        buf_needed += p_compiler->len_x64_FLAGA;
        break;
      case k_opcode_FLAGX:
        buf_needed += p_compiler->len_x64_FLAGX;
        break;
      case k_opcode_FLAGY:
        buf_needed += p_compiler->len_x64_FLAGY;
        break;
      case k_opcode_LDA_Z:
        buf_needed += p_compiler->len_x64_LDA_Z;
        break;
      case 0xA9: /* LDA imm */
        buf_needed += p_compiler->len_x64_0xA9;
        break;
      case k_opcode_LDX_Z:
        buf_needed += p_compiler->len_x64_LDX_Z;
        break;
      case 0xA2: /* LDX imm */
        buf_needed += p_compiler->len_x64_0xA2;
        break;
      case k_opcode_LDY_Z:
        buf_needed += p_compiler->len_x64_LDY_Z;
        break;
      case 0xA0: /* LDY imm */
        buf_needed += p_compiler->len_x64_0xA0;
        break;
      default:
        assert(0);
        break;
      }
    }

    /* If the opcode doesn't fit, execute any uops that are eliminated at the
     * current position, and execute a jump to the block continuation.
     */
    if (util_buffer_remaining(p_buf) < buf_needed) {
      p_compiler->addr_is_block_continuation[addr_6502] = 1;
      util_buffer_set_pos(p_single_opcode_buf, 0);
      assert(p_prev_opcode != NULL);
      for (i_uops = 0; i_uops < p_prev_opcode->num_fixup_uops; ++i_uops) {
        p_uop = p_prev_opcode->fixup_uops[i_uops];
        jit_compiler_emit_uop(p_compiler, p_single_opcode_buf, p_uop);
      }
      temp_uop.uopcode = 0x4C; /* JMP abs */
      temp_uop.value1 = addr_6502;
      temp_uop.value2 = 0;
      jit_compiler_emit_uop(p_compiler, p_single_opcode_buf, &temp_uop);

      util_buffer_append(p_buf, p_single_opcode_buf);
      break;
    }

    util_buffer_append(p_buf, p_single_opcode_buf);

    p_details->p_host_address = p_host_address;
    p_prev_opcode = p_details;
  }

  total_num_opcodes = i_opcodes;

  /* Fifth, update any values (metadata and/or binary) that may have changed
   * now we know the full extent of the emitted binary.
   */
  p_uop = NULL;
  p_details_fixup = NULL;
  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    p_details = &opcode_details[i_opcodes];
    if (p_details->eliminated) {
      continue;
    }
    if (p_details->cycles_run_start != -1) {
      p_details_fixup = p_details;
      p_uop = &p_details_fixup->uops[0];
      assert(p_details_fixup->num_uops >= 2);
      assert(p_uop->uopcode == k_opcode_countdown);
      p_details_fixup->cycles_run_start = 0;
      p_uop->value2 = 0;

      util_buffer_setup(p_single_opcode_buf,
                        p_details_fixup->p_host_address,
                        p_uop->len_x64);
      /* The replacement uop could be shorter (e.g. 4-byte length -> 1-byte) but
       * never longer so fill with nop.
       */
      util_buffer_fill_to_end(p_single_opcode_buf, '\x90');
    }

    p_details_fixup->cycles_run_start += p_details->max_cycles_merged;
    p_uop->value2 = p_details_fixup->cycles_run_start;
    util_buffer_set_pos(p_single_opcode_buf, 0);
    jit_compiler_emit_uop(p_compiler, p_single_opcode_buf, p_uop);
  }

  /* Sixth, update compiler metadata. */
  cycles = 0;
  p_prev_opcode = NULL;
  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    uint8_t num_bytes_6502;
    uint8_t i;
    uint32_t jit_ptr;
    uint16_t addr_6502;

    p_details = &opcode_details[i_opcodes];
    if (p_details->eliminated) {
      continue;
    }
    addr_6502 = p_details->addr_6502;
    if (p_details->cycles_run_start != -1) {
      cycles = p_details->cycles_run_start;
    }

    num_bytes_6502 = p_details->len_bytes_6502_merged;
    jit_ptr = (uint32_t) (size_t) p_details->p_host_address;
    for (i = 0; i < num_bytes_6502; ++i) {
      p_compiler->p_jit_ptrs[addr_6502] = jit_ptr;

      if (addr_6502 != start_addr_6502) {
        jit_invalidate_jump_target(p_compiler, addr_6502);
        p_compiler->addr_is_block_start[addr_6502] = 0;
      }

      p_compiler->addr_is_block_continuation[addr_6502] = 0;

      p_compiler->addr_nz_fixup[addr_6502] = 0;
      p_compiler->addr_a_fixup[addr_6502] = -1;
      p_compiler->addr_x_fixup[addr_6502] = -1;
      p_compiler->addr_y_fixup[addr_6502] = -1;

      if (i == 0) {
        p_compiler->addr_opcode[addr_6502] = p_details->opcode_6502;

        p_compiler->addr_cycles_fixup[addr_6502] = cycles;
        if (p_prev_opcode != NULL) {
          for (i_uops = 0; i_uops < p_prev_opcode->num_fixup_uops; ++i_uops) {
            p_uop = p_prev_opcode->fixup_uops[i_uops];
            switch (p_uop->uopcode) {
            case k_opcode_FLAGA:
              p_compiler->addr_nz_fixup[addr_6502] = k_a;
              break;
            case k_opcode_FLAGX:
              p_compiler->addr_nz_fixup[addr_6502] = k_x;
              break;
            case k_opcode_FLAGY:
              p_compiler->addr_nz_fixup[addr_6502] = k_y;
              break;
            case k_opcode_LDA_Z:
              p_compiler->addr_a_fixup[addr_6502] = 0;
              break;
            case 0xA9: /* LDA imm */
              p_compiler->addr_a_fixup[addr_6502] = (uint8_t) p_uop->value1;
              break;
            case k_opcode_LDX_Z:
              p_compiler->addr_x_fixup[addr_6502] = 0;
              break;
            case 0xA2: /* LDX imm */
              p_compiler->addr_x_fixup[addr_6502] = (uint8_t) p_uop->value1;
              break;
            case k_opcode_LDY_Z:
              p_compiler->addr_y_fixup[addr_6502] = 0;
              break;
            case 0xA0: /* LDY imm */
              p_compiler->addr_y_fixup[addr_6502] = (uint8_t) p_uop->value1;
              break;
            default:
              assert(0);
              break;
            }
          }
        }
      } else {
        p_compiler->addr_opcode[addr_6502] = -1;

        p_compiler->addr_cycles_fixup[addr_6502] = -1;
      }

      addr_6502++;
    }
    cycles -= p_details->max_cycles_merged;
    p_prev_opcode = p_details;
  }

  /* Fill the unused portion of the buffer with 0xcc, i.e. int3.
   * There are a few good reasons for this:
   * 1) Clarity: see where a code block ends, especially if there was
   * previously a larger code block at this address.
   * 2) Bug detection: better chance of a clean crash if something does a bad
   * jump.
   * 3) Performance. int3 will stop the Intel instruction decoder.
   */
  util_buffer_fill_to_end(p_buf, '\xcc');
}

int64_t
jit_compiler_fixup_state(struct jit_compiler* p_compiler,
                         struct state_6502* p_state_6502,
                         int64_t countdown) {
  uint16_t pc_6502 = p_state_6502->reg_pc;
  int32_t cycles_fixup = p_compiler->addr_cycles_fixup[pc_6502];
  uint8_t nz_fixup = p_compiler->addr_nz_fixup[pc_6502];
  int32_t a_fixup = p_compiler->addr_a_fixup[pc_6502];
  int32_t x_fixup = p_compiler->addr_x_fixup[pc_6502];
  int32_t y_fixup = p_compiler->addr_y_fixup[pc_6502];

  assert(cycles_fixup > 0);
  countdown += cycles_fixup;

  if (a_fixup != -1) {
    p_state_6502->reg_a = a_fixup;
  }
  if (x_fixup != -1) {
    p_state_6502->reg_x = x_fixup;
  }
  if (y_fixup != -1) {
    p_state_6502->reg_y = y_fixup;
  }
  if (nz_fixup != 0) {
    uint8_t reg_val = 0;
    uint8_t flag_n;
    uint8_t flag_z;
    uint8_t flags_new;
    switch (nz_fixup) {
    case k_a:
      reg_val = p_state_6502->reg_a;
      break;
    case k_x:
      reg_val = p_state_6502->reg_x;
      break;
    case k_y:
      reg_val = p_state_6502->reg_y;
      break;
    default:
      assert(0);
      break;
    }

    flag_n = !!(reg_val & 0x80);
    flag_z = (reg_val == 0);
    flags_new = 0;
    flags_new |= (flag_n << k_flag_negative);
    flags_new |= (flag_z << k_flag_zero);
    p_state_6502->reg_flags &= ~((1 << k_flag_negative) | (1 << k_flag_zero));
    p_state_6502->reg_flags |= flags_new;
  }

  return countdown;
}

void
jit_compiler_memory_range_invalidate(struct jit_compiler* p_compiler,
                                     uint16_t addr,
                                     uint16_t len) {
  uint32_t i;

  uint32_t addr_end = (addr + len);

  assert(addr_end >= addr);

  for (i = addr; i < addr_end; ++i) {
    p_compiler->addr_opcode[i] = -1;
    p_compiler->addr_is_block_start[i] = 0;
    p_compiler->addr_is_block_continuation[i] = 0;

    p_compiler->addr_cycles_fixup[i] = -1;
    p_compiler->addr_nz_fixup[i] = 0;
    p_compiler->addr_a_fixup[i] = -1;
    p_compiler->addr_x_fixup[i] = -1;
    p_compiler->addr_y_fixup[i] = -1;
  }
}

int
jit_compiler_is_compiling_for_code_in_zero_page(
    struct jit_compiler* p_compiler) {
  return p_compiler->compile_for_code_in_zero_page;
}

void
jit_compiler_set_compiling_for_code_in_zero_page(
    struct jit_compiler* p_compiler, int value) {
  p_compiler->compile_for_code_in_zero_page = value;
}
