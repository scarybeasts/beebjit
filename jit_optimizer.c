#include "jit_optimizer.h"

#include "defs_6502.h"
#include "jit_compiler.h"
#include "jit_opcode.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const int32_t k_value_unknown = -1;

static void
jit_optimizer_eliminate(struct jit_opcode_details** pp_elim_opcode,
                        struct jit_uop* p_elim_uop,
                        struct jit_opcode_details* p_curr_opcode) {
  struct jit_opcode_details* p_elim_opcode = *pp_elim_opcode;

  *pp_elim_opcode = NULL;

  assert(!p_elim_uop->eliminated);
  p_elim_uop->eliminated = 1;

  p_elim_opcode++;
  while (p_elim_opcode <= p_curr_opcode) {
    uint32_t num_fixup_uops = p_elim_opcode->num_fixup_uops;
    assert(num_fixup_uops < k_max_uops_per_opcode);
    /* Prepend the elimination so that fixups are applied in order of last
     * fixup first. This is important because some fixups trash each other,
     * such as FLAGX trashing any pending SAVE_CARRY.
     */
    (void) memmove(&p_elim_opcode->fixup_uops[1],
                   &p_elim_opcode->fixup_uops[0],
                   (sizeof(struct jit_uop*) * num_fixup_uops));
    p_elim_opcode->fixup_uops[0] = p_elim_uop;
    p_elim_opcode->num_fixup_uops++;
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
jit_optimizer_uop_could_write(struct jit_uop* p_uop, uint16_t addr) {
  int32_t write_addr_start = -1;
  int32_t write_addr_end = -1;

  int32_t uopcode = p_uop->uopcode;

  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    uint8_t opmode = g_opmodes[uopcode];
    uint8_t opmem = g_opmem[optype];
    if ((opmem == k_write) || (opmem == k_rw)) {
      switch (opmode) {
      case k_zpg:
      case k_abs:
        write_addr_start = p_uop->value1;
        write_addr_end = p_uop->value1;
        break;
      case k_abx:
      case k_aby:
      case k_idx:
      case k_idy:
      case k_zpx:
      case k_zpy:
        /* NOTE: could be more refined with at least abx, aby. */
        write_addr_start = 0;
        write_addr_end = (k_6502_addr_space_size - 1);
      default:
        break;
      }
    }
  } else {
    switch (uopcode) {
    case k_opcode_STOA_IMM:
      write_addr_start = p_uop->value1;
      write_addr_end = p_uop->value1;
      break;
    default:
      break;
    }
  }

  if (write_addr_start == -1) {
    return 0;
  }
  if (addr >= write_addr_start && addr <= write_addr_end) {
    return 1;
  }
  return 0;
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
    case k_opcode_FLAG_MEM:
    case k_opcode_LDA_SCRATCH_n:
    case k_opcode_LDA_Z:
    case k_opcode_LDX_Z:
    case k_opcode_LDY_Z:
    case k_opcode_LSR_ACC_n:
    case k_opcode_SUB_ABS:
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
  if (jit_optimizer_uopcode_can_jump(uopcode)) {
    return 1;
  }
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
    case k_opcode_SUB_ABS:
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

static int
jit_optimizer_uop_idy_match(struct jit_uop* p_uop, struct jit_uop* p_idy_uop) {
  if ((p_uop->uopcode == k_opcode_MODE_IND_8) &&
      (p_uop->value1 == p_idy_uop->value1)) {
    return 1;
  }
  return 0;
}

static int
jit_optimizer_uop_invalidates_idy(struct jit_uop* p_uop,
                                  struct jit_uop* p_idy_uop) {
  int ret = 1;
  int32_t uopcode = p_uop->uopcode;

  /* If this opcode could write to the idy indirect address, we must
   * invalidate.
   */
  if (jit_optimizer_uop_could_write(p_uop, p_idy_uop->value1) ||
      jit_optimizer_uop_could_write(p_uop, (uint8_t) (p_idy_uop->value1 + 1))) {
    return 1;
  }

  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    switch (optype) {
    case k_nop:
    case k_adc:
    case k_and:
    case k_cmp:
    case k_eor:
    case k_ora:
    case k_lda:
    case k_ldx:
    case k_ldy:
    case k_sbc:
    case k_sta:
    case k_stx:
    case k_sty:
    case k_inx:
    case k_dex:
    case k_iny:
    case k_dey:
    case k_tax:
    case k_txa:
    case k_tay:
    case k_tya:
      ret = 0;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_debug:
    case k_opcode_ADD_ABS:
    case k_opcode_ADD_ABX:
    case k_opcode_ADD_ABY:
    case k_opcode_ADD_IMM:
    case k_opcode_ADD_SCRATCH:
    case k_opcode_ADD_SCRATCH_Y:
    case k_opcode_CHECK_BCD:
    case k_opcode_FLAGA:
    case k_opcode_FLAGX:
    case k_opcode_FLAGY:
    case k_opcode_FLAG_MEM:
    case k_opcode_LDA_SCRATCH_n:
    case k_opcode_LDA_Z:
    case k_opcode_LDX_Z:
    case k_opcode_LDY_Z:
    case k_opcode_LOAD_CARRY_FOR_BRANCH:
    case k_opcode_LOAD_CARRY_FOR_CALC:
    case k_opcode_LOAD_CARRY_INV_FOR_CALC:
    case k_opcode_LOAD_OVERFLOW:
    case k_opcode_SAVE_CARRY:
    case k_opcode_SAVE_CARRY_INV:
    case k_opcode_SAVE_OVERFLOW:
    case k_opcode_STOA_IMM:
    case k_opcode_SUB_ABS:
    case k_opcode_SUB_IMM:
      ret = 0;
      break;
    default:
      break;
    }
  }
  return ret;
}

/* TODO: these lists are duplicative and awful. Improve. */
static int
jit_optimizer_uopcode_needs_or_trashes_overflow(int32_t uopcode) {
  /* Many things need or trash overflow so we'll just enumerate what's safe. */
  int ret = 1;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    switch (optype) {
    case k_nop:
    case k_adc:
    case k_sbc:
    case k_bit:
    case k_clv:
    case k_lda:
    case k_ldx:
    case k_ldy:
    case k_sta:
    case k_stx:
    case k_sty:
    case k_sec:
    case k_clc:
    case k_pla:
    case k_pha:
    case k_tax:
    case k_tay:
    case k_txa:
    case k_tya:
      ret = 0;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_debug:
    case k_opcode_ABX_CHECK_PAGE_CROSSING:
    case k_opcode_ABY_CHECK_PAGE_CROSSING:
    case k_opcode_ADD_ABS:
    case k_opcode_ADD_ABX:
    case k_opcode_ADD_ABY:
    case k_opcode_ADD_IMM:
    case k_opcode_ADD_SCRATCH:
    case k_opcode_ADD_SCRATCH_Y:
    case k_opcode_CHECK_BCD:
    case k_opcode_CHECK_PAGE_CROSSING_SCRATCH_n:
    case k_opcode_CHECK_PENDING_IRQ:
    case k_opcode_IDY_CHECK_PAGE_CROSSING:
    case k_opcode_LDA_SCRATCH_n:
    case k_opcode_LOAD_CARRY_FOR_BRANCH:
    case k_opcode_LOAD_CARRY_FOR_CALC:
    case k_opcode_LOAD_CARRY_INV_FOR_CALC:
    case k_opcode_MODE_ABX:
    case k_opcode_MODE_ABY:
    case k_opcode_MODE_IND_8:
    case k_opcode_MODE_IND_16:
    case k_opcode_MODE_IND_SCRATCH:
    case k_opcode_MODE_ZPX:
    case k_opcode_MODE_ZPY:
    case k_opcode_SAVE_CARRY:
    case k_opcode_SAVE_CARRY_INV:
    case k_opcode_SAVE_OVERFLOW:
    case k_opcode_STOA_IMM:
    case k_opcode_SUB_ABS:
    case k_opcode_SUB_IMM:
    case k_opcode_WRITE_INV_ABS:
    case k_opcode_WRITE_INV_SCRATCH:
    case k_opcode_WRITE_INV_SCRATCH_Y:
      ret = 0;
      break;
    default:
      break;
    }
  }
  return ret;
}

