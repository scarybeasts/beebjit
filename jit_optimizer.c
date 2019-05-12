#include "jit_optimizer.h"

#include "defs_6502.h"
#include "jit_compiler_defs.h"

#include <assert.h>
#include <stdio.h>

static const int32_t k_value_unknown = -1;

static struct jit_uop*
jit_optimizer_find_uop(struct jit_compiler* p_compiler,
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
jit_optimizer_eliminate(struct jit_compiler* p_compiler,
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

void
jit_optimizer_optimize(struct jit_compiler* p_compiler,
                       struct jit_opcode_details* p_opcodes,
                       uint32_t num_opcodes) {
  uint32_t i_opcodes;

  int32_t reg_a;
  int32_t reg_x;
  int32_t reg_y;
  int32_t flag_carry;
  int32_t flag_decimal;

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

  /* Pass 1: tag opcodes with any known register and flag values. Also replace
   * single uops with replacements if known registers offer better alternatives.
   * Classic example is CLC; ADC. At the ADC instruction, it is known that
   * CF==0 so the ADC can become just an ADD.
   */
  reg_a = k_value_unknown;
  reg_x = k_value_unknown;
  reg_y = k_value_unknown;
  flag_carry = k_value_unknown;
  flag_decimal = k_value_unknown;
  for (i_opcodes = 0; i_opcodes < num_opcodes; ++i_opcodes) {
    uint32_t num_uops;
    uint32_t i_uops;
    uint8_t opreg;

    struct jit_opcode_details* p_opcode = &p_opcodes[i_opcodes];
    uint8_t opcode_6502 = p_opcode->opcode_6502;
    uint16_t operand_6502 = p_opcode->operand_6502;
    uint8_t optype = g_optypes[opcode_6502];
    uint8_t opmode = g_opmodes[opcode_6502];
    int changes_carry = g_optype_changes_carry[optype];

    p_opcode->num_fixup_uops = 0;
    p_opcode->len_bytes_6502_merged = p_opcode->len_bytes_6502_orig;
    p_opcode->max_cycles_merged = p_opcode->max_cycles_orig;
    p_opcode->eliminated = 0;

    p_opcode->reg_a = reg_a;
    p_opcode->reg_x = reg_x;
    p_opcode->reg_y = reg_y;
    p_opcode->flag_carry = flag_carry;
    p_opcode->flag_decimal = flag_carry;

    /* TODO: seems hacky, should g_optype_sets_register just be per-opcode? */
    opreg = g_optype_sets_register[optype];
    if (opmode == k_acc) {
      opreg = k_a;
    }

    num_uops = p_opcode->num_uops;
    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct jit_uop* p_uop = &p_opcode->uops[i_uops];
      int32_t uopcode = p_uop->uopcode;

      p_uop->eliminated = 0;

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
  }

  /* Pass 2: eliminate and merge opcodes as we can. */
  for (i_opcodes = 0; i_opcodes < num_opcodes; ++i_opcodes) {
    uint32_t i_uops;
    uint32_t num_uops;

    struct jit_opcode_details* p_opcode = &p_opcodes[i_opcodes];
    uint8_t opcode_6502 = p_opcode->opcode_6502;
    uint8_t optype = g_optypes[opcode_6502];
    uint8_t opmode = g_opmodes[opcode_6502];
    uint8_t opbranch = g_opbranch[optype];
    int changes_nz = g_optype_changes_nz_flags[optype];

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
        struct jit_uop* p_modify_uop = jit_optimizer_find_uop(p_compiler,
                                                              p_prev_opcode,
                                                              old_uopcode);
        if (p_modify_uop != NULL) {
          p_modify_uop->uopcode = new_uopcode;
          p_modify_uop->value1 = 1;
        } else {
          p_modify_uop = jit_optimizer_find_uop(p_compiler,
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
      jit_optimizer_eliminate(p_compiler,
                              &p_flags_opcode,
                              &p_flags_uop,
                              p_opcode);
    }
    /* Eliminate constant loads that are folded into other opcodes. */
    if ((p_lda_opcode != NULL) && ((optype == k_lda) ||
                                   (optype == k_pla) ||
                                   (optype == k_txa) ||
                                   (optype == k_tya))) {
      jit_optimizer_eliminate(p_compiler, &p_lda_opcode, &p_lda_uop, p_opcode);
    }
    if ((p_ldx_opcode != NULL) && ((optype == k_ldx) ||
                                   (optype == k_tax) ||
                                   (optype == k_tsx))) {
      jit_optimizer_eliminate(p_compiler, &p_ldx_opcode, &p_ldx_uop, p_opcode);
    }
    if ((p_ldy_opcode != NULL) && ((optype == k_ldy) || (optype == k_tay))) {
      jit_optimizer_eliminate(p_compiler, &p_ldy_opcode, &p_ldy_uop, p_opcode);
    }

    num_uops = p_opcode->num_uops;
    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct jit_uop* p_uop = &p_opcode->uops[i_uops];
      int32_t uopcode = p_uop->uopcode;

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
    }

    p_prev_opcode = p_opcode;
  }
}
