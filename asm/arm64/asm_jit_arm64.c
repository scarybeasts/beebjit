#include "../asm_jit.h"

#include "../../defs_6502.h"
#include "../../os_alloc.h"
#include "../../util.h"
#include "../asm_common.h"
#include "../asm_jit_defs.h"
#include "../asm_opcodes.h"
#include "../asm_util.h"
#include "asm_helper_arm64.h"

#include <assert.h>

enum {
  k_opcode_arm64_addr_load_byte = 0x1000,
  k_opcode_arm64_addr_trunc_8bit,
  k_opcode_arm64_check_page_crossing_ABX,
  k_opcode_arm64_check_page_crossing_ABY,
  k_opcode_arm64_load_byte_pair,
  k_opcode_arm64_load_byte_pair_base,
  k_opcode_arm64_mode_ABX,
  k_opcode_arm64_mode_ABY,
  k_opcode_arm64_value_load_ABS,
  k_opcode_arm64_value_set_hi,
  k_opcode_arm64_value_store_ABS,
  k_opcode_arm64_write_inv_ABS,
  k_opcode_arm64_ADC_IMM,
  k_opcode_arm64_ADD_IMM,
  k_opcode_arm64_AND_IMM,
  k_opcode_arm64_CMP_IMM,
  k_opcode_arm64_CPX_IMM,
  k_opcode_arm64_CPY_IMM,
  k_opcode_arm64_EOR_IMM,
  k_opcode_arm64_LDA_ABS,
  k_opcode_arm64_LDA_IMM,
  k_opcode_arm64_LDX_ABS,
  k_opcode_arm64_LDX_IMM,
  k_opcode_arm64_LDY_ABS,
  k_opcode_arm64_LDY_IMM,
  k_opcode_arm64_ORA_IMM,
  k_opcode_arm64_SBC_IMM,
  k_opcode_arm64_STA_ABS,
  k_opcode_arm64_STX_ABS,
  k_opcode_arm64_STY_ABS,
  k_opcode_arm64_SUB_IMM,
};

#define ASM(x)                                                                 \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy(p_buf, asm_jit_ ## x, asm_jit_ ## x ## _END);                       \
}

#define ASM_IMM12(x)                                                           \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_arm64_imm12(p_buf,                                            \
                             asm_jit_ ## x,                                    \
                             asm_jit_ ## x ## _END,                            \
                             value1);                                          \
}

#define ASM_IMM14(x)                                                           \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_arm64_imm14_pc_rel(p_buf,                                     \
                                    asm_jit_ ## x,                             \
                                    asm_jit_ ## x ## _END,                     \
                                    (void*) (uintptr_t) value1);               \
}

#define ASM_IMM16(x)                                                           \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_arm64_imm16(p_buf,                                            \
                             asm_jit_ ## x,                                    \
                             asm_jit_ ## x ## _END,                            \
                             value1);                                          \
}

#define ASM_IMM19(x)                                                           \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_arm64_imm19_pc_rel(p_buf,                                     \
                                    asm_jit_ ## x,                             \
                                    asm_jit_ ## x ## _END,                     \
                                    (void*) (uintptr_t) value1);               \
}

#define ASM_IMM26(x)                                                           \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_arm64_imm26_pc_rel(p_buf,                                     \
                                    asm_jit_ ## x,                             \
                                    asm_jit_ ## x ## _END,                     \
                                    (void*) (uintptr_t) value1);               \
}

#define ASM_IMMR_IMMS(x)                                                       \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  if (!asm_calculate_immr_imms(&immr, &imms, value1)) assert(0);               \
  asm_copy_patch_arm64_imm12(p_buf,                                            \
                             asm_jit_ ## x,                                    \
                             asm_jit_ ## x ## _END,                            \
                             ((immr << 6) | imms));                            \
}

#define ASM_IMMR_IMMS_RAW(x)                                                   \
{                                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_arm64_imm12(p_buf,                                            \
                             asm_jit_ ## x,                                    \
                             asm_jit_ ## x ## _END,                            \
                             ((immr << 6) | imms));                            \
}

int
asm_jit_is_enabled(void) {
  return 1;
}

void
asm_jit_test_preconditions(void) {
}

int
asm_jit_supports_uopcode(int32_t uopcode) {
  if (uopcode == k_opcode_ST_IMM) {
    /* ARM64 can't store immediate values, and any internal expansion would
     * just be the same as the unoptimized code. So save the complexity.
     */
    return 0;
  }
  return 1;
}

int
asm_jit_uses_indirect_mappings(void) {
  return 0;
}

