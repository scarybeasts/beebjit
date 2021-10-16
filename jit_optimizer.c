#include "jit_optimizer.h"

#include "defs_6502.h"
#include "jit_opcode.h"
#include "asm/asm_opcodes.h"
#include "asm/asm_util.h"

#include <assert.h>
#include <string.h>

static const int32_t k_value_unknown = -1;

static void
jit_optimizer_merge_opcodes(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_prev_opcode = NULL;
  int32_t prev_optype = -1;
  int32_t uopcode = -1;
  struct jit_opcode_details* p_opcode;
  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint8_t optype;
    if (p_opcode->opmode_6502 != k_acc) {
      p_prev_opcode = NULL;
      continue;
    }
    optype = p_opcode->optype_6502;
    switch (optype) {
    case k_asl: uopcode = k_opcode_ASL_acc; break;
    case k_lsr: uopcode = k_opcode_LSR_acc; break;
    case k_rol: uopcode = k_opcode_ROL_acc; break;
    case k_ror: uopcode = k_opcode_ROR_acc; break;
    default: assert(0); break;
    }
    if ((p_prev_opcode != NULL) && (optype == prev_optype)) {
      int32_t index;
      struct asm_uop* p_uop = jit_opcode_find_uop(p_prev_opcode,
                                                  &index,
                                                  uopcode);
      assert(p_uop != NULL);
      if (p_uop->value1 < 7) {
        p_uop->value1++;
        jit_opcode_eliminate(p_opcode);
      }
    } else {
      p_prev_opcode = p_opcode;
      prev_optype = optype;
    }
  }
}

static void
jit_optimizer_calculate_known_values(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  int32_t reg_a = k_value_unknown;
  int32_t reg_x = k_value_unknown;
  int32_t reg_y = k_value_unknown;
  int32_t flag_carry = k_value_unknown;
  int32_t flag_decimal = k_value_unknown;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint16_t operand_6502 = p_opcode->operand_6502;
    uint8_t optype = p_opcode->optype_6502;
    uint8_t opreg = p_opcode->opreg_6502;
    uint8_t opmode = p_opcode->opmode_6502;
    int changes_carry = g_optype_changes_carry[optype];

    p_opcode->reg_a = reg_a;
    p_opcode->reg_x = reg_x;
    p_opcode->reg_y = reg_y;
    p_opcode->flag_carry = flag_carry;
    p_opcode->flag_decimal = flag_decimal;

    switch (optype) {
    case k_clc:
    case k_bcs:
      flag_carry = 0;
      break;
    case k_sec:
    case k_bcc:
      flag_carry = 1;
      break;
    case k_dey:
      if (reg_y != k_value_unknown) {
        reg_y = (uint8_t) (reg_y - 1);
      }
      break;
    case k_txa:
      reg_a = reg_x;
      break;
    case k_tya:
      reg_a = reg_y;
      break;
    case k_ldy:
      if ((opmode == k_imm) && (!p_opcode->is_dynamic_operand)) {
        reg_y = operand_6502;
      } else {
        reg_y = k_value_unknown;
      }
      break;
    case k_ldx:
      if ((opmode == k_imm) && !p_opcode->is_dynamic_operand) {
        reg_x = operand_6502;
      } else {
        reg_x = k_value_unknown;
      }
      break;
    case k_tay:
      reg_y = reg_a;
      break;
    case k_lda:
      if ((opmode == k_imm) && !p_opcode->is_dynamic_operand) {
        reg_a = operand_6502;
      } else {
        reg_a = k_value_unknown;
      }
      break;
    case k_tax:
      reg_x = reg_a;
      break;
    case k_iny:
      if (reg_y != k_value_unknown) {
        reg_y = (uint8_t) (reg_y + 1);
      }
      break;
    case k_dex:
      if (reg_x != k_value_unknown) {
        reg_x = (uint8_t) (reg_x - 1);
      }
      break;
    case k_cld:
      flag_decimal = 0;
      break;
    case k_inx:
      if (reg_x != k_value_unknown) {
        reg_x = (uint8_t) (reg_x + 1);
      }
      break;
    case k_sed:
      flag_decimal = 1;
      break;
    default:
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
      break;
    }
  }
}

