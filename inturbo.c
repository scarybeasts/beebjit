#include "inturbo.h"

#include "asm_tables.h"
#include "asm_x64_abi.h"
#include "asm_x64_common.h"
#include "asm_x64_inturbo.h"
#include "bbc_options.h"
#include "defs_6502.h"
#include "memory_access.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>
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

  /* Inturbo ABI. */
  void* p_interp_callback;
  void* p_interp_object;

  struct memory_access* p_memory_access;
  struct bbc_options* p_options;
  uint8_t* p_inturbo_base;
  uint64_t* p_jump_table;
};

static void
inturbo_fill_tables(struct inturbo_struct* p_inturbo) {
  size_t i;
  uint16_t special_addr_above;
  uint16_t temp_u16;

  struct util_buffer* p_buf = util_buffer_create();
  uint8_t* p_inturbo_base = p_inturbo->p_inturbo_base;
  uint8_t* p_inturbo_opcodes_ptr = p_inturbo_base;
  uint64_t* p_jump_table = p_inturbo->p_jump_table;

  struct bbc_options* p_options = p_inturbo->p_options;
  void* p_debug_callback_object = p_options->p_debug_callback_object;
  int debug = p_options->debug_active_at_addr(p_debug_callback_object, 0xFFFF);
  struct memory_access* p_memory_access = p_inturbo->p_memory_access;
  void* p_memory_object = p_memory_access->p_callback_obj;

  special_addr_above = p_memory_access->memory_read_needs_callback_above(
      p_memory_object);
  temp_u16 = p_memory_access->memory_write_needs_callback_above(
      p_memory_object);
  if (temp_u16 < special_addr_above) {
    special_addr_above = temp_u16;
  }

  /* Opcode pointers for the "next opcode" jump table. */
  for (i = 0; i < 256; ++i) {
    p_jump_table[i] =
        (uint64_t) (p_inturbo_base + (i * k_inturbo_bytes_per_opcode));
  }

  for (i = 0; i < 256; ++i) {
    uint8_t opmode = g_opmodes[i];
    uint8_t optype = g_optypes[i];
    uint8_t opreg = g_optype_sets_register[optype];

    util_buffer_setup(p_buf, p_inturbo_opcodes_ptr, k_inturbo_bytes_per_opcode);

    if (debug) {
      asm_x64_emit_inturbo_enter_debug(p_buf);
    }

    switch (opmode) {
    case k_nil:
    case k_acc:
    case k_imm:
    case k_rel:
    case 0:
      break;
    case k_zpg:
      asm_x64_emit_inturbo_mode_zpg(p_buf);
      break;
    case k_abs:
      /* JSR is handled differently. */
      if (optype == k_jsr) {
        break;
      }
      asm_x64_emit_inturbo_mode_abs(p_buf, special_addr_above);
      break;
    case k_abx:
      /* NOTE: could run abx, aby, idy modes more efficiently by doing the
       * address + register addition in a per-optype manner.
       */
      asm_x64_emit_inturbo_mode_abx(p_buf, special_addr_above);
      break;
    case k_aby:
      asm_x64_emit_inturbo_mode_aby(p_buf, special_addr_above);
      break;
    case k_zpx:
      asm_x64_emit_inturbo_mode_zpx(p_buf);
      break;
    case k_zpy:
      asm_x64_emit_inturbo_mode_zpy(p_buf);
      break;
    case k_idx:
      asm_x64_emit_inturbo_mode_idx(p_buf, special_addr_above);
      break;
    case k_idy:
      asm_x64_emit_inturbo_mode_idy(p_buf, special_addr_above);
      break;
    default:
      //assert(0);
      break;
    }

    switch (optype) {
    case k_kil:
      switch (i) {
      case 0x02: /* EXIT */
        asm_x64_emit_instruction_EXIT(p_buf);
        break;
      case 0xF2: /* CRASH */
        asm_x64_emit_instruction_CRASH(p_buf);
        break;
      default:
        asm_x64_emit_instruction_TRAP(p_buf);
      }
      asm_x64_emit_instruction_TRAP(p_buf);
    case k_adc:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_ADC_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_ADC_scratch_interp(p_buf);
      }
      break;
    case k_and:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_AND_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_AND_scratch_interp(p_buf);
      }
      break;
    case k_asl:
      if (opmode == k_acc) {
        asm_x64_emit_instruction_ASL_acc_interp(p_buf);
      } else {
        asm_x64_emit_instruction_ASL_scratch_interp(p_buf);
      }
      break;
    case k_bcc:
      asm_x64_emit_instruction_BCC_interp(p_buf);
      break;
    case k_bcs:
      asm_x64_emit_instruction_BCS_interp(p_buf);
      break;
    case k_beq:
      asm_x64_emit_instruction_BEQ_interp(p_buf);
      break;
    case k_bit:
      asm_x64_emit_instruction_BIT_interp(p_buf);
      break;
    case k_bmi:
      asm_x64_emit_instruction_BMI_interp(p_buf);
      break;
    case k_bne:
      asm_x64_emit_instruction_BNE_interp(p_buf);
      break;
    case k_bpl:
      asm_x64_emit_instruction_BPL_interp(p_buf);
      break;
    case k_brk:
      asm_x64_emit_instruction_BRK_interp(p_buf);
      opmode = 0;
      break;
    case k_bvc:
      asm_x64_emit_instruction_BVC_interp(p_buf);
      break;
    case k_bvs:
      asm_x64_emit_instruction_BVS_interp(p_buf);
      break;
    case k_clc:
      asm_x64_emit_instruction_CLC(p_buf);
      break;
    case k_cld:
      asm_x64_emit_instruction_CLD(p_buf);
      break;
    case k_cli:
      asm_x64_emit_instruction_CLI(p_buf);
      break;
    case k_cmp:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_CMP_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_CMP_scratch_interp(p_buf);
      }
      break;
    case k_cpx:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_CPX_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_CPX_scratch_interp(p_buf);
      }
      break;
    case k_cpy:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_CPY_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_CPY_scratch_interp(p_buf);
      }
      break;
    case k_dec:
      asm_x64_emit_instruction_DEC_scratch_interp(p_buf);
      break;
    case k_dex:
      asm_x64_emit_instruction_DEX(p_buf);
      break;
    case k_dey:
      asm_x64_emit_instruction_DEY(p_buf);
      break;
    case k_inc:
      asm_x64_emit_instruction_INC_scratch_interp(p_buf);
      break;
    case k_inx:
      asm_x64_emit_instruction_INX(p_buf);
      break;
    case k_iny:
      asm_x64_emit_instruction_INY(p_buf);
      break;
    case k_jmp:
      asm_x64_emit_instruction_JMP_scratch_interp(p_buf);
      opmode = 0;
      break;
    case k_jsr:
      asm_x64_emit_instruction_JSR_scratch_interp(p_buf);
      opmode = 0;
      break;
    case k_lda:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_LDA_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_LDA_scratch_interp(p_buf);
      }
      break;
    case k_ldx:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_LDX_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_LDX_scratch_interp(p_buf);
      }
      break;
    case k_ldy:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_LDY_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_LDY_scratch_interp(p_buf);
      }
      break;
    case k_nop:
      break;
    case k_pha:
      asm_x64_emit_instruction_PHA(p_buf);
      break;
    case k_php:
      asm_x64_emit_instruction_PHP(p_buf);
      break;
    case k_pla:
      asm_x64_emit_instruction_PLA(p_buf);
      break;
    case k_plp:
      asm_x64_emit_instruction_PLP(p_buf);
      break;
    case k_ror:
      if (opmode == k_acc) {
        asm_x64_emit_instruction_ROR_acc_interp(p_buf);
      } else {
        asm_x64_emit_instruction_ROR_scratch_interp(p_buf);
      }
      break;
    case k_rti:
      asm_x64_emit_instruction_RTI_interp(p_buf);
      opmode = 0;
      break;
    case k_rts:
      asm_x64_emit_instruction_RTS_interp(p_buf);
      opmode = 0;
      break;
    case k_sbc:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_SBC_imm_interp(p_buf);
      } else {
        asm_x64_emit_instruction_SBC_scratch_interp(p_buf);
      }
      break;
    case k_sec:
      asm_x64_emit_instruction_SEC(p_buf);
      break;
    case k_sei:
      asm_x64_emit_instruction_SEI(p_buf);
      break;
    case k_sta:
      asm_x64_emit_instruction_STA_scratch_interp(p_buf);
      break;
    case k_stx:
      asm_x64_emit_instruction_STX_scratch_interp(p_buf);
      break;
    case k_tax:
      asm_x64_emit_instruction_TAX(p_buf);
      break;
    case k_tay:
      asm_x64_emit_instruction_TAY(p_buf);
      break;
    case k_tsx:
      asm_x64_emit_instruction_TSX(p_buf);
      break;
    case k_txs:
      asm_x64_emit_instruction_TXS(p_buf);
      break;
    default:
      asm_x64_emit_instruction_TRAP(p_buf);
      break;
    }

    switch (opreg) {
    case k_a:
      asm_x64_emit_instruction_A_NZ_flags(p_buf);
      break;
    case k_x:
      asm_x64_emit_instruction_X_NZ_flags(p_buf);
      break;
    case k_y:
      asm_x64_emit_instruction_Y_NZ_flags(p_buf);
      break;
    default:
      break;
    }

    switch (opmode) {
    case 0:
    case k_rel:
      break;
    case k_nil:
    case k_acc:
      asm_x64_emit_inturbo_advance_pc_1(p_buf);
      break;
    case k_imm:
    case k_zpg:
    case k_zpx:
    case k_zpy:
    case k_idx:
    case k_idy:
      asm_x64_emit_inturbo_advance_pc_2(p_buf);
      break;
    case k_abs:
    case k_abx:
    case k_aby:
    case k_ind:
      asm_x64_emit_inturbo_advance_pc_3(p_buf);
      break;
    default:
      assert(0);
      break;
    }

    /* Load next opcode from 6502 PC, jump to correct next asm opcode inturbo
     * handler.
     */
    asm_x64_emit_inturbo_next_opcode(p_buf);

    p_inturbo_opcodes_ptr += k_inturbo_bytes_per_opcode;
  }

  util_buffer_destroy(p_buf);
}

struct inturbo_struct*
inturbo_create(struct state_6502* p_state_6502,
               struct memory_access* p_memory_access,
               struct timing_struct* p_timing,
               struct bbc_options* p_options,
               void* p_interp_callback,
               void* p_interp_object) {
  struct inturbo_struct* p_inturbo = malloc(sizeof(struct inturbo_struct));

  (void) p_timing;

  if (p_inturbo == NULL) {
    errx(1, "couldn't allocate inturbo_struct");
  }
  (void) memset(p_inturbo, '\0', sizeof(struct inturbo_struct));

  asm_tables_init();

  p_state_6502->reg_pc = (uint32_t) (size_t) p_memory_access->p_mem_read;

  asm_x64_abi_init(&p_inturbo->abi, p_options, p_state_6502);

  p_inturbo->p_interp_callback = p_interp_callback;
  p_inturbo->p_interp_object = p_interp_object;

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
  uint8_t* p_mem_read = p_inturbo->p_memory_access->p_mem_read;
  uint8_t opcode = p_mem_read[addr_6502];
  uint32_t p_start_address = p_jump_table[opcode];

  asm_x64_asm_enter(p_inturbo, p_start_address);
}
