#include "jit_compiler.h"

#include "bbc_options.h"
#include "defs_6502.h"
#include "jit_opcode.h"
#include "jit_optimizer.h"
#include "log.h"
#include "memory_access.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"

#include "asm/asm_common.h"
#include "asm/asm_defs_host.h"
#include "asm/asm_jit.h"
#include "asm/asm_jit_defs.h"
#include "asm/asm_opcodes.h"

#include <assert.h>
#include <string.h>

enum {
  k_opcode_history_length = 8,
};

struct jit_compile_history {
  uint64_t times[k_opcode_history_length];
  int32_t opcodes[k_opcode_history_length];
  uint8_t was_self_modified[k_opcode_history_length];
  uint32_t ring_buffer_index;
  int32_t opcode;
};

enum {
  k_max_addr_space_per_compile = 512,
};

struct jit_compiler {
  struct asm_jit_struct* p_asm;
  struct timing_struct* p_timing;
  struct memory_access* p_memory_access;
  uint8_t* p_mem_read;
  void* (*get_block_host_address)(void* p, uint16_t addr);
  void* p_host_address_object;
  uint32_t* p_jit_ptrs;
  int32_t* p_code_blocks;
  int debug;
  int log_dynamic;
  uint8_t* p_opcode_types;
  uint8_t* p_opcode_modes;
  uint8_t* p_opcode_mem;
  uint8_t* p_opcode_cycles;

  int option_accurate_timings;
  int option_no_optimize;
  int option_no_dynamic_operand;
  int option_no_dynamic_opcode;
  int option_no_sub_instruction;
  uint32_t max_6502_opcodes_per_block;
  uint32_t dynamic_trigger;

  struct util_buffer* p_tmp_buf;
  struct util_buffer* p_single_uopcode_buf;
  struct util_buffer* p_single_uopcode_epilog_buf;
  uint32_t jit_ptr_no_code;
  uint32_t jit_ptr_dynamic_operand;

  uint32_t len_asm_jmp;
  uint32_t len_asm_invalidated;
  uint32_t len_asm_nop;

  int compile_for_code_in_zero_page;

  struct jit_compile_history history[k_6502_addr_space_size];
  uint8_t addr_is_block_start[k_6502_addr_space_size];
  uint8_t addr_is_block_continuation[k_6502_addr_space_size];

  int32_t addr_cycles_fixup[k_6502_addr_space_size];
  uint8_t addr_nz_fixup[k_6502_addr_space_size];
  int32_t addr_nz_mem_fixup[k_6502_addr_space_size];
  uint8_t addr_o_fixup[k_6502_addr_space_size];
  uint8_t addr_c_fixup[k_6502_addr_space_size];
  int32_t addr_a_fixup[k_6502_addr_space_size];
  int32_t addr_x_fixup[k_6502_addr_space_size];
  int32_t addr_y_fixup[k_6502_addr_space_size];

  /* State used within jit_compiler_compile_block() and subroutines. */
  struct jit_opcode_details opcode_details[k_max_addr_space_per_compile];
  struct jit_opcode_details* p_last_opcode;
  uint16_t start_addr_6502;
  int has_unresolved_jumps;
};

static void
jit_invalidate_jump_target(struct jit_compiler* p_compiler, uint16_t addr) {
  void* p_host_ptr =
      p_compiler->get_block_host_address(p_compiler->p_host_address_object,
                                         addr);
  asm_jit_invalidate_code_at(p_host_ptr);
}

static int32_t
jit_compiler_get_current_opcode(struct jit_compiler* p_compiler,
                                uint16_t addr_6502) {
  return p_compiler->history[addr_6502].opcode;
}

static int
jit_has_invalidated_code(struct jit_compiler* p_compiler, uint16_t addr_6502) {
  void* p_host_ptr =
      p_compiler->get_block_host_address(p_compiler->p_host_address_object,
                                         addr_6502);
  uint32_t jit_ptr = p_compiler->p_jit_ptrs[addr_6502];

  (void) p_host_ptr;
  assert(jit_ptr != (uint32_t) (size_t) p_host_ptr);

  /* TODO: this shouldn't be necessary. Is invalidating a range not clearing
   * JIT pointers properly?
   */
  if (jit_compiler_get_current_opcode(p_compiler, addr_6502) == -1) {
    return 0;
  }

  if (jit_ptr == p_compiler->jit_ptr_no_code) {
    return 0;
  }

  assert(p_compiler->p_code_blocks[addr_6502] != -1);


  return asm_jit_is_invalidated_code_at((void*) (uintptr_t) jit_ptr);
}

struct jit_compiler*
jit_compiler_create(struct asm_jit_struct* p_asm,
                    struct timing_struct* p_timing,
                    struct memory_access* p_memory_access,
                    void* (*get_block_host_address)(void*, uint16_t),
                    void* p_host_address_object,
                    uint32_t* p_jit_ptrs,
                    uint32_t jit_ptr_no_code,
                    uint32_t jit_ptr_dynamic_operand,
                    int32_t* p_code_blocks,
                    struct bbc_options* p_options,
                    int debug,
                    uint8_t* p_opcode_types,
                    uint8_t* p_opcode_modes,
                    uint8_t* p_opcode_mem,
                    uint8_t* p_opcode_cycles) {
  uint32_t i;
  struct util_buffer* p_tmp_buf;
  uint8_t buf[256];
  struct asm_uop tmp_uop;

  uint32_t max_6502_opcodes_per_block = 65536;
  uint32_t dynamic_trigger = 4;

  struct jit_compiler* p_compiler = util_mallocz(sizeof(struct jit_compiler));

  /* Check invariants required for compact code generation. */
  assert(K_JIT_CONTEXT_OFFSET_JIT_PTRS < 0x80);

  p_compiler->p_asm = p_asm;
  p_compiler->p_timing = p_timing;
  p_compiler->p_memory_access = p_memory_access;
  p_compiler->p_mem_read = p_memory_access->p_mem_read;
  p_compiler->get_block_host_address = get_block_host_address;
  p_compiler->p_host_address_object = p_host_address_object;
  p_compiler->p_jit_ptrs = p_jit_ptrs;
  p_compiler->p_code_blocks = p_code_blocks;
  p_compiler->debug = debug;
  p_compiler->p_opcode_types = p_opcode_types;
  p_compiler->p_opcode_modes = p_opcode_modes;
  p_compiler->p_opcode_mem = p_opcode_mem;
  p_compiler->p_opcode_cycles = p_opcode_cycles;

  p_compiler->option_accurate_timings = util_has_option(p_options->p_opt_flags,
                                                        "jit:accurate-timings");
  if (p_options->accurate) {
    p_compiler->option_accurate_timings = 1;
  }
  p_compiler->option_no_optimize = util_has_option(p_options->p_opt_flags,
                                                   "jit:no-optimize");
  p_compiler->option_no_dynamic_operand =
      util_has_option(p_options->p_opt_flags, "jit:no-dynamic-operand");
  p_compiler->option_no_dynamic_opcode =
      util_has_option(p_options->p_opt_flags, "jit:no-dynamic-opcode");
  p_compiler->option_no_sub_instruction =
      util_has_option(p_options->p_opt_flags, "jit:no-sub-instruction");
  if (!asm_jit_supports_optimizer()) {
    p_compiler->option_no_optimize = 1;
    p_compiler->option_no_dynamic_operand = 1;
    p_compiler->option_no_dynamic_opcode = 1;
    p_compiler->option_no_sub_instruction = 1;
  }

  p_compiler->log_dynamic = util_has_option(p_options->p_log_flags,
                                            "jit:dynamic");

  (void) util_get_u32_option(&max_6502_opcodes_per_block,
                             p_options->p_opt_flags,
                             "jit:max-ops");
  if (max_6502_opcodes_per_block < 1) {
    max_6502_opcodes_per_block = 1;
  }
  p_compiler->max_6502_opcodes_per_block = max_6502_opcodes_per_block;
  (void) util_get_u32_option(&dynamic_trigger,
                             p_options->p_opt_flags,
                             "jit:dynamic-trigger=");
  if (dynamic_trigger < 1) {
    dynamic_trigger = 1;
  }
  p_compiler->dynamic_trigger = dynamic_trigger;

  p_compiler->compile_for_code_in_zero_page = 0;

  p_tmp_buf = util_buffer_create();
  p_compiler->p_tmp_buf = p_tmp_buf;
  p_compiler->p_single_uopcode_buf = util_buffer_create();
  p_compiler->p_single_uopcode_epilog_buf = util_buffer_create();

  p_compiler->jit_ptr_no_code = jit_ptr_no_code;
  p_compiler->jit_ptr_dynamic_operand = jit_ptr_dynamic_operand;

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_compiler->p_jit_ptrs[i] = p_compiler->jit_ptr_no_code;
    p_compiler->p_code_blocks[i] = -1;
  }

  /* Calculate lengths of sequences we need to know. */
  util_buffer_setup(p_tmp_buf, &buf[0], sizeof(buf));
  util_buffer_set_base_address(p_tmp_buf, NULL);
  /* Note: target pointer is a short jump range, in case the asm backend emits
   * a shorter sequence for that.
  */
  jit_opcode_make_uop1(&tmp_uop, k_opcode_JMP, 0);
  asm_emit_jit(p_compiler->p_asm, p_tmp_buf, NULL, &tmp_uop);
  p_compiler->len_asm_jmp = util_buffer_get_pos(p_tmp_buf);

  util_buffer_setup(p_tmp_buf, &buf[0], sizeof(buf));
  asm_emit_jit_invalidated(p_tmp_buf);
  p_compiler->len_asm_invalidated = util_buffer_get_pos(p_tmp_buf);

  util_buffer_setup(p_tmp_buf, &buf[0], sizeof(buf));
  asm_emit_instruction_REAL_NOP(p_tmp_buf);
  p_compiler->len_asm_nop = util_buffer_get_pos(p_tmp_buf);
  assert(p_compiler->len_asm_nop > 0);

  return p_compiler;
}

