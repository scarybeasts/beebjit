#include "jit_optimizer.h"

#include "defs_6502.h"
#include "jit_opcode.h"
#include "asm/asm_jit.h"
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

    if (p_opcode->ends_block) {
      continue;
    }

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

    if (p_opcode->ends_block) {
      continue;
    }

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

    /* The transforms below will crash if we've written the opcode to be an
     * interp or inturbo bail.
     */
    if (p_opcode->ends_block) {
      continue;
    }

    if (p_opcode->is_dynamic_operand) {
      continue;
    }

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
    case k_sta:
      if (p_opcode->reg_a == k_value_unknown) {
        break;
      }
      if ((p_opcode->opmode_6502 != k_zpg) &&
          (p_opcode->opmode_6502 != k_abs)) {
        break;
      }
      if (!asm_jit_supports_uopcode(k_opcode_ST_IMM)) {
        break;
      }
      p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_STA);
      assert(p_uop != NULL);
      asm_make_uop0(p_uop, k_opcode_ST_IMM);
      p_uop->value2 = p_opcode->reg_a;
      break;
    case k_stx:
      if (p_opcode->reg_x == k_value_unknown) {
        break;
      }
      if ((p_opcode->opmode_6502 != k_zpg) &&
          (p_opcode->opmode_6502 != k_abs)) {
        break;
      }
      if (!asm_jit_supports_uopcode(k_opcode_ST_IMM)) {
        break;
      }
      p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_STX);
      assert(p_uop != NULL);
      asm_make_uop0(p_uop, k_opcode_ST_IMM);
      p_uop->value2 = p_opcode->reg_x;
      break;
    case k_sty:
      if (p_opcode->reg_y == k_value_unknown) {
        break;
      }
      if ((p_opcode->opmode_6502 != k_zpg) &&
          (p_opcode->opmode_6502 != k_abs)) {
        break;
      }
      if (!asm_jit_supports_uopcode(k_opcode_ST_IMM)) {
        break;
      }
      p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_STY);
      assert(p_uop != NULL);
      asm_make_uop0(p_uop, k_opcode_ST_IMM);
      p_uop->value2 = p_opcode->reg_y;
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

    if ((p_opcode->opmode_6502 == k_idy) &&
        (p_opcode->reg_y != k_value_unknown)) {
      uint8_t reg_y = p_opcode->reg_y;
      p_uop = jit_opcode_find_uop(p_opcode, &index, k_opcode_addr_add_base_y);
      assert(p_uop != NULL);
      p_uop->uopcode = k_opcode_addr_add_base_constant;
      p_uop->value1 = reg_y;
      p_uop = jit_opcode_find_uop(p_opcode,
                                  &index,
                                  k_opcode_check_page_crossing_y);
      if (p_uop != NULL) {
        p_uop->uopcode = k_opcode_check_page_crossing_n;
        p_uop->value1 = reg_y;
        if (reg_y == 0) {
          /* Y is known to be zero, so skip the page crossing check entirely. */
          p_uop->is_eliminated = 1;
          p_opcode->max_cycles = 5;
        }
      }
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
jit_optimizer_eliminate_mode_loads(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  int32_t curr_base_addr_index = -1;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint32_t i_uops;
    uint32_t num_uops = p_opcode->num_uops;
    struct asm_uop* p_addr_set_uop = NULL;
    struct asm_uop* p_addr_add_uop = NULL;
    struct asm_uop* p_addr_load_uop = NULL;
    int is_write = 0;
    int is_simple_addr = 1;
    int is_changing_addr_reg = 0;
    int is_tricky_opcode = 0;

    if (p_opcode->ends_block) {
      continue;
    }

    if (p_opcode->is_eliminated) {
      continue;
    }

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct asm_uop* p_uop = &p_opcode->uops[i_uops];
      int32_t uopcode = p_uop->uopcode;
      switch (uopcode) {
      case k_opcode_addr_set:
        p_addr_set_uop = p_uop;
        break;
      case k_opcode_addr_add_x_8bit:
        p_addr_add_uop = p_uop;
        break;
      case k_opcode_addr_base_load_16bit_wrap:
        p_addr_load_uop = p_uop;
        break;
      case k_opcode_PHP:
      case k_opcode_PLP:
        is_tricky_opcode = 1;
        break;
      default:
        break;
      }

      if ((uopcode > k_opcode_addr_begin) && (uopcode < k_opcode_addr_end)) {
        if (uopcode != k_opcode_addr_set) {
          is_simple_addr = 0;
        }
        if (!p_uop->is_eliminated) {
          is_changing_addr_reg = 1;
        }
      }
    }

    if ((p_addr_load_uop != NULL) && (p_addr_add_uop == NULL)) {
      /* It's mode IDY. */
      int32_t this_base_addr_index;
      assert(p_addr_set_uop != NULL);
      this_base_addr_index = p_addr_set_uop->value1;
      if (this_base_addr_index == curr_base_addr_index) {
        p_addr_set_uop->is_eliminated = 1;
        p_addr_load_uop->is_eliminated = 1;
      }
      curr_base_addr_index = this_base_addr_index;
    } else if (is_changing_addr_reg) {
      /* Changing the address register invalidates the optimization. */
      curr_base_addr_index = -1;
    }

    /* Writes to where the base address is stored invaldates the cached base
     * address.
     */
    is_write = !!(p_opcode->opmem_6502 & k_opmem_write_flag);
    if (is_write && (curr_base_addr_index != -1)) {
      uint16_t simple_addr = p_addr_set_uop->value1;
      uint16_t curr_base_addr_index_next = (uint8_t) (curr_base_addr_index + 1);
      if (is_simple_addr &&
          (curr_base_addr_index != simple_addr) &&
          (curr_base_addr_index_next != simple_addr)) {
        /* This write doesn't affect the cached base address. */
      } else {
        curr_base_addr_index = -1;
      }
    }

    /* This is hacky, but for now don't carry the optimization across a couple
     * of "tricky" opcodes where the backends might re-use the cached address
     * register as a scratch space.
     */
    if (is_tricky_opcode) {
      curr_base_addr_index = -1;
    }
  }
}

