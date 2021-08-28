#include "../asm_jit.h"

#include "../../defs_6502.h"
#include "../../os_alloc.h"
#include "../../util.h"
#include "../asm_common.h"
#include "../asm_jit_defs.h"
#include "../asm_opcodes.h"
#include "asm_helper_arm64.h"

#include <assert.h>

enum {
  k_opcode_arm64_addr_trunc_8bit = 0x1000,
  k_opcode_arm64_mode_ABX,
  k_opcode_arm64_mode_ABY,
  k_opcode_arm64_value_load_ABS,
  k_opcode_arm64_value_set_hi,
  k_opcode_arm64_value_store_ABS,
  k_opcode_arm64_ADC_IMM,
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
  uint8_t immr;                                                                \
  uint8_t imms;                                                                \
  if (!asm_calculate_immr_imms(&immr, &imms, value1)) assert(0);               \
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
asm_jit_supports_optimizer(void) {
  return 0;
}

int
asm_jit_supports_uopcode(int32_t uopcode) {
  int ret = 1;

  /* Some uopcodes don't make sense on ARM64 because they would be more likely
   * a pessimization than an optimization.
   * One example is STOA, which writes a constant to a memory location. ARM64
   * does not have a single instruction to do this, unlike x64. In the case of
   * STOA, keeping with the original load / store sequence avoids potentially
   * adding an extra constant load.
   */
  switch (uopcode) {
  case k_opcode_STOA_IMM:
    ret = 0;
    break;
  default:
    break;
  }

  return ret;
}

struct asm_jit_struct*
asm_jit_create(void* p_jit_base,
               int (*is_memory_always_ram)(void* p, uint16_t addr),
               void* p_memory_object) {
  (void) p_jit_base;
  (void) is_memory_always_ram;
  (void) p_memory_object;

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
  size_t mapping_size = (k_6502_addr_space_size * K_BBC_JIT_BYTES_PER_BYTE);

  (void) p_asm;

  os_alloc_make_mapping_read_write_exec((void*) (uintptr_t) K_BBC_JIT_ADDR,
                                        mapping_size);
}

void
asm_jit_finish_code_updates(struct asm_jit_struct* p_asm) {
  void* p_start = (void*) (uintptr_t) K_BBC_JIT_ADDR;
  size_t mapping_size = (k_6502_addr_space_size * K_BBC_JIT_BYTES_PER_BYTE);
  void* p_end = (p_start + mapping_size);

  (void) p_asm;

  os_alloc_make_mapping_read_exec((void*) (uintptr_t) K_BBC_JIT_ADDR,
                                  mapping_size);
  /* mprotect(), as far as I can discern, does not guarantee to clear icache
   * for PROT_EXEC mappings.
   */
  __builtin___clear_cache(p_start, p_end);
}

int
asm_jit_handle_fault(struct asm_jit_struct* p_asm,
                     uintptr_t* p_pc,
                     uint16_t addr_6502,
                     void* p_fault_addr,
                     int is_write) {
  void* p_jit_end = ((void*) K_BBC_JIT_ADDR +
                     (k_6502_addr_space_size * K_BBC_JIT_BYTES_PER_BYTE));
  /* NOTE: is_write will come in as unknown due to ARM64 kernel challenges
   * reporting this flag.
   * The way to work around this, if necessary, is to read the faulting
   * instruction to see what it is doing.
   */
  (void) addr_6502;
  (void) is_write;

  /* Currently, the only fault we expect to see is for attempts to invalidate
   * JIT code. The JIT code mapping is kept read-only.
   */
  if ((p_fault_addr < (void*) K_BBC_JIT_ADDR) || (p_fault_addr >= p_jit_end)) {
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
asm_emit_jit_value_load(struct util_buffer* p_buf, uint16_t value1) {
  if (value1 < 0x1000) {
    ASM_IMM12(value_load_ABS);
  } else {
    ASM_IMM16(value_set);
    ASM(value_load_value);
  }
}

static void
asm_emit_jit_LOAD_BYTE_PAIR(struct util_buffer* p_buf,
                            uint16_t addr1,
                            uint16_t addr2) {
  asm_emit_jit_addr_load(p_buf, addr1);
  asm_emit_jit_value_load(p_buf, addr2);
  ASM(LOAD_BYTE_PAIR_or);
}

static void
asm_emit_jit_jump_interp(struct util_buffer* p_buf, uint16_t value1) {
  ASM_IMM16(load_PC);
  ASM(jump_interp);
}

static void
asm_emit_jit_MODE_IND_16(struct util_buffer* p_buf, uint16_t addr) {
  /* Wraps around 0x10FF -> 0x1000. */
  uint16_t next_addr;
  if ((addr & 0xFF) == 0xFF) {
    next_addr = (addr & 0xFF00);
  } else {
    next_addr = (addr + 1);
  }
  asm_emit_jit_LOAD_BYTE_PAIR(p_buf, addr, next_addr);
}

void
asm_jit_rewrite(struct asm_jit_struct* p_asm,
                struct asm_uop* p_uops,
                uint32_t num_uops) {
  uint32_t i_uops;

  (void) p_asm;

  for (i_uops = 0; i_uops < num_uops; ++i_uops) {
    struct asm_uop* p_uop = &p_uops[i_uops];
    int32_t uopcode = p_uop->uopcode;
    int32_t value1 = p_uop->value1;
    struct asm_uop* p_next_uop;
    struct asm_uop* p_next_uop2;
    struct asm_uop* p_next_uop3;
    int do_eliminate_carry;
    int do_rmw_abs_store;
    int do_keep_load;
    uint8_t immr;
    uint8_t imms;

    do_eliminate_carry = 0;
    do_rmw_abs_store = 0;

    switch (uopcode) {
    case k_opcode_value_set:
      assert((i_uops + 1) < num_uops);
      i_uops++;
      p_next_uop = &p_uops[i_uops];
      switch (p_next_uop->uopcode) {
      case k_opcode_ADC:
        p_uop->uopcode = k_opcode_arm64_value_set_hi;
        p_uop->value1 = (p_uop->value1 << 8);
        p_next_uop->uopcode = k_opcode_arm64_ADC_IMM;
        break;
      case k_opcode_ALR:
        do_eliminate_carry = 1;
        break;
      case k_opcode_AND:
        if (asm_calculate_immr_imms(&immr, &imms, value1)) {
          p_uop->uopcode = k_opcode_arm64_AND_IMM;
          p_next_uop->is_eliminated = 1;
        }
        break;
      case k_opcode_CMP:
        p_uop->uopcode = k_opcode_arm64_CMP_IMM;
        p_next_uop->is_eliminated = 1;
        do_eliminate_carry = 1;
        break;
      case k_opcode_CPX:
        p_uop->uopcode = k_opcode_arm64_CPX_IMM;
        p_next_uop->is_eliminated = 1;
        do_eliminate_carry = 1;
        break;
      case k_opcode_CPY:
        p_uop->uopcode = k_opcode_arm64_CPY_IMM;
        p_next_uop->is_eliminated = 1;
        do_eliminate_carry = 1;
        break;
      case k_opcode_EOR:
        if (asm_calculate_immr_imms(&immr, &imms, value1)) {
          p_uop->uopcode = k_opcode_arm64_EOR_IMM;
          p_next_uop->is_eliminated = 1;
        }
        break;
      case k_opcode_LDA:
        p_uop->uopcode = k_opcode_arm64_LDA_IMM;
        p_next_uop->is_eliminated = 1;
        break;
      case k_opcode_LDX:
        p_uop->uopcode = k_opcode_arm64_LDX_IMM;
        p_next_uop->is_eliminated = 1;
        break;
      case k_opcode_LDY:
        p_uop->uopcode = k_opcode_arm64_LDY_IMM;
        p_next_uop->is_eliminated = 1;
        break;
      case k_opcode_NOP: break;
      case k_opcode_ORA:
        if (asm_calculate_immr_imms(&immr, &imms, value1)) {
          p_uop->uopcode = k_opcode_arm64_ORA_IMM;
          p_next_uop->is_eliminated = 1;
        }
        break;
      case k_opcode_SBC:
        p_uop->uopcode = k_opcode_arm64_value_set_hi;
        p_uop->value1 = (p_uop->value1 << 8);
        p_next_uop->uopcode = k_opcode_arm64_SBC_IMM;
        break;
      default:
        assert(0);
        break;
      }
      break;
    case k_opcode_addr_set:
      assert((i_uops + 1) < num_uops);
      i_uops++;
      p_next_uop = &p_uops[i_uops];
      do_rmw_abs_store = 0;
      do_keep_load = 0;
      switch (p_next_uop->uopcode) {
      case k_opcode_value_load:
        assert((i_uops + 1) < num_uops);
        i_uops++;
        p_next_uop2 = &p_uops[i_uops];
        switch (p_next_uop2->uopcode) {
        case k_opcode_ADC:
        case k_opcode_AND:
        case k_opcode_EOR:
        case k_opcode_ORA:
        case k_opcode_SBC:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_value_load_ABS;
          } else {
            do_keep_load = 1;
          }
          break;
        case k_opcode_ASL_value:
        case k_opcode_LSR_value:
        case k_opcode_ROL_value:
        case k_opcode_ROR_value:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_value_load_ABS;
            do_rmw_abs_store = 1;
          } else {
            do_keep_load = 1;
          }
          do_eliminate_carry = 1;
          break;
        case k_opcode_BIT:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_value_load_ABS;
          } else {
            do_keep_load = 1;
          }
          break;
        case k_opcode_CMP:
        case k_opcode_CPX:
        case k_opcode_CPY:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_value_load_ABS;
          } else {
            do_keep_load = 1;
          }
          do_eliminate_carry = 1;
          break;
        case k_opcode_DEC_value:
        case k_opcode_INC_value:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_value_load_ABS;
            do_rmw_abs_store = 1;
          } else {
            do_keep_load = 1;
          }
          break;
        case k_opcode_LDA:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_LDA_ABS;
            p_next_uop2->is_eliminated = 1;
          }
          break;
        case k_opcode_LDX:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_LDX_ABS;
            p_next_uop2->is_eliminated = 1;
          }
          break;
        case k_opcode_LDY:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_LDY_ABS;
            p_next_uop2->is_eliminated = 1;
          }
          break;
        case k_opcode_NOP: break;
        case k_opcode_SAX: break;
        case k_opcode_SLO:
          do_eliminate_carry = 1;
          do_keep_load = 1;
          break;
        case k_opcode_STA:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_STA_ABS;
            p_next_uop2->is_eliminated = 1;
          }
          break;
        case k_opcode_STX:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_STX_ABS;
            p_next_uop2->is_eliminated = 1;
          }
          break;
        case k_opcode_STY:
          if (p_uop->value1 < 0x1000) {
            p_uop->uopcode = k_opcode_arm64_STY_ABS;
            p_next_uop2->is_eliminated = 1;
          }
          break;
        default:
          assert(0);
          break;
        }
        if (!do_keep_load) {
          p_next_uop->is_eliminated = 1;
        }
        break;
      /* TODO: should be just one opcode. */
      case k_opcode_addr_load_16bit_zpg:
      case k_opcode_value_load_16bit:
        /* Mode IDY, or indirect JMP mode addressing. */
        p_uop->uopcode = k_opcode_MODE_IND_16;
        p_next_uop->is_eliminated = 1;
        break;
      case k_opcode_addr_add_x_8bit:
        /* Mode ZPX. */
        p_uop->uopcode = k_opcode_arm64_mode_ABX;
        p_next_uop->uopcode = k_opcode_arm64_addr_trunc_8bit;
        break;
      case k_opcode_addr_add_y_8bit:
        /* Mode ZPY. */
        p_uop->uopcode = k_opcode_arm64_mode_ABY;
        p_next_uop->uopcode = k_opcode_arm64_addr_trunc_8bit;
        break;
      case k_opcode_addr_add_x:
        /* Mode ABX. */
        if (p_uop->value1 < 0x1000) {
          p_uop->uopcode = k_opcode_arm64_mode_ABX;
          p_next_uop->is_eliminated = 1;
        }
        break;
      case k_opcode_addr_add_y:
        /* Mode ABY. */
        if (p_uop->value1 < 0x1000) {
          p_uop->uopcode = k_opcode_arm64_mode_ABY;
          p_next_uop->is_eliminated = 1;
        }
        break;
      default:
        assert(0);
        break;
      }
      break;
    case k_opcode_ASL_acc:
    case k_opcode_LSR_acc:
    case k_opcode_ROL_acc:
    case k_opcode_ROR_acc:
      do_eliminate_carry = 1;
      break;
    case k_opcode_value_load:
      if (p_uop->is_eliminated) {
        break;
      }
      assert((i_uops + 1) < num_uops);
      i_uops++;
      p_next_uop = &p_uops[i_uops];
      switch (p_next_uop->uopcode) {
      case k_opcode_ASL_value:
      case k_opcode_LSR_value:
      case k_opcode_ROL_value:
      case k_opcode_ROR_value:
        do_eliminate_carry = 1;
        break;
      case k_opcode_LDA:
      case k_opcode_LDX:
      case k_opcode_LDY:
      case k_opcode_STA:
      case k_opcode_STX:
      case k_opcode_STY:
        p_uop->is_eliminated = 1;
        break;
      default:
        break;
      }
      break;
    default:
      break;
    }

    if (do_eliminate_carry) {
      assert((i_uops + 1) < num_uops);
      i_uops++;
      p_next_uop = &p_uops[i_uops];
      assert((p_next_uop->uopcode == k_opcode_SAVE_CARRY_INV) ||
             (p_next_uop->uopcode == k_opcode_SAVE_CARRY));
      p_next_uop->is_eliminated = 1;
    }
    if (do_rmw_abs_store) {
      assert((i_uops + 1) < num_uops);
      i_uops++;
      p_next_uop3 = &p_uops[i_uops];
      assert(p_next_uop3->uopcode == k_opcode_value_store);
      assert(p_uop->uopcode == k_opcode_arm64_value_load_ABS);
      p_next_uop3->uopcode = k_opcode_arm64_value_store_ABS;
      p_next_uop3->value1 = p_uop->value1;
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
  uint32_t tmp;

  (void) p_asm;

  assert(uopcode >= 0x100);

  switch (uopcode) {
  case k_opcode_add_cycles: ASM_IMM12(countdown_add); break;
  case k_opcode_addr_check:
    asm_emit_jit_jump_interp(p_buf_epilog, value1);
    value1 = (uint32_t) (uintptr_t) util_buffer_get_base_address(p_buf_epilog);
    ASM(addr_check_add);
    ASM_IMM14(addr_check_tbnz);
    break;
  case k_opcode_addr_add_x: ASM(addr_add_x); break;
  case k_opcode_addr_add_y: ASM(addr_add_y); break;
  case k_opcode_addr_load_16bit_zpg: ASM(addr_load_16bit_zpg); break;
  case k_opcode_addr_set: ASM_IMM16(addr_set); break;
  case k_opcode_check_bcd:
    asm_emit_jit_jump_interp(p_buf_epilog, value1);
    value1 = (uint32_t) (uintptr_t) util_buffer_get_base_address(p_buf_epilog);
    ASM_IMM14(check_bcd);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_SCRATCH_Y:
    ASM(check_page_crossing_addr_Y);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_X_n:
    value1 = (0x100 - (value1 & 0xFF));
    ASM_IMM12(check_page_crossing_ABX_sub);
    ASM(check_page_crossing_ABX_add);
    break;
  case k_opcode_CHECK_PAGE_CROSSING_Y_n:
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
  case k_opcode_debug: ASM_IMM16(load_PC); ASM(call_debug); break;
  case k_opcode_flags_nz_a: asm_emit_instruction_A_NZ_flags(p_buf); break;
  case k_opcode_flags_nz_x: asm_emit_instruction_X_NZ_flags(p_buf); break;
  case k_opcode_flags_nz_y: asm_emit_instruction_Y_NZ_flags(p_buf); break;
  case k_opcode_flags_nz_value: ASM(flags_nz_value); break;
  case k_opcode_interp: asm_emit_jit_jump_interp(p_buf, value1); break;
  case k_opcode_JMP_SCRATCH_n:
    if (value1 != 0) {
      ASM_IMM12(addr_add);
    }
    ASM(JMP_addr);
    break;
  case k_opcode_LOAD_CARRY_FOR_BRANCH: break;
  case k_opcode_LOAD_CARRY_FOR_CALC: break;
  case k_opcode_LOAD_CARRY_INV_FOR_CALC: break;
  case k_opcode_LOAD_OVERFLOW: break;
  /* TODO: remove these two. */
  case k_opcode_MODE_ABX: break;
  case k_opcode_MODE_ABY: break;
  case k_opcode_MODE_IND_16: asm_emit_jit_MODE_IND_16(p_buf, value1); break;
  case k_opcode_PULL_16: ASM(pull_16bit); break;
  case k_opcode_PUSH_16:
    tmp = value1;
    value1 = (value1 >> 8);
    ASM_IMM16(value_set);
    ASM(push);
    value1 = (tmp & 0xFF);
    ASM_IMM16(value_set);
    ASM(push);
    break;
  case k_opcode_SAVE_CARRY: ASM(save_carry); break;
  /* TODO: "inv" carry is an Intel-ism, get rid! */
  case k_opcode_SAVE_CARRY_INV: ASM(save_carry); break;
  case k_opcode_SAVE_OVERFLOW: ASM(save_overflow); break;
  case k_opcode_value_load: ASM(value_load_addr); break;
  case k_opcode_value_set: ASM_IMM16(value_set); break;
  case k_opcode_value_store: ASM(value_store_addr); break;
  case k_opcode_WRITE_INV_ABS:
    /* TODO: can do better, depending on whether addr <= 0x1000. */
    ASM_IMM16(addr_set);
    ASM(write_inv_addr);
    break;
  case k_opcode_WRITE_INV_SCRATCH: ASM(write_inv_addr); break;
  case k_opcode_WRITE_INV_SCRATCH_Y: ASM(write_inv_addr); break;
  case k_opcode_ADC: ASM(ADC); break;
  case k_opcode_ALR: ASM(ALR); break;
  case k_opcode_AND: ASM(AND); break;
  case k_opcode_ASL_acc: ASM(ASL_ACC); break;
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
  case k_opcode_LDX: ASM(LDX); break;
  case k_opcode_LDY: ASM(LDY); break;
  case k_opcode_LSR_acc: ASM(LSR_ACC); break;
  case k_opcode_LSR_value: ASM(LSR); break;
  case k_opcode_NOP: asm_emit_instruction_REAL_NOP(p_buf); break;
  case k_opcode_ORA: ASM(ORA); break;
  case k_opcode_PHA: asm_emit_instruction_PHA(p_buf); break;
  case k_opcode_PHP: asm_emit_instruction_PHP(p_buf); break;
  case k_opcode_PLA: asm_emit_instruction_PLA(p_buf); break;
  case k_opcode_PLP: asm_emit_instruction_PLP(p_buf); break;
  case k_opcode_ROL_acc: ASM(ROL_ACC); break;
  case k_opcode_ROL_value: ASM(ROL); break;
  case k_opcode_ROR_acc: ASM(ROR_ACC); break;
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
  case k_opcode_TAX: asm_emit_instruction_TAX(p_buf); break;
  case k_opcode_TAY: asm_emit_instruction_TAY(p_buf); break;
  case k_opcode_TSX: asm_emit_instruction_TSX(p_buf); break;
  case k_opcode_TXA: asm_emit_instruction_TXA(p_buf); break;
  case k_opcode_TXS: asm_emit_instruction_TXS(p_buf); break;
  case k_opcode_TYA: asm_emit_instruction_TYA(p_buf); break;
  case k_opcode_arm64_mode_ABX: ASM_IMM12(mode_ABX); break;
  case k_opcode_arm64_mode_ABY: ASM_IMM12(mode_ABY); break;
  case k_opcode_arm64_addr_trunc_8bit: ASM(addr_trunc_8bit); break;
  case k_opcode_arm64_value_load_ABS: ASM_IMM12(value_load_ABS); break;
  case k_opcode_arm64_value_set_hi: ASM_IMM16(value_set_hi); break;
  case k_opcode_arm64_value_store_ABS: ASM_IMM12(value_store_ABS); break;
  case k_opcode_arm64_ADC_IMM: ASM(ADC_IMM); break;
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
  default:
    assert(0);
  }
}