static void
jit_optimizer_replace_uops(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  int had_check_bcd = 0;
  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    int32_t index;
    struct asm_uop* p_uop;
    int32_t load_uopcode_old = -1;
    int32_t load_uopcode_new = -1;
    uint8_t load_uopcode_value = 0;
    int do_eliminate_check_bcd = 0;
    int do_eliminate_load_carry = 0;

    switch (p_opcode->optype_6502) {
    case k_adc:
      if ((p_opcode->flag_decimal == 0) || had_check_bcd) {
        do_eliminate_check_bcd = 1;
      }
      if (p_opcode->flag_carry == 0) {
        p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_ADC);
        assert(p_uop != NULL);
        asm_make_uop0(p_uop, k_opcode_ADD);
        do_eliminate_load_carry = 1;
      }
      had_check_bcd = 1;
      break;
    case k_dex:
      if (p_opcode->reg_x == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_DEX;
      load_uopcode_new = k_opcode_LDX;
      load_uopcode_value = (p_opcode->reg_x - 1);
      break;
    case k_dey:
      if (p_opcode->reg_y == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_DEY;
      load_uopcode_new = k_opcode_LDY;
      load_uopcode_value = (p_opcode->reg_y - 1);
      break;
    case k_inx:
      if (p_opcode->reg_x == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_INX;
      load_uopcode_new = k_opcode_LDX;
      load_uopcode_value = (p_opcode->reg_x + 1);
      break;
    case k_iny:
      if (p_opcode->reg_y == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_INY;
      load_uopcode_new = k_opcode_LDY;
      load_uopcode_value = (p_opcode->reg_y + 1);
      break;
    case k_sbc:
      if ((p_opcode->flag_decimal == 0) || had_check_bcd) {
        do_eliminate_check_bcd = 1;
      }
      if (p_opcode->flag_carry == 1) {
        p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_SBC);
        assert(p_uop != NULL);
        asm_make_uop0(p_uop, k_opcode_SUB);
        do_eliminate_load_carry = 1;
      }
      had_check_bcd = 1;
      break;
    case k_tax:
      if (p_opcode->reg_a == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_TAX;
      load_uopcode_new = k_opcode_LDX;
      load_uopcode_value = p_opcode->reg_a;
      break;
    case k_tay:
      if (p_opcode->reg_a == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_TAY;
      load_uopcode_new = k_opcode_LDY;
      load_uopcode_value = p_opcode->reg_a;
      break;
    case k_txa:
      if (p_opcode->reg_x == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_TXA;
      load_uopcode_new = k_opcode_LDA;
      load_uopcode_value = p_opcode->reg_x;
      break;
    case k_tya:
      if (p_opcode->reg_y == k_value_unknown) {
        break;
      }
      load_uopcode_old = k_opcode_TYA;
      load_uopcode_new = k_opcode_LDA;
      load_uopcode_value = p_opcode->reg_y;
      break;
    default:
      break;
    }

    if (do_eliminate_check_bcd) {
      p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_check_bcd);
      assert(p_uop != NULL);
      p_uop->is_eliminated = 1;
    }
    if (do_eliminate_load_carry) {
      p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_load_carry);
      assert(p_uop != NULL);
      p_uop->is_eliminated = 1;
    }

    if (load_uopcode_old != -1) {
      p_uop = jit_opcode_find_uop(p_opcode, &index, load_uopcode_old);
      assert(p_uop != NULL);
      asm_make_uop1(p_uop, k_opcode_value_set, load_uopcode_value);
      p_uop = jit_opcode_insert_uop(p_opcode, (index + 1));
      asm_make_uop0(p_uop, load_uopcode_new);
    }
  }
}

static void
jit_optimizer_eliminate_nz_flag_saving(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  struct asm_uop* p_nz_flags_uop = NULL;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint32_t num_uops = p_opcode->num_uops;
    uint32_t i_uops;
    int is_eliminated;

    if (p_opcode->is_eliminated) {
      continue;
    }

    if (p_nz_flags_uop != NULL) {
      p_opcode->nz_flags_location = p_nz_flags_uop->uopcode;
    }

    /* PHP needs the NZ flags. */
    if (p_opcode->optype_6502 == k_php) {
      p_nz_flags_uop = NULL;
    }
    /* Any jump, including conditional, must commit flags. */
    if (p_opcode->opbranch_6502 != k_bra_n) {
      p_nz_flags_uop = NULL;
    }

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct asm_uop* p_uop = &p_opcode->uops[i_uops];
      int32_t nz_flags_uopcode = 0;
      switch (p_uop->uopcode) {
      case k_opcode_flags_nz_a:
      case k_opcode_flags_nz_x:
      case k_opcode_flags_nz_y:
      case k_opcode_flags_nz_value:
        nz_flags_uopcode = p_uop->uopcode;
        break;
      default:
        break;
      }
      if (nz_flags_uopcode == 0) {
        continue;
      }
      is_eliminated = p_uop->is_eliminated;
      (void) is_eliminated;
      /* Eliminate the previous flag set, if appropriate. */
      if (p_nz_flags_uop != NULL) {
        p_nz_flags_uop->is_eliminated = 1;
      }
      /* Don't eliminate flags from the value register for now. */
      if (nz_flags_uopcode == k_opcode_flags_nz_value) {
        p_nz_flags_uop = NULL;
      } else {
        p_nz_flags_uop = p_uop;
      }
    }
  }
}