void
jit_compiler_destroy(struct jit_compiler* p_compiler) {
  util_buffer_destroy(p_compiler->p_tmp_buf);
  util_buffer_destroy(p_compiler->p_single_uopcode_buf);
  util_buffer_destroy(p_compiler->p_single_uopcode_epilog_buf);
  util_free(p_compiler);
}

static struct jit_opcode_details*
jit_compiler_get_opcode_for_6502_addr(struct jit_compiler* p_compiler,
                                      uint16_t addr) {
  uint16_t start_addr_6502 = p_compiler->start_addr_6502;
  uint16_t end_addr_6502 = (start_addr_6502 + k_max_addr_space_per_compile);
  if (addr < start_addr_6502) {
    return NULL;
  }
  if (addr >= end_addr_6502) {
    return NULL;
  }

  return &p_compiler->opcode_details[addr - start_addr_6502];
}

static void*
jit_compiler_resolve_branch_target(struct jit_compiler* p_compiler,
                                   uint16_t addr_6502) {
  void* p_ret = NULL;
  struct jit_opcode_details* p_target_details =
      jit_compiler_get_opcode_for_6502_addr(p_compiler, addr_6502);

  /* If the branch target address is in our currently compiling block, jump
   * there.
   * (Disabled as it's part of extended block support.)
   */
  if ((p_target_details != NULL) &&
      (p_target_details->addr_6502 != -1) &&
      p_target_details->is_branch_landing_addr) {
    if (p_target_details->p_host_address_prefix_end != NULL) {
      p_ret = p_target_details->p_host_address_prefix_end;
    } else {
      p_compiler->has_unresolved_jumps = 1;
    }
  } else {
    void* p_host_address_object = p_compiler->p_host_address_object;
    p_ret = p_compiler->get_block_host_address(p_host_address_object,
                                               addr_6502);
  }
  return p_ret;
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

  struct memory_access* p_memory_access = p_compiler->p_memory_access;
  uint8_t* p_mem_read = p_compiler->p_mem_read;
  void* p_memory_callback = p_memory_access->p_callback_obj;
  uint16_t addr_plus_1 = (addr_6502 + 1);
  uint16_t addr_plus_2 = (addr_6502 + 2);
  struct asm_uop* p_uop = &p_details->uops[0];
  struct asm_uop* p_first_post_debug_uop = p_uop;
  int use_interp = 0;
  int could_page_cross = 1;
  uint16_t rel_target_6502 = 0;
  uint32_t jit_addr = 0;

  (void) memset(p_details, '\0', sizeof(struct jit_opcode_details));
  p_details->addr_6502 = addr_6502;
  p_details->branch_addr_6502 = -1;
  p_details->min_6502_addr = -1;
  p_details->max_6502_addr = -1;

  opcode_6502 = p_mem_read[addr_6502];
  optype = p_compiler->p_opcode_types[opcode_6502];
  opmode = p_compiler->p_opcode_modes[opcode_6502];
  opmem = p_compiler->p_opcode_mem[opcode_6502];

  p_details->opcode_6502 = opcode_6502;
  p_details->num_bytes_6502 = g_opmodelens[opmode];
  p_details->branches = g_opbranch[optype];
  p_details->ends_block = 0;
  if (p_details->branches == k_bra_y) {
    p_details->ends_block = 1;
  }

  p_details->num_fixup_uops = 0;
  p_details->p_host_address_prefix_end = NULL;
  p_details->p_host_address_start = NULL;
  p_details->cycles_run_start = -1;

  if (p_compiler->debug) {
    jit_opcode_make_uop1(p_uop, k_opcode_debug, addr_6502);
    p_uop++;
    p_first_post_debug_uop = p_uop;
  }

  if ((optype == k_adc) || (optype == k_sbc)) {
    jit_opcode_make_uop1(p_uop, k_opcode_check_bcd, addr_6502);
    p_uop++;
  }
  if ((optype == k_adc) ||
      (optype == k_sbc) ||
      (optype == k_rol) ||
      (optype == k_ror) ||
      (optype == k_bcc) ||
      (optype == k_bcs)) {
    jit_opcode_make_uop0(p_uop, k_opcode_load_carry);
    p_uop++;
  }
  if ((optype == k_bvc) || (optype == k_bvs)) {
    jit_opcode_make_uop0(p_uop, k_opcode_load_overflow);
    p_uop++;
  }

  /* Mode resolution and possibly per-mode uops. */
  switch (opmode) {
  case 0:
  case k_nil:
  case k_acc:
    operand_6502 = 0;
    break;
  case k_imm:
    if (optype == k_brk) {
      break;
    }
    operand_6502 = p_mem_read[addr_plus_1];
    jit_opcode_make_uop1(p_uop, k_opcode_value_set, operand_6502);
    p_uop++;
    break;
  case k_zpg:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = operand_6502;
    p_details->max_6502_addr = operand_6502;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_set, operand_6502);
    p_uop++;
    break;
  case k_zpx:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFF;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_set, operand_6502);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_addr_add_x_8bit);
    p_uop++;
    break;
  case k_zpy:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFF;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_set, operand_6502);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_addr_add_y_8bit);
    p_uop++;
    break;
  case k_rel:
    operand_6502 = p_mem_read[addr_plus_1];
    rel_target_6502 = ((int) addr_6502 + 2 + (int8_t) operand_6502);
    p_details->branch_addr_6502 = rel_target_6502;
    jit_addr = (intptr_t) jit_compiler_resolve_branch_target(p_compiler,
                                                             rel_target_6502);
    break;
  case k_abs:
  case k_abx:
  case k_aby:
    operand_6502 = ((p_mem_read[addr_plus_2] << 8) | p_mem_read[addr_plus_1]);
    p_details->min_6502_addr = operand_6502;
    p_details->max_6502_addr = operand_6502;
    if ((optype == k_jmp) || (optype == k_jsr)) {
      /* JMP / JSR abs isn't really "abs" as it's not fetching data from the
       * memory address.
       */
      break;
    }
    jit_opcode_make_uop1(p_uop, k_opcode_addr_set, operand_6502);
    p_uop++;
    if ((operand_6502 & 0xFF) == 0x00) {
      could_page_cross = 0;
    }
    if (opmode == k_abx) {
      jit_opcode_make_uop0(p_uop, k_opcode_addr_add_x);
      p_uop++;
    }
    if (opmode == k_aby) {
      jit_opcode_make_uop0(p_uop, k_opcode_addr_add_y);
      p_uop++;
    }
    if (opmode == k_abx || opmode == k_aby) {
      p_details->max_6502_addr += 0xFF;
      if (p_details->max_6502_addr > 0xFFFF) {
        p_details->min_6502_addr = (p_details->max_6502_addr & 0xFFFF);
        p_details->max_6502_addr = operand_6502;
        /* Use the interpreter for address space wraps. Otherwise the JIT code
         * will do an out-of-bounds access.
         */
        use_interp = 1;
      }
    }

    if (opmem & k_opmem_read_flag) {
      if (p_memory_access->memory_read_needs_callback(
              p_memory_callback, p_details->min_6502_addr)) {
        use_interp = 1;
      }
      if (p_memory_access->memory_read_needs_callback(
              p_memory_callback, p_details->max_6502_addr)) {
        use_interp = 1;
      }
    }
    if (opmem & k_opmem_write_flag) {
      if (p_memory_access->memory_write_needs_callback(
              p_memory_callback, p_details->min_6502_addr)) {
        use_interp = 1;
      }
      if (p_memory_access->memory_write_needs_callback(
              p_memory_callback, p_details->max_6502_addr)) {
        use_interp = 1;
      }
    }
    break;
  case k_ind:
    operand_6502 = ((p_mem_read[addr_plus_2] << 8) | p_mem_read[addr_plus_1]);
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFFFF;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_set, operand_6502);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_value_load_16bit);
    p_uop++;
    break;
  case k_idx:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFFFF;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_set, operand_6502);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_addr_add_x_8bit);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_addr_load_16bit_zpg);
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_check, addr_6502);
    p_uop++;
    break;
  case k_idy:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFFFF;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_set, operand_6502);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_addr_load_16bit_zpg);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_addr_add_y);
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_addr_check, addr_6502);
    p_uop++;
    break;
  default:
    assert(0);
    operand_6502 = 0;
    break;
  }

  if (opmem & k_opmem_read_flag) {
    jit_opcode_make_uop0(p_uop, k_opcode_value_load);
    p_uop++;
  }

  p_details->operand_6502 = operand_6502;

  p_details->max_cycles = p_compiler->p_opcode_cycles[opcode_6502];
  if (p_compiler->option_accurate_timings) {
    if ((opmem & k_opmem_read_flag) &&
        (opmode == k_abx || opmode == k_aby || opmode == k_idy) &&
        could_page_cross) {
      p_details->max_cycles++;
    } else if (opmode == k_rel) {
      /* Taken branches take 1 cycles longer, or 2 cycles longer if there's
       * also a page crossing.
       */
      if (((addr_6502 + 2) >> 8) ^ (rel_target_6502 >> 8)) {
        p_details->max_cycles += 2;
      } else {
        p_details->max_cycles++;
      }
    }
  }

  /* Per-type uops. */
  switch (optype) {
  case k_adc:
    jit_opcode_make_uop0(p_uop, k_opcode_ADC);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_overflow);
    p_uop++;
    break;
  case k_alr:
    jit_opcode_make_uop0(p_uop, k_opcode_ALR);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  case k_and: jit_opcode_make_uop0(p_uop, k_opcode_AND); p_uop++; break;
  case k_asl:
    if (opmode == k_acc) {
      jit_opcode_make_uop0(p_uop, k_opcode_ASL_acc);
      p_uop++;
    } else {
      jit_opcode_make_uop0(p_uop, k_opcode_ASL_value);
      p_uop++;
    }
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  case k_bcc:
    jit_opcode_make_uop1(p_uop, k_opcode_BCC, jit_addr);
    p_uop++;
    break;
  case k_bcs:
    jit_opcode_make_uop1(p_uop, k_opcode_BCS, jit_addr);
    p_uop++;
    break;
  case k_beq:
    jit_opcode_make_uop1(p_uop, k_opcode_BEQ, jit_addr);
    p_uop++;
    break;
  case k_bit:
    jit_opcode_make_uop0(p_uop, k_opcode_BIT); p_uop++; break;
  case k_bmi:
    jit_opcode_make_uop1(p_uop, k_opcode_BMI, jit_addr);
    p_uop++;
    break;
  case k_bne:
    jit_opcode_make_uop1(p_uop, k_opcode_BNE, jit_addr);
    p_uop++;
    break;
  case k_bpl:
    jit_opcode_make_uop1(p_uop, k_opcode_BPL, jit_addr);
    p_uop++;
    break;
  case k_brk:
    jit_opcode_make_uop1(p_uop, k_opcode_PUSH_16, (uint16_t) (addr_6502 + 2));
    p_uop++;
    /* PHP */
    jit_opcode_make_uop0(p_uop, k_opcode_PHP);
    p_uop++;
    /* SEI */
    jit_opcode_make_uop0(p_uop, k_opcode_SEI);
    p_uop++;
    /* MODE_IND */
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_IND_16, k_6502_vector_irq);
    p_uop++;
    /* JMP_SCRATCH_n */
    jit_opcode_make_uop1(p_uop, k_opcode_JMP_SCRATCH_n, 0);
    p_uop++;
    break;
  case k_bvc:
    jit_opcode_make_uop1(p_uop, k_opcode_BVC, jit_addr);
    p_uop++;
    break;
  case k_bvs:
    jit_opcode_make_uop1(p_uop, k_opcode_BVS, jit_addr);
    p_uop++;
    break;
  case k_clc: jit_opcode_make_uop0(p_uop, k_opcode_CLC); p_uop++; break;
  case k_cld: jit_opcode_make_uop0(p_uop, k_opcode_CLD); p_uop++; break;
  case k_cli:
    jit_opcode_make_uop1(p_uop, k_opcode_check_pending_irq, addr_6502);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_CLI);
    p_uop++;
    break;
  case k_clv: jit_opcode_make_uop0(p_uop, k_opcode_CLV); p_uop++; break;
  case k_cmp:
    jit_opcode_make_uop0(p_uop, k_opcode_CMP);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  case k_cpx:
    jit_opcode_make_uop0(p_uop, k_opcode_CPX);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  case k_cpy:
    jit_opcode_make_uop0(p_uop, k_opcode_CPY);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  case k_dec: jit_opcode_make_uop0(p_uop, k_opcode_DEC_value); p_uop++; break;
  case k_dex: jit_opcode_make_uop0(p_uop, k_opcode_DEX); p_uop++; break;
  case k_dey: jit_opcode_make_uop0(p_uop, k_opcode_DEY); p_uop++; break;
  case k_eor: jit_opcode_make_uop0(p_uop, k_opcode_EOR); p_uop++; break;
  case k_inc: jit_opcode_make_uop0(p_uop, k_opcode_INC_value); p_uop++; break;
  case k_inx: jit_opcode_make_uop0(p_uop, k_opcode_INX); p_uop++; break;
  case k_iny: jit_opcode_make_uop0(p_uop, k_opcode_INY); p_uop++; break;
  case k_jmp:
    if (opmode == k_ind) {
      jit_opcode_make_uop1(p_uop, k_opcode_JMP_SCRATCH_n, 0);
      p_uop++;
    } else {
      assert(opmode == k_abs);
      p_details->branch_addr_6502 = operand_6502;
      jit_addr = (intptr_t) jit_compiler_resolve_branch_target(p_compiler,
                                                               operand_6502);
      jit_opcode_make_uop1(p_uop, k_opcode_JMP, jit_addr);
      p_uop++;
    }
    break;
  case k_jsr:
    p_details->branch_addr_6502 = operand_6502;
    jit_addr = (intptr_t) jit_compiler_resolve_branch_target(p_compiler,
                                                             operand_6502);
    jit_opcode_make_uop1(p_uop, k_opcode_PUSH_16, (uint16_t) (addr_6502 + 2));
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_JMP, jit_addr);
    p_uop++;
    break;
  case k_lda: jit_opcode_make_uop0(p_uop, k_opcode_LDA); p_uop++; break;
  case k_ldx: jit_opcode_make_uop0(p_uop, k_opcode_LDX); p_uop++; break;
  case k_ldy: jit_opcode_make_uop0(p_uop, k_opcode_LDY); p_uop++; break;
  case k_lsr:
    if (opmode == k_acc) {
      jit_opcode_make_uop0(p_uop, k_opcode_LSR_acc);
      p_uop++;
    } else {
      jit_opcode_make_uop0(p_uop, k_opcode_LSR_value);
      p_uop++;
    }
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  /* NOTE: sends undocumented modes of NOP along to JIT. Zalaga uses NOP abx
   * in a hot path.
   */
  case k_nop: jit_opcode_make_uop0(p_uop, k_opcode_NOP); p_uop++; break;
  case k_ora: jit_opcode_make_uop0(p_uop, k_opcode_ORA); p_uop++; break;
  case k_pha: jit_opcode_make_uop0(p_uop, k_opcode_PHA); p_uop++; break;
  case k_pla: jit_opcode_make_uop0(p_uop, k_opcode_PLA); p_uop++; break;
  case k_php: jit_opcode_make_uop0(p_uop, k_opcode_PHP); p_uop++; break;
  case k_plp:
    jit_opcode_make_uop1(p_uop, k_opcode_check_pending_irq, addr_6502);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_PLP);
    p_uop++;
    break;
  case k_rol:
    if (opmode == k_acc) {
      jit_opcode_make_uop0(p_uop, k_opcode_ROL_acc);
      p_uop++;
    } else {
      jit_opcode_make_uop0(p_uop, k_opcode_ROL_value);
      p_uop++;
    }
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  case k_ror:
    if (opmode == k_acc) {
      jit_opcode_make_uop0(p_uop, k_opcode_ROR_acc);
      p_uop++;
    } else {
      jit_opcode_make_uop0(p_uop, k_opcode_ROR_value);
      p_uop++;
    }
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    break;
  case k_rti:
    /* Bounce to the interpreter for RTI. The problem with RTI is that it
     * might jump all over the place without any particular pattern, because
     * interrupts will happen all over the place. If we are not careful, over
     * time, RTI will split all of the JIT blocks into 1-instruction blocks,
     * which will be super slow.
     */
    use_interp = 1;
    break;
  case k_rts:
    jit_opcode_make_uop0(p_uop, k_opcode_PULL_16);
    p_uop++;
    /* TODO: may increment 0xFFFF -> 0x10000, which may crash. */
    jit_opcode_make_uop1(p_uop, k_opcode_JMP_SCRATCH_n, 1);
    p_uop++;
    break;
  case k_sax:
    jit_opcode_make_uop0(p_uop, k_opcode_SAX);
    p_uop++;
    /* TODO: value_store should be added generically. */
    jit_opcode_make_uop0(p_uop, k_opcode_value_store);
    p_uop++;
    break;
  case k_sbc:
    jit_opcode_make_uop0(p_uop, k_opcode_SBC);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
    p_uop++;
    jit_opcode_make_uop0(p_uop, k_opcode_save_overflow);
    p_uop++;
    break;
  case k_sec: jit_opcode_make_uop0(p_uop, k_opcode_SEC); p_uop++; break;
  case k_sed: jit_opcode_make_uop0(p_uop, k_opcode_SED); p_uop++; break;
  case k_sei: jit_opcode_make_uop0(p_uop, k_opcode_SEI); p_uop++; break;
  case k_slo:
    /* Only send SLO along to the asm backend for the simple mode used by
     * Zalaga. This avoids the backend having to implement too much for a
     * non-critical opcode.
     */
    if ((opmode == k_abs) || (opmode == k_zpg)) {
      jit_opcode_make_uop0(p_uop, k_opcode_SLO);
      p_uop++;
      jit_opcode_make_uop0(p_uop, k_opcode_save_carry);
      p_uop++;
    } else {
      use_interp = 1;
    }
    break;
  case k_sta: jit_opcode_make_uop0(p_uop, k_opcode_STA); p_uop++; break;
  case k_stx: jit_opcode_make_uop0(p_uop, k_opcode_STX); p_uop++; break;
  case k_sty: jit_opcode_make_uop0(p_uop, k_opcode_STY); p_uop++; break;
  case k_tax: jit_opcode_make_uop0(p_uop, k_opcode_TAX); p_uop++; break;
  case k_tay: jit_opcode_make_uop0(p_uop, k_opcode_TAY); p_uop++; break;
  case k_tsx: jit_opcode_make_uop0(p_uop, k_opcode_TSX); p_uop++; break;
  case k_txs: jit_opcode_make_uop0(p_uop, k_opcode_TXS); p_uop++; break;
  case k_txa: jit_opcode_make_uop0(p_uop, k_opcode_TXA); p_uop++; break;
  case k_tya: jit_opcode_make_uop0(p_uop, k_opcode_TYA); p_uop++; break;
  default:
    /* Various undocumented opcodes. */
    use_interp = 1;
    break;
  }

  if (use_interp) {
    p_uop = p_first_post_debug_uop;

    jit_opcode_make_uop1(p_uop, k_opcode_interp, addr_6502);
    p_uop++;
    p_details->ends_block = 1;

    p_details->num_uops = (p_uop - &p_details->uops[0]);
    assert(p_details->num_uops <= k_max_uops_per_opcode);
    return;
  }

  if (g_optype_changes_nz_flags[optype]) {
    switch (g_optype_sets_register[optype]) {
    case k_a:
      jit_opcode_make_uop0(p_uop, k_opcode_flags_nz_a);
      p_uop++;
      break;
    case k_x:
      jit_opcode_make_uop0(p_uop, k_opcode_flags_nz_x);
      p_uop++;
      break;
    case k_y:
      jit_opcode_make_uop0(p_uop, k_opcode_flags_nz_y);
      p_uop++;
      break;
    default:
      if (opmode == k_acc) {
        jit_opcode_make_uop0(p_uop, k_opcode_flags_nz_a);
        p_uop++;
      } else if (opmem == (k_opmem_read_flag | k_opmem_write_flag)) {
        jit_opcode_make_uop0(p_uop, k_opcode_flags_nz_value);
        p_uop++;
      }
      break;
    }
  }

  /* Post-main per-mode uops. */
  /* Opcodes that write, including uopcodes for handling self-modifying code. */
  /* TODO: stack page invalidations. */
  if (opmem & k_opmem_write_flag) {
    jit_opcode_make_uop0(p_uop, k_opcode_value_store);
    p_uop++;

    switch (opmode) {
    case k_abs:
    case k_abx:
    case k_aby:
    case k_idx:
    case k_idy:
      jit_opcode_make_uop0(p_uop, k_opcode_write_inv);
      p_uop++;
      break;
    case k_zpg:
    case k_zpx:
    case k_zpy:
      if (p_compiler->compile_for_code_in_zero_page) {
        jit_opcode_make_uop0(p_uop, k_opcode_write_inv);
        p_uop++;
      }
      break;
    default:
      assert(0);
      break;
    }
  }

  /* Accurate timings for page crossing cycles. */
  if (p_compiler->option_accurate_timings &&
      (opmem == k_opmem_read_flag) &&
      could_page_cross) {
    /* NOTE: must do page crossing cycles fixup after the main uop, because it
     * may fault (e.g. for hardware register access) and then fixup. We're
     * only guaranteed that the JIT handled the uop if we get here.
     */
    switch (opmode) {
    case k_abx:
      jit_opcode_make_uop1(p_uop,
                           k_opcode_CHECK_PAGE_CROSSING_X_n,
                           operand_6502);
      p_uop++;
      break;
    case k_aby:
      jit_opcode_make_uop1(p_uop,
                           k_opcode_CHECK_PAGE_CROSSING_Y_n,
                           operand_6502);
      p_uop++;
      break;
    case k_idy:
      jit_opcode_make_uop1(p_uop, k_opcode_CHECK_PAGE_CROSSING_SCRATCH_Y, 0);
      p_uop++;
      break;
    default:
      break;
    }
  }

  /* Accurate timings for branches. */
  if ((opmode == k_rel) && p_compiler->option_accurate_timings) {
    /* Fixup countdown if a branch wasn't taken. */
    jit_opcode_make_uop1(p_uop,
                         k_opcode_add_cycles,
                         (uint8_t) (p_details->max_cycles - 2));
    p_uop++;
  }

  p_details->num_uops = (p_uop - &p_details->uops[0]);
  assert(p_details->num_uops <= k_max_uops_per_opcode);
}

