#include "inturbo.h"

#include "asm_tables.h"
#include "asm_x64.h"
#include "asm_x64_abi.h"
#include "bbc_options.h"
#include "defs_6502.h"
#include "memory_access.h"
#include "state_6502.h"
#include "util.h"

#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_inturbo_bytes_per_opcode = 128;
static void* k_inturbo_opcodes_addr = (void*) 0x40000000;
static void* k_inturbo_jump_table_addr = (void*) 0x3f000000;
static const size_t k_inturbo_jump_table_size = 4096;

struct inturbo_struct {
  struct asm_x64_abi abi;

  struct memory_access* p_memory_access;
  struct bbc_options* p_options;
  unsigned char* p_inturbo_base;
  uint64_t* p_jump_table;
};

static void
inturbo_fill_tables(struct inturbo_struct* p_inturbo) {
  size_t i;

  struct util_buffer* p_buf = util_buffer_create();
  unsigned char* p_inturbo_base = p_inturbo->p_inturbo_base;
  unsigned char* p_inturbo_opcodes_ptr = p_inturbo_base;
  uint64_t* p_jump_table = p_inturbo->p_jump_table;

  struct bbc_options* p_options = p_inturbo->p_options;
  void* p_debug_callback_object = p_options->p_debug_callback_object;
  int debug = p_options->debug_active_at_addr(p_debug_callback_object, 0xFFFF);

  /* Opcode pointers for the "next opcode" jump table. */
  for (i = 0; i < 256; ++i) {
    p_jump_table[i] =
        (uint64_t) (p_inturbo_base + (i * k_inturbo_bytes_per_opcode));
  }

  for (i = 0; i < 256; ++i) {
    void* p_begin;
    void* p_end;
    unsigned char opmode = g_opmodes[i];
    unsigned char optype = g_optypes[i];
    unsigned char opreg = g_optype_sets_register[i];
    util_buffer_setup(p_buf, p_inturbo_opcodes_ptr, k_inturbo_bytes_per_opcode);

    if (debug) {
      asm_x64_copy(p_buf,
                   asm_x64_inturbo_enter_debug,
                   asm_x64_inturbo_enter_debug_END,
                   0);
    }

    switch (opmode) {
    case k_nil:
    case k_acc:
    case k_imm:
    case 0:
    default:
      p_begin = NULL;
      p_end = NULL;
      break;
    case k_zpg:
      p_begin = asm_x64_inturbo_mode_zpg;
      p_end = asm_x64_inturbo_mode_zpg_END;
      break;
    case k_abs:
      p_begin = asm_x64_inturbo_mode_abs;
      p_end = asm_x64_inturbo_mode_abs_END;
      break;
    }

    if (p_begin) {
      asm_x64_copy(p_buf, p_begin, p_end, 0);
    }

    switch (optype) {
    case k_lda:
      if (opmode == k_imm) {
        p_begin = asm_x64_instruction_LDA_imm_interp;
        p_end = asm_x64_instruction_LDA_imm_interp_END;
      } else {
        p_begin = asm_x64_instruction_LDA_scratch_interp;
        p_end = asm_x64_instruction_LDA_scratch_interp_END;
      }
      break;
    case k_nop:
      p_begin = NULL;
      p_end = NULL;
      break;
    case k_php:
      asm_x64_copy(p_buf,
                   asm_x64_asm_emit_intel_flags_to_scratch,
                   asm_x64_asm_emit_intel_flags_to_scratch_END,
                   0);
      asm_x64_copy(p_buf,
                   asm_x64_set_brk_flag_in_scratch,
                   asm_x64_set_brk_flag_in_scratch_END,
                   0);
      p_begin = asm_x64_push_from_scratch;
      p_end = asm_x64_push_from_scratch_END;
      break;
    default:
      p_begin = asm_x64_instruction_TRAP;
      p_end = asm_x64_instruction_TRAP_END;
      break;
    }

    if (p_begin) {
      asm_x64_copy(p_buf, p_begin, p_end, 0);
    }

    switch (opreg) {
    case k_a:
      p_begin = asm_x64_instruction_A_NZ_flags;
      p_end = asm_x64_instruction_A_NZ_flags_END;
      break;
    case k_x:
      p_begin = asm_x64_instruction_X_NZ_flags;
      p_end = asm_x64_instruction_X_NZ_flags_END;
      break;
    case k_y:
      p_begin = asm_x64_instruction_Y_NZ_flags;
      p_end = asm_x64_instruction_Y_NZ_flags_END;
      break;
    default:
      p_begin = NULL;
      p_end = NULL;
      break;
    }

    if (p_begin) {
      asm_x64_copy(p_buf, p_begin, p_end, 0);
    }

    switch (opmode) {
    case k_nil:
    case k_acc:
    case 0:
      /* All this does is advance 6502 PC by 1. */
      p_begin = asm_x64_inturbo_mode_nil;
      p_end = asm_x64_inturbo_mode_nil_END;
      break;
    case k_imm:
      /* All this does is advance 6502 PC by 2. */
      p_begin = asm_x64_inturbo_mode_imm;
      p_end = asm_x64_inturbo_mode_imm_END;
      break;
    default:
      /* Other modes have already done the PC advance. */
      p_begin = NULL;
      p_end = NULL;
      break;
    }

    if (p_begin) {
      asm_x64_copy(p_buf, p_begin, p_end, 0);
    }

    /* Load next opcode from 6502 PC, jump to correct next asm opcode inturbo
     * handler.
     */
    asm_x64_copy(p_buf,
                 asm_x64_inturbo_next_opcode,
                 asm_x64_inturbo_next_opcode_END,
                 0);

    p_inturbo_opcodes_ptr += k_inturbo_bytes_per_opcode;
  }

  util_buffer_destroy(p_buf);
}