static void
jit_optimizer_eliminate_nz_flag_saving(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  struct asm_uop* p_nz_flags_uop = NULL;
  uint16_t nz_mem_addr = 0;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint32_t num_uops = p_opcode->num_uops;
    uint32_t i_uops;

    if (p_opcode->ends_block) {
      continue;
    }

    if (p_opcode->is_eliminated) {
      continue;
    }

    if (p_nz_flags_uop != NULL) {
      if (p_nz_flags_uop->uopcode == k_opcode_flags_nz_mem) {
        p_opcode->nz_flags_location = nz_mem_addr;
      } else {
        p_opcode->nz_flags_location = -p_nz_flags_uop->uopcode;
      }
    }

    /* PHP needs the NZ flags. */
    if (p_opcode->optype_6502 == k_php) {
      p_nz_flags_uop = NULL;
    }
    /* Any jump, including conditional, must commit flags. */
    if (p_opcode->opbranch_6502 != k_bra_n) {
      p_nz_flags_uop = NULL;
    }
    /* A write might invalidate flag state stored in memory. */
    if ((p_nz_flags_uop != NULL) &&
        (p_nz_flags_uop->uopcode == k_opcode_flags_nz_mem) &&
        jit_opcode_can_write_to_addr(p_opcode, nz_mem_addr)) {
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
      case k_opcode_flags_nz_mem:
        nz_flags_uopcode = p_uop->uopcode;
        break;
      default:
        break;
      }
      if (nz_flags_uopcode == 0) {
        continue;
      }
      /* Eliminate the previous flag set, if appropriate. */
      if (p_nz_flags_uop != NULL) {
        p_nz_flags_uop->is_eliminated = 1;
      }
      if (p_uop->is_merged) {
        /* The x64 rewriter will have merge eliminated a lot of NZ flag
         * writes.
         */
        assert(p_uop->is_eliminated);
        p_nz_flags_uop = NULL;
      } else {
        p_nz_flags_uop = p_uop;
        if (nz_flags_uopcode == k_opcode_flags_nz_mem) {
          nz_mem_addr = p_uop->value1;
        }
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
    int32_t index;

    if (p_opcode->ends_block) {
      continue;
    }

    if (p_opcode->is_eliminated) {
      continue;
    }

    if (p_save_carry_uop != NULL) {
      p_opcode->c_flag_location = p_save_carry_uop->uopcode;
    }
    if (p_save_overflow_uop != NULL) {
      p_opcode->v_flag_location = p_save_overflow_uop->uopcode;
    }

    /* If this is a carry-related branch, we might be able to collapse out the
     * carry load, if we've tracking a carry save.
     */
    if ((p_opcode->optype_6502 == k_bcc) || (p_opcode->optype_6502 == k_bcs)) {
      if (p_save_carry_uop != NULL) {
        if ((p_save_carry_uop->uopcode == k_opcode_save_carry) ||
            (p_save_carry_uop->uopcode == k_opcode_save_carry_inverted)) {
          int is_inversion =
              (p_save_carry_uop->uopcode == k_opcode_save_carry_inverted);
          struct asm_uop* p_load_carry_uop =
              jit_opcode_find_uop(p_opcode, &index, k_opcode_load_carry);
          p_load_carry_uop->is_eliminated = 1;
          /* If it's an inversion, flip BCC <-> BCS. */
          if (is_inversion) {
            if (p_opcode->optype_6502 == k_bcc) {
              struct asm_uop* p_branch_uop =
                  jit_opcode_find_uop(p_opcode, &index, k_opcode_BCC);
              p_branch_uop->uopcode = k_opcode_BCS;
            } else {
              struct asm_uop* p_branch_uop =
                  jit_opcode_find_uop(p_opcode, &index, k_opcode_BCS);
              p_branch_uop->uopcode = k_opcode_BCC;
            }
          }
        }
      }
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
      case k_opcode_load_carry_inverted:
        /* The carry load will be eliminated if this was made an ADD / SUB, or
         * by the ARM64 backend.
         */
        if (p_uop->is_eliminated && !p_uop->is_merged) {
          break;
        }
        /* Eliminate the store and the load together if we've got a pair. */
        if (!p_uop->is_merged &&
            (p_save_carry_uop != NULL) &&
            ((p_save_carry_uop->uopcode == k_opcode_save_carry) ||
                (p_save_carry_uop->uopcode == k_opcode_save_carry_inverted))) {
          int do_invert =
              (p_save_carry_uop->uopcode == k_opcode_save_carry_inverted);
          if (p_uop->uopcode == k_opcode_load_carry_inverted) {
            do_invert ^= 1;
          }
          p_save_carry_uop->is_eliminated = 1;
          if (do_invert) {
            p_uop->uopcode = k_opcode_carry_invert;
          } else {
            p_uop->is_eliminated = 1;
          }
        }
        p_save_carry_uop = NULL;
        break;
      case k_opcode_save_carry:
      case k_opcode_save_carry_inverted:
        had_save_carry = 1;
        /* FALL THROUGH */
      case k_opcode_CLC:
      case k_opcode_SEC:
        if (p_save_carry_uop != NULL) {
          p_save_carry_uop->is_eliminated = 1;
        }
        if (p_uop->is_merged) {
          assert(p_uop->is_eliminated);
          p_save_carry_uop = NULL;
        } else {
          p_save_carry_uop = p_uop;
        }
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
      case k_opcode_flags_nz_mem:
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
      case k_opcode_BIT:
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
  struct asm_uop* p_load_a_uop = NULL;
  struct asm_uop* p_load_x_uop = NULL;
  struct asm_uop* p_load_y_uop = NULL;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint32_t i_uops;
    uint32_t num_uops = p_opcode->num_uops;
    int32_t imm_value = -1;
    struct asm_uop* p_load_uop = NULL;
    struct asm_uop* p_load_flags_uop = NULL;
    int is_self_modify_invalidated = p_opcode->self_modify_invalidated;

    if (p_opcode->ends_block) {
      continue;
    }

    if (p_opcode->is_eliminated) {
      continue;
    }

    /* Any jump, including conditional, must commit register values. */
    if (p_opcode->opbranch_6502 != k_bra_n) {
      p_load_a_uop = NULL;
      p_load_x_uop = NULL;
      p_load_y_uop = NULL;
    }

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      struct asm_uop* p_uop = &p_opcode->uops[i_uops];
      switch (p_uop->uopcode) {
      case k_opcode_value_set:
        imm_value = p_uop->value1;
        break;
      case k_opcode_LDA:
        if (p_load_a_uop != NULL) {
          p_load_a_uop->is_eliminated = 1;
        }
        if (imm_value >= 0) {
          p_load_a_uop = p_uop;
        } else {
          p_load_a_uop = NULL;
        }
        p_load_uop = p_uop;
        break;
      case k_opcode_LDX:
        if (p_load_x_uop != NULL) {
          p_load_x_uop->is_eliminated = 1;
        }
        if (imm_value >= 0) {
          p_load_x_uop = p_uop;
        } else {
          p_load_x_uop = NULL;
        }
        p_load_uop = p_uop;
        break;
      case k_opcode_LDY:
        if (p_load_y_uop != NULL) {
          p_load_y_uop->is_eliminated = 1;
        }
        if (imm_value >= 0) {
          p_load_y_uop = p_uop;
        } else {
          p_load_y_uop = NULL;
        }
        p_load_uop = p_uop;
        break;
      case k_opcode_flags_nz_a:
        p_load_flags_uop = p_uop;
        /* FALL THROUGH */
      case k_opcode_ADC:
      case k_opcode_ADD:
      case k_opcode_ALR:
      case k_opcode_AND:
      case k_opcode_ASL_acc:
      case k_opcode_BIT:
      case k_opcode_CMP:
      case k_opcode_EOR:
      case k_opcode_LSR_acc:
      case k_opcode_ORA:
      case k_opcode_PHA:
      case k_opcode_ROL_acc:
      case k_opcode_ROR_acc:
      case k_opcode_SBC:
      case k_opcode_SLO:
      case k_opcode_STA:
      case k_opcode_SUB:
      case k_opcode_TAX:
      case k_opcode_TAY:
        if (!p_uop->is_eliminated || p_uop->is_merged) {
          p_load_a_uop = NULL;
        }
        break;
      case k_opcode_flags_nz_x:
        p_load_flags_uop = p_uop;
        /* FALL THROUGH */
      case k_opcode_check_page_crossing_x:
      case k_opcode_addr_add_x:
      case k_opcode_addr_add_x_8bit:
      case k_opcode_CPX:
      case k_opcode_DEX:
      case k_opcode_INX:
      case k_opcode_STX:
      case k_opcode_TXS:
      case k_opcode_TXA:
        if (!p_uop->is_eliminated || p_uop->is_merged) {
          p_load_x_uop = NULL;
        }
        break;
      case k_opcode_flags_nz_y:
        p_load_flags_uop = p_uop;
        /* FALL THROUGH */
      case k_opcode_check_page_crossing_y:
      case k_opcode_addr_add_y:
      case k_opcode_addr_add_y_8bit:
      case k_opcode_CPY:
      case k_opcode_DEY:
      case k_opcode_INY:
      case k_opcode_STY:
      case k_opcode_TYA:
        if (!p_uop->is_eliminated || p_uop->is_merged) {
          p_load_y_uop = NULL;
        }
        break;
      case k_opcode_flags_nz_value:
      case k_opcode_flags_nz_mem:
        p_load_flags_uop = p_uop;
        break;
      default:
        if (p_uop->uopcode == k_opcode_SAX) {
          if (!p_uop->is_eliminated || p_uop->is_merged) {
            p_load_a_uop = NULL;
            p_load_x_uop = NULL;
          }
        }
        break;
      }
    }

    /*if ((p_countdown_uop != NULL) && (p_load_flags_uop != NULL)) {*/
      /* If the opcode immediately following a countdown check sets the NZ
       * flags, then we can safely use a faster countdown check that clobbers
       * the NZ flags.
       * Even if the countdown check fires, and an IRQ is raised, the IRQ won't
       * trigger until after the opcode immediately following the countdown
       * check.
       */
      /* Disabled: buggy, hit by Exile.
       * Optimization is worthwhile, and can likely be re-enabled but this
       * will require some work.
       * p_countdown_uop->uopcode = k_opcode_countdown_no_preserve_nz_flags;
       */
    /*}*/

    /* This is subtle, but if we're in the middle of resolving self-modification
     * on a merged (or merged into) opcode, don't eliminate the merged opcode.
     * This will enable the self-modification detector to emit a dynamic
     * operand for the correct opcode. After this, the resulting recompile
     * will still retain the elimination optimization if appropriate.
     */
    if (is_self_modify_invalidated) {
      p_load_a_uop = NULL;
      p_load_x_uop = NULL;
      p_load_y_uop = NULL;
    }

    /* Replace loads of immediate #0 + flags setting with a single uopcode. */
    if (p_load_uop != NULL) {
      assert(p_load_flags_uop != NULL);
      if ((imm_value == 0) && !p_load_flags_uop->is_eliminated) {
        switch (p_load_uop->uopcode) {
        case k_opcode_LDA:
          p_load_uop->uopcode = k_opcode_LDA_zero_and_flags;
          break;
        case k_opcode_LDX:
          p_load_uop->uopcode = k_opcode_LDX_zero_and_flags;
          break;
        case k_opcode_LDY:
          p_load_uop->uopcode = k_opcode_LDY_zero_and_flags;
          break;
        default:
          assert(0);
          break;
        }
        p_load_uop->backend_tag = 0;
        /* This whole shebang might already be eliminated, but no harm in doing
         * part of it again if that's the case.
         */
        p_load_flags_uop->is_eliminated = 1;
        p_load_flags_uop->is_merged = 1;
      }
    }
  }
}