static void
jit_compiler_add_history(struct jit_compiler* p_compiler,
                         uint16_t addr_6502,
                         int32_t opcode_6502,
                         int is_self_modified,
                         uint64_t ticks) {
  uint32_t ring_buffer_index;
  struct jit_compile_history* p_history = &p_compiler->history[addr_6502];

  p_history->opcode = opcode_6502;

  ring_buffer_index = p_history->ring_buffer_index;

  ring_buffer_index++;
  if (ring_buffer_index == k_opcode_history_length) {
    ring_buffer_index = 0;
  }

  p_history->ring_buffer_index = ring_buffer_index;
  p_history->opcodes[ring_buffer_index] = opcode_6502;
  p_history->times[ring_buffer_index] = ticks;
  p_history->was_self_modified[ring_buffer_index] = is_self_modified;
}

static inline void
jit_compiler_get_dynamic_history(struct jit_compiler* p_compiler,
                                 uint32_t* p_new_opcode_count,
                                 uint32_t* p_new_opcode_invalidate_count,
                                 uint32_t* p_any_opcode_count,
                                 uint32_t* p_any_opcode_invalidate_count,
                                 uint8_t new_opcode,
                                 uint16_t addr_6502,
                                 int is_self_modify_invalidated) {
  uint32_t i;
  uint64_t ticks = timing_get_total_timer_ticks(p_compiler->p_timing);
  struct jit_compile_history* p_history = &p_compiler->history[addr_6502];
  uint32_t index = p_history->ring_buffer_index;
  int had_opcode_mismatch = 0;

  uint32_t new_opcode_count = 0;
  uint32_t new_opcode_invalidate_count = 0;
  uint32_t any_opcode_count = 0;
  uint32_t any_opcode_invalidate_count = 0;

  for (i = 0; i < k_opcode_history_length; ++i) {
    int was_self_modified;
    int32_t old_opcode = p_history->opcodes[index];
    /* Stop counting if we run out of opcodes. */
    if (old_opcode == -1) {
      break;
    }
    /* Stop counting if the events are over a second old. */
    if ((ticks - p_history->times[index]) > 200000000) {
      break;
    }
    /* Switch from dynamic operand to dynamic opcode counting if the opcode
     * differs.
     */
    if (old_opcode != (int) new_opcode) {
      had_opcode_mismatch = 1;
    }

    was_self_modified = p_history->was_self_modified[index];
    if ((i == 0) && is_self_modify_invalidated && !had_opcode_mismatch) {
      new_opcode_invalidate_count++;
    }

    if (!had_opcode_mismatch) {
      new_opcode_count++;
      if (was_self_modified) {
        new_opcode_invalidate_count++;
      }
    }
    any_opcode_count++;
    if (was_self_modified) {
      any_opcode_invalidate_count++;
    }

    if (index == 0) {
      index = (k_opcode_history_length - 1);
    } else {
      index--;
    }
  }

  *p_new_opcode_count = new_opcode_count;
  *p_new_opcode_invalidate_count = new_opcode_invalidate_count;
  *p_any_opcode_count = any_opcode_count;
  *p_any_opcode_invalidate_count = any_opcode_invalidate_count;
}

