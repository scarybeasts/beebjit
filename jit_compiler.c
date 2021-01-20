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

#include <assert.h>
#include <string.h>

enum {
  k_opcode_history_length = 4,
};

struct jit_compile_history {
  uint64_t times[k_opcode_history_length];
  int32_t opcodes[k_opcode_history_length];
  uint32_t ring_buffer_index;
  uint16_t opcode;
};

struct jit_compiler {
  struct timing_struct* p_timing;
  struct memory_access* p_memory_access;
  uint8_t* p_mem_read;
  void* (*get_block_host_address)(void* p, uint16_t addr);
  void* (*get_trampoline_host_address)(void* p, uint16_t addr);
  void* p_host_address_object;
  uint32_t* p_jit_ptrs;
  int32_t* p_code_blocks;
  int debug;
  int log_dynamic;
  uint8_t* p_opcode_types;
  uint8_t* p_opcode_modes;
  uint8_t* p_opcode_cycles;

  int option_accurate_timings;
  int option_no_optimize;
  int option_no_dynamic_operand;
  int option_no_dynamic_opcode;
  uint32_t max_6502_opcodes_per_block;
  uint32_t dynamic_trigger;

  struct util_buffer* p_tmp_buf;
  struct util_buffer* p_single_uopcode_buf;
  uint32_t jit_ptr_no_code;
  uint32_t jit_ptr_dynamic_operand;

  uint32_t len_asm_jmp;

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
};

enum {
  k_max_opcodes_per_compile = 256,
};

static void
jit_invalidate_jump_target(struct jit_compiler* p_compiler, uint16_t addr) {
  void* p_host_ptr =
      p_compiler->get_block_host_address(p_compiler->p_host_address_object,
                                         addr);
  util_buffer_setup(p_compiler->p_tmp_buf, p_host_ptr, 2);
  asm_emit_jit_call_compile_trampoline(p_compiler->p_tmp_buf);
}

static int32_t
jit_compiler_get_current_opcode(struct jit_compiler* p_compiler,
                                uint16_t addr_6502) {
  return p_compiler->history[addr_6502].opcode;
}

static int
jit_has_invalidated_code(struct jit_compiler* p_compiler, uint16_t addr_6502) {
  uint8_t* p_raw_ptr;
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

  p_raw_ptr = (uint8_t*) (uintptr_t) jit_ptr;
  /* TODO: don't hard code this? */
  if ((p_raw_ptr[0] == 0xff) && (p_raw_ptr[1] == 0x17)) {
    return 1;
  }
  return 0;
}

struct jit_compiler*
jit_compiler_create(struct timing_struct* p_timing,
                    struct memory_access* p_memory_access,
                    void* (*get_block_host_address)(void*, uint16_t),
                    void* (*get_trampoline_host_address)(void*, uint16_t),
                    void* p_host_address_object,
                    uint32_t* p_jit_ptrs,
                    int32_t* p_code_blocks,
                    struct bbc_options* p_options,
                    int debug,
                    uint8_t* p_opcode_types,
                    uint8_t* p_opcode_modes,
                    uint8_t* p_opcode_cycles) {
  uint32_t i;
  struct util_buffer* p_tmp_buf;
  uint8_t buf[256];

  uint32_t max_6502_opcodes_per_block = 65536;
  uint32_t dynamic_trigger = 4;

  struct jit_compiler* p_compiler = util_mallocz(sizeof(struct jit_compiler));

  /* Check invariants required for compact code generation. */
  assert(K_JIT_CONTEXT_OFFSET_JIT_PTRS < 0x80);

  p_compiler->p_timing = p_timing;
  p_compiler->p_memory_access = p_memory_access;
  p_compiler->p_mem_read = p_memory_access->p_mem_read;
  p_compiler->get_block_host_address = get_block_host_address;
  p_compiler->get_trampoline_host_address = get_trampoline_host_address;
  p_compiler->p_host_address_object = p_host_address_object;
  p_compiler->p_jit_ptrs = p_jit_ptrs;
  p_compiler->p_code_blocks = p_code_blocks;
  p_compiler->debug = debug;
  p_compiler->p_opcode_types = p_opcode_types;
  p_compiler->p_opcode_modes = p_opcode_modes;
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

  p_compiler->jit_ptr_no_code =
      (uint32_t) (size_t) get_block_host_address(p_host_address_object,
                                                 (k_6502_addr_space_size - 1));
  p_compiler->jit_ptr_dynamic_operand =
      (uint32_t) (size_t) get_block_host_address(p_host_address_object,
                                                 (k_6502_addr_space_size - 2));

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_compiler->p_jit_ptrs[i] = p_compiler->jit_ptr_no_code;
    p_compiler->p_code_blocks[i] = -1;
  }

  /* Calculate lengths of sequences we need to know. */
  util_buffer_setup(p_tmp_buf, &buf[0], sizeof(buf));
  /* Note: target pointer is a short jump range. */
  asm_emit_jit_JMP(p_tmp_buf, &buf[0]);
  p_compiler->len_asm_jmp = util_buffer_get_pos(p_tmp_buf);

  return p_compiler;
}