static int
jit_optimizer_uopcode_needs_or_trashes_carry(int32_t uopcode) {
  /* Many things need or trash carry so we'll just enumerate what's safe. */
  int ret = 1;
  if (uopcode <= 0xFF) {
    uint8_t optype = g_optypes[uopcode];
    switch (optype) {
    case k_nop:
    case k_cmp:
    case k_cpx:
    case k_cpy:
    case k_inx:
    case k_dex:
    case k_iny:
    case k_dey:
    case k_asl:
    case k_lsr:
    case k_lda:
    case k_ldx:
    case k_ldy:
    case k_sta:
    case k_stx:
    case k_sty:
    case k_sec:
    case k_clc:
    case k_pla:
    case k_pha:
    case k_tax:
    case k_tay:
    case k_txa:
    case k_tya:
      ret = 0;
      break;
    default:
      break;
    }
  } else {
    switch (uopcode) {
    case k_opcode_debug:
    case k_opcode_ABX_CHECK_PAGE_CROSSING:
    case k_opcode_ABY_CHECK_PAGE_CROSSING:
    case k_opcode_ADD_ABS:
    case k_opcode_ADD_ABX:
    case k_opcode_ADD_ABY:
    case k_opcode_ADD_IMM:
    case k_opcode_ADD_SCRATCH:
    case k_opcode_ADD_SCRATCH_Y:
    case k_opcode_ASL_ACC_n:
    case k_opcode_CHECK_BCD:
    case k_opcode_CHECK_PAGE_CROSSING_SCRATCH_n:
    case k_opcode_CHECK_PENDING_IRQ:
    case k_opcode_IDY_CHECK_PAGE_CROSSING:
    case k_opcode_LDA_SCRATCH_n:
    case k_opcode_LOAD_CARRY_FOR_BRANCH:
    case k_opcode_LOAD_CARRY_FOR_CALC:
    case k_opcode_LOAD_CARRY_INV_FOR_CALC:
    case k_opcode_LSR_ACC_n:
    case k_opcode_MODE_ABX:
    case k_opcode_MODE_ABY:
    case k_opcode_MODE_IND_8:
    case k_opcode_MODE_IND_16:
    case k_opcode_MODE_IND_SCRATCH:
    case k_opcode_MODE_ZPX:
    case k_opcode_MODE_ZPY:
    case k_opcode_SAVE_CARRY:
    case k_opcode_SAVE_CARRY_INV:
    case k_opcode_SAVE_OVERFLOW:
    case k_opcode_STOA_IMM:
    case k_opcode_SUB_ABS:
    case k_opcode_SUB_IMM:
    case k_opcode_WRITE_INV_ABS:
    case k_opcode_WRITE_INV_SCRATCH:
    case k_opcode_WRITE_INV_SCRATCH_Y:
      ret = 0;
      break;
    default:
      break;
    }
  }
  return ret;
}

