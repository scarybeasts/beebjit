#include "inturbo.h"

#include "asm_x64_abi.h"
#include "asm_x64_common.h"
#include "asm_x64_inturbo.h"
#include "bbc_options.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "interp.h"
#include "memory_access.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_inturbo_bytes_per_opcode = 256;
static void* k_inturbo_opcodes_addr = (void*) 0x40000000;

struct inturbo_struct {
  struct cpu_driver driver;

  struct interp_struct* p_interp;
  int debug_subsystem_active;
  uint8_t* p_inturbo_base;
};

static void
inturbo_fill_tables(struct inturbo_struct* p_inturbo) {
  size_t i;
  uint16_t special_addr_above;
  uint16_t temp_u16;

  struct util_buffer* p_buf = util_buffer_create();
  uint8_t* p_inturbo_base = p_inturbo->p_inturbo_base;
  uint8_t* p_inturbo_opcodes_ptr = p_inturbo_base;

  struct bbc_options* p_options = p_inturbo->driver.p_options;
  int accurate = p_options->accurate;
  int debug = p_inturbo->debug_subsystem_active;
  struct memory_access* p_memory_access = p_inturbo->driver.p_memory_access;
  void* p_memory_object = p_memory_access->p_callback_obj;

  special_addr_above = p_memory_access->memory_read_needs_callback_above(
      p_memory_object);
  temp_u16 = p_memory_access->memory_write_needs_callback_above(
      p_memory_object);
  if (temp_u16 < special_addr_above) {
    special_addr_above = temp_u16;
  }

  for (i = 0;
       i < 256;
       (p_inturbo_opcodes_ptr += k_inturbo_bytes_per_opcode), ++i) {
    uint8_t pc_advance;

    uint8_t opmode = g_opmodes[i];
    uint8_t optype = g_optypes[i];
    uint8_t opmem = g_opmem[optype];
    uint8_t opreg = 0;
    uint8_t opcycles = g_opcycles[i];
    int check_page_crossing_read = 0;

    util_buffer_setup(p_buf, p_inturbo_opcodes_ptr, k_inturbo_bytes_per_opcode);

    if (debug) {
      asm_x64_emit_inturbo_enter_debug(p_buf);
    }

    /* Preflight checks. Some opcodes or situations are tricky enough we want
     * to go straight to the interpreter.
     */
    switch (optype) {
    case k_kil:
      if (i != 0x02) {
        /* Not EXIT. Use interpreter. */
        asm_x64_emit_inturbo_call_interp(p_buf);
        continue;
      }
      break;
    case k_adc:
    case k_sbc:
      /* TODO: very lazy / slow to bounce to interpreter for BCD. */
      asm_x64_emit_inturbo_check_decimal(p_buf);
      break;
    case k_cli:
    case k_plp:
    case k_rti:
      /* If the opcode could unmask an interrupt, bounce to interpreter. */
      asm_x64_emit_inturbo_check_interrupt(p_buf);
      break;
    default:
      break;
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
      asm_x64_emit_inturbo_mode_abs(p_buf);
      asm_x64_emit_inturbo_check_special_address(p_buf, special_addr_above);
      break;
    case k_abx:
      asm_x64_emit_inturbo_mode_abx(p_buf);
      asm_x64_emit_inturbo_check_special_address(p_buf, special_addr_above);
      if ((opmem == k_read) && accurate) {
        check_page_crossing_read = 1;
        /* Accurate checks for the +1 cycle if a page boundary is crossed. */
        asm_x64_emit_inturbo_mode_abx_check_page_crossing(p_buf);
      }
      break;
    case k_aby:
      asm_x64_emit_inturbo_mode_aby(p_buf);
      asm_x64_emit_inturbo_check_special_address(p_buf, special_addr_above);
      if ((opmem == k_read) && accurate) {
        check_page_crossing_read = 1;
        /* Accurate checks for the +1 cycle if a page boundary is crossed. */
        asm_x64_emit_inturbo_mode_aby_check_page_crossing(p_buf);
      }
      break;
    case k_zpx:
      asm_x64_emit_inturbo_mode_zpx(p_buf);
      break;
    case k_zpy:
      asm_x64_emit_inturbo_mode_zpy(p_buf);
      break;
    case k_idx:
      asm_x64_emit_inturbo_mode_idx(p_buf);
      asm_x64_emit_inturbo_check_special_address(p_buf, special_addr_above);
      break;
    case k_idy:
      asm_x64_emit_inturbo_mode_idy(p_buf);
      asm_x64_emit_inturbo_check_special_address(p_buf, special_addr_above);
      if ((opmem == k_read) && accurate) {
        check_page_crossing_read = 1;
        /* Accurate checks for the +1 cycle if a page boundary is crossed. */
        asm_x64_emit_inturbo_mode_idy_check_page_crossing(p_buf);
      }
      break;
    case k_ind:
      asm_x64_emit_inturbo_mode_ind(p_buf);
      break;
    default:
      assert(0);
      break;
    }

    /* Check for countdown expiry. */
    if (check_page_crossing_read) {
      asm_x64_emit_inturbo_check_countdown_with_page_crossing(p_buf, opcycles);
    } else {
      asm_x64_emit_inturbo_check_countdown(p_buf, opcycles);
    }

    switch (optype) {
    case k_kil:
      switch (i) {
      case 0x02: /* EXIT */
        asm_x64_emit_instruction_EXIT(p_buf);
        break;
      default:
        assert(0);
        break;
      }
      break;
    case k_adc:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_ADC_imm_interp(p_buf);
      } else if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_ADC_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_ADC_scratch_interp(p_buf);
      }
      break;
    case k_alr:
      asm_x64_emit_instruction_ALR_imm_interp(p_buf);
      break;
    case k_and:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_AND_imm_interp(p_buf);
      } else if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_AND_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_AND_scratch_interp(p_buf);
      }
      break;
    case k_asl:
      if (opmode == k_acc) {
        asm_x64_emit_instruction_ASL_acc_interp(p_buf);
      } else if (opmode == k_abx) {
        asm_x64_emit_instruction_ASL_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_ASL_scratch_interp(p_buf);
      }
      break;
    case k_bcc:
      if (accurate) {
        asm_x64_emit_instruction_BCC_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BCC_interp(p_buf);
      }
      break;
    case k_bcs:
      if (accurate) {
        asm_x64_emit_instruction_BCS_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BCS_interp(p_buf);
      }
      break;
    case k_beq:
      if (accurate) {
        asm_x64_emit_instruction_BEQ_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BEQ_interp(p_buf);
      }
      break;
    case k_bit:
      asm_x64_emit_instruction_BIT_interp(p_buf);
      break;
    case k_bmi:
      if (accurate) {
        asm_x64_emit_instruction_BMI_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BMI_interp(p_buf);
      }
      break;
    case k_bne:
      if (accurate) {
        asm_x64_emit_instruction_BNE_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BNE_interp(p_buf);
      }
      break;
    case k_bpl:
      if (accurate) {
        asm_x64_emit_instruction_BPL_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BPL_interp(p_buf);
      }
      break;
    case k_brk:
      asm_x64_emit_instruction_BRK_interp(p_buf);
      opmode = 0;
      break;
    case k_bvc:
      if (accurate) {
        asm_x64_emit_instruction_BVC_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BVC_interp(p_buf);
      }
      break;
    case k_bvs:
      if (accurate) {
        asm_x64_emit_instruction_BVS_interp_accurate(p_buf);
      } else {
        asm_x64_emit_instruction_BVS_interp(p_buf);
      }
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
    case k_clv:
      asm_x64_emit_instruction_CLV(p_buf);
      break;
    case k_cmp:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_CMP_imm_interp(p_buf);
      } else if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_CMP_scratch_interp_based(p_buf);
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
      if (opmode == k_abx) {
        asm_x64_emit_instruction_DEC_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_DEC_scratch_interp(p_buf);
      }
      break;
    case k_dex:
      asm_x64_emit_instruction_DEX(p_buf);
      break;
    case k_dey:
      asm_x64_emit_instruction_DEY(p_buf);
      break;
    case k_eor:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_EOR_imm_interp(p_buf);
      } else if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_EOR_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_EOR_scratch_interp(p_buf);
      }
      break;
    case k_inc:
      if (opmode == k_abx) {
        asm_x64_emit_instruction_INC_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_INC_scratch_interp(p_buf);
      }
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
      } else if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_LDA_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_LDA_scratch_interp(p_buf);
      }
      opreg = k_a;
      break;
    case k_ldx:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_LDX_imm_interp(p_buf);
      } else if (opmode == k_aby) {
        asm_x64_emit_instruction_LDX_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_LDX_scratch_interp(p_buf);
      }
      opreg = k_x;
      break;
    case k_ldy:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_LDY_imm_interp(p_buf);
      } else if (opmode == k_abx) {
        asm_x64_emit_instruction_LDY_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_LDY_scratch_interp(p_buf);
      }
      opreg = k_y;
      break;
    case k_lsr:
      if (opmode == k_acc) {
        asm_x64_emit_instruction_LSR_acc_interp(p_buf);
      } else if (opmode == k_abx) {
        asm_x64_emit_instruction_LSR_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_LSR_scratch_interp(p_buf);
      }
      break;
    case k_nop:
      break;
    case k_ora:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_ORA_imm_interp(p_buf);
      } else if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_ORA_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_ORA_scratch_interp(p_buf);
      }
      break;
    case k_pha:
      asm_x64_emit_instruction_PHA(p_buf);
      break;
    case k_php:
      asm_x64_emit_instruction_PHP(p_buf);
      break;
    case k_pla:
      asm_x64_emit_instruction_PLA(p_buf);
      opreg = k_a;
      break;
    case k_plp:
      asm_x64_emit_instruction_PLP(p_buf);
      break;
    case k_rol:
      if (opmode == k_acc) {
        asm_x64_emit_instruction_ROL_acc_interp(p_buf);
      } else if (opmode == k_abx) {
        asm_x64_emit_instruction_ROL_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_ROL_scratch_interp(p_buf);
      }
      break;
    case k_ror:
      if (opmode == k_acc) {
        asm_x64_emit_instruction_ROR_acc_interp(p_buf);
      } else if (opmode == k_abx) {
        asm_x64_emit_instruction_ROR_scratch_interp_based(p_buf);
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
    case k_sax:
      asm_x64_emit_instruction_SAX_scratch_interp(p_buf);
      break;
    case k_sbc:
      if (opmode == k_imm) {
        asm_x64_emit_instruction_SBC_imm_interp(p_buf);
      } else if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_SBC_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_SBC_scratch_interp(p_buf);
      }
      break;
    case k_sec:
      asm_x64_emit_instruction_SEC(p_buf);
      break;
    case k_sed:
      asm_x64_emit_instruction_SED(p_buf);
      break;
    case k_sei:
      asm_x64_emit_instruction_SEI(p_buf);
      break;
    case k_slo:
      asm_x64_emit_instruction_SLO_scratch_interp(p_buf);
      break;
    case k_sta:
      if (opmode == k_abx || opmode == k_aby || opmode == k_idy) {
        asm_x64_emit_instruction_STA_scratch_interp_based(p_buf);
      } else {
        asm_x64_emit_instruction_STA_scratch_interp(p_buf);
      }
      break;
    case k_stx:
      asm_x64_emit_instruction_STX_scratch_interp(p_buf);
      break;
    case k_sty:
      asm_x64_emit_instruction_STY_scratch_interp(p_buf);
      break;
    case k_tax:
      asm_x64_emit_instruction_TAX(p_buf);
      opreg = k_x;
      break;
    case k_tay:
      asm_x64_emit_instruction_TAY(p_buf);
      opreg = k_y;
      break;
    case k_tsx:
      asm_x64_emit_instruction_TSX(p_buf);
      opreg = k_x;
      break;
    case k_txa:
      asm_x64_emit_instruction_TXA(p_buf);
      opreg = k_a;
      break;
    case k_txs:
      asm_x64_emit_instruction_TXS(p_buf);
      break;
    case k_tya:
      asm_x64_emit_instruction_TYA(p_buf);
      opreg = k_a;
      break;
    default:
      /* Let the interpreter crash out on unknown opcodes. This is also a way
       * of handling the really weird opcodes by letting the interpreter deal
       * with them.
       */
      asm_x64_emit_inturbo_call_interp(p_buf);
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
      pc_advance = 0;
      break;
    case k_nil:
    case k_acc:
      pc_advance = 1;
      break;
    case k_imm:
    case k_zpg:
    case k_zpx:
    case k_zpy:
    case k_idx:
    case k_idy:
      pc_advance = 2;
      break;
    case k_abs:
    case k_abx:
    case k_aby:
    case k_ind:
      pc_advance = 3;
      break;
    default:
      pc_advance = 0;
      assert(0);
      break;
    }

    /* Advance PC, load next opcode, jump to correct opcode handler. */
    asm_x64_emit_inturbo_advance_pc_and_next(p_buf, pc_advance);
  }

  util_buffer_destroy(p_buf);
}