struct inturbo_struct*
inturbo_create(struct state_6502* p_state_6502,
               struct memory_access* p_memory_access,
               struct bbc_timing* p_timing,
               struct bbc_options* p_options) {
  struct inturbo_struct* p_inturbo = malloc(sizeof(struct inturbo_struct));

  (void) p_timing;

  if (p_inturbo == NULL) {
    errx(1, "couldn't allocate inturbo_struct");
  }
  (void) memset(p_inturbo, '\0', sizeof(struct inturbo_struct));

  asm_tables_init();

  p_state_6502->reg_pc = (uint32_t) (size_t) p_memory_access->p_mem_read;

  asm_x64_abi_init(&p_inturbo->abi, p_options, p_state_6502);
  p_inturbo->p_memory_access = p_memory_access;
  p_inturbo->p_options = p_options;

  p_inturbo->p_inturbo_base = util_get_guarded_mapping(
      k_inturbo_opcodes_addr,
      256 * k_inturbo_bytes_per_opcode);
  util_make_mapping_read_write_exec(
      p_inturbo->p_inturbo_base,
      256 * k_inturbo_bytes_per_opcode);

  p_inturbo->p_jump_table = util_get_guarded_mapping(k_inturbo_jump_table_addr,
                                                     k_inturbo_jump_table_size);

  inturbo_fill_tables(p_inturbo);

  return p_inturbo;
}

void
inturbo_destroy(struct inturbo_struct* p_inturbo) {
  util_free_guarded_mapping(p_inturbo->p_inturbo_base,
                            256 * k_inturbo_bytes_per_opcode);
  util_free_guarded_mapping(p_inturbo->p_jump_table, k_inturbo_jump_table_size);
  free(p_inturbo);
}

void
inturbo_enter(struct inturbo_struct* p_inturbo) {
  uint64_t* p_jump_table = p_inturbo->p_jump_table;
  uint16_t addr_6502 = state_6502_get_pc(p_inturbo->abi.p_state_6502);
  unsigned char* p_mem_read = p_inturbo->p_memory_access->p_mem_read;
  unsigned char opcode = p_mem_read[addr_6502];
  uint32_t p_start_address = p_jump_table[opcode];

  asm_x64_asm_enter(p_inturbo, p_start_address);
}

void
inturbo_async_timer_tick(struct inturbo_struct* p_inturbo) {
  (void) p_inturbo;
}