static void
jit_compiler_try_make_dynamic_opcode(struct jit_opcode_details* p_opcode) {
  uint8_t opmode;
  int32_t page_crossing_search_uopcode;
  int32_t page_crossing_replace_uopcode;
  int32_t write_inv_search_uopcode;
  int32_t write_inv_replace_uopcode;
  int32_t write_inv_erase_uopcode;
  int32_t new_uopcode;

  int32_t opcode_6502 = p_opcode->opcode_6502;
  uint16_t addr_6502 = p_opcode->addr_6502;
  assert(opcode_6502 == p_opcode->opcode_6502);

  opmode = defs_6502_get_6502_opmode_map()[opcode_6502];
  page_crossing_search_uopcode = -1;
  page_crossing_replace_uopcode = -1;
  write_inv_search_uopcode = -1;
  write_inv_replace_uopcode = -1;
  write_inv_erase_uopcode = -1;
  new_uopcode = -1;

  switch (opmode) {
  case k_imm:
    switch (opcode_6502) {
    case 0x09: /* ORA */
    case 0x29: /* AND */
    case 0x49: /* EOR */
    case 0x69: /* ADC */
    case 0xA9: /* LDA */
    case 0xC9: /* CMP */
    case 0xE9: /* SBC */
      /* Convert imm to abs. */
      new_uopcode = (opcode_6502 + 4);
      break;
    case 0xA0: /* LDY */
    case 0xA2: /* LDX */
    case 0xC0: /* CPY */
    case 0xE0: /* CPX */
      /* Convert imm to abs. */
      new_uopcode = (opcode_6502 + 0xC);
      break;
    default:
      break;
    }
    if (new_uopcode != -1) {
      jit_opcode_find_replace1(p_opcode,
                               opcode_6502,
                               new_uopcode,
                               (uint16_t) (addr_6502 + 1));
    }
    break;
  case k_zpg:
    switch (opcode_6502) {
    case 0xA5: /* LDA */
      new_uopcode = 0xA1; /* LDA idx */
      break;
    default:
      break;
    }
    if (new_uopcode != -1) {
      jit_opcode_find_replace2(p_opcode,
                               opcode_6502,
                               0, //k_opcode_LOAD_SCRATCH_8,
                               (uint16_t) (addr_6502 + 1),
                               new_uopcode,
                               0);
    }
    break;
  case k_abs:
    switch (opcode_6502) {
    case 0x4C: /* JMP abs */
      new_uopcode = k_opcode_JMP_SCRATCH_n;
      break;
    case 0x8C: /* STY abs */
      new_uopcode = 0x94; /* STY zpx -- which is STY_scratch. */
      //write_inv_search_uopcode = k_opcode_WRITE_INV_ABS;
      //write_inv_replace_uopcode = k_opcode_WRITE_INV_SCRATCH;
      break;
    case 0x8D: /* STA abs */
      new_uopcode = 0x95; /* STA zpx -- which is STA_SCRATCH. */
      //write_inv_search_uopcode = k_opcode_WRITE_INV_ABS;
      //write_inv_replace_uopcode = k_opcode_WRITE_INV_SCRATCH;
      break;
    case 0xAE: /* LDX abs */
      new_uopcode = 0xB6; /* LDX zpy -- which is LDX_scratch. */
      break;
    default:
      break;
    }
    /* abs mode can hit hardware registers and compile to an interp call. */
    if (jit_opcode_find_uop(p_opcode, opcode_6502) == NULL) {
      new_uopcode = -1;
    }
    if (new_uopcode != -1) {
      jit_opcode_find_replace2(p_opcode,
                               opcode_6502,
                               0, //k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               new_uopcode,
                               0);
    }
    break;
  case k_aby:
    switch (opcode_6502) {
    case 0x99: /* STA aby */
      new_uopcode = 0x91; /* STA idy */
      //write_inv_search_uopcode = k_opcode_WRITE_INV_SCRATCH;
      //write_inv_replace_uopcode = k_opcode_WRITE_INV_SCRATCH_Y;
      //write_inv_erase_uopcode = k_opcode_MODE_ABY;
      break;
    case 0xB9: /* LDA aby */
      new_uopcode = 0xB1; /* LDA idy */
      page_crossing_search_uopcode = k_opcode_CHECK_PAGE_CROSSING_Y_n;
      page_crossing_replace_uopcode = k_opcode_CHECK_PAGE_CROSSING_SCRATCH_Y;
      break;
    default:
      break;
    }
    /* aby mode can hit hardware registers and compile to an interp call. */
    if (jit_opcode_find_uop(p_opcode, opcode_6502) == NULL) {
      new_uopcode = -1;
    }
    if (new_uopcode != -1) {
      jit_opcode_find_replace2(p_opcode,
                               opcode_6502,
                               0, //k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               new_uopcode,
                               0);
    }
    break;
  default:
    switch (opcode_6502) {
    case 0x6C: /* JMP ind */
      //new_uopcode = k_opcode_MODE_IND_SCRATCH_16;
      jit_opcode_find_replace2(p_opcode,
                               k_opcode_MODE_IND_16,
                               0, //k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               0, //k_opcode_MODE_IND_SCRATCH_16,
                               0);
      break;
    case 0xBD: /* LDA abx */
      //new_uopcode = k_opcode_LDA_SCRATCH_X;
      jit_opcode_find_replace2(p_opcode,
                               0xBD,
                               0, //k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               0, //k_opcode_LDA_SCRATCH_X,
                               0);
      page_crossing_search_uopcode = k_opcode_CHECK_PAGE_CROSSING_X_n;
      //page_crossing_replace_uopcode = k_opcode_CHECK_PAGE_CROSSING_SCRATCH_X;
      break;
    default:
      break;
    }
    break;
  }

  if (new_uopcode == -1) {
    return;
  }

  p_opcode->is_dynamic_operand = 1;
  if (page_crossing_search_uopcode != -1) {
    struct asm_uop* p_uop = jit_opcode_find_uop(p_opcode,
                                                page_crossing_search_uopcode);
    if (p_uop != NULL) {
      jit_opcode_make_uop1(p_uop, page_crossing_replace_uopcode, 0);
    }
  }
  if (write_inv_search_uopcode != -1) {
    struct asm_uop* p_uop = jit_opcode_find_uop(p_opcode,
                                                write_inv_search_uopcode);
    assert(p_uop != NULL);
    jit_opcode_make_uop1(p_uop, write_inv_replace_uopcode, 0);
  }
  if (write_inv_erase_uopcode != -1) {
    jit_opcode_erase_uop(p_opcode, write_inv_erase_uopcode);
  }
}