static int
inturbo_interp_instruction_callback(uint8_t opcode,
                                    int is_irq,
                                    int irq_pending) {
  (void) opcode;

  if (is_irq || irq_pending) {
    /* Keep interpreting to handle the IRQ. */
    return 0;
  }

  /* Stop interpreting, i.e. bounce back to inturbo. */
  return 1;
}

static int64_t
inturbo_enter_interp(struct inturbo_struct* p_inturbo, int64_t countdown) {
  struct timing_struct* p_timing = p_inturbo->driver.p_timing;
  struct interp_struct* p_interp = p_inturbo->p_interp;

  /* TODO: get rid of this. */
  if (countdown <= 0) {
    countdown = timing_advance_time(p_timing, countdown);
  }

  uint32_t ret = interp_enter_with_details(p_interp,
                                           countdown,
                                           inturbo_interp_instruction_callback);

  (void) ret;
  assert(ret == (uint32_t) -1);

  countdown = timing_get_countdown(p_timing);

  return countdown;
}

static void
inturbo_destroy(struct cpu_driver* p_cpu_driver) {
  struct inturbo_struct* p_inturbo = (struct inturbo_struct*) p_cpu_driver;

  struct cpu_driver* p_interp_cpu_driver =
      (struct cpu_driver*) p_inturbo->p_interp;

  p_interp_cpu_driver->destroy(p_interp_cpu_driver);

  util_free_guarded_mapping(p_inturbo->p_inturbo_base,
                            (256 * k_inturbo_bytes_per_opcode));
  free(p_inturbo);
}

