#include "jit_optimizer.h"

#include "defs_6502.h"
#include "jit_opcode.h"
#include "asm/asm_opcodes.h"
#include "asm/asm_util.h"

#include <assert.h>
#include <string.h>

static const int32_t k_value_unknown = -1;

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
    uint8_t opcode_6502 = p_opcode->opcode_6502;
    uint16_t operand_6502 = p_opcode->operand_6502;
    uint8_t optype = defs_6502_get_6502_optype_map()[opcode_6502];
    uint8_t opreg = g_optype_sets_register[optype];
    uint8_t opmode = defs_6502_get_6502_opmode_map()[opcode_6502];
    int changes_carry = g_optype_changes_carry[optype];

    /* TODO: seems hacky, should g_optype_sets_register just be per-opcode? */
    if (opmode == k_acc) {
      opreg = k_a;
    }

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
  for (p_opcode = p_opcodes;
       p_opcode->addr_6502 != -1;
       p_opcode += p_opcode->num_bytes_6502) {
    int32_t load_uopcode_old = -1;
    int32_t load_uopcode_new = -1;
    uint8_t load_uopcode_value = 0;
    int32_t index;
    uint8_t opcode_6502 = p_opcode->opcode_6502;
    uint8_t optype = defs_6502_get_6502_optype_map()[opcode_6502];
    struct asm_uop* p_uop;

    switch (optype) {
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

    if (load_uopcode_old != -1) {
      p_uop = jit_opcode_find_uop(p_opcode, &index, load_uopcode_old);
      assert(p_uop != NULL);
      asm_make_uop1(p_uop, k_opcode_value_set, load_uopcode_value);
      p_uop = jit_opcode_insert_uop(p_opcode, (index + 1));
      asm_make_uop0(p_uop, load_uopcode_new);
    }
  }
}

struct jit_opcode_details*
jit_optimizer_optimize(struct jit_opcode_details* p_opcodes) {
  /* Pass 1: tag opcodes with any known register and flag values. */
  jit_optimizer_calculate_known_values(p_opcodes);

  /* Pass 2: replacements of uops with better ones if known state offers the
   * opportunity.
   * Classic example is CLC; ADC. At the ADC instruction, it is known that
   * CF==0 so the ADC can become just an ADD.
   */
  jit_optimizer_replace_uops(p_opcodes);

  return NULL;
}