static void
jit_optimizer_append_uop(struct jit_opcode_details* p_opcode,
                         int32_t uopcode) {
  uint8_t num_uops = p_opcode->num_uops;
  struct jit_uop* p_uop = &p_opcode->uops[num_uops];
  assert(num_uops < k_max_uops_per_opcode);
  p_opcode->num_uops++;
  p_uop->uopcode = uopcode;
  p_uop->uoptype = -1;
  p_uop->value1 = 0;
  p_uop->value2 = 0;

  p_uop->len_x64 = 0;
  p_uop->eliminated = 0;
}

uint32_t
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
  struct jit_opcode_details* p_idy_opcode;
  struct jit_uop* p_idy_uop;
  struct jit_opcode_details* p_overflow_opcode;
  struct jit_uop* p_overflow_uop;
  struct jit_opcode_details* p_carry_write_opcode;
  struct jit_uop* p_carry_write_uop;
  int carry_flipped_for_branch;

  struct jit_opcode_details* p_bcd_opcode = &p_opcodes[1];

  (void) p_compiler;

  /* Use a compiler-provide scratch opcode to eliminate all BCD checks and do
   * it just once at the start of the block, if any ADC / SBC are present.
   */
  assert(num_opcodes > 2);
  assert(p_bcd_opcode->eliminated);
  assert(p_bcd_opcode->num_uops == 1);
  p_bcd_opcode->uops[0].uopcode = k_opcode_CHECK_BCD;

  /* Pass 1: tag opcodes with any known register and flag values. Also replace
   * single uops with replacements if known registers offer better alternatives.
   * Classic example is CLC; ADC. At the ADC instruction, it is known that
   * CF==0 so the ADC can become just an ADD.
   * In the case of self-modifying code, this loop can also replace uopcodes
   * with equivalents that load operands dynamically, to prevent continual
   * recompilation.
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
    int32_t revalidate_count;

    struct jit_opcode_details* p_opcode = &p_opcodes[i_opcodes];
    uint16_t addr_6502 = p_opcode->addr_6502;
    uint8_t opcode_6502 = p_opcode->opcode_6502;
    uint16_t operand_6502 = p_opcode->operand_6502;
    uint8_t optype = g_optypes[opcode_6502];
    uint8_t opmode = g_opmodes[opcode_6502];
    int changes_carry = g_optype_changes_carry[optype];
    int use_interp = 0;

    if (p_opcode == p_bcd_opcode) {
      continue;
    }

    p_opcode->reg_a = reg_a;
    p_opcode->reg_x = reg_x;
    p_opcode->reg_y = reg_y;
    p_opcode->flag_carry = flag_carry;
    p_opcode->flag_decimal = flag_decimal;

    /* TODO: seems hacky, should g_optype_sets_register just be per-opcode? */
    opreg = g_optype_sets_register[optype];
    if (opmode == k_acc) {
      opreg = k_a;
    }

    revalidate_count = jit_compiler_get_revalidate_count(p_compiler, addr_6502);
    if (revalidate_count >= 4) {
      struct jit_uop* p_rewrite_uop;

      switch (opcode_6502) {
      case 0xA9: /* LDA imm */
        p_rewrite_uop = jit_opcode_find_uop(p_opcode, 0xA9);
        assert(p_rewrite_uop != NULL);
        p_rewrite_uop->uopcode = 0xAD; /* LDA abs */
        p_rewrite_uop->value1 = (addr_6502 + 1);
        p_opcode->dynamic_operand = 1;
        break;
      default:
        break;
      }
    }

    num_uops = p_opcode->num_uops;
    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct jit_uop* p_uop = &p_opcode->uops[i_uops];
      int32_t uopcode = p_uop->uopcode;
      int32_t new_add_uopcode = -1;
      int32_t new_sub_uopcode = -1;

      switch (uopcode) {
      case 0x61: /* ADC idx */
      case 0x75: /* ADC zpx */
        new_add_uopcode = k_opcode_ADD_SCRATCH;
        break;
      case 0x65: /* ADC zpg */
      case 0x6D: /* ADC abs */
        new_add_uopcode = k_opcode_ADD_ABS;
        break;
      case 0x69: /* ADC imm */
        if (flag_carry == 0) {
          new_add_uopcode = k_opcode_ADD_IMM;
        } else if (flag_carry == 1) {
          /* NOTE: if this is common, we can optimize this case. */
          printf("LOG:JIT:optimizer sees ADC #$imm with C==1\n");
        }
        break;
      case 0x71: /* ADC idy */
        new_add_uopcode = k_opcode_ADD_SCRATCH_Y;
        break;
      case 0x79: /* ADC aby */
        new_add_uopcode = k_opcode_ADD_ABY;
        break;
      case 0x7D: /* ADC abx */
        new_add_uopcode = k_opcode_ADD_ABX;
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
      case 0x88: /* DEY */
        if (reg_y != k_value_unknown) {
          uopcode = 0xA0; /* LDY imm */
          p_uop->value1 = (uint8_t) (reg_y - 1);
          jit_optimizer_append_uop(p_opcode, k_opcode_FLAGY);
        }
        break;
      case 0x8A: /* TXA */
        if (reg_x != k_value_unknown) {
          uopcode = 0xA9; /* LDA imm */
          p_uop->value1 = reg_x;
        }
        break;
      case 0x98: /* TYA */
        if (reg_y != k_value_unknown) {
          uopcode = 0xA9; /* LDA imm */
          p_uop->value1 = reg_y;
        }
        break;
      case 0xA8: /* TAY */
        if (reg_a != k_value_unknown) {
          uopcode = 0xA0; /* LDY imm */
          p_uop->value1 = reg_a;
        }
        break;
      case 0xAA: /* TAX */
        if (reg_a != k_value_unknown) {
          uopcode = 0xA2; /* LDX imm */
          p_uop->value1 = reg_a;
        }
        break;
      case 0xC8: /* INY */
        if (reg_y != k_value_unknown) {
          uopcode = 0xA0; /* LDY imm */
          p_uop->value1 = (uint8_t) (reg_y + 1);
          jit_optimizer_append_uop(p_opcode, k_opcode_FLAGY);
        }
        break;
      case 0xCA: /* DEX */
        if (reg_x != k_value_unknown) {
          uopcode = 0xA2; /* LDX imm */
          p_uop->value1 = (uint8_t) (reg_x - 1);
          jit_optimizer_append_uop(p_opcode, k_opcode_FLAGX);
        }
        break;
      case 0xE5: /* SBC zpg */
      case 0xED: /* SBC abs */
        new_sub_uopcode = k_opcode_SUB_ABS;
        break;
      case 0xE8: /* INX */
        if (reg_x != k_value_unknown) {
          uopcode = 0xA2; /* LDX imm */
          p_uop->value1 = (uint8_t) (reg_x + 1);
          jit_optimizer_append_uop(p_opcode, k_opcode_FLAGX);
        }
        break;
      case 0xE9: /* SBC imm */
        new_sub_uopcode = k_opcode_SUB_IMM;
        break;
      case k_opcode_CHECK_BCD:
        if (flag_decimal == k_value_unknown) {
          p_uop->eliminated = 1;
          p_bcd_opcode->eliminated = 0;
        } else if (flag_decimal == 0) {
          p_uop->eliminated = 1;
        } else {
          use_interp = 1;
        }
        break;
      default:
        break;
      }

      if ((new_add_uopcode != -1) && (flag_carry == 0)) {
        /* Eliminate LOAD_CARRY_FOR_CALC, flip ADC to ADD. */
        struct jit_uop* p_elim_uop;
        uopcode = new_add_uopcode;
        p_elim_uop = jit_opcode_find_uop(p_opcode,
                                         k_opcode_LOAD_CARRY_FOR_CALC);
        assert(p_elim_uop != NULL);
        p_elim_uop->eliminated = 1;
      }
      if ((new_sub_uopcode != -1) && (flag_carry == 1)) {
        /* Eliminate LOAD_CARRY_INV_FOR_CALC, flip SBC to SUB. */
        struct jit_uop* p_elim_uop;
        uopcode = new_sub_uopcode;
        p_elim_uop = jit_opcode_find_uop(p_opcode,
                                         k_opcode_LOAD_CARRY_INV_FOR_CALC);
        assert(p_elim_uop != NULL);
        p_elim_uop->eliminated = 1;
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
              jit_opcode_find_uop(p_opcode, k_opcode_IDY_CHECK_PAGE_CROSSING);
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
    case 0xB0: /* BCS */
      flag_carry = 0;
      break;
    case 0x38: /* SEC */
    case 0x90: /* BCC */
      flag_carry = 1;
      break;
    case 0x88: /* DEY */
      if (reg_y != k_value_unknown) {
        reg_y = (uint8_t) (reg_y - 1);
      }
      break;
    case 0x8A: /* TXA */
      reg_a = reg_x;
      break;
    case 0x98: /* TYA */
      reg_a = reg_y;
      break;
    case 0xA0: /* LDY imm */
      reg_y = operand_6502;
      break;
    case 0xA2: /* LDX imm */
      reg_x = operand_6502;
      break;
    case 0xA8: /* TAY */
      reg_y = reg_a;
      break;
    case 0xA9: /* LDA imm */
      reg_a = operand_6502;
      break;
    case 0xAA: /* TAX */
      reg_x = reg_a;
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

    if (use_interp) {
      jit_opcode_replace1(p_opcode, k_opcode_interp, addr_6502);
      p_opcode->ends_block = 1;
      num_opcodes = (i_opcodes + 1);
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
        struct jit_uop* p_modify_uop = jit_opcode_find_uop(p_prev_opcode,
                                                           old_uopcode);
        if (p_modify_uop != NULL) {
          p_modify_uop->uopcode = new_uopcode;
          p_modify_uop->value1 = 1;
        } else {
          p_modify_uop = jit_opcode_find_uop(p_prev_opcode, new_uopcode);
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

  /* Pass 3: first uopcode elimination pass, particularly FLAGn. */
  p_nz_flags_opcode = NULL;
  p_nz_flags_uop = NULL;
  p_idy_opcode = NULL;
  p_idy_uop = NULL;
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

      /* Finalize eliminations. */
      /* NZ flag load. */
      if ((p_nz_flags_opcode != NULL) &&
          jit_optimizer_uopcode_sets_nz_flags(uopcode)) {
        jit_optimizer_eliminate(&p_nz_flags_opcode, p_nz_flags_uop, p_opcode);
      }
      /* idy indirect load. */
      if ((p_idy_opcode != NULL) &&
          jit_optimizer_uop_idy_match(p_uop, p_idy_uop)) {
        struct jit_opcode_details* p_eliminate_opcode = p_opcode;
        jit_optimizer_eliminate(&p_eliminate_opcode, p_uop, NULL);
      }

      /* Cancel eliminations. */
      /* NZ flag load. */
      if (p_nz_flags_opcode != NULL) {
        int32_t nz_flags_uopcode = p_nz_flags_uop->uopcode;
        if (jit_optimizer_uopcode_needs_nz_flags(uopcode) ||
            ((nz_flags_uopcode == k_opcode_FLAG_MEM) &&
             jit_optimizer_uop_could_write(p_uop, p_nz_flags_uop->value1))) {
          /* If we can't eliminate a flag load, there's a special case of
           * loading 0 into a register where we can collapse the register load
           * and flag load.
           */
          int32_t find_uopcode = -1;
          int32_t replace_uopcode = -1;
          struct jit_uop* p_find_uop;
          switch (nz_flags_uopcode) {
          case k_opcode_FLAGA:
            find_uopcode = 0xA9; /* LDA imm */
            replace_uopcode = k_opcode_LDA_Z;
            break;
          case k_opcode_FLAGX:
            find_uopcode = 0xA2; /* LDX imm */
            replace_uopcode = k_opcode_LDX_Z;
            break;
          case k_opcode_FLAGY:
            find_uopcode = 0xA0; /* LDY imm */
            replace_uopcode = k_opcode_LDY_Z;
            break;
          case k_opcode_FLAG_MEM:
            break;
          default:
            assert(0);
            break;
          }
          p_find_uop = jit_opcode_find_uop(p_nz_flags_opcode, find_uopcode);
          if ((p_find_uop != NULL) && (p_find_uop->value1 == 0x00)) {
            p_find_uop->uopcode = replace_uopcode;
            p_find_uop->uoptype = -1;
            p_nz_flags_uop->eliminated = 1;
          }
          p_nz_flags_opcode = NULL;
        }
      }
      /* idy indirect load. */
      if ((p_idy_opcode != NULL) &&
          jit_optimizer_uop_invalidates_idy(p_uop, p_idy_uop)) {
        p_idy_opcode = NULL;
      }

      /* Keep track of uops we may be able to eliminate. */
      switch (uopcode) {
      case k_opcode_FLAGA:
      case k_opcode_FLAGX:
      case k_opcode_FLAGY:
      case k_opcode_FLAG_MEM:
        p_nz_flags_opcode = p_opcode;
        p_nz_flags_uop = p_uop;
        break;
      case k_opcode_MODE_IND_8:
        p_idy_opcode = p_opcode;
        p_idy_uop = p_uop;
        break;
      default:
        break;
      }
    }
  }

  /* Pass 4: second uopcode elimination pass, particularly those eliminations
   * that only occur well after FLAGn has been eliminated.
   */
  p_lda_opcode = NULL;
  p_lda_uop = NULL;
  p_ldx_opcode = NULL;
  p_ldx_uop = NULL;
  p_ldy_opcode = NULL;
  p_ldy_uop = NULL;
  p_overflow_opcode = NULL;
  p_overflow_uop = NULL;
  p_carry_write_opcode = NULL;
  p_carry_write_uop = NULL;
  carry_flipped_for_branch = 0;
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
      if (p_uop->eliminated) {
        continue;
      }

      /* Finalize eliminations. */
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
      /* Overflow flag save elimination. */
      if ((p_overflow_opcode != NULL) && (uopcode == k_opcode_SAVE_OVERFLOW)) {
        jit_optimizer_eliminate(&p_overflow_opcode, p_overflow_uop, p_opcode);
      }
      /* Carry flag save elimination. */
      if (p_carry_write_opcode != NULL) {
        struct jit_opcode_details* p_eliminate_opcode = p_opcode;
        int32_t carry_write_uopcode = p_carry_write_uop->uopcode;
        int inversion = 0;
        switch (uopcode) {
        case k_opcode_SAVE_CARRY:
        case k_opcode_SAVE_CARRY_INV:
        case 0x18: /* CLC */
        case 0x38: /* SEC */
          jit_optimizer_eliminate(&p_carry_write_opcode,
                                  p_carry_write_uop,
                                  p_opcode);
          break;
        case k_opcode_LOAD_CARRY_FOR_BRANCH:
        case k_opcode_LOAD_CARRY_FOR_CALC:
        case k_opcode_LOAD_CARRY_INV_FOR_CALC:
          if (uopcode != k_opcode_LOAD_CARRY_FOR_BRANCH) {
            /* Eliminate unfinalized write. */
            jit_optimizer_eliminate(&p_carry_write_opcode,
                                    p_carry_write_uop,
                                    p_opcode);
          }
          /* Eliminate load or replace with direct host carry flag change. */
          inversion ^= (uopcode == k_opcode_LOAD_CARRY_INV_FOR_CALC);
          switch (carry_write_uopcode) {
          case k_opcode_SAVE_CARRY:
          case k_opcode_SAVE_CARRY_INV:
            inversion ^= (carry_write_uopcode == k_opcode_SAVE_CARRY_INV);
            if (inversion) {
              if (uopcode == k_opcode_LOAD_CARRY_FOR_BRANCH) {
                carry_flipped_for_branch = 1;
                jit_optimizer_eliminate(&p_eliminate_opcode, p_uop, NULL);
              } else {
                p_uop->uopcode = k_opcode_INVERT_CARRY;
              }
            } else {
              jit_optimizer_eliminate(&p_eliminate_opcode, p_uop, NULL);
            }
            break;
          case 0x18: /* CLC */
          case 0x38: /* SEC */
            inversion ^= (carry_write_uopcode == 0x38); /* SEC */
            if (inversion) {
              p_uop->uopcode = k_opcode_SET_CARRY;
            } else {
              p_uop->uopcode = k_opcode_CLEAR_CARRY;
            }
            break;
          default:
            assert(0);
            break;
          }
          break;
        default:
          break;
        }
      }
      /* Carry flip vs. carry branch elimination. */
      if (carry_flipped_for_branch) {
        switch (uopcode) {
        case 0x90: /* BCC */
          p_uop->uopcode = 0xB0; /* BCS */
          break;
        case 0xB0: /* BCS */
          p_uop->uopcode = 0x90; /* BCC */
          break;
        default:
          break;
        }
      }

      /* Cancel eliminations. */
      /* LDA A load. */
      if ((p_lda_opcode != NULL) && jit_optimizer_uopcode_needs_a(uopcode)) {
        p_lda_opcode = NULL;
      }
      /* LDX X load. */
      if ((p_ldx_opcode != NULL) && jit_optimizer_uopcode_needs_x(uopcode)) {
        p_ldx_opcode = NULL;
      }
      /* LDX Y load. */
      if ((p_ldy_opcode != NULL) && jit_optimizer_uopcode_needs_y(uopcode)) {
        p_ldy_opcode = NULL;
      }
      /* Overflow flag save elimination. */
      if ((p_overflow_opcode != NULL) &&
          jit_optimizer_uopcode_needs_or_trashes_overflow(uopcode)) {
        p_overflow_opcode = NULL;
      }
      /* Carry flag save elimination. */
      if ((p_carry_write_opcode != NULL) &&
          jit_optimizer_uopcode_needs_or_trashes_carry(uopcode)) {
        p_carry_write_opcode = NULL;
      }
      /* Many eliminations can't cross branches, or need modifications. */
      if (jit_optimizer_uopcode_can_jump(uopcode)) {
        p_lda_opcode = NULL;
        p_ldx_opcode = NULL;
        p_ldy_opcode = NULL;
        p_overflow_opcode = NULL;
        p_carry_write_opcode = NULL;
        carry_flipped_for_branch = 0;
      }

      /* Keep track of uops we may be able to eliminate. */
      switch (uopcode) {
      case 0xA9: /* LDA imm */
        p_lda_opcode = p_opcode;
        p_lda_uop = p_uop;
        break;
      case 0xA2: /* LDX imm */
        p_ldx_opcode = p_opcode;
        p_ldx_uop = p_uop;
        break;
      case 0xA0: /* LDY imm */
        p_ldy_opcode = p_opcode;
        p_ldy_uop = p_uop;
        break;
      case k_opcode_SAVE_OVERFLOW:
        p_overflow_opcode = p_opcode;
        p_overflow_uop = p_uop;
        break;
      case k_opcode_SAVE_CARRY:
      case k_opcode_SAVE_CARRY_INV:
      case 0x18: /* CLC */
      case 0x38: /* SEC */
        p_carry_write_opcode = p_opcode;
        p_carry_write_uop = p_uop;
        break;
      default:
        break;
      }
    }
  }

  return num_opcodes;
}