static void
jit_optimizer_eliminate_c_v_flag_saving(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  struct asm_uop* p_save_carry_uop = NULL;
  struct asm_uop* p_save_overflow_uop = NULL;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint32_t num_uops = p_opcode->num_uops;
    uint32_t i_uops;
    int had_save_carry = 0;
    int had_save_overflow = 0;

    if (p_opcode->is_eliminated) {
      continue;
    }

    if (p_save_carry_uop != NULL) {
      p_opcode->c_flag_location = p_save_carry_uop->uopcode;
    }
    if (p_save_overflow_uop != NULL) {
      p_opcode->v_flag_location = p_save_overflow_uop->uopcode;
    }

    /* Any jump, including conditional, must commit flags. */
    if (p_opcode->opbranch_6502 != k_bra_n) {
      p_save_carry_uop = NULL;
      p_save_overflow_uop = NULL;
    }

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct asm_uop* p_uop = &p_opcode->uops[i_uops];
      switch (p_uop->uopcode) {
      case k_opcode_load_carry:
        /* The carry load will be eliminated if this was made an ADD / SUB, or
         * by the ARM64 backend.
         */
        if (p_uop->is_eliminated && !p_uop->is_merged) {
          break;
        }
        if (!p_uop->is_merged &&
            (p_save_carry_uop != NULL) &&
            (p_save_carry_uop->uopcode == k_opcode_save_carry)) {
          p_save_carry_uop->is_eliminated = 1;
          p_uop->is_eliminated = 1;
        }
        p_save_carry_uop = NULL;
        break;
      case k_opcode_save_carry:
        had_save_carry = 1;
        /* FALL THROUGH */
      case k_opcode_CLC:
      case k_opcode_SEC:
        if (p_save_carry_uop != NULL) {
          p_save_carry_uop->is_eliminated = 1;
        }
        p_save_carry_uop = p_uop;
        break;
      case k_opcode_load_overflow:
        p_save_overflow_uop = NULL;
        break;
      case k_opcode_save_overflow:
        if (p_save_overflow_uop != NULL) {
          p_save_overflow_uop->is_eliminated = 1;
        }
        p_save_overflow_uop = p_uop;
        had_save_overflow = 1;
        break;
      case k_opcode_flags_nz_a:
      case k_opcode_flags_nz_x:
      case k_opcode_flags_nz_y:
      case k_opcode_flags_nz_value:
        /* This might be best abstracted into the asm backends somehow, but for
         * now, let's observe that both the x64 and ARM64 backends cannot
         * preserve the host carry / overflow flags across a "test" instruction.
         */
        if (!p_uop->is_eliminated) {
          p_save_carry_uop = NULL;
          p_save_overflow_uop = NULL;
        }
        /* The NZ flag set might be part of a host instruction, in which case
         * it's also typical to trash host carry / overflow flags, unless the
         * carry / overflow is being set at the same time.
         */
        if (p_uop->is_merged) {
          if (!had_save_carry) {
            p_save_carry_uop = NULL;
          }
          if (!had_save_overflow) {
            p_save_overflow_uop = NULL;
          }
        }
        break;
      case k_opcode_CLD:
      case k_opcode_CLI:
      case k_opcode_SED:
      case k_opcode_SEI:
        /* TODO: the Intel x64 backend trashes the host carry / overflow on
         * these and it probably shouldn't.
         */
        p_save_carry_uop = NULL;
        p_save_overflow_uop = NULL;
        break;
      default:
        break;
      }
    }
  }
}