struct asm_jit_struct*
asm_jit_create(void* p_jit_base,
               int (*is_memory_always_ram)(void* p, uint16_t addr),
               void* p_memory_object) {
  (void) p_jit_base;
  (void) is_memory_always_ram;
  (void) p_memory_object;

  /* Leave the JIT code pages in a read-only state. */
  asm_jit_finish_code_updates(NULL);

  return NULL;
}

void
asm_jit_destroy(struct asm_jit_struct* p_asm) {
  (void) p_asm;
}

void*
asm_jit_get_private(struct asm_jit_struct* p_asm) {
  (void) p_asm;
  return NULL;
}

void
asm_jit_start_code_updates(struct asm_jit_struct* p_asm) {
  (void) p_asm;
  os_alloc_make_mapping_read_write_exec((void*) K_JIT_ADDR, K_JIT_SIZE);
}

void
asm_jit_finish_code_updates(struct asm_jit_struct* p_asm) {
  (void) p_asm;

  os_alloc_make_mapping_read_exec((void*) K_JIT_ADDR, K_JIT_SIZE);
  /* mprotect(), as far as I can discern, does not guarantee to clear icache
   * for PROT_EXEC mappings.
   */
  __builtin___clear_cache((void*) K_JIT_ADDR, (void*) K_JIT_ADDR_END);
}

int
asm_jit_handle_fault(struct asm_jit_struct* p_asm,
                     uintptr_t* p_pc,
                     int is_inturbo,
                     int32_t addr_6502,
                     void* p_fault_addr,
                     int is_write) {
  /* NOTE: is_write will come in as unknown due to ARM64 kernel challenges
   * reporting this flag.
   * The way to work around this, if necessary, is to read the faulting
   * instruction to see what it is doing.
   */
  (void) is_inturbo;
  (void) addr_6502;
  (void) is_write;

  /* Currently, the only fault we expect to see is for attempts to invalidate
   * JIT code. The JIT code mapping is kept read-only.
   */
  if ((p_fault_addr < (void*) K_JIT_ADDR) ||
      (p_fault_addr >= (void*) K_JIT_ADDR_END)) {
    return 0;
  }

  /* TODO: if we keep this model of faulting for self-modification, we'll
   * likely want to twiddle just the affected page. Currently, we twiddle the
   * whole mapping.
   */
  asm_jit_start_code_updates(p_asm);
  asm_jit_invalidate_code_at(p_fault_addr);
  asm_jit_finish_code_updates(p_asm);

  /* Skip over the write invalidation instruction. */
  *p_pc += 4;

  return 1;
}

void
asm_jit_invalidate_code_at(void* p) {
  uint32_t* p_dst = (uint32_t*) p;
  /* blr x29 */
  *p_dst = 0xd63f03a0;
}

int
asm_jit_is_invalidated_code_at(void* p) {
  uint32_t code = *(uint32_t*) p;
  return (code == 0xd63f03a0);
}

static void
asm_emit_jit_addr_load(struct util_buffer* p_buf, uint16_t value1) {
  if (value1 < 0x1000) {
    ASM_IMM12(addr_load);
  } else {
    ASM_IMM16(addr_set);
    ASM(addr_load_addr);
  }
}

static void
asm_emit_jit_addr_hi_load(struct util_buffer* p_buf, uint16_t value1) {
  if (value1 < 0x1000) {
    ASM_IMM12(scratch_load_ABS);
  } else {
    ASM_IMM16(scratch_set);
    ASM(scratch_load_scratch);
  }
}

static void
asm_emit_jit_jump_interp(struct util_buffer* p_buf, uint16_t addr_6502) {
  void asm_jit_call_interp(void);
  uint32_t value1 = addr_6502;
  ASM_IMM16(load_PC);
  value1 = (uint32_t) (uintptr_t) asm_jit_call_interp;
  ASM_IMM26(jump_interp);
}

