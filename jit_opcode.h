#ifndef JIT_OPCODE_H
#define JIT_OPCODE_H

struct jit_uop {
  /* Static details. */
  int32_t uopcode;
  int32_t uoptype;
  int32_t value1;
  int32_t value2;

  /* Dynamic details that are calculated as compilation proceeds. */
  uint32_t len_x64;
  int eliminated;
};

enum {
  k_max_uops_per_opcode = 16,
};

struct jit_opcode_details {
  /* Static details. */
  int internal;
  uint16_t addr_6502;
  uint8_t opcode_6502;
  uint16_t operand_6502;
  uint8_t len_bytes_6502_orig;
  uint8_t max_cycles_orig;
  int branches;

  /* Partially dynamic details that may be changed by optimization. */
  uint8_t num_uops;
  struct jit_uop uops[k_max_uops_per_opcode];

  /* Dynamic details that are calculated as compilation proceeds. */
  int ends_block;
  void* p_host_address;
  int32_t cycles_run_start;
  int32_t reg_a;
  int32_t reg_x;
  int32_t reg_y;
  int32_t flag_carry;
  int32_t flag_decimal;
  uint8_t num_fixup_uops;
  struct jit_uop* fixup_uops[k_max_uops_per_opcode];
  uint8_t len_bytes_6502_merged;
  uint8_t max_cycles_merged;
  int eliminated;
  int self_modify_invalidated;
  int dynamic_operand;
};

enum {
  k_opcode_countdown = 0x100,
  k_opcode_debug,
  k_opcode_interp,
  k_opcode_ABX_CHECK_PAGE_CROSSING,
  k_opcode_ABY_CHECK_PAGE_CROSSING,
  k_opcode_ADD_CYCLES,
  k_opcode_ADD_ABS,
  k_opcode_ADD_ABX,
  k_opcode_ADD_ABY,
  k_opcode_ADD_IMM,
  k_opcode_ADD_SCRATCH,
  k_opcode_ADD_SCRATCH_Y,
  k_opcode_ASL_ACC_n,
  k_opcode_CHECK_BCD,
  k_opcode_CHECK_PAGE_CROSSING_SCRATCH_n,
  k_opcode_CHECK_PENDING_IRQ,
  k_opcode_CLEAR_CARRY,
  k_opcode_FLAGA,
  k_opcode_FLAGX,
  k_opcode_FLAGY,
  k_opcode_FLAG_MEM,
  k_opcode_IDY_CHECK_PAGE_CROSSING,
  k_opcode_INC_SCRATCH,
  k_opcode_INVERT_CARRY,
  k_opcode_JMP_SCRATCH,
  k_opcode_LDA_SCRATCH_n,
  k_opcode_LDA_Z,
  k_opcode_LDX_Z,
  k_opcode_LDY_Z,
  k_opcode_LOAD_CARRY_FOR_BRANCH,
  k_opcode_LOAD_CARRY_FOR_CALC,
  k_opcode_LOAD_CARRY_INV_FOR_CALC,
  k_opcode_LOAD_OVERFLOW,
  k_opcode_LSR_ACC_n,
  k_opcode_MODE_ABX,
  k_opcode_MODE_ABY,
  k_opcode_MODE_IND,
  k_opcode_MODE_IND_SCRATCH,
  k_opcode_MODE_ZPX,
  k_opcode_MODE_ZPY,
  k_opcode_PULL_16,
  k_opcode_PUSH_16,
  k_opcode_ROL_ACC_n,
  k_opcode_ROR_ACC_n,
  k_opcode_SAVE_CARRY,
  k_opcode_SAVE_CARRY_INV,
  k_opcode_SAVE_OVERFLOW,
  k_opcode_SET_CARRY,
  k_opcode_STOA_IMM,
  k_opcode_SUB_ABS,
  k_opcode_SUB_IMM,
  k_opcode_WRITE_INV_ABS,
  k_opcode_WRITE_INV_SCRATCH,
  k_opcode_WRITE_INV_SCRATCH_Y,
};

#endif /* JIT_OPCODE_H */