static void
jit_optimizer_eliminate_axy_loads(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  struct asm_uop* p_load_y_uop = NULL;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint32_t num_uops = p_opcode->num_uops;
    uint32_t i_uops;
    int is_imm = 0;

    if (p_opcode->is_eliminated) {
      continue;
    }

    /* Any jump, including conditional, must commit register values. */
    if (p_opcode->opbranch_6502 != k_bra_n) {
      p_load_y_uop = NULL;
    }

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct asm_uop* p_uop = &p_opcode->uops[i_uops];
      switch (p_uop->uopcode) {
      case k_opcode_value_set:
        is_imm = 1;
        break;
      case k_opcode_LDY:
        if (p_load_y_uop != NULL) {
          p_load_y_uop->is_eliminated = 1;
        }
        if (is_imm) {
          p_load_y_uop = p_uop;
        } else {
          p_load_y_uop = NULL;
        }
        break;
      case k_opcode_check_page_crossing_y:
      case k_opcode_addr_add_y:
      case k_opcode_addr_add_y_8bit:
      case k_opcode_flags_nz_y:
      case k_opcode_CPY:
      case k_opcode_DEY:
      case k_opcode_INY:
      case k_opcode_STY:
      case k_opcode_TYA:
        if (!p_uop->is_eliminated || p_uop->is_merged) {
          p_load_y_uop = NULL;
        }
        break;
      default:
        break;
      }
    }
  }
}

struct jit_opcode_details*
jit_optimizer_optimize_pre_rewrite(struct jit_opcode_details* p_opcodes) {
  /* Pass 1: opcode merging. LSR A and similar opcodes. */
  jit_optimizer_merge_opcodes(p_opcodes);

  /* Pass 2: tag opcodes with any known register and flag values. */
  jit_optimizer_calculate_known_values(p_opcodes);

  /* Pass 3: replacements of uops with better ones if known state offers the
   * opportunity.
   * Classic example is CLC; ADC. At the ADC instruction, it is known that
   * CF==0 so the ADC can become just an ADD.
   */
  jit_optimizer_replace_uops(p_opcodes);

  return NULL;
}

void
jit_optimizer_optimize_post_rewrite(struct jit_opcode_details* p_opcodes) {
  /* Pass 1: NZ flag saving elimination. */
  jit_optimizer_eliminate_nz_flag_saving(p_opcodes);

  /* Pass 2: carry and overflow flag saving elimination. */
  jit_optimizer_eliminate_c_v_flag_saving(p_opcodes);

  /* Pass 3: eliminate redundant register sets. This comes alive after previous
   * passes. It triggers most significantly for code that uses INY to index
   * an unrolled sequence of e.g LDA ($00),Y loads.
   */
  jit_optimizer_eliminate_axy_loads(p_opcodes);
}