void
asm_jit_rewrite(struct asm_jit_struct* p_asm,
                struct asm_uop* p_uops,
                uint32_t num_uops) {
  struct asm_uop* p_main_uop;
  struct asm_uop* p_mode_uop;
  struct asm_uop* p_load_uop;
  struct asm_uop* p_store_uop;
  struct asm_uop* p_load_carry_uop;
  struct asm_uop* p_save_carry_uop;
  struct asm_uop* p_load_overflow_uop;
  struct asm_uop* p_flags_uop;
  struct asm_uop* p_inv_uop;
  struct asm_uop* p_addr_check_uop;
  struct asm_uop* p_page_crossing_uop;
  struct asm_uop* p_tmp_uop;
  int32_t uopcode;
  int32_t value1;
  uint8_t immr;
  uint8_t imms;
  int is_imm;
  int is_abs;
  uint16_t addr;

  (void) p_asm;

  asm_breakdown_from_6502(p_uops,
                          num_uops,
                          &p_main_uop,
                          &p_mode_uop,
                          &p_load_uop,
                          &p_store_uop,
                          &p_load_carry_uop,
                          &p_save_carry_uop,
                          &p_load_overflow_uop,
                          &p_flags_uop,
                          &p_inv_uop,
                          &p_addr_check_uop,
                          &p_page_crossing_uop);
  if (p_main_uop == NULL) {
    return;
  }

  /* Some ARM64 operations have built-in carry save management. */
  switch (p_main_uop->uopcode) {
  case k_opcode_ALR:
  case k_opcode_ASL_acc:
  case k_opcode_ASL_value:
  case k_opcode_CMP:
  case k_opcode_CPX:
  case k_opcode_CPY:
  case k_opcode_LSR_acc:
  case k_opcode_LSR_value:
  case k_opcode_PLP:
  case k_opcode_ROL_acc:
  case k_opcode_ROL_value:
  case k_opcode_ROR_acc:
  case k_opcode_ROR_value:
  case k_opcode_SLO:
    assert(p_save_carry_uop != NULL);
    p_save_carry_uop->is_eliminated = 1;
    p_save_carry_uop->is_merged = 1;
    break;
  default:
    break;
  }

  /* All curent operations have built-in carry load management. That is to say,
   * nothing takes the host carry flag as input.
   */
  if (p_load_carry_uop != NULL) {
    /* A transform such as ADC -> ADD will remove the load carry altogether,
     * so don't mark it as merged.
     */
    if (!p_load_carry_uop->is_eliminated) {
      p_load_carry_uop->is_eliminated = 1;
      p_load_carry_uop->is_merged = 1;
    }
  }

  /* ADD / SUB (but not ADC / SBC!) operations set the NZ flags
   * automatically.
   */
  switch (p_main_uop->uopcode) {
  case k_opcode_ADD:
  case k_opcode_CMP:
  case k_opcode_CPX:
  case k_opcode_CPY:
  case k_opcode_SUB:
    assert(p_flags_uop != NULL);
    p_flags_uop->is_eliminated = 1;
    p_flags_uop->is_merged = 1;
    break;
  default:
    break;
  }

  if (p_mode_uop == NULL) {
    return;
  }

  is_imm = 0;
  is_abs = 0;
  addr = 0;

  uopcode = p_mode_uop->uopcode;
  if ((uopcode == k_opcode_addr_add_x) || (uopcode == k_opcode_addr_add_y)) {
    p_tmp_uop = (p_mode_uop - 1);
    if (p_tmp_uop->uopcode == k_opcode_addr_load_16bit_nowrap) {
      /* Back up for dyanmic operand load. */
      p_mode_uop = p_tmp_uop;
    }
  }

  uopcode = p_mode_uop->uopcode;
  value1 = p_mode_uop->value1;
  switch (uopcode) {
  case k_opcode_value_set:
    /* Mode IMM. */
    assert(p_main_uop != NULL);
    is_imm = 1;
    switch (p_main_uop->uopcode) {
    case k_opcode_ADC:
      p_mode_uop->backend_tag = k_opcode_arm64_value_set_hi;
      p_mode_uop->value1 = (value1 << 8);
      p_main_uop->backend_tag = k_opcode_arm64_ADC_IMM;
      break;
    case k_opcode_ADD:
      p_mode_uop->backend_tag = k_opcode_arm64_value_set_hi;
      p_mode_uop->value1 = (value1 << 8);
      p_main_uop->backend_tag = k_opcode_arm64_ADD_IMM;
      break;
    case k_opcode_ALR: break;
    case k_opcode_AND:
      if (asm_calculate_immr_imms(&immr, &imms, value1)) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_AND_IMM;
        p_main_uop->value1 = value1;
      }
      break;
    case k_opcode_CMP:
      p_mode_uop->is_eliminated = 1;
      p_main_uop->backend_tag = k_opcode_arm64_CMP_IMM;
      p_main_uop->value1 = value1;
      break;
    case k_opcode_CPX:
      p_mode_uop->is_eliminated = 1;
      p_main_uop->backend_tag = k_opcode_arm64_CPX_IMM;
      p_main_uop->value1 = value1;
      break;
    case k_opcode_CPY:
      p_mode_uop->is_eliminated = 1;
      p_main_uop->backend_tag = k_opcode_arm64_CPY_IMM;
      p_main_uop->value1 = value1;
      break;
    case k_opcode_EOR:
      if (asm_calculate_immr_imms(&immr, &imms, value1)) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_EOR_IMM;
        p_main_uop->value1 = value1;
      }
      break;
    case k_opcode_LDA:
      p_mode_uop->is_eliminated = 1;
      p_main_uop->backend_tag = k_opcode_arm64_LDA_IMM;
      p_main_uop->value1 = value1;
      break;
    case k_opcode_LDX:
      p_mode_uop->is_eliminated = 1;
      p_main_uop->backend_tag = k_opcode_arm64_LDX_IMM;
      p_main_uop->value1 = value1;
      break;
    case k_opcode_LDY:
      p_mode_uop->is_eliminated = 1;
      p_main_uop->backend_tag = k_opcode_arm64_LDY_IMM;
      p_main_uop->value1 = value1;
      break;
    case k_opcode_NOP: break;
    case k_opcode_ORA:
      if (asm_calculate_immr_imms(&immr, &imms, value1)) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_ORA_IMM;
        p_main_uop->value1 = value1;
      }
      break;
    case k_opcode_SBC:
      p_mode_uop->backend_tag = k_opcode_arm64_value_set_hi;
      p_mode_uop->value1 = (value1 << 8);
      p_main_uop->backend_tag = k_opcode_arm64_SBC_IMM;
      break;
    case k_opcode_SUB:
      p_mode_uop->backend_tag = k_opcode_arm64_value_set_hi;
      p_mode_uop->value1 = (value1 << 8);
      p_main_uop->backend_tag = k_opcode_arm64_SUB_IMM;
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_opcode_addr_set:
    /* Mode ABS or ZPG. */
    is_abs = 1;
    addr = value1;
    switch (p_main_uop->uopcode) {
    case k_opcode_ADC:
    case k_opcode_ADD:
    case k_opcode_AND:
    case k_opcode_ASL_value:
    case k_opcode_BIT:
    case k_opcode_CMP:
    case k_opcode_CPX:
    case k_opcode_CPY:
    case k_opcode_EOR:
    case k_opcode_DEC_value:
    case k_opcode_INC_value:
    case k_opcode_LSR_value:
    case k_opcode_NOP:
    case k_opcode_ORA:
    case k_opcode_ROL_value:
    case k_opcode_ROR_value:
    case k_opcode_SAX:
    case k_opcode_SBC:
    case k_opcode_SLO:
    case k_opcode_SUB:
      if (addr < 0x1000) {
        p_mode_uop->is_eliminated = 1;
        if (p_load_uop != NULL) {
          p_load_uop->backend_tag = k_opcode_arm64_value_load_ABS;
          p_load_uop->value1 = addr;
        }
        if (p_store_uop != NULL) {
          p_store_uop->backend_tag = k_opcode_arm64_value_store_ABS;
          p_store_uop->value1 = addr;
        }
      }
      break;
    case k_opcode_LDA:
      if (addr < 0x1000) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_LDA_ABS;
        p_main_uop->value1 = addr;
      }
      break;
    case k_opcode_LDX:
      if (addr < 0x1000) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_LDX_ABS;
        p_main_uop->value1 = addr;
      }
      break;
    case k_opcode_LDY:
      if (addr < 0x1000) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_LDY_ABS;
        p_main_uop->value1 = addr;
      }
      break;
    case k_opcode_STA:
      if (addr < 0x1000) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_STA_ABS;
        p_main_uop->value1 = addr;
      }
      break;
    case k_opcode_STX:
      if (addr < 0x1000) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_STX_ABS;
        p_main_uop->value1 = addr;
      }
      break;
    case k_opcode_STY:
      if (addr < 0x1000) {
        p_mode_uop->is_eliminated = 1;
        p_main_uop->backend_tag = k_opcode_arm64_STY_ABS;
        p_main_uop->value1 = addr;
      }
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_opcode_value_load_16bit_wrap:
    /* Mode IND JMP mode addressing. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    addr = p_mode_uop->value1;
    p_mode_uop->is_eliminated = 1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_arm64_load_byte_pair;
    p_mode_uop->value1 = addr;
    /* Wraps around 0x10FF -> 0x1000. */
    addr = (addr + 1);
    if (!(addr & 0xFF)) addr -= 0x100;
    p_mode_uop->value2 = addr;
    break;
  case k_opcode_addr_load_16bit_nowrap:
    /* Fetch for 16-bit dynamic operand. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    addr = p_mode_uop->value1;
    p_mode_uop->is_eliminated = 1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_arm64_load_byte_pair;
    p_mode_uop->value1 = addr;
    p_mode_uop->value2 = (uint16_t) (p_mode_uop->value1 + 1);
    break;
  case k_opcode_addr_load_8bit:
    /* Fetch for 8-bit dynamic operand (mode ZPG). */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    addr = p_mode_uop->value1;
    p_mode_uop->is_eliminated = 1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_arm64_addr_load_byte;
    p_mode_uop->value1 = addr;
    break;
  case k_opcode_addr_add_x_8bit:
    /* Mode ZPX. */
    p_mode_uop->backend_tag = k_opcode_arm64_addr_trunc_8bit;
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->backend_tag = k_opcode_arm64_mode_ABX;
    break;
  case k_opcode_addr_add_y_8bit:
    /* Mode ZPY. */
    p_mode_uop->backend_tag = k_opcode_arm64_addr_trunc_8bit;
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->backend_tag = k_opcode_arm64_mode_ABY;
    break;
  case k_opcode_addr_add_x:
    /* Mode ABX. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    addr = p_mode_uop->value1;
    if (addr < 0x1000) {
      p_mode_uop->is_eliminated = 1;
      p_mode_uop++;
      p_mode_uop->backend_tag = k_opcode_arm64_mode_ABX;
      p_mode_uop->value1 = addr;
    }
    if (p_page_crossing_uop != NULL) {
      p_page_crossing_uop->backend_tag = k_opcode_arm64_check_page_crossing_ABX;
      p_page_crossing_uop->value1 = addr;
    }
    break;
  case k_opcode_addr_add_y:
    /* Mode ABY. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    addr = p_mode_uop->value1;
    if (addr < 0x1000) {
      p_mode_uop->is_eliminated = 1;
      p_mode_uop++;
      p_mode_uop->backend_tag = k_opcode_arm64_mode_ABY;
      p_mode_uop->value1 = addr;
    }
    if (p_page_crossing_uop != NULL) {
      p_page_crossing_uop->backend_tag = k_opcode_arm64_check_page_crossing_ABY;
      p_page_crossing_uop->value1 = addr;
    }
    break;
  case k_opcode_addr_load_16bit_wrap:
    /* Mode IDX. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_add_x_8bit);
    p_mode_uop->backend_tag = k_opcode_arm64_addr_trunc_8bit;
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->backend_tag = k_opcode_arm64_mode_ABX;
    break;
  case k_opcode_addr_add_base_y:
  case k_opcode_addr_add_base_constant:
    /* Mode IDY, Y known or unknown. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_base_load_16bit_wrap);
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    addr = p_mode_uop->value1;
    p_mode_uop->is_eliminated = 1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_arm64_load_byte_pair_base;
    p_mode_uop->value1 = addr;
    p_mode_uop->value2 = (uint8_t) (addr + 1);
    break;
  default:
    assert(0);
    break;
  }

  /* Loads and stores have the load/store built in to the operation. */
  if (!is_imm) {
    switch (p_main_uop->uopcode) {
    case k_opcode_LDA:
    case k_opcode_LDX:
    case k_opcode_LDY:
      assert(p_load_uop != NULL);
      p_load_uop->is_eliminated = 1;
      break;
    case k_opcode_STA:
    case k_opcode_STX:
    case k_opcode_STY:
      assert(p_store_uop != NULL);
      p_store_uop->is_eliminated = 1;
      break;
    default:
      break;
    }
  }

  if (p_inv_uop != NULL) {
    if (is_abs && (addr < 0x1000)) {
      p_inv_uop->backend_tag = k_opcode_arm64_write_inv_ABS;
      p_inv_uop->value1 = addr;
    }
  }
}