static uint16_t
jit_compiler_get_end_addr_6502(struct jit_compiler* p_compiler) {
  uint16_t end_addr_6502;
  int32_t addr_6502;
  struct jit_opcode_details* p_details = p_compiler->p_last_opcode;

  addr_6502 = p_details->addr_6502;
  assert(addr_6502 != -1);
  end_addr_6502 = (uint16_t) addr_6502;
  end_addr_6502 += p_details->num_bytes_6502;

  return end_addr_6502;
}

static void
jit_compiler_make_last_opcode(struct jit_compiler* p_compiler,
                              struct jit_opcode_details* p_details) {
  struct jit_opcode_details* p_terminating_details;

  assert(p_details->addr_6502 != -1);

  p_terminating_details = (p_details + p_details->num_bytes_6502);
  assert((p_terminating_details - &p_compiler->opcode_details[0]) <
         k_max_addr_space_per_compile);
  p_terminating_details->addr_6502 = -1;
  p_compiler->p_last_opcode = p_details;
}

static void
jit_compiler_find_compile_bounds(struct jit_compiler* p_compiler) {
  uint16_t addr_6502 = p_compiler->start_addr_6502;
  int is_next_post_branch_addr = 1;
  uint32_t opcode_index = 0;
  int is_next_block_continuation = 0;
  uint32_t total_num_opcodes = 0;
  struct jit_opcode_details* p_details = NULL;

  while (1) {
    uint8_t opcode_6502;
    struct jit_opcode_details* p_next_details =
        &p_compiler->opcode_details[opcode_index];
    int is_branch_landing_addr_backup = p_next_details->is_branch_landing_addr;

    jit_compiler_get_opcode_details(p_compiler, p_next_details, addr_6502);

    /* Check it fits in the address space we've got left. */
    if ((opcode_index + p_next_details->num_bytes_6502) >=
        k_max_addr_space_per_compile) {
      break;
    }

    p_details = p_next_details;
    p_details->is_branch_landing_addr = is_branch_landing_addr_backup;
    p_details->is_post_branch_addr = is_next_post_branch_addr;

    opcode_6502 = p_details->opcode_6502;
    opcode_index += p_details->num_bytes_6502;
    addr_6502 += p_details->num_bytes_6502;
    total_num_opcodes++;

    is_next_post_branch_addr = 0;
    /* Tag branch and jump targets. */
    if ((p_details->branches == k_bra_m) || (opcode_6502 == 0x4C)) {
      struct jit_opcode_details* p_target_details;
      int32_t branch_addr_6502 = p_details->branch_addr_6502;
      assert(branch_addr_6502 != -1);
      p_target_details =
          jit_compiler_get_opcode_for_6502_addr(p_compiler, branch_addr_6502);
      if (p_target_details != NULL) {
        /* NOTE: extended block support disabled as it needs more research.
         * It can't be re-enabled without disabling the optimizer, and also
         * forcing x64 branch opcodes to always emit 32-bit offsets.
         */
        if (0) {
          p_target_details->is_branch_landing_addr = 1;
        }
      }
    }
    if (p_details->branches == k_bra_m) {
      is_next_post_branch_addr = 1;
    }

    /* Exit loop condition: this opcode ends the block, e.g. RTS, JMP etc. */
    if (p_details->ends_block) {
      uint8_t opcode_6502 = p_details->opcode_6502;
      assert(p_details->branches != k_bra_m);
      /* Let compilation continue if this is a JMP or RTS and the next opcode
       * is referenced previously in the block.
       * (Disabled as it's part of extended block support.)
       */
      if ((opcode_6502 == 0x4C) || (opcode_6502 == 0x60)) {
        struct jit_opcode_details* p_next_details =
            jit_compiler_get_opcode_for_6502_addr(p_compiler, addr_6502);
        if ((p_next_details != NULL) &&
            (p_next_details->is_branch_landing_addr)) {
          p_details->ends_block = 0;
        }
      }
      if (p_details->ends_block) {
        break;
      }
    }

    /* Exit loop condition: next opcode is the start of a block boundary. */
    if (p_compiler->addr_is_block_start[addr_6502]) {
      break;
    }

    /* Exit loop condition:
     * - We've compiled the configurable max number of 6502 opcodes.
     */
    if (total_num_opcodes == p_compiler->max_6502_opcodes_per_block) {
      is_next_block_continuation = 1;
      break;
    }
  }

  assert(addr_6502 > p_compiler->start_addr_6502);

  /* Terminate the list of opcodes. */
  jit_compiler_make_last_opcode(p_compiler, p_details);

  p_compiler->addr_is_block_continuation[addr_6502] =
      is_next_block_continuation;
}