static void
jit_optimizer_merge_countdowns(struct jit_opcode_details* p_opcodes) {
  struct jit_opcode_details* p_opcode;
  struct asm_uop* p_add_cycles_uop = NULL;

  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    uint32_t i_uops;
    uint32_t num_uops = p_opcode->num_uops;
    struct asm_uop* p_countdown_uop = NULL;

    if (p_opcode->is_eliminated) {
      continue;
    }

    for (i_uops = 0; i_uops < num_uops; ++i_uops) {
      uint32_t cycles;
      struct asm_uop* p_uop = &p_opcode->uops[i_uops];
      switch (p_uop->uopcode) {
      case k_opcode_add_cycles:
        p_add_cycles_uop = p_uop;
        break;
      case k_opcode_countdown:
        /* Merge any branch-not-taken countdown fixup into the countdown check
         * subtraction itself.
         */
        assert(p_opcode->cycles_run_start >= 0);
        p_countdown_uop = p_uop;
        cycles = p_countdown_uop->value2;
        if (p_add_cycles_uop != NULL) {
          if (cycles >= p_add_cycles_uop->value1) {
            cycles -= p_add_cycles_uop->value1;
            p_countdown_uop->value2 = cycles;
            p_add_cycles_uop->is_eliminated = 1;
            p_add_cycles_uop->is_merged = 1;
          }
        }
        /* Eliminate countdown checks for 0 cycles. These happen at the start
         * of a block that contains just an inturbo callout.
         */
        if (cycles == 0) {
          p_countdown_uop->is_eliminated = 1;
        }
        break;
      default:
        break;
      }
    }
  }
}