void
asm_emit_jit_invalidated(struct util_buffer* p_buf) {
  /* blr x29 */
  util_buffer_add_4b(p_buf, 0xa0, 0x03, 0x3f, 0xd6);
}

void
asm_emit_jit(struct asm_jit_struct* p_asm,
             struct util_buffer* p_buf,
             struct util_buffer* p_buf_epilog,
             struct asm_uop* p_uop) {
  int32_t uopcode = p_uop->uopcode;
  uint32_t value1 = p_uop->value1;
  uint32_t value2 = p_uop->value2;
  uint8_t immr = 0;
  uint8_t imms = 0;
  uint32_t i;
  uint32_t tmp;

  (void) p_asm;

  if (p_uop->backend_tag >= 0x1000) {
    uopcode = p_uop->backend_tag;
  }
  assert(uopcode >= 0x100);

  switch (uopcode) {
  case k_opcode_add_cycles: ASM_IMM12(countdown_add); break;
  case k_opcode_addr_check:
    asm_emit_jit_jump_interp(p_buf_epilog, value1);
    value1 = (uint32_t) (uintptr_t) util_buffer_get_base_address(p_buf_epilog);
    ASM(addr_check_add);
    ASM_IMM14(addr_check_tbnz);
    break;
  case k_opcode_addr_add_base_constant:
    ASM_IMM12(addr_add_base_constant);
    break;
  case k_opcode_addr_add_base_y: ASM(addr_add_base_y); break;
  case k_opcode_addr_add_x: ASM(addr_add_x); break;
  case k_opcode_addr_add_y: ASM(addr_add_y); break;
  case k_opcode_addr_load_16bit_wrap: ASM(addr_load_16bit_wrap); break;
  case k_opcode_addr_set: ASM_IMM16(addr_set); break;
  case k_opcode_check_bcd:
    asm_emit_jit_jump_interp(p_buf_epilog, value1);
    value1 = (uint32_t) (uintptr_t) util_buffer_get_base_address(p_buf_epilog);
    ASM_IMM14(check_bcd);
    break;
  case k_opcode_check_page_crossing_n: ASM(check_page_crossing_n); break;
  case k_opcode_check_page_crossing_x: ASM(check_page_crossing_x); break;
  case k_opcode_check_page_crossing_y: ASM(check_page_crossing_y); break;
  case k_opcode_arm64_check_page_crossing_ABX:
    value1 = (0x100 - (value1 & 0xFF));
    ASM_IMM12(check_page_crossing_ABX_sub);
    ASM(check_page_crossing_ABX_add);
    break;
  case k_opcode_arm64_check_page_crossing_ABY:
    value1 = (0x100 - (value1 & 0xFF));
    ASM_IMM12(check_page_crossing_ABY_sub);
    ASM(check_page_crossing_ABY_add);
    break;
  case k_opcode_check_pending_irq:
    asm_emit_jit_jump_interp(p_buf_epilog, value1);
    ASM(check_pending_irq_load);
    value1 = (uint32_t) (uintptr_t) util_buffer_get_base_address(p_buf_epilog);
    ASM_IMM19(check_pending_irq_cbnz);
    break;
  case k_opcode_countdown:
    asm_emit_jit_jump_interp(p_buf_epilog, value1);
    value1 = value2;
    ASM_IMM12(countdown_sub);
    value1 = (uint32_t) (uintptr_t) util_buffer_get_base_address(p_buf_epilog);
    ASM_IMM14(countdown_tbnz);
    break;
  case k_opcode_debug:
    ASM_IMM16(load_PC);
    value1 = (uint32_t) (uintptr_t) asm_debug;
    ASM_IMM26(call_debug);
    break;
  case k_opcode_flags_nz_a: asm_emit_instruction_A_NZ_flags(p_buf); break;
  case k_opcode_flags_nz_x: asm_emit_instruction_X_NZ_flags(p_buf); break;
  case k_opcode_flags_nz_y: asm_emit_instruction_Y_NZ_flags(p_buf); break;
  case k_opcode_flags_nz_value: ASM(flags_nz_value); break;
  case k_opcode_interp: asm_emit_jit_jump_interp(p_buf, value1); break;
  case k_opcode_inturbo: ASM_IMM16(load_PC); ASM(call_inturbo); break;
  case k_opcode_JMP_SCRATCH_n:
    if (value1 != 0) {
      ASM_IMM12(addr_add);
    }
    ASM(JMP_addr);
    break;
  case k_opcode_load_carry: break;
  case k_opcode_load_overflow: break;
  case k_opcode_PULL_16: ASM(pull_16bit); break;
  case k_opcode_PUSH_16:
    tmp = value1;
    value1 = (value1 >> 8);
    ASM_IMM16(scratch_set);
    ASM(push);
    value1 = (tmp & 0xFF);
    ASM_IMM16(scratch_set);
    ASM(push);
    break;
  case k_opcode_save_carry: ASM(save_carry); break;
  case k_opcode_save_overflow: ASM(save_overflow); break;
  case k_opcode_value_load: ASM(value_load_addr); break;
  case k_opcode_value_set: ASM_IMM16(value_set); break;
  case k_opcode_value_store: ASM(value_store_addr); break;
  case k_opcode_write_inv: ASM(write_inv); break;
  case k_opcode_ADC: ASM(ADC); break;
  case k_opcode_ADD: ASM(ADD); break;
  case k_opcode_ALR: ASM(ALR); break;
  case k_opcode_AND: ASM(AND); break;
  case k_opcode_ASL_acc:
    immr = (8 - value1);
    imms = (8 - value1);
    ASM_IMMR_IMMS_RAW(ASL_ACC_ubfm_carry);
    immr = (64 - value1);
    imms = (7 - value1);
    ASM_IMMR_IMMS_RAW(ASL_ACC_ubfm_shift);
    break;
  case k_opcode_ASL_value: ASM(ASL); break;
  case k_opcode_BCC: ASM_IMM19(BCC); break;
  case k_opcode_BCS: ASM_IMM19(BCS); break;
  case k_opcode_BEQ: ASM_IMM19(BEQ); break;
  case k_opcode_BIT: asm_emit_instruction_BIT_value(p_buf); break;
  case k_opcode_BMI: ASM_IMM19(BMI); break;
  case k_opcode_BNE: ASM_IMM19(BNE); break;
  case k_opcode_BPL: ASM_IMM19(BPL); break;
  case k_opcode_BVC: ASM_IMM19(BVC); break;
  case k_opcode_BVS: ASM_IMM19(BVS); break;
  case k_opcode_CLC: asm_emit_instruction_CLC(p_buf); break;
  case k_opcode_CLD: asm_emit_instruction_CLD(p_buf); break;
  case k_opcode_CLI: asm_emit_instruction_CLI(p_buf); break;
  case k_opcode_CLV: asm_emit_instruction_CLV(p_buf); break;
  case k_opcode_CMP: ASM(CMP); break;
  case k_opcode_CPX: ASM(CPX); break;
  case k_opcode_CPY: ASM(CPY); break;
  case k_opcode_DEC_value: ASM(DEC); break;
  case k_opcode_DEX: asm_emit_instruction_DEX(p_buf); break;
  case k_opcode_DEY: asm_emit_instruction_DEY(p_buf); break;
  case k_opcode_EOR: ASM(EOR); break;
  case k_opcode_INC_value: ASM(INC); break;
  case k_opcode_INX: asm_emit_instruction_INX(p_buf); break;
  case k_opcode_INY: asm_emit_instruction_INY(p_buf); break;
  case k_opcode_JMP: ASM_IMM26(JMP); break;
  case k_opcode_LDA: ASM(LDA); break;
  case k_opcode_LDA_zero_and_flags: ASM(LDA_zero_and_flags); break;
  case k_opcode_LDX: ASM(LDX); break;
  case k_opcode_LDX_zero_and_flags: ASM(LDX_zero_and_flags); break;
  case k_opcode_LDY: ASM(LDY); break;
  case k_opcode_LDY_zero_and_flags: ASM(LDY_zero_and_flags); break;
  case k_opcode_LSR_acc:
    immr = (value1 - 1);
    imms = 0;
    ASM_IMMR_IMMS_RAW(LSR_ACC_ubfm_carry);
    immr = value1;
    imms = 7;
    ASM_IMMR_IMMS_RAW(LSR_ACC_ubfm_shift);
    break;
  case k_opcode_LSR_value: ASM(LSR); break;
  case k_opcode_NOP: asm_emit_instruction_REAL_NOP(p_buf); break;
  case k_opcode_ORA: ASM(ORA); break;
  case k_opcode_PHA: asm_emit_instruction_PHA(p_buf); break;
  case k_opcode_PHP: asm_emit_instruction_PHP(p_buf); break;
  case k_opcode_PLA: asm_emit_instruction_PLA(p_buf); break;
  case k_opcode_PLP: asm_emit_instruction_PLP(p_buf); break;
  case k_opcode_ROL_acc:
    /* TODO: better optimize ROL_acc and ROR_acc. */
    for (i = 0; i < value1; ++i) {
      ASM(ROL_ACC);
    }
    break;
  case k_opcode_ROL_value: ASM(ROL); break;
  case k_opcode_ROR_acc:
    for (i = 0; i < value1; ++i) {
      ASM(ROR_ACC);
    }
    break;
  case k_opcode_ROR_value: ASM(ROR); break;
  case k_opcode_SAX: ASM(SAX); break;
  case k_opcode_SBC: ASM(SBC); break;
  case k_opcode_SEC: asm_emit_instruction_SEC(p_buf); break;
  case k_opcode_SED: asm_emit_instruction_SED(p_buf); break;
  case k_opcode_SEI: asm_emit_instruction_SEI(p_buf); break;
  case k_opcode_SLO: ASM(SLO); break;
  case k_opcode_STA: ASM(STA); break;
  case k_opcode_STX: ASM(STX); break;
  case k_opcode_STY: ASM(STY); break;
  case k_opcode_SUB: ASM(SUB); break;
  case k_opcode_TAX: asm_emit_instruction_TAX(p_buf); break;
  case k_opcode_TAY: asm_emit_instruction_TAY(p_buf); break;
  case k_opcode_TSX: asm_emit_instruction_TSX(p_buf); break;
  case k_opcode_TXA: asm_emit_instruction_TXA(p_buf); break;
  case k_opcode_TXS: asm_emit_instruction_TXS(p_buf); break;
  case k_opcode_TYA: asm_emit_instruction_TYA(p_buf); break;
  case k_opcode_arm64_addr_load_byte: asm_emit_jit_addr_load(p_buf, value1); break;
  case k_opcode_arm64_addr_trunc_8bit: ASM(addr_trunc_8bit); break;
  case k_opcode_arm64_load_byte_pair:
    asm_emit_jit_addr_load(p_buf, value1);
    asm_emit_jit_addr_hi_load(p_buf, value2);
    ASM(load_byte_pair_or);
    break;
  case k_opcode_arm64_load_byte_pair_base:
    if (value1 < 0x1000) {
      ASM_IMM12(addr_load_base);
    } else {
      ASM_IMM16(addr_set_base);
      ASM(addr_base_load_addr_base);
    }
    asm_emit_jit_addr_hi_load(p_buf, value2);
    ASM(load_byte_pair_base_or);
    break;
  case k_opcode_arm64_mode_ABX: ASM_IMM12(mode_ABX); break;
  case k_opcode_arm64_mode_ABY: ASM_IMM12(mode_ABY); break;
  case k_opcode_arm64_value_load_ABS: ASM_IMM12(value_load_ABS); break;
  case k_opcode_arm64_value_set_hi: ASM_IMM16(value_set_hi); break;
  case k_opcode_arm64_value_store_ABS: ASM_IMM12(value_store_ABS); break;
  case k_opcode_arm64_write_inv_ABS:
    ASM_IMM12(write_inv_ABS_load);
    ASM(write_inv_ABS_store);
    break;
  case k_opcode_arm64_ADC_IMM: ASM(ADC_IMM); break;
  case k_opcode_arm64_ADD_IMM: ASM(ADD_IMM); break;
  case k_opcode_arm64_AND_IMM: ASM_IMMR_IMMS(AND_IMM); break;
  case k_opcode_arm64_CMP_IMM:
    ASM_IMM12(CMP_IMM_subs);
    ASM(CMP_IMM_flags);
    break;
  case k_opcode_arm64_CPX_IMM:
    ASM_IMM12(CPX_IMM_subs);
    ASM(CPX_IMM_flags);
    break;
  case k_opcode_arm64_CPY_IMM:
    ASM_IMM12(CPY_IMM_subs);
    ASM(CPY_IMM_flags);
    break;
  case k_opcode_arm64_EOR_IMM: ASM_IMMR_IMMS(EOR_IMM); break;
  case k_opcode_arm64_LDA_ABS: ASM_IMM12(LDA_ABS); break;
  case k_opcode_arm64_LDA_IMM: ASM_IMM16(LDA_IMM); break;
  case k_opcode_arm64_LDX_ABS: ASM_IMM12(LDX_ABS); break;
  case k_opcode_arm64_LDX_IMM: ASM_IMM16(LDX_IMM); break;
  case k_opcode_arm64_LDY_IMM: ASM_IMM16(LDY_IMM); break;
  case k_opcode_arm64_LDY_ABS: ASM_IMM12(LDY_ABS); break;
  case k_opcode_arm64_ORA_IMM: ASM_IMMR_IMMS(ORA_IMM); break;
  case k_opcode_arm64_SBC_IMM: ASM(SBC_IMM); break;
  case k_opcode_arm64_STA_ABS: ASM_IMM12(STA_ABS); break;
  case k_opcode_arm64_STX_ABS: ASM_IMM12(STX_ABS); break;
  case k_opcode_arm64_STY_ABS: ASM_IMM12(STY_ABS); break;
  case k_opcode_arm64_SUB_IMM: ASM(SUB_IMM); break;
  default:
    assert(0);
  }
}
