#ifndef ASM_OPCODES_H
#define ASM_OPCODES_H

#include <stdint.h>

struct asm_uop {
  int32_t uopcode;
  int32_t value1;
  int32_t value2;
  int is_eliminated;
};

enum {
  /* Misc. management opcodes, 0x100 - 0x1FF. */
  k_opcode_add_cycles = 0x100,
  k_opcode_addr_check,
  k_opcode_check_bcd,
  k_opcode_CHECK_PAGE_CROSSING_SCRATCH_Y,
  k_opcode_CHECK_PAGE_CROSSING_X_n,
  k_opcode_CHECK_PAGE_CROSSING_Y_n,
  k_opcode_check_pending_irq,
  k_opcode_countdown,
  k_opcode_debug,
  k_opcode_interp,
  k_opcode_inturbo,
  k_opcode_LOAD_CARRY_FOR_BRANCH,
  k_opcode_LOAD_CARRY_FOR_CALC,
  k_opcode_LOAD_CARRY_INV_FOR_CALC,
  k_opcode_LOAD_OVERFLOW,
  k_opcode_SAVE_CARRY,
  k_opcode_SAVE_CARRY_INV,
  k_opcode_SAVE_OVERFLOW,

  /* Addressing opcodes, 0x200 - 0x2FF. */
  k_opcode_addr_set = 0x200,
  k_opcode_addr_add_x,
  k_opcode_addr_add_y,
  k_opcode_addr_add_x_8bit,
  k_opcode_addr_add_y_8bit,
  k_opcode_addr_load_16bit_zpg,
  k_opcode_write_inv,

  /* ALU opcodes, 0x300 - 0x3FF. */
  k_opcode_value_set = 0x300,
  k_opcode_value_load,
  k_opcode_value_load_16bit,
  k_opcode_value_store,
  k_opcode_ADC,
  k_opcode_ALR,
  k_opcode_AND,
  k_opcode_ASL_acc,
  k_opcode_ASL_value,
  k_opcode_BIT,
  k_opcode_CMP,
  k_opcode_CPX,
  k_opcode_CPY,
  k_opcode_DEC_value,
  k_opcode_DEX,
  k_opcode_DEY,
  k_opcode_EOR,
  k_opcode_INC_value,
  k_opcode_INX,
  k_opcode_INY,
  k_opcode_LSR_acc,
  k_opcode_LSR_value,
  k_opcode_ORA,
  k_opcode_ROL_acc,
  k_opcode_ROL_value,
  k_opcode_ROR_acc,
  k_opcode_ROR_value,
  k_opcode_SAX,
  k_opcode_SBC,
  k_opcode_SLO,
  k_opcode_flags_nz_a,
  k_opcode_flags_nz_x,
  k_opcode_flags_nz_y,
  k_opcode_flags_nz_value,

  /* Non-ALU opcodes, 0x400 - 0x4FF. */
  k_opcode_JMP_SCRATCH_n = 0x400,
  k_opcode_PULL_16,
  k_opcode_PUSH_16,
  k_opcode_BCC,
  k_opcode_BCS,
  k_opcode_BEQ,
  k_opcode_BNE,
  k_opcode_BMI,
  k_opcode_BPL,
  k_opcode_BVC,
  k_opcode_BVS,
  k_opcode_CLC,
  k_opcode_CLD,
  k_opcode_CLI,
  k_opcode_CLV,
  k_opcode_LDA,
  k_opcode_LDX,
  k_opcode_LDY,
  k_opcode_NOP,
  k_opcode_JMP,
  k_opcode_PHA,
  k_opcode_PHP,
  k_opcode_PLA,
  k_opcode_PLP,
  k_opcode_SEC,
  k_opcode_SED,
  k_opcode_SEI,
  k_opcode_STA,
  k_opcode_STX,
  k_opcode_STY,
  k_opcode_TAX,
  k_opcode_TAY,
  k_opcode_TSX,
  k_opcode_TXS,
  k_opcode_TXA,
  k_opcode_TYA,

  /* TODO: hide the asm backend specific optimization opcodes. */
  k_opcode_MODE_IND_16 = 0x500,

  /* The asm backends can make their own opcodes from 0x1000, but these are
   * private and not exposed.
   */
};

#endif /* ASM_OPCODES_H */