void
jit_optimizer_optimize_pre_rewrite(struct jit_opcode_details* p_opcodes) {
  /* Pass 1: opcode merging. LSR A and similar opcodes. */
  jit_optimizer_merge_opcodes(p_opcodes);

  /* Pass 2: tag opcodes with any known register and flag values. */
  jit_optimizer_calculate_known_values(p_opcodes);

  /* Pass 3: replacements of uops with better ones if known state offers the
   * opportunity.
   * 1) Classic example is CLC; ADC. At the ADC instruction, it is known that
   * CF==0 so the ADC can become just an ADD.
   * 2) We rewrite e.g. INY to be LDY #n if the value of Y is statically known.
   * This is common for unrolled loops.
   * 3) We rewrite e.g. LDA ($3A),Y to make the "Y" addition in the address
   * calculation constant, if Y is statically known. This is common for
   * unrolled loops.
   */
  jit_optimizer_replace_uops(p_opcodes);
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
   * It's also a convenient pass to look for remaining loads of the constant
   * zero, with NZ flag setting, and replace with a dedicated opcode.
   * Furthermore, it's also a convenient pass to merge post-branch cycles
   * fixups with countdowns.
   */
  jit_optimizer_eliminate_axy_loads(p_opcodes);

  /* Pass 4: merge post-branch countdown opcodes. */
  jit_optimizer_merge_countdowns(p_opcodes);

  /* Pass 5: eliminate repeated mode loads, e.g EOR ($70),Y STA ($70),Y. */
  jit_optimizer_eliminate_mode_loads(p_opcodes);
}