static uint32_t
inturbo_enter(struct cpu_driver* p_cpu_driver) {
  int64_t countdown;
  uint32_t run_result;

  struct inturbo_struct* p_inturbo = (struct inturbo_struct*) p_cpu_driver;
  uint16_t addr_6502 = state_6502_get_pc(p_inturbo->driver.abi.p_state_6502);
  uint8_t* p_mem_read = p_inturbo->driver.p_memory_access->p_mem_read;
  struct timing_struct* p_timing = p_inturbo->driver.p_timing;
  uint8_t opcode = p_mem_read[addr_6502];
  uint32_t p_start_address =
      (uint32_t) (size_t) (k_inturbo_opcodes_addr +
                           (opcode * k_inturbo_bytes_per_opcode));

  countdown = timing_get_countdown(p_timing);

  run_result = asm_x64_asm_enter(p_inturbo, p_start_address, countdown);

  return run_result;
}

static char*
inturbo_get_address_info(struct cpu_driver* p_cpu_driver, uint16_t addr) {
  (void) p_cpu_driver;
  (void) addr;

  return "TRBO";
}

static void
inturbo_init(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp;
  int debug_subsystem_active;

  struct inturbo_struct* p_inturbo = (struct inturbo_struct*) p_cpu_driver;

  struct state_6502* p_state_6502 = p_inturbo->driver.abi.p_state_6502;
  struct memory_access* p_memory_access = p_inturbo->driver.p_memory_access;
  struct timing_struct* p_timing = p_inturbo->driver.p_timing;
  struct bbc_options* p_options = p_inturbo->driver.p_options;
  struct debug_struct* p_debug_object = p_options->p_debug_object;

  p_cpu_driver->destroy = inturbo_destroy;
  p_cpu_driver->enter = inturbo_enter;
  p_cpu_driver->get_address_info = inturbo_get_address_info;

  debug_subsystem_active = p_options->debug_active_at_addr(
      p_debug_object, 0xFFFF);
  p_inturbo->debug_subsystem_active = debug_subsystem_active;

  /* The inturbo mode uses an interpreter to handle complicated situations,
   * such as IRQs, hardware accesses, etc.
   */
  p_interp = (struct interp_struct*) cpu_driver_alloc(k_cpu_mode_interp,
                                                      p_state_6502,
                                                      p_memory_access,
                                                      p_timing,
                                                      p_options);
  if (p_interp == NULL) {
    errx(1, "couldn't allocate interp_struct");
  }
  p_inturbo->p_interp = p_interp;

  p_inturbo->driver.abi.p_interp_callback = inturbo_enter_interp;
  p_inturbo->driver.abi.p_interp_object = p_inturbo;

  p_inturbo->p_inturbo_base = util_get_guarded_mapping(
      k_inturbo_opcodes_addr,
      (256 * k_inturbo_bytes_per_opcode));
  util_make_mapping_read_write_exec(
      p_inturbo->p_inturbo_base,
      (256 * k_inturbo_bytes_per_opcode));

  inturbo_fill_tables(p_inturbo);
}

struct inturbo_struct*
inturbo_create() {
  struct inturbo_struct* p_inturbo = malloc(sizeof(struct inturbo_struct));
  if (p_inturbo == NULL) {
    errx(1, "couldn't allocate inturbo_struct");
  }
  (void) memset(p_inturbo, '\0', sizeof(struct inturbo_struct));

  p_inturbo->driver.init = inturbo_init;

  return p_inturbo;
}