static void
jit_compiler_check_dynamics(struct jit_compiler* p_compiler,
                            int32_t* p_out_sub_instruction_addr_6502) {
  struct jit_opcode_details* p_details;

  for (p_details = &p_compiler->opcode_details[0];
       p_details->addr_6502 != -1;
       p_details += p_details->num_bytes_6502) {
    uint16_t addr_6502;
    uint8_t opcode_6502;
    uint32_t opcode_6502_len;
    uint32_t new_opcode_count;
    uint32_t new_opcode_invalidate_count;
    uint32_t any_opcode_count;
    uint32_t any_opcode_invalidate_count;
    int is_self_modify_invalidated = 0;
    int is_dynamic_operand_match = 0;

    opcode_6502_len = p_details->num_bytes_6502;
    assert(opcode_6502_len > 0);

    /* Check self-modified status for each opcode. It's used in
     * jit_compiler_emit_dynamic_opcode() just below and when storing metadata
     * later.
     */
    addr_6502 = p_details->addr_6502;
    opcode_6502 = p_details->opcode_6502;
    if (jit_has_invalidated_code(p_compiler, addr_6502)) {
      is_self_modify_invalidated = 1;
      p_details->self_modify_invalidated = 1;
    }

    jit_compiler_get_dynamic_history(p_compiler,
                                     &new_opcode_count,
                                     &new_opcode_invalidate_count,
                                     &any_opcode_count,
                                     &any_opcode_invalidate_count,
                                     opcode_6502,
                                     addr_6502,
                                     is_self_modify_invalidated);

    /* Check if this is a sub-instruction situation. This is where a clever
     * 6502 programmer jumps in to the middle of an opcode as an optimization.
     * Exile uses it a lot; you'll also find it in Thrust, Galaforce 2.
     */
    if (!p_compiler->option_no_sub_instruction &&
        (new_opcode_invalidate_count == 0) &&
        (new_opcode_count >= p_compiler->dynamic_trigger) &&
        (opcode_6502_len > 1)) {
      uint32_t next_new_opcode_count;
      uint32_t next_new_opcode_invalidate_count;
      uint32_t next_any_opcode_count;
      uint32_t next_any_opcode_invalidate_count;
      uint16_t next_addr_6502 = (addr_6502 + 1);
      uint8_t next_opcode_6502 = p_compiler->p_mem_read[next_addr_6502];
      uint8_t next_opmode = p_compiler->p_opcode_modes[next_opcode_6502];
      uint32_t next_opcode_6502_len = g_opmodelens[next_opmode];

      jit_compiler_get_dynamic_history(p_compiler,
                                       &next_new_opcode_count,
                                       &next_new_opcode_invalidate_count,
                                       &next_any_opcode_count,
                                       &next_any_opcode_invalidate_count,
                                       next_opcode_6502,
                                       next_addr_6502,
                                       0);
      if ((next_new_opcode_invalidate_count == 0) &&
          (next_new_opcode_count >= p_compiler->dynamic_trigger) &&
          (next_opcode_6502_len == (opcode_6502_len - 1))) {
        if (p_compiler->log_dynamic) {
          log_do_log(k_log_jit,
                     k_log_info,
                     "compiling sub-instruction at $%.4X",
                     addr_6502);
        }
        *p_out_sub_instruction_addr_6502 = next_addr_6502;
        jit_compiler_make_last_opcode(p_compiler, p_details);
        break;
      }
    }

    if (!p_compiler->option_no_dynamic_operand &&
        (new_opcode_invalidate_count >= p_compiler->dynamic_trigger)) {
      is_dynamic_operand_match = 1;
      /* This can be a no-op if we don't support dynamic operands with this
       * particular opcode. In such a case, we'll fall through and potentially
       * make the entire opcode dynamic.
       */
      jit_compiler_try_make_dynamic_opcode(p_details);
      if (p_details->is_dynamic_operand) {
        if (p_compiler->log_dynamic) {
          log_do_log(k_log_jit,
                     k_log_info,
                     "compiling dynamic operand at $%.4X (opcode $%.2X)",
                     addr_6502,
                     opcode_6502);
        }
        continue;
      }
    }
    if (p_compiler->option_no_dynamic_opcode) {
      continue;
    }
    if ((any_opcode_invalidate_count < p_compiler->dynamic_trigger) &&
        !is_dynamic_operand_match) {
      continue;
    }
    if (p_compiler->log_dynamic) {
      log_do_log(k_log_jit,
                 k_log_info,
                 "compiling dynamic opcode at $%.4X (current opcode $%.2X)",
                 addr_6502,
                 opcode_6502);
    }
    jit_opcode_make_uop1(&p_details->uops[0], k_opcode_inturbo, addr_6502);
    p_details->num_uops = 1;
    p_details->ends_block = 1;
    p_details->is_dynamic_opcode = 1;
    p_details->is_dynamic_operand = 1;
    jit_compiler_make_last_opcode(p_compiler, p_details);
    /* The dyanmic opcode doesn't directly consume 6502 cycles itself -- the
     * mechanics of that are internal to the inturbo machine.
     */
    p_details->max_cycles = 0;
    break;
  }
}

static void
jit_compiler_asm_rewrite(struct jit_compiler* p_compiler) {
  struct jit_opcode_details* p_details;

  for (p_details = &p_compiler->opcode_details[0];
       p_details->addr_6502 != -1;
       p_details += p_details->num_bytes_6502) {
    asm_jit_rewrite(p_compiler->p_asm,
                    &p_details->uops[0],
                    p_details->num_uops);
  }
}

static void
jit_compiler_setup_cycle_counts(struct jit_compiler* p_compiler) {
  struct jit_opcode_details* p_details;
  struct asm_uop* p_uop = NULL;
  struct jit_opcode_details* p_details_fixup = NULL;

  for (p_details = &p_compiler->opcode_details[0];
       p_details->addr_6502 != -1;
       p_details += p_details->num_bytes_6502) {
    /* Calculate total cycle count at each branch landing location. */
    int needs_countdown = 0;
    assert(p_details->cycles_run_start == -1);
    if (p_details->is_post_branch_addr) {
      needs_countdown = 1;
    }
    if (p_details->is_branch_landing_addr) {
      needs_countdown = 1;
    }
    if (needs_countdown) {
      p_uop = jit_opcode_insert_uop(p_details,
                                    0,
                                    k_opcode_countdown,
                                    p_details->addr_6502);
      p_details->cycles_run_start = 0;
      assert(!p_details->has_prefix_uop);
      p_details->has_prefix_uop = 1;
      p_details_fixup = p_details;
    }

    p_details_fixup->cycles_run_start += p_details->max_cycles;
    p_uop->value2 = p_details_fixup->cycles_run_start;
  }
}

static void
jit_compiler_emit_uops(struct jit_compiler* p_compiler) {
  struct jit_opcode_details* p_details;
  uint8_t single_opcode_buffer[128];
  uint8_t single_opcode_epilog_buffer[128];
  struct util_buffer* p_single_uopcode_buf = p_compiler->p_single_uopcode_buf;
  struct util_buffer* p_single_uopcode_epilog_buf =
      p_compiler->p_single_uopcode_epilog_buf;
  struct util_buffer* p_tmp_buf = p_compiler->p_tmp_buf;
  uint16_t addr_6502 = p_compiler->start_addr_6502;
  uint32_t block_epilog_len = 0;
  void* p_host_address_base =
      p_compiler->get_block_host_address(p_compiler->p_host_address_object,
                                         addr_6502);

  util_buffer_setup(p_tmp_buf, p_host_address_base, K_BBC_JIT_BYTES_PER_BYTE);
  util_buffer_set_base_address(p_tmp_buf, p_host_address_base);
  util_buffer_setup(p_single_uopcode_buf,
                    &single_opcode_buffer[0],
                    sizeof(single_opcode_buffer));
  util_buffer_setup(p_single_uopcode_epilog_buf,
                    &single_opcode_epilog_buffer[0],
                    sizeof(single_opcode_epilog_buffer));

  /* Pad the buffers with traps, i.e. int3 on Intel.
   * There are a few good reasons for this:
   * 1) Clarity: see where a code block ends, especially if there was
   * previously a larger code block at this address.
   * 2) Bug detection: better chance of a clean crash if something does a bad
   * jump.
   * 3) Performance. Traps may stop the instruction decoder.
   */
  asm_fill_with_trap(p_tmp_buf);
  util_buffer_set_pos(p_tmp_buf, 0);

  for (p_details = &p_compiler->opcode_details[0];
       p_details->addr_6502 != -1;
       p_details += p_details->num_bytes_6502) {
    uint32_t i_uops;
    uint32_t num_uops;
    int ends_block;
    size_t opcode_len_asm = 0;

    num_uops = p_details->num_uops;
    ends_block = p_details->ends_block;

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      size_t buf_needed;
      size_t out_buf_pos;
      void* p_host_address;
      uint32_t epilog_len;
      int needs_reemit;
      int is_prefix_uop;
      int is_postfix_uop;
      uint32_t epilog_pos;
      struct asm_uop* p_uop = &p_details->uops[i_uops];

      if (p_uop->is_eliminated) {
        continue;
      }

      epilog_pos = 0;
      needs_reemit = 0;

      out_buf_pos = util_buffer_get_pos(p_tmp_buf);
      p_host_address = (p_host_address_base + out_buf_pos);
      util_buffer_set_base_address(p_single_uopcode_buf, p_host_address);
      util_buffer_set_base_address(p_single_uopcode_epilog_buf, p_host_address);
      util_buffer_set_pos(p_single_uopcode_buf, 0);
      util_buffer_set_pos(p_single_uopcode_epilog_buf, 0);
      asm_emit_jit(p_compiler->p_asm,
                   p_single_uopcode_buf,
                   p_single_uopcode_epilog_buf,
                   p_uop);

      epilog_len = util_buffer_get_pos(p_single_uopcode_epilog_buf);
      if (epilog_len > 0) {
        needs_reemit = 1;
      }

      /* Calculate if this uopcode fits. In order to fit, not only must the
       * uopcode itself fit, but there must be space for a possible jump to the
       * block continuation.
       */
      buf_needed = (util_buffer_get_pos(p_single_uopcode_buf) +
                    util_buffer_get_pos(p_single_uopcode_epilog_buf));
      if (!ends_block || (i_uops != (num_uops - 1))) {
        buf_needed += p_compiler->len_asm_jmp;
      }

      if ((util_buffer_remaining(p_tmp_buf) - block_epilog_len) < buf_needed) {
        struct asm_uop tmp_uop;
        /* Emit jump to the next adjacent code block. We'll need to jump over
         * the compile trampoline at the beginning of the block.
         */
        void* p_resume =
            (p_host_address_base + util_buffer_get_length(p_tmp_buf));
        p_resume += p_compiler->len_asm_invalidated;
        jit_opcode_make_uop1(&tmp_uop,
                             k_opcode_JMP,
                             (int32_t) (uintptr_t) p_resume);
        asm_emit_jit(p_compiler->p_asm, p_tmp_buf, NULL, &tmp_uop);

        /* Continue compiling the code block in the next host block, after the
         * compile trampoline.
         */
        addr_6502++;
        p_host_address_base =
            p_compiler->get_block_host_address(
                p_compiler->p_host_address_object, addr_6502);
        util_buffer_setup(p_tmp_buf,
                          p_host_address_base,
                          K_BBC_JIT_BYTES_PER_BYTE);
        util_buffer_set_base_address(p_tmp_buf, p_host_address_base);

        asm_fill_with_trap(p_tmp_buf);
        util_buffer_set_pos(p_tmp_buf, 0);
        asm_emit_jit_invalidated(p_tmp_buf);

        block_epilog_len = 0;
        needs_reemit = 1;
      }

      if (needs_reemit) {
        /* Re-emit the current uopcode because it is now at a different host
         * address. Also, the host address of any epilog is now known whereas
         * it was not before. Jump target calculations will have changed.
         */
        util_buffer_set_pos(p_single_uopcode_buf, 0);
        util_buffer_set_pos(p_single_uopcode_epilog_buf, 0);
        p_host_address = (p_host_address_base + util_buffer_get_pos(p_tmp_buf));
        util_buffer_set_base_address(p_single_uopcode_buf, p_host_address);
        epilog_pos = util_buffer_get_length(p_tmp_buf);
        epilog_pos -= block_epilog_len;
        epilog_pos -= epilog_len;
        util_buffer_set_base_address(p_single_uopcode_epilog_buf,
                                     (p_host_address_base + epilog_pos));
        asm_emit_jit(p_compiler->p_asm,
                     p_single_uopcode_buf,
                     p_single_uopcode_epilog_buf,
                     p_uop);
      }

      block_epilog_len += epilog_len;

      /* Keep a note of the host address of where the JIT code prefixes start,
       * actual code starts, and all code ends.
       * The actual code start will be set in the jit_ptrs array later,
       * and is where any self-modification invalidation will write to. It is
       * after any countdown opcodes.
       * The prefix code start will be used as a branch target for branches
       * within a block.
       */
      is_prefix_uop = ((i_uops == 0) && p_details->has_prefix_uop);
      is_postfix_uop = ((i_uops == (num_uops - 1)) &&
                        p_details->has_postfix_uop);
      if (!is_prefix_uop && !is_postfix_uop) {
        if (p_details->p_host_address_start == NULL) {
          p_details->p_host_address_start = p_host_address;
        }
      }

      util_buffer_append(p_tmp_buf, p_single_uopcode_buf);
      if (is_prefix_uop) {
        p_details->p_host_address_prefix_end =
            (p_host_address_base + util_buffer_get_pos(p_tmp_buf));
      }

      opcode_len_asm += util_buffer_get_pos(p_single_uopcode_buf);

      /* Plop in any epilog. */
      if (epilog_pos > 0) {
        size_t curr_pos = util_buffer_get_pos(p_tmp_buf);
        util_buffer_set_pos(p_tmp_buf, epilog_pos);
        util_buffer_append(p_tmp_buf, p_single_uopcode_epilog_buf);
        util_buffer_set_pos(p_tmp_buf, curr_pos);
      }
    }

    if (opcode_len_asm > 0) {
      /* If there's any output, need at least the length of the
       * self-modified invalidation sequence.
       */
      assert(opcode_len_asm >= p_compiler->len_asm_invalidated);
    }
  }
}