void
jit_compiler_destroy(struct jit_compiler* p_compiler) {
  util_buffer_destroy(p_compiler->p_tmp_buf);
  util_buffer_destroy(p_compiler->p_single_uopcode_buf);
  util_free(p_compiler);
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
  uint16_t rel_target_6502 = 0;

  (void) memset(p_details, '\0', sizeof(struct jit_opcode_details));
  p_details->addr_6502 = addr_6502;
  p_details->min_6502_addr = -1;
  p_details->max_6502_addr = -1;

  opcode_6502 = p_mem_read[addr_6502];
  optype = p_compiler->p_opcode_types[opcode_6502];
  opmode = p_compiler->p_opcode_modes[opcode_6502];
  opmem = g_opmem[optype];

  p_details->opcode_6502 = opcode_6502;
  p_details->len_bytes_6502_orig = g_opmodelens[opmode];
  p_details->branches = g_opbranch[optype];
  p_details->ends_block = 0;
  if (p_details->branches == k_bra_y) {
    p_details->ends_block = 1;
  }

  p_details->num_fixup_uops = 0;
  p_details->len_bytes_6502_merged = p_details->len_bytes_6502_orig;
  p_details->eliminated = 0;
  p_details->p_host_address = NULL;
  p_details->cycles_run_start = -1;

  if (p_compiler->debug) {
    jit_opcode_make_uop1(p_uop, k_opcode_debug, addr_6502);
    p_uop++;
    p_first_post_debug_uop = p_uop;
  }

  /* Mode resolution and possibly per-mode uops. */
  switch (opmode) {
  case 0:
  case k_nil:
  case k_acc:
    operand_6502 = 0;
    break;
  case k_imm:
    operand_6502 = p_mem_read[addr_plus_1];
    break;
  case k_zpg:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = operand_6502;
    p_details->max_6502_addr = operand_6502;
    break;
  case k_zpx:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFF;
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_ZPX, operand_6502);
    p_uop++;
    break;
  case k_zpy:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFF;
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_ZPY, operand_6502);
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
    p_details->min_6502_addr = operand_6502;
    p_details->max_6502_addr = operand_6502;
    if ((operand_6502 & 0xFF) == 0x00) {
      could_page_cross = 0;
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

    if (opmem == k_read || opmem == k_rw) {
      if (p_memory_access->memory_read_needs_callback(
              p_memory_callback, p_details->min_6502_addr)) {
        use_interp = 1;
      }
      if (p_memory_access->memory_read_needs_callback(
              p_memory_callback, p_details->max_6502_addr)) {
        use_interp = 1;
      }
    }
    if (opmem == k_write || opmem == k_rw) {
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
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_IND_16, operand_6502);
    p_uop++;
    break;
  case k_idx:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFFFF;
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_ZPX, operand_6502);
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_IND_SCRATCH_8, addr_6502);
    p_uop++;
    break;
  case k_idy:
    operand_6502 = p_mem_read[addr_plus_1];
    p_details->min_6502_addr = 0;
    p_details->max_6502_addr = 0xFFFF;
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_IND_8, operand_6502);
    p_uop++;
    break;
  default:
    assert(0);
    operand_6502 = 0;
    break;
  }

  p_details->operand_6502 = operand_6502;

  p_details->max_cycles_orig = p_compiler->p_opcode_cycles[opcode_6502];
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
  p_details->max_cycles_merged = p_details->max_cycles_orig;

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

    jit_opcode_make_uop1(p_uop, k_opcode_interp, addr_6502);
    p_uop++;
    p_details->ends_block = 1;

    p_details->num_uops = (p_uop - &p_details->uops[0]);
    assert(p_details->num_uops <= k_max_uops_per_opcode);
    return;
  }

  /* Pre-main uops. */
  switch (optype) {
  case k_adc:
    jit_opcode_make_uop1(p_uop, k_opcode_CHECK_BCD, 0);
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_LOAD_CARRY_FOR_CALC, 0);
    p_uop++;
    break;
  case k_bcc:
  case k_bcs:
    jit_opcode_make_uop1(p_uop, k_opcode_LOAD_CARRY_FOR_BRANCH, 0);
    p_uop++;
    break;
  case k_bvc:
  case k_bvs:
    jit_opcode_make_uop1(p_uop, k_opcode_LOAD_OVERFLOW, 0);
    p_uop++;
    break;
  case k_cli:
  case k_plp:
    jit_opcode_make_uop1(p_uop, k_opcode_CHECK_PENDING_IRQ, addr_6502);
    p_uop++;
    break;
  case k_jsr:
    jit_opcode_make_uop1(p_uop, k_opcode_PUSH_16, (uint16_t) (addr_6502 + 2));
    p_uop++;
    break;
  case k_rol:
  case k_ror:
    jit_opcode_make_uop1(p_uop, k_opcode_LOAD_CARRY_FOR_CALC, 0);
    p_uop++;
    break;
  case k_rti:
    jit_opcode_make_uop1(p_uop, k_opcode_CHECK_PENDING_IRQ, addr_6502);
    p_uop++;
    /* PLP */
    jit_opcode_make_uop1(p_uop, 0x28, 0);
    p_uop->uoptype = k_plp;
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_PULL_16, 0);
    p_uop++;
    break;
  case k_rts:
    jit_opcode_make_uop1(p_uop, k_opcode_PULL_16, 0);
    p_uop++;
    /* TODO: may increment 0xFFFF -> 0x10000, which may crash. */
    jit_opcode_make_uop1(p_uop, k_opcode_INC_SCRATCH, 0);
    p_uop++;
    break;
  case k_sbc:
    jit_opcode_make_uop1(p_uop, k_opcode_CHECK_BCD, 0);
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_LOAD_CARRY_INV_FOR_CALC, 0);
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
    jit_opcode_make_uop1(p_uop, opcode_6502, rel_target_6502);
    p_uop->uoptype = optype;
    p_uop++;
    break;
  case k_brk:
    jit_opcode_make_uop1(p_uop, k_opcode_PUSH_16, (uint16_t) (addr_6502 + 2));
    p_uop++;
    /* PHP */
    jit_opcode_make_uop1(p_uop, 0x08, 0);
    p_uop->uoptype = k_php;
    p_uop++;
    /* SEI */
    jit_opcode_make_uop1(p_uop, 0x78, 0);
    p_uop->uoptype = k_sei;
    p_uop++;
    /* MODE_IND */
    jit_opcode_make_uop1(p_uop, k_opcode_MODE_IND_16, k_6502_vector_irq);
    p_uop++;
    /* JMP_SCRATCH */
    jit_opcode_make_uop1(p_uop, k_opcode_JMP_SCRATCH, 0);
    p_uop++;
    break;
  case k_jmp:
    if (opmode == k_ind) {
      jit_opcode_make_uop1(p_uop, k_opcode_JMP_SCRATCH, 0);
      p_uop++;
    } else {
      main_written = 0;
    }
    break;
  case k_jsr:
    /* JMP abs */
    jit_opcode_make_uop1(p_uop, 0x4C, operand_6502);
    p_uop->uoptype = k_jmp;
    p_uop++;
    break;
  case k_rti:
  case k_rts:
    jit_opcode_make_uop1(p_uop, k_opcode_JMP_SCRATCH, 0);
    p_uop++;
    break;
  default:
    main_written = 0;
    break;
  }
  if (!main_written) {
    jit_opcode_make_uop1(p_uop, opcode_6502, operand_6502);
    p_uop->uoptype = optype;
    p_uop->uopmode = opmode;
    /* Set value2 to the address, which will be used to bounce unhandled
     * opcodes into the interpreter.
     */
    p_uop->value2 = addr_6502;
    p_uop++;
  }

  /* Post-main uops. */
  switch (optype) {
  case k_adc:
    jit_opcode_make_uop1(p_uop, k_opcode_SAVE_CARRY, 0);
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_SAVE_OVERFLOW, 0);
    p_uop++;
    break;
  case k_alr:
  case k_asl:
  case k_lsr:
  case k_slo:
    jit_opcode_make_uop1(p_uop, k_opcode_SAVE_CARRY, 0);
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
      jit_opcode_make_uop1(p_uop,
                           k_opcode_ADD_CYCLES,
                           (uint8_t) (p_details->max_cycles_orig - 2));
      p_uop++;
    }
    break;
  case k_cmp:
  case k_cpx:
  case k_cpy:
    jit_opcode_make_uop1(p_uop, k_opcode_SAVE_CARRY_INV, 0);
    p_uop++;
    break;
  case k_lda:
  case k_txa:
  case k_tya:
  case k_pla:
    jit_opcode_make_uop1(p_uop, k_opcode_FLAGA, 0);
    p_uop++;
    break;
  case k_ldx:
  case k_tax:
  case k_tsx:
    jit_opcode_make_uop1(p_uop, k_opcode_FLAGX, 0);
    p_uop++;
    break;
  case k_ldy:
  case k_tay:
    jit_opcode_make_uop1(p_uop, k_opcode_FLAGY, 0);
    p_uop++;
    break;
  case k_rol:
  case k_ror:
    jit_opcode_make_uop1(p_uop, k_opcode_SAVE_CARRY, 0);
    p_uop++;
    switch (opmode) {
    case k_acc:
      jit_opcode_make_uop1(p_uop, k_opcode_FLAGA, 0);
      p_uop++;
      break;
    case k_zpg:
    case k_abs:
      jit_opcode_make_uop1(p_uop, k_opcode_FLAG_MEM, operand_6502);
      p_uop++;
    default:
      break;
    }
    break;
  case k_sbc:
    jit_opcode_make_uop1(p_uop, k_opcode_SAVE_CARRY_INV, 0);
    p_uop++;
    jit_opcode_make_uop1(p_uop, k_opcode_SAVE_OVERFLOW, 0);
    p_uop++;
    break;
  default:
    break;
  }

  /* Post-main per-mode uops. */
  /* Code invalidation for writes, aka. self-modifying code. */
  /* TODO: stack page invalidations. */
  if (opmem == k_write || opmem == k_rw) {
    switch (opmode) {
    case k_abs:
      jit_opcode_make_uop1(p_uop, k_opcode_WRITE_INV_ABS, operand_6502);
      p_uop++;
      break;
    case k_abx:
      jit_opcode_make_uop1(p_uop, k_opcode_MODE_ABX, operand_6502);
      p_uop++;
      jit_opcode_make_uop1(p_uop, k_opcode_WRITE_INV_SCRATCH, 0);
      p_uop++;
      break;
    case k_aby:
      jit_opcode_make_uop1(p_uop, k_opcode_MODE_ABY, operand_6502);
      p_uop++;
      jit_opcode_make_uop1(p_uop, k_opcode_WRITE_INV_SCRATCH, 0);
      p_uop++;
      break;
    case k_idx:
      jit_opcode_make_uop1(p_uop, k_opcode_WRITE_INV_SCRATCH, 0);
      p_uop++;
      break;
    case k_idy:
      jit_opcode_make_uop1(p_uop, k_opcode_WRITE_INV_SCRATCH_Y, 0);
      p_uop++;
      break;
    case k_zpg:
      if (p_compiler->compile_for_code_in_zero_page) {
        jit_opcode_make_uop1(p_uop, k_opcode_WRITE_INV_ABS, operand_6502);
        p_uop++;
      }
      break;
    case k_zpx:
    case k_zpy:
      if (p_compiler->compile_for_code_in_zero_page) {
        jit_opcode_make_uop1(p_uop, k_opcode_WRITE_INV_SCRATCH, 0);
        p_uop++;
      }
      break;
    default:
      break;
    }
  }
  /* Accurate timings for page crossing cycles. */
  if (p_compiler->option_accurate_timings &&
      (opmem == k_read) &&
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

  /* The segment we need to hit for memory accesses.
   * We have different segments:
   * READ
   * WRITE
   * READ INDIRECT
   * WRITE INDIRECT
   * These segments are generally different virtual views on top of the same
   * physical 6502 memory backing buffer. The differences are that writes to
   * ROM area are silently / quickly ignored, and the indirect segments fault
   * if hardware registers are hit.
   * To minimize L1 DTLB issues, we'll generally map all accesses to READ
   * INDIRECT when we know it doesn't make a difference.
   */
  uint32_t segment_abs;
  uint32_t segment_abs_write;
  uint32_t segment_abn;
  uint32_t segment_abn_write;
  int is_always_ram;
  int is_always_ram_abn;

  is_always_ram = p_memory_access->memory_is_always_ram(p_memory_object,
                                                        (uint16_t) value1);
  /* Assumes address space wrap and hardware register access taken care of
   * elsewhere.
   */
  is_always_ram_abn = (is_always_ram &&
                       p_memory_access->memory_is_always_ram(
                           p_memory_object, (uint16_t) (value1 + 0xFF)));
  /* Always calculate the segment even for irrelevant uopcodes; it's simpler. */
  segment_abs = K_BBC_MEM_READ_IND_ADDR;
  segment_abs_write = K_BBC_MEM_READ_IND_ADDR;
  segment_abn = K_BBC_MEM_READ_IND_ADDR;
  segment_abn_write = K_BBC_MEM_READ_IND_ADDR;
  if (!is_always_ram) {
    segment_abs = K_BBC_MEM_READ_FULL_ADDR;
    segment_abs_write = K_BBC_MEM_WRITE_FULL_ADDR;
  }
  if (!is_always_ram_abn) {
    segment_abn = K_BBC_MEM_READ_FULL_ADDR;
    segment_abn_write = K_BBC_MEM_WRITE_FULL_ADDR;
  }

  /* Resolve any addresses to real pointers. */
  switch (uopcode) {
  case k_opcode_countdown:
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
    asm_emit_jit_check_countdown(p_dest_buf,
                                 (uint32_t) value2,
                                 (void*) (size_t) value1);
    break;
  case k_opcode_debug:
    asm_emit_jit_call_debug(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_interp:
    asm_emit_jit_jump_interp(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_inturbo:
    asm_emit_jit_call_inturbo(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_for_testing:
    asm_emit_jit_for_testing(p_dest_buf);
    break;
  case k_opcode_ADD_CYCLES:
    asm_emit_jit_ADD_CYCLES(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_ADD_ABS:
    asm_emit_jit_ADD_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case k_opcode_ADD_ABX:
    asm_emit_jit_ADD_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case k_opcode_ADD_ABY:
    asm_emit_jit_ADD_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case k_opcode_ADD_IMM:
    asm_emit_jit_ADD_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_ADD_SCRATCH:
    asm_emit_jit_ADD_SCRATCH(p_dest_buf, 0);
    break;
  case k_opcode_ADD_SCRATCH_Y:
    asm_emit_jit_ADD_SCRATCH_Y(p_dest_buf);
    break;
  case k_opcode_ASL_ACC_n:
    asm_emit_jit_ASL_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_CHECK_BCD:
    asm_emit_jit_CHECK_BCD(p_dest_buf);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_SCRATCH_n:
    asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_SCRATCH_X:
    asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_X(p_dest_buf);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_SCRATCH_Y:
    asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_Y(p_dest_buf);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_X_n:
    asm_emit_jit_CHECK_PAGE_CROSSING_X_n(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_Y_n:
    asm_emit_jit_CHECK_PAGE_CROSSING_Y_n(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_CHECK_PENDING_IRQ:
    asm_emit_jit_CHECK_PENDING_IRQ(p_dest_buf, (void*) (size_t) value1);
    break;
  case k_opcode_CLEAR_CARRY:
    asm_emit_jit_CLEAR_CARRY(p_dest_buf);
    break;
  case k_opcode_EOR_SCRATCH_n:
    asm_emit_jit_EOR_SCRATCH(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_FLAGA:
    asm_emit_instruction_A_NZ_flags(p_dest_buf);
    break;
  case k_opcode_FLAGX:
    asm_emit_instruction_X_NZ_flags(p_dest_buf);
    break;
  case k_opcode_FLAGY:
    asm_emit_instruction_Y_NZ_flags(p_dest_buf);
    break;
  case k_opcode_FLAG_MEM:
    asm_emit_jit_FLAG_MEM(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_INC_SCRATCH:
    asm_emit_jit_INC_SCRATCH(p_dest_buf);
    break;
  case k_opcode_INVERT_CARRY:
    asm_emit_jit_INVERT_CARRY(p_dest_buf);
    break;
  case k_opcode_JMP_SCRATCH:
    asm_emit_jit_JMP_SCRATCH(p_dest_buf);
    break;
  case k_opcode_LDA_SCRATCH_n:
    asm_emit_jit_LDA_SCRATCH(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_LDA_SCRATCH_X:
    asm_emit_jit_LDA_SCRATCH_X(p_dest_buf);
    break;
  case k_opcode_LDA_Z:
    asm_emit_jit_LDA_Z(p_dest_buf);
    break;
  case k_opcode_LDX_Z:
    asm_emit_jit_LDX_Z(p_dest_buf);
    break;
  case k_opcode_LDY_Z:
    asm_emit_jit_LDY_Z(p_dest_buf);
    break;
  case k_opcode_LOAD_CARRY_FOR_BRANCH:
    asm_emit_jit_LOAD_CARRY_FOR_BRANCH(p_dest_buf);
    break;
  case k_opcode_LOAD_CARRY_FOR_CALC:
    asm_emit_jit_LOAD_CARRY_FOR_CALC(p_dest_buf);
    break;
  case k_opcode_LOAD_CARRY_INV_FOR_CALC:
    asm_emit_jit_LOAD_CARRY_INV_FOR_CALC(p_dest_buf);
    break;
  case k_opcode_LOAD_OVERFLOW:
    asm_emit_jit_LOAD_OVERFLOW(p_dest_buf);
    break;
  case k_opcode_LOAD_SCRATCH_8:
    asm_emit_jit_LOAD_SCRATCH_8(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_LOAD_SCRATCH_16:
    asm_emit_jit_LOAD_SCRATCH_16(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_LSR_ACC_n:
    asm_emit_jit_LSR_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_MODE_ABX:
    asm_emit_jit_MODE_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_ABY:
    asm_emit_jit_MODE_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_IND_8:
    asm_emit_jit_MODE_IND_8(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_MODE_IND_16:
    asm_emit_jit_MODE_IND_16(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case k_opcode_MODE_IND_SCRATCH_8:
    asm_emit_jit_MODE_IND_SCRATCH_8(p_dest_buf);
    break;
  case k_opcode_MODE_IND_SCRATCH_16:
    asm_emit_jit_MODE_IND_SCRATCH_16(p_dest_buf);
    break;
  case k_opcode_MODE_ZPX:
    asm_emit_jit_MODE_ZPX(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_MODE_ZPY:
    asm_emit_jit_MODE_ZPY(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_PULL_16:
    asm_emit_jit_PULL_16(p_dest_buf);
    break;
  case k_opcode_PUSH_16:
    asm_emit_jit_PUSH_16(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ROL_ACC_n:
    asm_emit_jit_ROL_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_ROR_ACC_n:
    asm_emit_jit_ROR_ACC_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_SAVE_CARRY:
    asm_emit_jit_SAVE_CARRY(p_dest_buf);
    break;
  case k_opcode_SAVE_CARRY_INV:
    asm_emit_jit_SAVE_CARRY_INV(p_dest_buf);
    break;
  case k_opcode_SAVE_OVERFLOW:
    asm_emit_jit_SAVE_OVERFLOW(p_dest_buf);
    break;
  case k_opcode_SET_CARRY:
    asm_emit_jit_SET_CARRY(p_dest_buf);
    break;
  case k_opcode_STA_SCRATCH_n:
    asm_emit_jit_STA_SCRATCH(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_STOA_IMM:
    asm_emit_jit_STOA_IMM(p_dest_buf, (uint16_t) value1, (uint8_t) value2);
    break;
  case k_opcode_SUB_ABS:
    asm_emit_jit_SUB_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case k_opcode_SUB_IMM:
    asm_emit_jit_SUB_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_WRITE_INV_ABS:
    asm_emit_jit_WRITE_INV_ABS(p_dest_buf, (uint32_t) value1);
    break;
  case k_opcode_WRITE_INV_SCRATCH:
    asm_emit_jit_WRITE_INV_SCRATCH(p_dest_buf);
    break;
  case k_opcode_WRITE_INV_SCRATCH_n:
    asm_emit_jit_WRITE_INV_SCRATCH_n(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_WRITE_INV_SCRATCH_Y:
    asm_emit_jit_WRITE_INV_SCRATCH_Y(p_dest_buf);
    break;
  case 0x01: /* ORA idx */
  case 0x15: /* ORA zpx */
    asm_emit_jit_ORA_SCRATCH(p_dest_buf, 0);
    break;
  case 0x04: /* NOP zpg */ /* Undocumented. */
  case 0xDC: /* NOP abx */ /* Undocumented. */
  case 0xEA: /* NOP */
  case 0xF4: /* NOP zpx */ /* Undocumented. */
    /* We don't really have to emit anything for a NOP, but for now and for
     * good readability, we'll emit a host NOP.
     * (The correct place to change if we wanted to not emit anything would be
     * to eliminate the 6502 opcode in the optimizer.)
     * We have a minimum of 2 bytes of x64 code per uop because that's the size
     * of the self-modified marker, so we'll need 2 1-byte x64 nops.
     */
    asm_emit_instruction_REAL_NOP(p_dest_buf);
    asm_emit_instruction_REAL_NOP(p_dest_buf);
    break;
  case 0x05: /* ORA zpg */
  case 0x0D: /* ORA abs */
    asm_emit_jit_ORA_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0x06: /* ASL zpg */
    asm_emit_jit_ASL_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x07: /* SLO zpg */ /* Undocumented. */
    asm_emit_jit_SLO_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x08:
    asm_emit_instruction_PHP(p_dest_buf);
    break;
  case 0x09:
    asm_emit_jit_ORA_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x0A:
    asm_emit_jit_ASL_ACC(p_dest_buf);
    break;
  case 0x0E: /* ASL abs */
    if (is_always_ram) {
      asm_emit_jit_ASL_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_ASL_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x10:
    asm_emit_jit_BPL(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x11: /* ORA idy */
    asm_emit_jit_ORA_SCRATCH_Y(p_dest_buf);
    break;
  case 0x16: /* ASL zpx */
    asm_emit_jit_ASL_scratch(p_dest_buf);
    break;
  case 0x18:
    asm_emit_instruction_CLC(p_dest_buf);
    break;
  case 0x19:
    asm_emit_jit_ORA_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x1D:
    asm_emit_jit_ORA_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x1E: /* ASL abx */
    if (is_always_ram_abn) {
      asm_emit_jit_ASL_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_ASL_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x21: /* AND idx */
  case 0x35: /* AND zpx */
    asm_emit_jit_AND_SCRATCH(p_dest_buf, 0);
    break;
  case 0x24: /* BIT zpg */
  case 0x2C: /* BIT abs */
    asm_emit_jit_BIT(p_dest_buf, (uint16_t) value1);
    break;
  case 0x25: /* AND zpg */
  case 0x2D: /* AND abs */
    asm_emit_jit_AND_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0x26: /* ROL zpg */
  case 0x2E: /* ROL abs */
    if (is_always_ram) {
      asm_emit_jit_ROL_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_ROL_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x28:
    asm_emit_instruction_PLP(p_dest_buf);
    break;
  case 0x29:
    asm_emit_jit_AND_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x2A:
    asm_emit_jit_ROL_ACC(p_dest_buf);
    break;
  case 0x30:
    asm_emit_jit_BMI(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x31: /* AND idy */
    asm_emit_jit_AND_SCRATCH_Y(p_dest_buf);
    break;
  case 0x36: /* ROL zpx */
    asm_emit_jit_ROL_scratch(p_dest_buf);
    break;
  case 0x38:
    asm_emit_instruction_SEC(p_dest_buf);
    break;
  case 0x39:
    asm_emit_jit_AND_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x3D:
    asm_emit_jit_AND_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x3E:
    asm_emit_jit_ROL_ABX_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x41: /* EOR idx */
  case 0x55: /* EOR zpx */
    asm_emit_jit_EOR_SCRATCH(p_dest_buf, 0);
    break;
  case 0x45: /* EOR zpg */
  case 0x4D: /* EOR abs */
    asm_emit_jit_EOR_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0x46: /* LSR zpg */
    asm_emit_jit_LSR_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x48:
    asm_emit_instruction_PHA(p_dest_buf);
    break;
  case 0x49:
    asm_emit_jit_EOR_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x4A:
    asm_emit_jit_LSR_ACC(p_dest_buf);
    break;
  case 0x4B: /* ALR imm */ /* Undocumented. */
    asm_emit_jit_ALR_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x4C:
  case k_opcode_jump_raw:
    asm_emit_jit_JMP(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x4E: /* LSR abs */
    if (is_always_ram) {
      asm_emit_jit_LSR_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_LSR_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x50:
    asm_emit_jit_BVC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x51: /* EOR idy */
    asm_emit_jit_EOR_SCRATCH_Y(p_dest_buf);
    break;
  case 0x56: /* LSR zpx */
    asm_emit_jit_LSR_scratch(p_dest_buf);
    break;
  case 0x58:
    asm_emit_instruction_CLI(p_dest_buf);
    break;
  case 0x59:
    asm_emit_jit_EOR_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x5D:
    asm_emit_jit_EOR_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x5E: /* LSR abx */
    if (is_always_ram_abn) {
      asm_emit_jit_LSR_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_LSR_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x61: /* ADC idx */
  case 0x75: /* ADC zpx */
    asm_emit_jit_ADC_SCRATCH(p_dest_buf, 0);
    break;
  case 0x65: /* ADC zpg */
  case 0x6D: /* ADC abs */
    asm_emit_jit_ADC_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0x66: /* ROR zpg */
  case 0x6E: /* ROR abs */
    if (is_always_ram) {
      asm_emit_jit_ROR_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_ROR_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x68:
    asm_emit_instruction_PLA(p_dest_buf);
    break;
  case 0x69:
    asm_emit_jit_ADC_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x6A:
    asm_emit_jit_ROR_ACC(p_dest_buf);
    break;
  case 0x70:
    asm_emit_jit_BVS(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x71: /* ADC idy */
    asm_emit_jit_ADC_SCRATCH_Y(p_dest_buf);
    break;
  case 0x76: /* ROR zpx */
    asm_emit_jit_ROR_scratch(p_dest_buf);
    break;
  case 0x78:
    asm_emit_instruction_SEI(p_dest_buf);
    break;
  case 0x79:
    asm_emit_jit_ADC_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x7D:
    asm_emit_jit_ADC_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0x7E:
    asm_emit_jit_ROR_ABX_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x81: /* STA idx */
  case 0x95: /* STA zpx */
    asm_emit_jit_STA_SCRATCH(p_dest_buf, 0);
    break;
  case 0x98:
    asm_emit_instruction_TYA(p_dest_buf);
    break;
  case 0x84: /* STY zpg */
  case 0x8C: /* STY abs */
    asm_emit_jit_STY_ABS(p_dest_buf, (uint16_t) value1, segment_abs_write);
    break;
  case 0x85: /* STA zpg */
  case 0x8D: /* STA abs */
    asm_emit_jit_STA_ABS(p_dest_buf, (uint16_t) value1, segment_abs_write);
    break;
  case 0x86: /* STX zpg */
  case 0x8E: /* STX abs */
    asm_emit_jit_STX_ABS(p_dest_buf, (uint16_t) value1, segment_abs_write);
    break;
  case 0x87: /* SAX zpg */ /* Undocumented. */
    asm_emit_jit_SAX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x88:
    asm_emit_instruction_DEY(p_dest_buf);
    break;
  case 0x8A:
    asm_emit_instruction_TXA(p_dest_buf);
    break;
  case 0x90:
    asm_emit_jit_BCC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x91: /* STA idy */
    asm_emit_jit_STA_SCRATCH_Y(p_dest_buf);
    break;
  case 0x94: /* STY zpx */
    asm_emit_jit_STY_scratch(p_dest_buf);
    break;
  case 0x96: /* STX zpy */
    asm_emit_jit_STX_scratch(p_dest_buf);
    break;
  case 0x99:
    asm_emit_jit_STA_ABY(p_dest_buf, (uint16_t) value1, segment_abn_write);
    break;
  case 0x9A:
    asm_emit_instruction_TXS(p_dest_buf);
    break;
  case 0x9C:
    asm_emit_jit_SHY_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x9D:
    asm_emit_jit_STA_ABX(p_dest_buf, (uint16_t) value1, segment_abn_write);
    break;
  case 0xA0:
    asm_emit_jit_LDY_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xA1: /* LDA idx */
  case 0xB5: /* LDA zpx */
    asm_emit_jit_LDA_SCRATCH(p_dest_buf, 0);
    break;
  case 0xA2:
    asm_emit_jit_LDX_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xA4: /* LDY zpg */
  case 0xAC: /* LDY abs */
    asm_emit_jit_LDY_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0xA5: /* LDA zpg */
  case 0xAD: /* LDA abs */
    asm_emit_jit_LDA_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0xA6: /* LDX zpg */
  case 0xAE: /* LDX abs */
    asm_emit_jit_LDX_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0xA8:
    asm_emit_instruction_TAY(p_dest_buf);
    break;
  case 0xA9:
    asm_emit_jit_LDA_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xAA:
    asm_emit_instruction_TAX(p_dest_buf);
    break;
  case 0xB0:
    asm_emit_jit_BCS(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xB1: /* LDA idy */
    asm_emit_jit_LDA_SCRATCH_Y(p_dest_buf);
    break;
  case 0xB4: /* LDY zpx */
    asm_emit_jit_LDY_scratch(p_dest_buf);
    break;
  case 0xB6: /* LDX zpy */
    asm_emit_jit_LDX_scratch(p_dest_buf);
    break;
  case 0xB8:
    asm_emit_instruction_CLV(p_dest_buf);
    break;
  case 0xB9:
    asm_emit_jit_LDA_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xBA:
    asm_emit_instruction_TSX(p_dest_buf);
    break;
  case 0xBC:
    asm_emit_jit_LDY_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xBD:
    asm_emit_jit_LDA_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xBE:
    asm_emit_jit_LDX_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xC0:
    asm_emit_jit_CPY_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xC1: /* CMP idx */
  case 0xD5: /* CMP zpx */
    asm_emit_jit_CMP_SCRATCH(p_dest_buf, 0);
    break;
  case 0xC4: /* CPY zpg */
  case 0xCC: /* CPY abs */
    asm_emit_jit_CPY_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0xC5: /* CMP zpg */
  case 0xCD: /* CMP abs */
    asm_emit_jit_CMP_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0xC6: /* DEC zpg */
    asm_emit_jit_DEC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC8:
    asm_emit_instruction_INY(p_dest_buf);
    break;
  case 0xC9:
    asm_emit_jit_CMP_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xCA:
    asm_emit_instruction_DEX(p_dest_buf);
    break;
  case 0xCE: /* DEC abs */
    if (is_always_ram) {
      asm_emit_jit_DEC_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_DEC_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xD0:
    asm_emit_jit_BNE(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xD1: /* CMP idy */
    asm_emit_jit_CMP_SCRATCH_Y(p_dest_buf);
    break;
  case 0xD6: /* DEC zpx */
    asm_emit_jit_DEC_scratch(p_dest_buf);
    break;
  case 0xD8:
    asm_emit_instruction_CLD(p_dest_buf);
    break;
  case 0xD9:
    asm_emit_jit_CMP_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xDD:
    asm_emit_jit_CMP_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xDE: /* DEC abx */
    if (is_always_ram_abn) {
      asm_emit_jit_DEC_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_DEC_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xE0:
    asm_emit_jit_CPX_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xE1: /* SBC idx */
  case 0xF5: /* SBC zpx */
    asm_emit_jit_SBC_SCRATCH(p_dest_buf, 0);
    break;
  case 0xE4: /* CPX zpg */
  case 0xEC: /* CPX abs */
    asm_emit_jit_CPX_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0xE5: /* SBC zpg */
  case 0xED: /* SBC abs */
    asm_emit_jit_SBC_ABS(p_dest_buf, (uint16_t) value1, segment_abs);
    break;
  case 0xE6: /* INC zpg */
    asm_emit_jit_INC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xE8:
    asm_emit_instruction_INX(p_dest_buf);
    break;
  case 0xE9:
    asm_emit_jit_SBC_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xEE: /* INC abs */
    if (is_always_ram) {
      asm_emit_jit_INC_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_INC_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xF0:
    asm_emit_jit_BEQ(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xF1: /* SBC idy */
    asm_emit_jit_SBC_SCRATCH_Y(p_dest_buf);
    break;
  case 0xF2:
    asm_emit_instruction_CRASH(p_dest_buf);
    break;
  case 0xF6: /* INC zpx */
    asm_emit_jit_INC_scratch(p_dest_buf);
    break;
  case 0xF8:
    asm_emit_instruction_SED(p_dest_buf);
    break;
  case 0xF9:
    asm_emit_jit_SBC_ABY(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xFD:
    asm_emit_jit_SBC_ABX(p_dest_buf, (uint16_t) value1, segment_abn);
    break;
  case 0xFE: /* INC abx */
    if (is_always_ram_abn) {
      asm_emit_jit_INC_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_emit_jit_INC_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  default:
    /* Use the interpreter for unknown opcodes. These could be either the
     * special re-purposed opcodes (e.g. CYCLES) or genuinely unused opcodes.
     */
    asm_emit_jit_jump_interp(p_dest_buf, (uint16_t) value2);
    break;
  }
}

static void
jit_compiler_add_history(struct jit_compiler* p_compiler,
                         uint16_t addr_6502,
                         int32_t opcode_6502,
                         int is_self_modified,
                         uint64_t ticks) {
  struct jit_compile_history* p_history = &p_compiler->history[addr_6502];
  uint32_t ring_buffer_index;

  p_history->opcode = opcode_6502;

  if (!is_self_modified) {
    return;
  }

  ring_buffer_index = p_history->ring_buffer_index;

  ring_buffer_index++;
  if (ring_buffer_index == k_opcode_history_length) {
    ring_buffer_index = 0;
  }

  p_history->ring_buffer_index = ring_buffer_index;
  p_history->opcodes[ring_buffer_index] = opcode_6502;
  p_history->times[ring_buffer_index] = ticks;
}

static int
jit_compiler_emit_dynamic_opcode(struct jit_compiler* p_compiler,
                                 int32_t* p_opcode,
                                 struct jit_opcode_details* p_details) {
  uint32_t i;
  uint64_t ticks = timing_get_total_timer_ticks(p_compiler->p_timing);
  uint16_t addr_6502 = p_details->addr_6502;
  struct jit_compile_history* p_history = &p_compiler->history[addr_6502];
  uint32_t index = p_history->ring_buffer_index;
  int32_t opcode = p_details->opcode_6502;
  uint32_t count = 0;
  uint32_t count_needed = p_compiler->dynamic_trigger;

  if (count_needed > k_opcode_history_length) {
    count_needed = k_opcode_history_length;
  }
  if (p_details->self_modify_invalidated && (opcode == p_history->opcode)) {
    count++;
  }

  for (i = 0; i < k_opcode_history_length; ++i) {
    /* Stop counting if we run out of opcodes. */
    if (p_history->opcodes[index] == -1) {
      break;
    }
    /* Stop counting if the events are over a second old. */
    if ((ticks - p_history->times[index]) > 200000000) {
      break;
    }
    /* Switch from dynamic operand to dynamic opcode if the opcode differs. */
    /* Stop counting if we run out of opcodes, or the opcode differs. */
    if (p_history->opcodes[index] != opcode) {
      opcode = -1;
    }
    if (index == 0) {
      index = (k_opcode_history_length - 1);
    } else {
      index--;
    }
    count++;
  }

  if (count < count_needed) {
    return 0;
  }

  *p_opcode = opcode;
  return 1;
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
                               k_opcode_LOAD_SCRATCH_8,
                               (uint16_t) (addr_6502 + 1),
                               new_uopcode,
                               0);
    }
    break;
  case k_abs:
    switch (opcode_6502) {
    case 0x4C: /* JMP abs */
      new_uopcode = k_opcode_JMP_SCRATCH;
      break;
    case 0x8C: /* STY abs */
      new_uopcode = 0x94; /* STY zpx -- which is STY_scratch. */
      write_inv_search_uopcode = k_opcode_WRITE_INV_ABS;
      write_inv_replace_uopcode = k_opcode_WRITE_INV_SCRATCH;
      break;
    case 0x8D: /* STA abs */
      new_uopcode = 0x95; /* STA zpx -- which is STA_SCRATCH. */
      write_inv_search_uopcode = k_opcode_WRITE_INV_ABS;
      write_inv_replace_uopcode = k_opcode_WRITE_INV_SCRATCH;
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
                               k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               new_uopcode,
                               0);
    }
    break;
  case k_aby:
    switch (opcode_6502) {
    case 0x99: /* STA aby */
      new_uopcode = 0x91; /* STA idy */
      write_inv_search_uopcode = k_opcode_WRITE_INV_SCRATCH;
      write_inv_replace_uopcode = k_opcode_WRITE_INV_SCRATCH_Y;
      write_inv_erase_uopcode = k_opcode_MODE_ABY;
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
                               k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               new_uopcode,
                               0);
    }
    break;
  default:
    switch (opcode_6502) {
    case 0x6C: /* JMP ind */
      new_uopcode = k_opcode_MODE_IND_SCRATCH_16;
      jit_opcode_find_replace2(p_opcode,
                               k_opcode_MODE_IND_16,
                               k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               k_opcode_MODE_IND_SCRATCH_16,
                               0);
      break;
    case 0xBD: /* LDA abx */
      new_uopcode = k_opcode_LDA_SCRATCH_X;
      jit_opcode_find_replace2(p_opcode,
                               0xBD,
                               k_opcode_LOAD_SCRATCH_16,
                               (uint16_t) (addr_6502 + 1),
                               k_opcode_LDA_SCRATCH_X,
                               0);
      page_crossing_search_uopcode = k_opcode_CHECK_PAGE_CROSSING_X_n;
      page_crossing_replace_uopcode = k_opcode_CHECK_PAGE_CROSSING_SCRATCH_X;
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
    struct jit_uop* p_uop = jit_opcode_find_uop(p_opcode,
                                                page_crossing_search_uopcode);
    if (p_uop != NULL) {
      jit_opcode_make_uop1(p_uop, page_crossing_replace_uopcode, 0);
    }
  }
  if (write_inv_search_uopcode != -1) {
    struct jit_uop* p_uop = jit_opcode_find_uop(p_opcode,
                                                write_inv_search_uopcode);
    assert(p_uop != NULL);
    jit_opcode_make_uop1(p_uop, write_inv_replace_uopcode, 0);
  }
  if (write_inv_erase_uopcode != -1) {
    jit_opcode_erase_uop(p_opcode, write_inv_erase_uopcode);
  }
}

uint32_t
jit_compiler_compile_block(struct jit_compiler* p_compiler,
                           int is_invalidation,
                           uint16_t start_addr_6502) {
  struct jit_opcode_details opcode_details[k_max_opcodes_per_compile];
  uint8_t single_opcode_buffer[128];
  struct jit_uop tmp_uop;
  uint64_t ticks;

  uint32_t i_opcodes;
  uint32_t i_uops;
  uint16_t addr_6502;
  uint32_t cycles;
  int needs_countdown;
  struct jit_opcode_details* p_details;
  struct jit_opcode_details* p_details_fixup;
  struct jit_uop* p_uop;
  void* p_host_address_base;
  struct util_buffer* p_tmp_buf = p_compiler->p_tmp_buf;
  struct util_buffer* p_single_uopcode_buf = p_compiler->p_single_uopcode_buf;

  /* total_num_opcodes includes internally generated opcodes such as jumping
   * from the end of a block to the start of the next. total_num_6502_opcodes
   * is a count of real 6502 opcodes consumed only.
   */
  uint32_t total_num_opcodes = 0;
  uint32_t total_num_6502_opcodes = 0;
  int block_ended = 0;
  int is_block_start = 0;
  int is_next_block_continuation = 0;

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

  /* Prepend opcodes at the start of every block. */
  addr_6502 = start_addr_6502;
  /* 1) Every block starts with a countdown check. */
  p_details = &opcode_details[total_num_opcodes];
  jit_opcode_make_internal_opcode1(p_details,
                                   addr_6502,
                                   k_opcode_countdown,
                                   addr_6502);
  p_details->cycles_run_start = 0;
  total_num_opcodes++;
  /* 2) An unused opcode for the optimizer to use if it wants. */
  p_details = &opcode_details[total_num_opcodes];
  jit_opcode_make_internal_opcode1(p_details, addr_6502, 0xEA, 0);
  p_details->eliminated = 1;
  total_num_opcodes++;

  /* First break all the opcodes for this run into uops.
   * This defines maximum possible bounds for the block and respects existing
   * known block boundaries.
   */
  needs_countdown = 0;
  while (1) {
    p_details = &opcode_details[total_num_opcodes];

    if (needs_countdown) {
      jit_opcode_make_internal_opcode1(p_details,
                                       addr_6502,
                                       k_opcode_countdown,
                                       addr_6502);
      p_details->cycles_run_start = 0;
      needs_countdown = 0;
      total_num_opcodes++;
      p_details = &opcode_details[total_num_opcodes];
    }

    assert(total_num_opcodes < k_max_opcodes_per_compile);

    jit_compiler_get_opcode_details(p_compiler, p_details, addr_6502);

    addr_6502 += p_details->len_bytes_6502_orig;
    total_num_opcodes++;
    total_num_6502_opcodes++;

    if (p_details->branches == k_bra_m) {
      needs_countdown = 1;
    }

    /* Exit loop condition: this opcode ends the block, e.g. RTS, JMP etc. */
    if (p_details->ends_block) {
      block_ended = 1;
      break;
    }

    /* Exit loop condition: next opcode is the start of a block boundary. */
    if (p_compiler->addr_is_block_start[addr_6502]) {
      break;
    }

    /* Exit loop condition: we've compiled the configurable max number of 6502
     * opcodes.
     */
    if (total_num_6502_opcodes == p_compiler->max_6502_opcodes_per_block) {
      is_next_block_continuation = 1;
      break;
    }

    /* Exit loop condition: we're out of space in our opcode buffer. The -1 is
     * because we need to reserve space for a final internal opcode we may need
     * to jump out of a block when the block doesn't fit.
     */
    if (total_num_opcodes == (k_max_opcodes_per_compile - 1)) {
      is_next_block_continuation = 1;
      break;
    }

    if (needs_countdown) {
      /* Exit loop condition: only useful to emit a countdown opcode if there's
       * also space for an opcode that does real work, and of course the
       * possibly needed opcode to jump out of a block that doesn't fit.
       */
      if (total_num_opcodes >= (k_max_opcodes_per_compile - 2)) {
        is_next_block_continuation = 1;
        break;
      }
    }
  }

  assert(addr_6502 > start_addr_6502);
  p_compiler->addr_is_block_continuation[addr_6502] =
      is_next_block_continuation;

  if (!block_ended) {
    p_details = &opcode_details[total_num_opcodes];
    total_num_opcodes++;
    /* JMP abs */
    jit_opcode_make_internal_opcode1(p_details, addr_6502, 0x4C, addr_6502);
    p_details->ends_block = 1;
  }

  /* Second, work out if we'll be compiling any dynamic opcodes / operands. */
  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    int32_t dynamic_opcode;
    p_details = &opcode_details[i_opcodes];

    /* Skip internal opcodes. */
    if (p_details->len_bytes_6502_orig == 0) {
      continue;
    }

    /* Check self-modified status for each opcode. It's used in
     * jit_compiler_emit_dynamic_opcode() just below and when storing metadata
     * later.
     */
    addr_6502 = p_details->addr_6502;
    if (jit_has_invalidated_code(p_compiler, addr_6502)) {
      p_details->self_modify_invalidated = 1;
    }

    if (!jit_compiler_emit_dynamic_opcode(p_compiler,
                                          &dynamic_opcode,
                                          p_details)) {
      continue;
    }

    if (!p_compiler->option_no_dynamic_operand && (dynamic_opcode != -1)) {
      jit_compiler_try_make_dynamic_opcode(p_details);
      if (p_details->is_dynamic_operand) {
        if (p_compiler->log_dynamic) {
          log_do_log(k_log_jit,
                     k_log_info,
                     "compiling dynamic operand at $%.4X (opcode $%.2X)",
                     addr_6502,
                     dynamic_opcode);
        }
        continue;
      }
    }
    if (!p_compiler->option_no_dynamic_opcode) {
      if (p_compiler->log_dynamic) {
        log_do_log(k_log_jit,
                   k_log_info,
                   "compiling dynamic opcode at $%.4X (current opcode $%.2X)",
                   addr_6502,
                   p_details->opcode_6502);
      }
      jit_opcode_make_uop1(&p_details->uops[0], k_opcode_inturbo, addr_6502);
      p_details->num_uops = 1;
      p_details->ends_block = 1;
      p_details->is_dynamic_opcode = 1;
      p_details->is_dynamic_operand = 1;
      /* The dyanmic opcode doesn't directly consume 6502 cycles itself -- the
       * mechanics of that are internal to the inturbo machine.
       */
      p_details->max_cycles_orig = 0;
      p_details->max_cycles_merged = 0;
      total_num_opcodes = (i_opcodes + 1);
      break;
    }
  }

  /* Third, walk the opcode list and calculate cycle counts. */
  p_uop = NULL;
  p_details_fixup = NULL;
  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    p_details = &opcode_details[i_opcodes];
    addr_6502 = p_details->addr_6502;

    /* Calculate cycle count at each opcode. */
    if (p_details->cycles_run_start != -1) {
      p_details_fixup = p_details;
      assert(p_details_fixup->num_uops == 1);
      assert(p_details_fixup->cycles_run_start == 0);
      p_uop = &p_details_fixup->uops[0];
      assert(p_uop->uopcode == k_opcode_countdown);
      assert(p_uop->value2 == 0);
    }

    p_details_fixup->cycles_run_start += p_details->max_cycles_orig;
    p_uop->value2 = p_details_fixup->cycles_run_start;
  }

  /* Fourth, run the optimizer across the list of opcodes. */
  if (!p_compiler->option_no_optimize) {
    total_num_opcodes = jit_optimizer_optimize(&opcode_details[0],
                                               total_num_opcodes);
  }

  /* Fifth, emit the uop stream to the output buffer. */
  addr_6502 = start_addr_6502;
  p_host_address_base =
      p_compiler->get_block_host_address(p_compiler->p_host_address_object,
                                         addr_6502);
  util_buffer_setup(p_tmp_buf, p_host_address_base, K_BBC_JIT_BYTES_PER_BYTE);
  util_buffer_set_base_address(p_tmp_buf, p_host_address_base);
  util_buffer_setup(p_single_uopcode_buf,
                    &single_opcode_buffer[0],
                    sizeof(single_opcode_buffer));

  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    uint32_t num_uops;
    int ends_block;
    size_t opcode_len_asm = 0;

    p_details = &opcode_details[i_opcodes];
    if (p_details->eliminated) {
      continue;
    }
    if (i_opcodes == (total_num_opcodes - 1)) {
      assert(p_details->ends_block);
    } else {
      assert(!p_details->ends_block);
    }

    num_uops = p_details->num_uops;
    ends_block = p_details->ends_block;

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      size_t buf_needed;
      size_t out_buf_pos;
      p_uop = &p_details->uops[i_uops];
      if (p_uop->eliminated) {
        continue;
      }

      out_buf_pos = util_buffer_get_pos(p_tmp_buf);
      util_buffer_set_base_address(p_single_uopcode_buf,
                                   (p_host_address_base + out_buf_pos));
      util_buffer_set_pos(p_single_uopcode_buf, 0);
      jit_compiler_emit_uop(p_compiler, p_single_uopcode_buf, p_uop);

      /* Calculate if this uopcode fits. In order to fit, not only must the
       * uopcode itself fit, but there must be space for a possible jump to the
       * block continuation.
       */
      buf_needed = util_buffer_get_pos(p_single_uopcode_buf);
      if (!ends_block || (i_uops != (num_uops - 1))) {
        buf_needed += p_compiler->len_asm_jmp;
      }

      if (util_buffer_remaining(p_tmp_buf) < buf_needed) {
        /* Emit jump to the next adjacent code block. We'll need to jump over
         * the compile trampoline at the beginning of the block.
         */
        void* p_resume =
            (p_host_address_base + util_buffer_get_length(p_tmp_buf));
        /* TODO: use the asm layer to decide how big the marker is. */
        p_resume += 2;
        jit_opcode_make_uop1(&tmp_uop,
                             k_opcode_jump_raw,
                             (int32_t) (uintptr_t) p_resume);
        jit_compiler_emit_uop(p_compiler, p_tmp_buf, &tmp_uop);
        util_buffer_fill_to_end(p_tmp_buf, '\xcc');

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
        /* Start writing after the invalidation marker. */
        util_buffer_set_pos(p_tmp_buf, 2);

        /* Re-emit the current uopcode because it is now at a different host
         * address. Jump target calculations will have changed.
         */
        util_buffer_set_pos(p_single_uopcode_buf, 0);
        util_buffer_set_base_address(p_single_uopcode_buf,
                                     (p_host_address_base + 2));
        jit_compiler_emit_uop(p_compiler, p_single_uopcode_buf, p_uop);
      }

      if (p_details->p_host_address == NULL) {
        p_details->p_host_address =
            (p_host_address_base + util_buffer_get_pos(p_tmp_buf));
      }

      util_buffer_append(p_tmp_buf, p_single_uopcode_buf);
      opcode_len_asm += util_buffer_get_pos(p_single_uopcode_buf);
    }

    if (opcode_len_asm > 0) {
      /* If there's any output, need at least 2 bytes because that the length of
       * the self-modification overwrite.
       */
      assert(opcode_len_asm >= 2);
    }
  }

  /* Fill the unused portion of the buffer with 0xcc, i.e. int3.
   * There are a few good reasons for this:
   * 1) Clarity: see where a code block ends, especially if there was
   * previously a larger code block at this address.
   * 2) Bug detection: better chance of a clean crash if something does a bad
   * jump.
   * 3) Performance. int3 will stop the Intel instruction decoder.
   */
  util_buffer_fill_to_end(p_tmp_buf, '\xcc');

  /* Sixth, update compiler metadata. */
  ticks = timing_get_total_timer_ticks(p_compiler->p_timing);
  cycles = 0;
  for (i_opcodes = 0; i_opcodes < total_num_opcodes; ++i_opcodes) {
    uint8_t num_bytes_6502;
    uint8_t i;
    uint32_t jit_ptr;
    uint32_t i_opcodes_lookahead;

    p_details = &opcode_details[i_opcodes];
    if (p_details->eliminated) {
      continue;
    }
    addr_6502 = p_details->addr_6502;
    if (p_details->cycles_run_start != -1) {
      cycles = p_details->cycles_run_start;
    }

    num_bytes_6502 = p_details->len_bytes_6502_merged;
    jit_ptr = 0;
    i_opcodes_lookahead = i_opcodes;
    while (jit_ptr == 0) {
      void* p_host_address = opcode_details[i_opcodes_lookahead].p_host_address;
      jit_ptr = (uint32_t) (uintptr_t) p_host_address;
      i_opcodes_lookahead++;
      if (jit_ptr == 0) {
        assert(i_opcodes_lookahead < total_num_opcodes);
      }
    }
    for (i = 0; i < num_bytes_6502; ++i) {
      p_compiler->p_jit_ptrs[addr_6502] = jit_ptr;
      p_compiler->p_code_blocks[addr_6502] = start_addr_6502;

      if (addr_6502 != start_addr_6502) {
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

      if (i == 0) {
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
          p_uop = p_details->fixup_uops[i_uops];
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
          case k_opcode_FLAG_MEM:
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
          case k_opcode_SAVE_OVERFLOW:
            p_compiler->addr_o_fixup[addr_6502] = 1;
            break;
          case k_opcode_SAVE_CARRY:
            p_compiler->addr_c_fixup[addr_6502] = 1;
            break;
          case k_opcode_SAVE_CARRY_INV:
            p_compiler->addr_c_fixup[addr_6502] = 2;
            break;
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
      } else {
        if (p_details->is_dynamic_operand) {
          p_compiler->p_jit_ptrs[addr_6502] =
              p_compiler->jit_ptr_dynamic_operand;
        }
        jit_compiler_add_history(p_compiler, addr_6502, -1, 0, ticks);
        p_compiler->addr_cycles_fixup[addr_6502] = -1;
      }

      addr_6502++;
    }
    cycles -= p_details->max_cycles_merged;
  }

  return (addr_6502 - start_addr_6502);
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
jit_compiler_testing_set_max_ops(struct jit_compiler* p_compiler,
                                 uint32_t num_ops) {
  p_compiler->max_6502_opcodes_per_block = num_ops;
}

void
jit_compiler_testing_set_dynamic_trigger(struct jit_compiler* p_compiler,
                                         uint32_t count) {
  p_compiler->dynamic_trigger = count;
}
