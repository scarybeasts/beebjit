#include "jit_optimizer.h"

#include "defs_6502.h"
#include "jit_compiler_defs.h"

#include <assert.h>
#include <stdio.h>

static const int32_t k_value_unknown = -1;

static struct jit_uop*
jit_optimizer_find_uop(struct jit_opcode_details* p_opcode, int32_t uopcode) {
  uint32_t i_uops;

  for (i_uops = 0; i_uops < p_opcode->num_uops; ++i_uops) {
    struct jit_uop* p_uop = &p_opcode->uops[i_uops];
    if (p_uop->uopcode == uopcode) {
      return p_uop;
    }
  }

  return NULL;
}

static void
jit_optimizer_eliminate(struct jit_opcode_details** pp_elim_opcode,
                        struct jit_uop* p_elim_uop,
                        struct jit_opcode_details* p_curr_opcode) {
  struct jit_opcode_details* p_elim_opcode = *pp_elim_opcode;

  *pp_elim_opcode = NULL;

  p_elim_uop->eliminated = 1;

  while (p_elim_opcode != p_curr_opcode) {
    assert(p_elim_opcode->num_fixup_uops < k_max_uops_per_opcode);
    p_elim_opcode->fixup_uops[p_elim_opcode->num_fixup_uops++] = p_elim_uop;
    p_elim_opcode++;
  }
}