static void
jit_compiler_update_metadata(struct jit_compiler* p_compiler) {
  struct jit_opcode_details* p_details;
  uint16_t addr_6502;
  int is_new_jit_ptr;
  uint64_t ticks = timing_get_total_timer_ticks(p_compiler->p_timing);
  uint32_t cycles = 0;
  uint32_t prev_jit_ptr = 0;
  uint32_t jit_ptr = 0;

  for (p_details = &p_compiler->opcode_details[0];
       p_details->addr_6502 != -1;
       p_details += p_details->num_bytes_6502) {
    uint8_t i;
    void* p_host_address_prefix_end = p_details->p_host_address_prefix_end;
    void* p_host_address_start = p_details->p_host_address_start;
    uint32_t num_bytes_6502 = p_details->num_bytes_6502;

    assert(num_bytes_6502 > 0);

    if (p_details->cycles_run_start != -1) {
      cycles = p_details->cycles_run_start;
    }

    /* Advance current jit pointer to the greater of end of any block prefix,
     * or start of block body.
     */
    if (p_host_address_prefix_end != NULL) {
      jit_ptr = (uint32_t) (uintptr_t) p_host_address_prefix_end;
    }
    if (p_host_address_start != NULL) {
      jit_ptr = (uint32_t) (uintptr_t) p_host_address_start;
    }
    is_new_jit_ptr = (jit_ptr != prev_jit_ptr);
    prev_jit_ptr = jit_ptr;

    addr_6502 = p_details->addr_6502;
    for (i = 0; i < num_bytes_6502; ++i) {
      p_compiler->p_jit_ptrs[addr_6502] = jit_ptr;
      p_compiler->p_code_blocks[addr_6502] = p_compiler->start_addr_6502;

      if (addr_6502 != p_compiler->start_addr_6502) {
        jit_invalidate_jump_target(p_compiler, addr_6502);
        p_compiler->addr_is_block_start[addr_6502] = 0;
        p_compiler->addr_is_block_continuation[addr_6502] = 0;
      }

      p_compiler->addr_nz_fixup[addr_6502] = 0;
      p_compiler->addr_nz_mem_fixup[addr_6502] = -1;
      p_compiler->addr_o_fixup[addr_6502] = 0;
      p_compiler->addr_c_fixup[addr_6502] = 0;
      p_compiler->addr_a_fixup[addr_6502] = -1;
      p_compiler->addr_x_fixup[addr_6502] = -1;
      p_compiler->addr_y_fixup[addr_6502] = -1;
      p_compiler->addr_cycles_fixup[addr_6502] = -1;

      if (i != 0) {
        if (p_details->is_dynamic_operand) {
          p_compiler->p_jit_ptrs[addr_6502] =
              p_compiler->jit_ptr_dynamic_operand;
        }
      } else if (!is_new_jit_ptr) {
        /* No metadata here. */
      } else {
        uint32_t i_uops;
        uint8_t opcode_6502 = p_details->opcode_6502;
        if (p_details->is_dynamic_opcode) {
          p_compiler->p_jit_ptrs[addr_6502] =
              p_compiler->jit_ptr_dynamic_operand;
        }
        jit_compiler_add_history(p_compiler,
                                 addr_6502,
                                 opcode_6502,
                                 p_details->self_modify_invalidated,
                                 ticks);

        p_compiler->addr_cycles_fixup[addr_6502] = cycles;
        for (i_uops = 0; i_uops < p_details->num_fixup_uops; ++i_uops) {
          struct asm_uop* p_uop = p_details->fixup_uops[i_uops];
          switch (p_uop->uopcode) {
          case k_opcode_flags_nz_a:
            p_compiler->addr_nz_fixup[addr_6502] = k_a;
            break;
          case k_opcode_flags_nz_x:
            p_compiler->addr_nz_fixup[addr_6502] = k_x;
            break;
          case k_opcode_flags_nz_y:
            p_compiler->addr_nz_fixup[addr_6502] = k_y;
            break;
          case k_opcode_flags_nz_value:
            p_compiler->addr_nz_mem_fixup[addr_6502] = (uint16_t) p_uop->value1;
            break;
          case 0xA9: /* LDA imm */
            p_compiler->addr_a_fixup[addr_6502] = (uint8_t) p_uop->value1;
            break;
          case 0xA2: /* LDX imm */
            p_compiler->addr_x_fixup[addr_6502] = (uint8_t) p_uop->value1;
            break;
          case 0xA0: /* LDY imm */
            p_compiler->addr_y_fixup[addr_6502] = (uint8_t) p_uop->value1;
            break;
          case k_opcode_save_overflow:
            p_compiler->addr_o_fixup[addr_6502] = 1;
            break;
          case k_opcode_save_carry:
            p_compiler->addr_c_fixup[addr_6502] = 1;
            break;
          //case k_opcode_SAVE_CARRY_INV:
          //  p_compiler->addr_c_fixup[addr_6502] = 2;
          //  break;
          case 0x18: /* CLC */
            p_compiler->addr_c_fixup[addr_6502] = 3;
            break;
          case 0x38: /* SEC */
            p_compiler->addr_c_fixup[addr_6502] = 4;
            break;
          default:
            assert(0);
            break;
          }
        }
      }

      addr_6502++;
    }
    cycles -= p_details->max_cycles;
  }
}