static int
jit_optimizer_uopcode_can_jump(int32_t uopcode) {
  int ret = 0;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    uint8_t opbranch = g_opbranch[optype];
    if (opbranch != k_bra_n) {
      ret = 1;
    }
  } else {
    switch (uopcode) {
    case k_opcode_JMP_SCRATCH:
      ret = 1;
      break;
    default:
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_sets_nz_flags(int32_t uopcode) {
  int ret;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    ret = g_optype_changes_nz_flags[optype];
  } else {
    switch (uopcode) {
    case k_opcode_ADD_ABS:
    case k_opcode_ADD_ABX:
    case k_opcode_ADD_ABY:
    case k_opcode_ADD_IMM:
    case k_opcode_ADD_SCRATCH:
    case k_opcode_ADD_SCRATCH_Y:
    case k_opcode_ASL_ACC_n:
    case k_opcode_FLAGA:
    case k_opcode_FLAGX:
    case k_opcode_FLAGY:
    case k_opcode_LDA_SCRATCH_n:
    case k_opcode_LDA_Z:
    case k_opcode_LDX_Z:
    case k_opcode_LDY_Z:
    case k_opcode_LSR_ACC_n:
    case k_opcode_SUB_IMM:
      ret = 1;
      break;
    default:
      ret = 0;
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_needs_nz_flags(int32_t uopcode) {
  switch (uopcode) {
  case 0x08: /* PHP */
    return 1;
  default:
    return 0;
  }
}

static int
jit_optimizer_uopcode_overwrites_a(int32_t uopcode) {
  int ret = 0;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    switch (optype) {
    case k_lda:
    case k_pla:
    case k_txa:
    case k_tya:
      ret = 1;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_LDA_SCRATCH_n:
    case k_opcode_LDA_Z:
      ret = 1;
      break;
    default:
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_needs_a(int32_t uopcode) {
  int ret = 0;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    uint8_t opmode = g_opmodes[uopcode];
    switch (optype) {
    case k_ora:
    case k_and:
    case k_bit:
    case k_eor:
    case k_pha:
    case k_adc:
    case k_sta:
    case k_tay:
    case k_tax:
    case k_cmp:
    case k_sbc:
    case k_sax:
    case k_alr:
    case k_slo:
      ret = 1;
      break;
    case k_asl:
    case k_rol:
    case k_lsr:
    case k_ror:
      if (opmode == k_acc) {
        ret = 1;
      }
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_ADD_ABS:
    case k_opcode_ADD_ABX:
    case k_opcode_ADD_ABY:
    case k_opcode_ADD_IMM:
    case k_opcode_ADD_SCRATCH:
    case k_opcode_ADD_SCRATCH_Y:
    case k_opcode_ASL_ACC_n:
    case k_opcode_FLAGA:
    case k_opcode_LSR_ACC_n:
    case k_opcode_ROL_ACC_n:
    case k_opcode_ROR_ACC_n:
    case k_opcode_SUB_IMM:
      ret = 1;
      break;
    default:
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_overwrites_x(int32_t uopcode) {
  int ret = 0;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    switch (optype) {
    case k_ldx:
    case k_tax:
    case k_tsx:
      ret = 1;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_LDX_Z:
      ret = 1;
      break;
    default:
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_needs_x(int32_t uopcode) {
  int ret = 0;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    uint8_t opmode = g_opmodes[uopcode];
    switch (optype) {
    case k_stx:
    case k_txa:
    case k_txs:
    case k_cpx:
    case k_dex:
    case k_inx:
    case k_sax:
      ret = 1;
      break;
    default:
      break;
    }
    switch (opmode) {
    case k_zpx:
    case k_abx:
    case k_idx:
      ret = 1;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_ABX_CHECK_PAGE_CROSSING:
    case k_opcode_ADD_ABX:
    case k_opcode_FLAGX:
    case k_opcode_MODE_ABX:
    case k_opcode_MODE_ZPX:
      ret = 1;
      break;
    default:
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_overwrites_y(int32_t uopcode) {
  int ret = 0;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    switch (optype) {
    case k_ldy:
    case k_tay:
      ret = 1;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_LDY_Z:
      ret = 1;
      break;
    default:
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_needs_y(int32_t uopcode) {
  int ret = 0;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    uint8_t opmode = g_opmodes[uopcode];
    switch (optype) {
    case k_sty:
    case k_dey:
    case k_tya:
    case k_cpy:
    case k_iny:
    case k_shy:
      ret = 1;
      break;
    default:
      break;
    }
    switch (opmode) {
    case k_zpy:
    case k_aby:
    case k_idy:
      ret = 1;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_ABY_CHECK_PAGE_CROSSING:
    case k_opcode_ADD_ABY:
    case k_opcode_ADD_SCRATCH_Y:
    case k_opcode_FLAGY:
    case k_opcode_IDY_CHECK_PAGE_CROSSING:
    case k_opcode_MODE_ABY:
    case k_opcode_MODE_ZPY:
    case k_opcode_WRITE_INV_SCRATCH_Y:
      ret = 1;
      break;
    default:
      break;
    }
  }
  return ret;
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

  struct jit_opcode_details* p_prev_opcode;

  struct jit_opcode_details* p_nz_flags_opcode;
  struct jit_uop* p_nz_flags_uop;
  struct jit_opcode_details* p_lda_opcode;
  struct jit_uop* p_lda_uop;
  struct jit_opcode_details* p_ldx_opcode;
  struct jit_uop* p_ldx_uop;
  struct jit_opcode_details* p_ldy_opcode;
  struct jit_uop* p_ldy_uop;

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
          uopcode = k_opcode_ADD_SCRATCH;
        }
        break;
      case 0x65: /* ADC zpg */
      case 0x6D: /* ADC abs */
        if (flag_carry == 0) {
          uopcode = k_opcode_ADD_ABS;
        }
        break;
      case 0x69: /* ADC imm */
        if (flag_carry == 0) {
          uopcode = k_opcode_ADD_IMM;
        } else if (flag_carry == 1) {
          /* NOTE: if this is common, we can optimize this case. */
          printf("LOG:JIT:optimizer sees ADC #$imm with C==1\n");
        }
        break;
      case 0x71: /* ADC idy */
        if (flag_carry == 0) {
          uopcode = k_opcode_ADD_SCRATCH_Y;
        }
        break;
      case 0x79: /* ADC aby */
        if (flag_carry == 0) {
          uopcode = k_opcode_ADD_ABY;
        }
        break;
      case 0x7D: /* ADC abx */
        if (flag_carry == 0) {
          uopcode = k_opcode_ADD_ABX;
        }
        break;
      case 0x84: /* STY zpg */
      case 0x8C: /* STY abs */
        if (reg_y != k_value_unknown) {
          uopcode = k_opcode_STOA_IMM;
          p_uop->value2 = reg_y;
        }
        break;
      case 0x85: /* STA zpg */
      case 0x8D: /* STA abs */
        if (reg_a != k_value_unknown) {
          uopcode = k_opcode_STOA_IMM;
          p_uop->value2 = reg_a;
        }
        break;
      case 0x86: /* STX zpg */
      case 0x8E: /* STX abs */
        if (reg_x != k_value_unknown) {
          uopcode = k_opcode_STOA_IMM;
          p_uop->value2 = reg_x;
        }
        break;
      case 0xE9: /* SBC imm */
        if (flag_carry == 1) {
          uopcode = k_opcode_SUB_IMM;
        }
        break;
      default:
        break;
      }

      if (reg_y != k_value_unknown) {
        int replaced = 0;
        switch (uopcode) {
        case 0xB1: /* LDA idy */
          uopcode = k_opcode_LDA_SCRATCH_n;
          p_uop->value1 = reg_y;
          replaced = 1;
          break;
        default:
          break;
        }

        if (replaced) {
          struct jit_uop* p_crossing_uop =
              jit_optimizer_find_uop(p_opcode,
                                     k_opcode_IDY_CHECK_PAGE_CROSSING);
          if (p_crossing_uop != NULL) {
            p_crossing_uop->uopcode = k_opcode_CHECK_PAGE_CROSSING_SCRATCH_n;
            p_crossing_uop->value1 = reg_y;
          }
        }
      }

      p_uop->uopcode = uopcode;
    }

    /* Update known state of registers, flags, etc. for next opcode. */
    switch (opcode_6502) {
    case 0x18: /* CLC */
      flag_carry = 0;
      break;
    case 0x38: /* SEC */
      flag_carry = 1;
      break;
    case 0x88: /* DEY */
      if (reg_y != k_value_unknown) {
        reg_y = (uint8_t) (reg_y - 1);
      }
      break;
    case 0x8A: /* TXA */
      if (reg_x != k_value_unknown) {
        reg_a = reg_x;
      }
      break;
    case 0x98: /* TYA */
      if (reg_y != k_value_unknown) {
        reg_a = reg_y;
      }
      break;
    case 0xA0: /* LDY imm */
      reg_y = operand_6502;
      break;
    case 0xA2: /* LDX imm */
      reg_x = operand_6502;
      break;
    case 0xA8: /* TAY */
      if (reg_a != k_value_unknown) {
        reg_y = reg_a;
      }
      break;
    case 0xA9: /* LDA imm */
      reg_a = operand_6502;
      break;
    case 0xAA: /* TAX */
      if (reg_a != k_value_unknown) {
        reg_x = reg_a;
      }
      break;
    case 0xC8: /* INY */
      if (reg_y != k_value_unknown) {
        reg_y = (uint8_t) (reg_y + 1);
      }
      break;
    case 0xCA: /* DEX */
      if (reg_x != k_value_unknown) {
        reg_x = (uint8_t) (reg_x - 1);
      }
      break;
    case 0xD8: /* CLD */
      flag_decimal = 0;
      break;
    case 0xE8: /* INX */
      if (reg_x != k_value_unknown) {
        reg_x = (uint8_t) (reg_x + 1);
      }
      break;
    case 0xF8: /* SED */
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

  /* Pass 2: merge 6502 macro opcodes as we can. */
  p_prev_opcode = NULL;
  for (i_opcodes = 0; i_opcodes < num_opcodes; ++i_opcodes) {
    struct jit_opcode_details* p_opcode = &p_opcodes[i_opcodes];

    uint8_t opcode_6502 = p_opcode->opcode_6502;

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
        struct jit_uop* p_modify_uop = jit_optimizer_find_uop(p_prev_opcode,
                                                              old_uopcode);
        if (p_modify_uop != NULL) {
          p_modify_uop->uopcode = new_uopcode;
          p_modify_uop->value1 = 1;
        } else {
          p_modify_uop = jit_optimizer_find_uop(p_prev_opcode, new_uopcode);
        }
        assert(p_modify_uop != NULL);
        p_opcode->eliminated = 1;
        p_modify_uop->value1++;
        p_prev_opcode->len_bytes_6502_merged += p_opcode->len_bytes_6502_orig;
        p_prev_opcode->max_cycles_merged += p_opcode->max_cycles_orig;

        continue;
      }
    }

    p_prev_opcode = p_opcode;
  }

  /* Pass 3: eliminate uopcodes as we can. */
  p_nz_flags_opcode = NULL;
  p_nz_flags_uop = NULL;
  p_lda_opcode = NULL;
  p_lda_uop = NULL;
  p_ldx_opcode = NULL;
  p_ldx_uop = NULL;
  p_ldy_opcode = NULL;
  p_ldy_uop = NULL;
  for (i_opcodes = 0; i_opcodes < num_opcodes; ++i_opcodes) {
    uint32_t i_uops;
    uint32_t num_uops;

    struct jit_opcode_details* p_opcode = &p_opcodes[i_opcodes];
    if (p_opcode->eliminated) {
      continue;
    }

    num_uops = p_opcode->num_uops;
    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct jit_uop* p_uop = &p_opcode->uops[i_uops];
      int32_t uopcode = p_uop->uopcode;
      assert(!p_uop->eliminated);

      /* Finalize eliminations. */
      /* NZ flag load. */
      if ((p_nz_flags_opcode != NULL) &&
          jit_optimizer_uopcode_sets_nz_flags(uopcode)) {
        jit_optimizer_eliminate(&p_nz_flags_opcode, p_nz_flags_uop, p_opcode);
      }
      /* LDA A load. */
      if ((p_lda_opcode != NULL) &&
          jit_optimizer_uopcode_overwrites_a(uopcode)) {
        jit_optimizer_eliminate(&p_lda_opcode, p_lda_uop, p_opcode);
      }
      /* LDX X load. */
      if ((p_ldx_opcode != NULL) &&
          jit_optimizer_uopcode_overwrites_x(uopcode)) {
        jit_optimizer_eliminate(&p_ldx_opcode, p_ldx_uop, p_opcode);
      }
      /* LDY Y load. */
      if ((p_ldy_opcode != NULL) &&
          jit_optimizer_uopcode_overwrites_y(uopcode)) {
        jit_optimizer_eliminate(&p_ldy_opcode, p_ldy_uop, p_opcode);
      }

      /* Cancel eliminations. */
      /* TODO: the FLAGn predicates are a bit hacky but shouldn't cause
       * trouble because we shouldn't eliminate e.g. LDA without also
       * eliminating the FLAGA. Without the predicates, the FLAGA would always
       * prevent the LDA from eliminating.
       * The correct way to do this is probably to have a first pass where flag        * updates are eliminated and then eliminate register writes in a
       * subsequent pass.
       */
      /* NZ flag load. */
      if (jit_optimizer_uopcode_needs_nz_flags(uopcode)) {
        p_nz_flags_opcode = NULL;
      }
      /* LDA A load. */
      if (jit_optimizer_uopcode_needs_a(uopcode) &&
          (uopcode != k_opcode_FLAGA)) {
        p_lda_opcode = NULL;
      }
      /* LDX X load. */
      if (jit_optimizer_uopcode_needs_x(uopcode) &&
          (uopcode != k_opcode_FLAGX)) {
        p_ldx_opcode = NULL;
      }
      /* LDX Y load. */
      if (jit_optimizer_uopcode_needs_y(uopcode) &&
          (uopcode != k_opcode_FLAGY)) {
        p_ldy_opcode = NULL;
      }
      /* Many eliminations can't cross branches. */
      if (jit_optimizer_uopcode_can_jump(uopcode)) {
        p_nz_flags_opcode = NULL;
        p_lda_opcode = NULL;
        p_ldx_opcode = NULL;
        p_ldy_opcode = NULL;
      }

      /* Keep track of uops we may be able to eliminate. */
      switch (uopcode) {
      case k_opcode_FLAGA:
      case k_opcode_FLAGX:
      case k_opcode_FLAGY:
        p_nz_flags_opcode = p_opcode;
        p_nz_flags_uop = p_uop;
        break;
      case k_opcode_LDA_Z:
      case 0xA9: /* LDA imm */
        p_lda_opcode = p_opcode;
        p_lda_uop = p_uop;
        break;
      case k_opcode_LDX_Z:
      case 0xA2: /* LDX imm */
        p_ldx_opcode = p_opcode;
        p_ldx_uop = p_uop;
        break;
      case k_opcode_LDY_Z:
      case 0xA0: /* LDY imm */
        p_ldy_opcode = p_opcode;
        p_ldy_uop = p_uop;
        break;
      default:
        break;
      }
    }
  }
}