uint32_t
jit_compiler_compile_block(struct jit_compiler* p_compiler,
                           int is_invalidation,
                           uint16_t start_addr_6502) {
  uint32_t i_opcodes;
  struct jit_opcode_details* p_details;
  uint16_t end_addr_6502;
  int is_block_start = 0;
  int32_t sub_instruction_addr_6502 = -1;

  for (i_opcodes = 0; i_opcodes < k_max_addr_space_per_compile; ++i_opcodes) {
    p_compiler->opcode_details[i_opcodes].addr_6502 = -1;
    p_compiler->opcode_details[i_opcodes].is_branch_landing_addr = 0;
  }

  p_compiler->start_addr_6502 = start_addr_6502;
  p_compiler->p_last_opcode = NULL;

  if (p_compiler->addr_is_block_start[start_addr_6502]) {
    /* Retain any existing block start determination. */
    is_block_start = 1;
  } else if (!p_compiler->addr_is_block_continuation[start_addr_6502] &&
             !is_invalidation) {
    /* New block starts are only created if this isn't a compilation
     * continuation, and this isn't an invalidation of existing code.
     */
    is_block_start = 1;
  }

  p_compiler->addr_is_block_start[start_addr_6502] = is_block_start;
  /* NOTE: p_compiler->addr_is_block_continuation[start_addr_6502] is left as
   * it currently is.
   * The only way to clear it is compile across the continuation boundary.
   */

  /* First break all the opcodes for this run into uops.
   * This defines maximum possible bounds for the block and respects existing
   * known block boundaries.
   */
  jit_compiler_find_compile_bounds(p_compiler);

  /* Second, work out if we'll be compiling any dynamic opcodes / operands.
   * NOTE: may truncate p_compiler->total_num_opcodes.
   */
  /*jit_compiler_check_dynamics(p_compiler,
                              &sub_instruction_addr_6502);*/
  (void) jit_compiler_check_dynamics;

  /* Third, offer the asm backend the chance to rewrite. Most significantly,
   * this is used as a coalesce pass. For example, the CISC-y x64 can take our
   * RISC-y uops and combine many of them. e.g. EOR abx can be done in one
   * x64 instruction.
   */
  jit_compiler_asm_rewrite(p_compiler);

  /* If the block didn't end with an explicit jump, put it in. */
  p_details = p_compiler->p_last_opcode;
  assert(p_details->addr_6502 != -1);
  if (!p_details->ends_block) {
    uint32_t jit_addr;
    end_addr_6502 = jit_compiler_get_end_addr_6502(p_compiler);
    /* JMP abs */
    jit_addr = (uintptr_t) jit_compiler_resolve_branch_target(p_compiler,
                                                              end_addr_6502);
    (void) jit_opcode_insert_uop(p_details,
                                 p_details->num_uops,
                                 k_opcode_JMP,
                                 jit_addr);
    assert(!p_details->has_postfix_uop);
    p_details->has_postfix_uop = 1;
    p_details->ends_block = 1;
  }

  /* Fourth, walk the opcode list; add countdown checks and calculate cycle
   * counts.
   */
  jit_compiler_setup_cycle_counts(p_compiler);

  /* Fifth, run the optimizer across the list of opcodes. */
  if (!p_compiler->option_no_optimize) {
    p_details = jit_optimizer_optimize(&p_compiler->opcode_details[0]);
    jit_compiler_make_last_opcode(p_compiler, p_details);
  }

  /* Sixth, emit the uop stream to the output buffer. */
  p_compiler->has_unresolved_jumps = 0;
  jit_compiler_emit_uops(p_compiler);
  if (p_compiler->has_unresolved_jumps) {
    /* Need to do it again if there were any unresolved forward jumps.
     * (Disabled as it's part of extended block support.)
     */
    p_compiler->has_unresolved_jumps = 0;
    jit_compiler_emit_uops(p_compiler);
    assert(!p_compiler->has_unresolved_jumps);
  }

  /* Seventh, update compiler metadata. */
  jit_compiler_update_metadata(p_compiler);

  if (sub_instruction_addr_6502 != -1) {
    struct asm_uop tmp_uop;
    struct util_buffer* p_tmp_buf = p_compiler->p_tmp_buf;
    void* p_host_address_base = p_compiler->get_block_host_address(
        p_compiler->p_host_address_object, sub_instruction_addr_6502);
    util_buffer_setup(p_tmp_buf, p_host_address_base, K_BBC_JIT_BYTES_PER_BYTE);
    jit_opcode_make_uop1(&tmp_uop, k_opcode_inturbo, sub_instruction_addr_6502);
    asm_emit_jit(p_compiler->p_asm, p_tmp_buf, NULL, &tmp_uop);
  }

  end_addr_6502 = jit_compiler_get_end_addr_6502(p_compiler);
  return (end_addr_6502 - p_compiler->start_addr_6502);
}

int64_t
jit_compiler_fixup_state(struct jit_compiler* p_compiler,
                         struct state_6502* p_state_6502,
                         int64_t countdown,
                         uint64_t host_rflags) {
  uint16_t pc_6502 = p_state_6502->reg_pc;
  int32_t cycles_fixup = p_compiler->addr_cycles_fixup[pc_6502];
  uint8_t nz_fixup = p_compiler->addr_nz_fixup[pc_6502];
  int32_t nz_mem_fixup = p_compiler->addr_nz_mem_fixup[pc_6502];
  uint8_t o_fixup = p_compiler->addr_o_fixup[pc_6502];
  uint8_t c_fixup = p_compiler->addr_c_fixup[pc_6502];
  int32_t a_fixup = p_compiler->addr_a_fixup[pc_6502];
  int32_t x_fixup = p_compiler->addr_x_fixup[pc_6502];
  int32_t y_fixup = p_compiler->addr_y_fixup[pc_6502];

  /* cycles_fixup can be 0 in the case the opcode is bouncing to the
   * interpreter -- an invalid opcode, for example.
   */
  assert(cycles_fixup >= 0);
  countdown += cycles_fixup;

  if (a_fixup != -1) {
    state_6502_set_a(p_state_6502, a_fixup);
  }
  if (x_fixup != -1) {
    state_6502_set_x(p_state_6502, x_fixup);
  }
  if (y_fixup != -1) {
    state_6502_set_y(p_state_6502, y_fixup);
  }
  if ((nz_fixup != 0) || (nz_mem_fixup != -1)) {
    uint8_t nz_val = 0;
    uint8_t flag_n;
    uint8_t flag_z;
    uint8_t flags_new;
    switch (nz_fixup) {
    case 0:
      assert(nz_mem_fixup != -1);
      nz_val = p_compiler->p_mem_read[nz_mem_fixup];
      break;
    case k_a:
      nz_val = p_state_6502->reg_a;
      break;
    case k_x:
      nz_val = p_state_6502->reg_x;
      break;
    case k_y:
      nz_val = p_state_6502->reg_y;
      break;
    default:
      assert(0);
      break;
    }

    flag_n = !!(nz_val & 0x80);
    flag_z = (nz_val == 0);
    flags_new = 0;
    flags_new |= (flag_n << k_flag_negative);
    flags_new |= (flag_z << k_flag_zero);
    p_state_6502->reg_flags &= ~((1 << k_flag_negative) | (1 << k_flag_zero));
    p_state_6502->reg_flags |= flags_new;
  }
  if (o_fixup) {
    int host_overflow_flag = !!(host_rflags & 0x0800);
    p_state_6502->reg_flags &= ~(1 << k_flag_overflow);
    p_state_6502->reg_flags |= (host_overflow_flag << k_flag_overflow);
  }
  if (c_fixup) {
    int new_carry;
    if (c_fixup == 1) {
      new_carry = !!(host_rflags & 0x0001);
    } else if (c_fixup == 2) {
      new_carry = !(host_rflags & 0x0001);
    } else if (c_fixup == 3) {
      new_carry = 0;
    } else {
      assert(c_fixup == 4);
      new_carry = 1;
    }
    p_state_6502->reg_flags &= ~(1 << k_flag_carry);
    p_state_6502->reg_flags |= (new_carry << k_flag_carry);
  }

  return countdown;
}

void
jit_compiler_memory_range_invalidate(struct jit_compiler* p_compiler,
                                     uint16_t addr,
                                     uint32_t len) {
  uint32_t i;

  uint32_t addr_end = (addr + len);

  assert(len <= k_6502_addr_space_size);
  assert(addr_end <= k_6502_addr_space_size);

  for (i = addr; i < addr_end; ++i) {
    uint32_t j;
    for (j = 0; j < k_opcode_history_length; ++j) {
      p_compiler->history[i].times[j] = 0;
      p_compiler->history[i].opcodes[j] = -1;
      p_compiler->history[i].was_self_modified[j] = 0;
    }
    p_compiler->history[i].ring_buffer_index = 0;
    p_compiler->history[i].opcode = -1;
    p_compiler->addr_is_block_start[i] = 0;
    p_compiler->addr_is_block_continuation[i] = 0;

    p_compiler->addr_cycles_fixup[i] = -1;
    p_compiler->addr_nz_fixup[i] = 0;
    p_compiler->addr_nz_mem_fixup[i] = -1;
    p_compiler->addr_o_fixup[i] = 0;
    p_compiler->addr_c_fixup[i] = 0;
    p_compiler->addr_a_fixup[i] = -1;
    p_compiler->addr_x_fixup[i] = -1;
    p_compiler->addr_y_fixup[i] = -1;
  }
}

int
jit_compiler_is_block_continuation(struct jit_compiler* p_compiler,
                                   uint16_t addr_6502) {
  return p_compiler->addr_is_block_continuation[addr_6502];
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

void
jit_compiler_testing_set_optimizing(struct jit_compiler* p_compiler,
                                    int optimizing) {
  p_compiler->option_no_optimize = !optimizing;
}

void
jit_compiler_testing_set_dynamic_operand(struct jit_compiler* p_compiler,
                                         int is_dynamic_operand) {
  p_compiler->option_no_dynamic_operand = !is_dynamic_operand;
}

void
jit_compiler_testing_set_dynamic_opcode(struct jit_compiler* p_compiler,
                                        int is_dynamic_opcode) {
  p_compiler->option_no_dynamic_opcode = !is_dynamic_opcode;
}

void
jit_compiler_testing_set_sub_instruction(struct jit_compiler* p_compiler,
                                         int is_sub_instruction) {
  p_compiler->option_no_sub_instruction = !is_sub_instruction;
}

void
jit_compiler_testing_set_max_ops(struct jit_compiler* p_compiler,
                                 uint32_t num_ops) {
  p_compiler->max_6502_opcodes_per_block = num_ops;
}

void
jit_compiler_testing_set_dynamic_trigger(struct jit_compiler* p_compiler,
                                         uint32_t count) {
  p_compiler->dynamic_trigger = count;
}

int32_t
jit_compiler_testing_get_cycles_fixup(struct jit_compiler* p_compiler,
                                      uint16_t addr) {
  return p_compiler->addr_cycles_fixup[addr];
}

int32_t
jit_compiler_testing_get_a_fixup(struct jit_compiler* p_compiler,
                                 uint16_t addr) {
  return p_compiler->addr_a_fixup[addr];
}
