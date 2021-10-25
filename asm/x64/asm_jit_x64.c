#include "../asm_jit.h"

#include "../../defs_6502.h"
#include "../../os_alloc.h"
#include "../../util.h"
#include "../asm_common.h"
#include "../asm_defs_host.h"
#include "../asm_jit_defs.h"
#include "../asm_opcodes.h"
#include "../asm_util.h"
/* For REG_MEM_OFFSET. */
#include "asm_defs_registers_x64.h"

#include <assert.h>

/* TODO: restore these optimizations internally to the x64 asm backend. */
enum {
  k_opcode_LDA_Z,
  k_opcode_LDX_Z,
  k_opcode_LDY_Z,
  k_opcode_STOA_IMM,
  k_opcode_CLEAR_CARRY,
  k_opcode_INVERT_CARRY,
  k_opcode_SET_CARRY,
};

enum {
  k_opcode_x64_check_page_crossing_ABX = 0x1000,
  k_opcode_x64_check_page_crossing_ABY,
  k_opcode_x64_check_page_crossing_IDY,
  k_opcode_x64_check_page_crossing_IDY_X,
  k_opcode_x64_load_ABS,
  k_opcode_x64_load_ZPG,
  k_opcode_x64_mode_ABX_and_load,
  k_opcode_x64_mode_ABX_store,
  k_opcode_x64_mode_IDY_load,
  k_opcode_x64_mode_IND,
  k_opcode_x64_mode_IND_nowrap,
  k_opcode_x64_mode_IND8,
  k_opcode_x64_mode_ZPX,
  k_opcode_x64_mode_ZPY,
  k_opcode_x64_store_ABS,
  k_opcode_x64_store_ZPG,
  k_opcode_x64_write_inv_ABS,
  k_opcode_x64_write_inv_ABX,
  k_opcode_x64_write_inv_ABY,
  k_opcode_x64_write_inv_IDY,
  k_opcode_x64_write_inv_IDY_n,
  k_opcode_x64_write_inv_IDY_X,
  k_opcode_x64_ADC_ABS,
  k_opcode_x64_ADC_ABX,
  k_opcode_x64_ADC_ABY,
  k_opcode_x64_ADC_addr,
  k_opcode_x64_ADC_addr_n,
  k_opcode_x64_ADC_addr_X,
  k_opcode_x64_ADC_addr_Y,
  k_opcode_x64_ADC_IMM,
  k_opcode_x64_ADC_ZPG,
  k_opcode_x64_ADD_ABS,
  k_opcode_x64_ADD_ABX,
  k_opcode_x64_ADD_ABY,
  k_opcode_x64_ADD_addr,
  k_opcode_x64_ADD_addr_n,
  k_opcode_x64_ADD_addr_X,
  k_opcode_x64_ADD_addr_Y,
  k_opcode_x64_ADD_IMM,
  k_opcode_x64_ADD_ZPG,
  k_opcode_x64_ALR_IMM,
  k_opcode_x64_AND_ABS,
  k_opcode_x64_AND_ABX,
  k_opcode_x64_AND_ABY,
  k_opcode_x64_AND_addr,
  k_opcode_x64_AND_addr_n,
  k_opcode_x64_AND_addr_X,
  k_opcode_x64_AND_addr_Y,
  k_opcode_x64_AND_ZPG,
  k_opcode_x64_AND_IMM,
  k_opcode_x64_ASL_ABS,
  k_opcode_x64_ASL_ACC_n,
  k_opcode_x64_ASL_ZPG,
  k_opcode_x64_CMP_ABS,
  k_opcode_x64_CMP_ABX,
  k_opcode_x64_CMP_ABY,
  k_opcode_x64_CMP_addr,
  k_opcode_x64_CMP_addr_n,
  k_opcode_x64_CMP_addr_X,
  k_opcode_x64_CMP_addr_Y,
  k_opcode_x64_CMP_IMM,
  k_opcode_x64_CMP_ZPG,
  k_opcode_x64_CPX_ABS,
  k_opcode_x64_CPX_IMM,
  k_opcode_x64_CPX_ZPG,
  k_opcode_x64_CPY_ABS,
  k_opcode_x64_CPY_IMM,
  k_opcode_x64_CPY_ZPG,
  k_opcode_x64_DEC_ABS,
  k_opcode_x64_DEC_ZPG,
  k_opcode_x64_EOR_ABS,
  k_opcode_x64_EOR_ABX,
  k_opcode_x64_EOR_ABY,
  k_opcode_x64_EOR_addr,
  k_opcode_x64_EOR_addr_n,
  k_opcode_x64_EOR_addr_X,
  k_opcode_x64_EOR_addr_Y,
  k_opcode_x64_EOR_IMM,
  k_opcode_x64_EOR_ZPG,
  k_opcode_x64_INC_ABS,
  k_opcode_x64_INC_ZPG,
  k_opcode_x64_LDA_addr,
  k_opcode_x64_LDA_addr_n,
  k_opcode_x64_LDA_addr_X,
  k_opcode_x64_LDA_addr_Y,
  k_opcode_x64_LDA_ABS,
  k_opcode_x64_LDA_ABX,
  k_opcode_x64_LDA_ABY,
  k_opcode_x64_LDA_IMM,
  k_opcode_x64_LDA_ZPG,
  k_opcode_x64_LDX_addr,
  k_opcode_x64_LDX_ABS,
  k_opcode_x64_LDX_ABY,
  k_opcode_x64_LDX_IMM,
  k_opcode_x64_LDX_ZPG,
  k_opcode_x64_LDY_addr,
  k_opcode_x64_LDY_ABS,
  k_opcode_x64_LDY_ABX,
  k_opcode_x64_LDY_IMM,
  k_opcode_x64_LDY_ZPG,
  k_opcode_x64_LSR_ABS,
  k_opcode_x64_LSR_ACC_n,
  k_opcode_x64_LSR_ZPG,
  k_opcode_x64_ORA_ABS,
  k_opcode_x64_ORA_ABX,
  k_opcode_x64_ORA_ABY,
  k_opcode_x64_ORA_addr,
  k_opcode_x64_ORA_addr_n,
  k_opcode_x64_ORA_addr_X,
  k_opcode_x64_ORA_addr_Y,
  k_opcode_x64_ORA_IMM,
  k_opcode_x64_ORA_ZPG,
  k_opcode_x64_ROL_ACC_n,
  k_opcode_x64_ROR_ACC_n,
  k_opcode_x64_SAX_ABS,
  k_opcode_x64_SBC_ABS,
  k_opcode_x64_SBC_ABX,
  k_opcode_x64_SBC_ABY,
  k_opcode_x64_SBC_addr,
  k_opcode_x64_SBC_addr_n,
  k_opcode_x64_SBC_addr_X,
  k_opcode_x64_SBC_addr_Y,
  k_opcode_x64_SBC_IMM,
  k_opcode_x64_SBC_ZPG,
  k_opcode_x64_SLO_ABS,
  k_opcode_x64_STA_addr,
  k_opcode_x64_STA_addr_n,
  k_opcode_x64_STA_addr_X,
  k_opcode_x64_STA_addr_Y,
  k_opcode_x64_STA_ABS,
  k_opcode_x64_STA_ABX,
  k_opcode_x64_STA_ABY,
  k_opcode_x64_STA_ZPG,
  k_opcode_x64_STX_addr,
  k_opcode_x64_STX_ABS,
  k_opcode_x64_STX_ZPG,
  k_opcode_x64_STY_addr,
  k_opcode_x64_STY_ABS,
  k_opcode_x64_STY_ZPG,
  k_opcode_x64_SUB_ABS,
  k_opcode_x64_SUB_ABX,
  k_opcode_x64_SUB_ABY,
  k_opcode_x64_SUB_addr,
  k_opcode_x64_SUB_addr_n,
  k_opcode_x64_SUB_addr_X,
  k_opcode_x64_SUB_addr_Y,
  k_opcode_x64_SUB_IMM,
  k_opcode_x64_SUB_ZPG,
};

#define ASM(x)                                                                 \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy(p_dest_buf, asm_jit_ ## x, asm_jit_ ## x ## _END);

#define ASM_U8(x)                                                              \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_byte(p_dest_buf, asm_jit_ ## x, asm_jit_ ## x ## _END, value1);

#define ASM_ADDR_U8(x)                                                         \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_byte(p_dest_buf,                                              \
                      asm_jit_ ## x,                                           \
                      asm_jit_ ## x ## _END,                                   \
                      (value1 - REG_MEM_OFFSET))

#define ASM_U32(x)                                                             \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_u32(p_dest_buf, asm_jit_ ## x, asm_jit_ ## x ## _END, value1);

#define ASM_ADDR_U32(x)                                                        \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  delta = (value2 - K_BBC_MEM_READ_IND_ADDR);                                  \
  asm_copy_patch_u32(p_dest_buf,                                               \
                     asm_jit_ ## x,                                            \
                     asm_jit_ ## x ## _END,                                    \
                     (value1 - REG_MEM_OFFSET + delta));

#define ASM_ADDR_U32_RAW(x)                                                    \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  asm_copy_patch_u32(p_dest_buf,                                               \
                     asm_jit_ ## x,                                            \
                     asm_jit_ ## x ## _END,                                    \
                     (value1 + value2));

#define ASM_Bxx(x)                                                             \
  void asm_jit_ ## x(void);                                                    \
  void asm_jit_ ## x ## _END(void);                                            \
  void asm_jit_ ## x ## _8bit(void);                                           \
  void asm_jit_ ## x ## _8bit_END(void);                                       \
  asm_emit_jit_jump(p_dest_buf,                                                \
                    (void*) (uintptr_t) value1,                                \
                    asm_jit_ ## x,                                             \
                    asm_jit_ ## x ## _END,                                     \
                    asm_jit_ ## x ## _8bit,                                    \
                    asm_jit_ ## x ## _8bit_END);

static struct os_alloc_mapping* s_p_mapping_trampolines;

struct asm_jit_struct {
  int (*is_memory_always_ram)(void* p, uint16_t addr);
  void* p_memory_object;
};

static void
asm_emit_jit_jump(struct util_buffer* p_buf,
                  void* p_target,
                  void* p_jmp_32bit,
                  void* p_jmp_end_32bit,
                  void* p_jmp_8bit,
                  void* p_jmp_end_8bit) {
  int32_t len_x64;
  int64_t delta;

  size_t offset = util_buffer_get_pos(p_buf);
  void* p_source = (util_buffer_get_base_address(p_buf) + offset);

  len_x64 = (p_jmp_end_8bit - p_jmp_8bit);
  delta = (p_target - (p_source + len_x64));

  if (delta <= INT8_MAX && delta >= INT8_MIN) {
    asm_copy(p_buf, p_jmp_8bit, p_jmp_end_8bit);
    asm_patch_byte(p_buf, offset, p_jmp_8bit, p_jmp_end_8bit, delta);
  } else {
    len_x64 = (p_jmp_end_32bit - p_jmp_32bit);
    delta = (p_target - (p_source + len_x64));
    assert(delta <= INT32_MAX && delta >= INT32_MIN);
    asm_copy(p_buf, p_jmp_32bit, p_jmp_end_32bit);
    asm_patch_int(p_buf, offset, p_jmp_32bit, p_jmp_end_32bit, delta);
  }
}

static void
asm_emit_jit_jump_interp_trampoline(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_jump_interp_trampoline(void);
  void asm_jit_jump_interp_trampoline_pc_patch(void);
  void asm_jit_jump_interp_trampoline_jump_patch(void);
  void asm_jit_jump_interp_trampoline_END(void);
  void asm_jit_interp(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_jump_interp_trampoline,
           asm_jit_jump_interp_trampoline_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_jump_interp_trampoline,
                asm_jit_jump_interp_trampoline_pc_patch,
                addr);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_jump_interp_trampoline,
                 asm_jit_jump_interp_trampoline_jump_patch,
                 asm_jit_interp);
}

int
asm_jit_is_enabled(void) {
  return 1;
}

void
asm_jit_test_preconditions(void) {
  void asm_jit_BEQ_8bit(void);
  void asm_jit_BEQ_8bit_END(void);
  if ((asm_jit_BEQ_8bit_END - asm_jit_BEQ_8bit) != 2) {
    util_bail("JIT assembly miscompiled -- clang issue? try opt build.");
  }
}

int
asm_jit_supports_uopcode(int32_t uopcode) {
  (void) uopcode;
  return 1;
}

struct asm_jit_struct*
asm_jit_create(void* p_jit_base,
               int (*is_memory_always_ram)(void* p, uint16_t addr),
               void* p_memory_object) {
  struct asm_jit_struct* p_asm;
  size_t mapping_size;
  uint32_t i;
  void* p_trampolines;
  struct util_buffer* p_temp_buf = util_buffer_create();

  (void) p_jit_base;

  assert(s_p_mapping_trampolines == NULL);

  p_asm = util_mallocz(sizeof(struct asm_jit_struct));
  p_asm->is_memory_always_ram = is_memory_always_ram;
  p_asm->p_memory_object = p_memory_object;

  /* This is the mapping that holds trampolines to jump out of JIT. These
   * one-per-6502-address trampolines enable the core JIT code to be simpler
   * and smaller, at the expense of more complicated bridging between JIT and
   * interp.
   */
  mapping_size = (k_6502_addr_space_size * K_JIT_TRAMPOLINE_BYTES);
  s_p_mapping_trampolines =
      os_alloc_get_mapping((void*) K_JIT_TRAMPOLINES_ADDR, mapping_size);
  p_trampolines = os_alloc_get_mapping_addr(s_p_mapping_trampolines);
  os_alloc_make_mapping_read_write_exec(p_trampolines, mapping_size);
  util_buffer_setup(p_temp_buf, p_trampolines, mapping_size);
  asm_fill_with_trap(p_temp_buf);

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    /* Initialize JIT trampoline. */
    util_buffer_setup(
        p_temp_buf,
        (p_trampolines + (i * K_JIT_TRAMPOLINE_BYTES)),
        K_JIT_TRAMPOLINE_BYTES);
    asm_emit_jit_jump_interp_trampoline(p_temp_buf, i);
  }

  util_buffer_destroy(p_temp_buf);

  return p_asm;
}

void
asm_jit_destroy(struct asm_jit_struct* p_asm) {
  assert(s_p_mapping_trampolines != NULL);
  os_alloc_free_mapping(s_p_mapping_trampolines);

  util_free(p_asm);
}

void*
asm_jit_get_private(struct asm_jit_struct* p_asm) {
  void asm_jit_compile_trampoline(void);
  (void) p_asm;
  return asm_jit_compile_trampoline;
}

void
asm_jit_start_code_updates(struct asm_jit_struct* p_asm) {
  (void) p_asm;
}

void
asm_jit_finish_code_updates(struct asm_jit_struct* p_asm) {
  (void) p_asm;
}

int
asm_jit_handle_fault(struct asm_jit_struct* p_asm,
                     uintptr_t* p_pc,
                     int is_inturbo,
                     int32_t addr_6502,
                     void* p_fault_addr,
                     int is_write) {
  int inaccessible_indirect_page;
  int bcd_fault_fixup;
  int stack_wrap_fault_fixup;
  int wrap_indirect_read;
  int wrap_indirect_write;

  (void) p_asm;

  /* x64 inturbo shouldn't be faulting ever. */
  if (is_inturbo) {
    return 0;
  }

  /* The indirect page fault occurs when an indirect addressing mode is used
   * to access 0xF000 - 0xFFFF, primarily of interest due to the hardware
   * registers. Using a fault + fixup here is a good performance boost for the
   * common case.
   * This fault is also encountered in the Windows port, which needs to use it
   * for ROM writes.
   */
  inaccessible_indirect_page = 0;
  /* The BCD fault occurs when the BCD flag is unknown and set at the start of
   * a block with ADC / SBC instructions.
   */
  bcd_fault_fixup = 0;
  /* The stack wrap fault occurs if a 16-bit stack access wraps the S
   * register.
   */
  stack_wrap_fault_fixup = 0;
  /* The address space indirect wrap faults occurs if an indirect 16-bit access
   * crosses the 0xFFFF address space boundary.
   */
  wrap_indirect_read = 0;
  wrap_indirect_write = 0;

  /* TODO: more checks, etc. */
  if ((p_fault_addr >=
          ((void*) K_BBC_MEM_WRITE_IND_ADDR + K_BBC_MEM_OS_ROM_OFFSET)) &&
      (p_fault_addr <
          ((void*) K_BBC_MEM_WRITE_IND_ADDR + K_6502_ADDR_SPACE_SIZE))) {
    if (is_write) {
      inaccessible_indirect_page = 1;
    }
  }
  if ((p_fault_addr >=
          ((void*) K_BBC_MEM_WRITE_IND_ADDR + K_6502_ADDR_SPACE_SIZE)) &&
      (p_fault_addr <=
          ((void*) K_BBC_MEM_WRITE_IND_ADDR + K_6502_ADDR_SPACE_SIZE + 0xFE))) {
    if (is_write) {
      wrap_indirect_write = 1;
    }
  }

  /* From this point on, nothing else is a write fault. */
  if (!inaccessible_indirect_page && !wrap_indirect_write && is_write) {
    return 0;
  }

  if ((p_fault_addr >=
          ((void*) K_BBC_MEM_READ_IND_ADDR + K_BBC_MEM_INACCESSIBLE_OFFSET)) &&
      (p_fault_addr <
          ((void*) K_BBC_MEM_READ_IND_ADDR + K_6502_ADDR_SPACE_SIZE))) {
    inaccessible_indirect_page = 1;
  }
  if ((p_fault_addr >=
          ((void*) K_BBC_MEM_READ_IND_ADDR + K_6502_ADDR_SPACE_SIZE)) &&
      (p_fault_addr <=
          ((void*) K_BBC_MEM_READ_IND_ADDR + K_6502_ADDR_SPACE_SIZE + 0xFE))) {
    wrap_indirect_read = 1;
  }
  if (p_fault_addr ==
          ((void*) K_BBC_MEM_READ_FULL_ADDR + K_6502_ADDR_SPACE_SIZE + 2)) {
    /* D flag alone. */
    bcd_fault_fixup = 1;
  }
  if (p_fault_addr ==
          ((void*) K_BBC_MEM_READ_FULL_ADDR + K_6502_ADDR_SPACE_SIZE + 6)) {
    /* D flag and I flag. */
    bcd_fault_fixup = 1;
  }
  if ((p_fault_addr == ((void*) K_BBC_MEM_READ_FULL_ADDR - 1)) ||
      (p_fault_addr == ((void*) K_BBC_MEM_READ_FULL_ADDR - 2))) {
    /* Wrap via pushing (decrementing). */
    stack_wrap_fault_fixup = 1;
  }
  if ((p_fault_addr ==
          ((void*) K_BBC_MEM_READ_FULL_ADDR + K_6502_ADDR_SPACE_SIZE)) ||
      (p_fault_addr ==
          ((void*) K_BBC_MEM_READ_FULL_ADDR + K_6502_ADDR_SPACE_SIZE + 1))) {
    /* Wrap via pulling (incrementing). */
    stack_wrap_fault_fixup = 1;
  }

  if (!inaccessible_indirect_page &&
      !bcd_fault_fixup &&
      !stack_wrap_fault_fixup &&
      !wrap_indirect_read &&
      !wrap_indirect_write) {
    return 0;
  }

  /* Fault is recognized.
   * Bounce into the interpreter via the trampolines.
   */
  *p_pc = (K_JIT_TRAMPOLINES_ADDR + (addr_6502 * K_JIT_TRAMPOLINE_BYTES));
  return 1;
}

void
asm_jit_invalidate_code_at(void* p) {
  uint16_t* p_dst = (uint16_t*) p;
  /* call [rdi] */
  *p_dst = 0x17ff;
}

int
asm_jit_is_invalidated_code_at(void* p) {
  uint16_t code = *(uint16_t*) p;
  return (code == 0x17ff);
}

void
asm_emit_jit_invalidated(struct util_buffer* p_buf) {
  /* call [rdi] */
  util_buffer_add_2b(p_buf, 0xff, 0x17);
}

static void
asm_emit_jit_check_countdown(struct util_buffer* p_buf,
                             struct util_buffer* p_buf_epilog,
                             uint32_t count,
                             uint16_t addr,
                             void* p_trampoline) {
  void asm_jit_check_countdown_8bit(void);
  void asm_jit_check_countdown_8bit_count_patch(void);
  void asm_jit_check_countdown_8bit_jump_patch(void);
  void asm_jit_check_countdown_8bit_END(void);
  void asm_jit_check_countdown(void);
  void asm_jit_check_countdown_count_patch(void);
  void asm_jit_check_countdown_jump_patch(void);
  void asm_jit_check_countdown_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  (void) p_buf_epilog;
  (void) addr;

  if (count <= 128) {
    asm_copy(p_buf,
             asm_jit_check_countdown_8bit,
             asm_jit_check_countdown_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_check_countdown_8bit,
                   asm_jit_check_countdown_8bit_count_patch,
                   -count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown_8bit,
                   asm_jit_check_countdown_8bit_jump_patch,
                   p_trampoline);
  } else {
    asm_copy(p_buf,
             asm_jit_check_countdown,
             asm_jit_check_countdown_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_check_countdown,
                  asm_jit_check_countdown_count_patch,
                  -count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown,
                   asm_jit_check_countdown_jump_patch,
                   p_trampoline);
  }
}

static void
asm_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_call_debug(void);
  void asm_jit_call_debug_pc_patch(void);
  void asm_jit_call_debug_call_patch(void);
  void asm_jit_call_debug_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_call_debug, asm_jit_call_debug_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_call_debug,
                asm_jit_call_debug_pc_patch,
                addr);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_call_debug,
                 asm_jit_call_debug_call_patch,
                 asm_debug);
}

static void
asm_emit_jit_call_inturbo(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_call_inturbo(void);
  void asm_jit_call_inturbo_pc_patch(void);
  void asm_jit_call_inturbo_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_call_inturbo, asm_jit_call_inturbo_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_call_inturbo,
                asm_jit_call_inturbo_pc_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
}

static void
asm_emit_jit_jump_interp(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_jump_interp(void);
  void asm_jit_jump_interp_pc_patch(void);
  void asm_jit_jump_interp_jump_patch(void);
  void asm_jit_jump_interp_END(void);
  void asm_jit_interp(void);
  size_t offset = util_buffer_get_pos(p_buf);

  /* Require the trampolines to be hosted above the main JIT code in virtual
   * memory. This ensures that the (uncommon) jumps out of JIT are forward
   * jumps, which may help on some CPUs.
   */
  assert(K_JIT_TRAMPOLINES_ADDR > K_JIT_ADDR);

  asm_copy(p_buf, asm_jit_jump_interp, asm_jit_jump_interp_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_jump_interp,
                asm_jit_jump_interp_pc_patch,
                addr);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_jump_interp,
                 asm_jit_jump_interp_jump_patch,
                 asm_jit_interp);
}

static void
asm_emit_jit_CHECK_PENDING_IRQ(struct util_buffer* p_buf,
                               struct util_buffer* p_buf_epilog,
                               uint16_t addr,
                               void* p_trampoline) {
  void asm_jit_CHECK_PENDING_IRQ(void);
  void asm_jit_CHECK_PENDING_IRQ_jump_patch(void);
  void asm_jit_CHECK_PENDING_IRQ_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  (void) p_buf_epilog;
  (void) addr;

  asm_copy(p_buf, asm_jit_CHECK_PENDING_IRQ, asm_jit_CHECK_PENDING_IRQ_END);
  asm_patch_jump(p_buf,
                 offset,
                 asm_jit_CHECK_PENDING_IRQ,
                 asm_jit_CHECK_PENDING_IRQ_jump_patch,
                 p_trampoline);
}

static void
asm_emit_jit_JMP_SCRATCH_n(struct util_buffer* p_buf, uint16_t n) {
  void asm_jit_JMP_SCRATCH_n(void);
  void asm_jit_JMP_SCRATCH_n_lea_patch(void);
  void asm_jit_JMP_SCRATCH_n_END(void);
  size_t offset = util_buffer_get_pos(p_buf);
  asm_copy(p_buf, asm_jit_JMP_SCRATCH_n, asm_jit_JMP_SCRATCH_n_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_JMP_SCRATCH_n,
                asm_jit_JMP_SCRATCH_n_lea_patch,
                ((K_JIT_ADDR >> K_JIT_BYTES_SHIFT) + n));
}

static void
asm_emit_jit_LDA_Z(struct util_buffer* p_buf) {
  void asm_jit_LDA_Z(void);
  void asm_jit_LDA_Z_END(void);
  asm_copy(p_buf, asm_jit_LDA_Z, asm_jit_LDA_Z_END);
}

static void
asm_emit_jit_LDX_Z(struct util_buffer* p_buf) {
  void asm_jit_LDX_Z(void);
  void asm_jit_LDX_Z_END(void);
  asm_copy(p_buf, asm_jit_LDX_Z, asm_jit_LDX_Z_END);
}

static void
asm_emit_jit_LDY_Z(struct util_buffer* p_buf) {
  void asm_jit_LDY_Z(void);
  void asm_jit_LDY_Z_END(void);
  asm_copy(p_buf, asm_jit_LDY_Z, asm_jit_LDY_Z_END);
}

static void
asm_emit_jit_MODE_ZPX(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_MODE_ZPX_8bit(void);
  void asm_jit_MODE_ZPX_8bit_lea_patch(void);
  void asm_jit_MODE_ZPX_8bit_END(void);
  void asm_jit_MODE_ZPX(void);
  void asm_jit_MODE_ZPX_lea_patch(void);
  void asm_jit_MODE_ZPX_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  if (value <= 0x7F) {
    asm_copy(p_buf, asm_jit_MODE_ZPX_8bit, asm_jit_MODE_ZPX_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_MODE_ZPX_8bit,
                   asm_jit_MODE_ZPX_8bit_lea_patch,
                   value);
  } else {
    asm_copy(p_buf, asm_jit_MODE_ZPX, asm_jit_MODE_ZPX_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_MODE_ZPX,
                  asm_jit_MODE_ZPX_lea_patch,
                  value);
  }
}

static void
asm_emit_jit_MODE_ZPY(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_MODE_ZPY_8bit(void);
  void asm_jit_MODE_ZPY_8bit_lea_patch(void);
  void asm_jit_MODE_ZPY_8bit_END(void);
  void asm_jit_MODE_ZPY(void);
  void asm_jit_MODE_ZPY_lea_patch(void);
  void asm_jit_MODE_ZPY_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  if (value <= 0x7F) {
    asm_copy(p_buf, asm_jit_MODE_ZPY_8bit, asm_jit_MODE_ZPY_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_MODE_ZPY_8bit,
                   asm_jit_MODE_ZPY_8bit_lea_patch,
                   value);
  } else {
    asm_copy(p_buf, asm_jit_MODE_ZPY, asm_jit_MODE_ZPY_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_MODE_ZPY,
                  asm_jit_MODE_ZPY_lea_patch,
                  value);
  }
}

static void
asm_emit_jit_PUSH_16(struct util_buffer* p_buf, uint16_t value) {
  void asm_jit_PUSH_16(void);
  void asm_jit_PUSH_16_word_patch(void);
  void asm_jit_PUSH_16_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_PUSH_16, asm_jit_PUSH_16_END);
  asm_patch_u16(p_buf,
                offset,
                asm_jit_PUSH_16,
                asm_jit_PUSH_16_word_patch,
                value);
}

static void
asm_emit_jit_STOA_IMM(struct util_buffer* p_buf, uint16_t addr, uint8_t value) {
  void asm_jit_STOA_IMM(void);
  void asm_jit_STOA_IMM_END(void);
  void asm_jit_STOA_IMM_8bit(void);
  void asm_jit_STOA_IMM_8bit_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  if (addr < 0x100) {
    asm_copy(p_buf, asm_jit_STOA_IMM_8bit, asm_jit_STOA_IMM_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_STOA_IMM_8bit,
                   asm_jit_STOA_IMM_8bit_END,
                   value);
    asm_patch_byte(p_buf,
                   (offset - 1),
                   asm_jit_STOA_IMM_8bit,
                   asm_jit_STOA_IMM_8bit_END,
                   (addr - REG_MEM_OFFSET));
  } else {
    asm_copy(p_buf, asm_jit_STOA_IMM, asm_jit_STOA_IMM_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_STOA_IMM,
                   asm_jit_STOA_IMM_END,
                   value);
    asm_patch_int(p_buf,
                  (offset - 1),
                  asm_jit_STOA_IMM,
                  asm_jit_STOA_IMM_END,
                  (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
  }
}

static void
asm_emit_jit_SLO_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_SLO_ABS(void);
  void asm_jit_SLO_ABS_mov1_patch(void);
  void asm_jit_SLO_ABS_mov2_patch(void);
  void asm_jit_SLO_ABS_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_SLO_ABS, asm_jit_SLO_ABS_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_SLO_ABS,
                asm_jit_SLO_ABS_mov1_patch,
                (addr - REG_MEM_OFFSET));
  asm_patch_int(p_buf,
                offset,
                asm_jit_SLO_ABS,
                asm_jit_SLO_ABS_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

static uint32_t
asm_jit_get_segment(struct asm_jit_struct* p_asm,
                    uint16_t addr,
                    int is_write,
                    int is_abn) {
  uint32_t segment;
  int is_always_ram = p_asm->is_memory_always_ram(p_asm->p_memory_object, addr);
  /* Assumes address space wrap and hardware register access taken care of
   * elsewhere.
   */
  if (is_abn) {
    is_always_ram &= p_asm->is_memory_always_ram(p_asm->p_memory_object,
                                                 (uint16_t) (addr + 0xFF));
  }

  if (is_always_ram) segment = K_BBC_MEM_READ_IND_ADDR;
  else if (!is_write) segment = K_BBC_MEM_READ_FULL_ADDR;
  else segment = K_BBC_MEM_WRITE_FULL_ADDR;

  return segment;
}

static int32_t
asm_jit_rewrite_IMM(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_IMM; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_IMM; break;
  case k_opcode_ALR: new_uopcode = k_opcode_x64_ALR_IMM; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_IMM; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_IMM; break;
  case k_opcode_CPX: new_uopcode = k_opcode_x64_CPX_IMM; break;
  case k_opcode_CPY: new_uopcode = k_opcode_x64_CPY_IMM; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_IMM; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_IMM; break;
  case k_opcode_LDX: new_uopcode = k_opcode_x64_LDX_IMM; break;
  case k_opcode_LDY: new_uopcode = k_opcode_x64_LDY_IMM; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_IMM; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_IMM; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_IMM; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_ZPG(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_ZPG; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_ZPG; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_ZPG; break;
  case k_opcode_ASL_value: new_uopcode = k_opcode_x64_ASL_ZPG; break;
  case k_opcode_BIT: new_uopcode = k_opcode_x64_load_ZPG; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_ZPG; break;
  case k_opcode_CPX: new_uopcode = k_opcode_x64_CPX_ZPG; break;
  case k_opcode_CPY: new_uopcode = k_opcode_x64_CPY_ZPG; break;
  case k_opcode_DEC_value: new_uopcode = k_opcode_x64_DEC_ZPG; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_ZPG; break;
  case k_opcode_INC_value: new_uopcode = k_opcode_x64_INC_ZPG; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_ZPG; break;
  case k_opcode_LDX: new_uopcode = k_opcode_x64_LDX_ZPG; break;
  case k_opcode_LDY: new_uopcode = k_opcode_x64_LDY_ZPG; break;
  case k_opcode_LSR_value: new_uopcode = k_opcode_x64_LSR_ZPG; break;
  case k_opcode_NOP: new_uopcode = k_opcode_NOP; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_ZPG; break;
  /* SAX and SLO only support ABS for now. */
  case k_opcode_SAX: new_uopcode = k_opcode_x64_SAX_ABS; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_ZPG; break;
  case k_opcode_SLO: new_uopcode = k_opcode_x64_SLO_ABS; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_ZPG; break;
  case k_opcode_STX: new_uopcode = k_opcode_x64_STX_ZPG; break;
  case k_opcode_STY: new_uopcode = k_opcode_x64_STY_ZPG; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_ZPG; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_ABS(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_ABS; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_ABS; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_ABS; break;
  case k_opcode_ASL_value: new_uopcode = k_opcode_x64_ASL_ABS; break;
  case k_opcode_BIT: new_uopcode = k_opcode_x64_load_ABS; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_ABS; break;
  case k_opcode_CPX: new_uopcode = k_opcode_x64_CPX_ABS; break;
  case k_opcode_CPY: new_uopcode = k_opcode_x64_CPY_ABS; break;
  case k_opcode_DEC_value: new_uopcode = k_opcode_x64_DEC_ABS; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_ABS; break;
  case k_opcode_INC_value: new_uopcode = k_opcode_x64_INC_ABS; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_ABS; break;
  case k_opcode_LDX: new_uopcode = k_opcode_x64_LDX_ABS; break;
  case k_opcode_LDY: new_uopcode = k_opcode_x64_LDY_ABS; break;
  case k_opcode_LSR_value: new_uopcode = k_opcode_x64_LSR_ABS; break;
  case k_opcode_NOP: new_uopcode = k_opcode_NOP; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_ABS; break;
  case k_opcode_SAX: new_uopcode = k_opcode_x64_SAX_ABS; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_ABS; break;
  case k_opcode_SLO: new_uopcode = k_opcode_x64_SLO_ABS; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_ABS; break;
  case k_opcode_STX: new_uopcode = k_opcode_x64_STX_ABS; break;
  case k_opcode_STY: new_uopcode = k_opcode_x64_STY_ABS; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_ABS; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_addr(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_addr; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_addr; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_addr; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_addr; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_addr; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_addr; break;
  case k_opcode_LDX: new_uopcode = k_opcode_x64_LDX_addr; break;
  case k_opcode_LDY: new_uopcode = k_opcode_x64_LDY_addr; break;
  case k_opcode_NOP: new_uopcode = k_opcode_NOP; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_addr; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_addr; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_addr; break;
  case k_opcode_STX: new_uopcode = k_opcode_x64_STX_addr; break;
  case k_opcode_STY: new_uopcode = k_opcode_x64_STY_addr; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_addr; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_IDY(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_addr_Y; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_addr_Y; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_addr_Y; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_addr_Y; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_addr_Y; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_addr_Y; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_addr_Y; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_addr_Y; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_addr_Y; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_addr_Y; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_IDY_n(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_addr_n; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_addr_n; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_addr_n; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_addr_n; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_addr_n; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_addr_n; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_addr_n; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_addr_n; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_addr_n; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_addr_n; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_IDY_X(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_addr_X; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_addr_X; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_addr_X; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_addr_X; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_addr_X; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_addr_X; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_addr_X; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_addr_X; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_addr_X; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_addr_X; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_ABX(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_ABX; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_ABX; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_ABX; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_ABX; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_ABX; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_ABX; break;
  case k_opcode_LDY: new_uopcode = k_opcode_x64_LDY_ABX; break;
  case k_opcode_NOP: new_uopcode = k_opcode_NOP; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_ABX; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_ABX; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_ABX; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_ABX; break;
  default: assert(0); break;
  }
  return new_uopcode;
}

static int32_t
asm_jit_rewrite_ABY(int32_t uopcode) {
  int32_t new_uopcode = -1;
  switch (uopcode) {
  case k_opcode_ADC: new_uopcode = k_opcode_x64_ADC_ABY; break;
  case k_opcode_ADD: new_uopcode = k_opcode_x64_ADD_ABY; break;
  case k_opcode_AND: new_uopcode = k_opcode_x64_AND_ABY; break;
  case k_opcode_CMP: new_uopcode = k_opcode_x64_CMP_ABY; break;
  case k_opcode_EOR: new_uopcode = k_opcode_x64_EOR_ABY; break;
  case k_opcode_LDA: new_uopcode = k_opcode_x64_LDA_ABY; break;
  case k_opcode_LDX: new_uopcode = k_opcode_x64_LDX_ABY; break;
  case k_opcode_NOP: new_uopcode = k_opcode_NOP; break;
  case k_opcode_ORA: new_uopcode = k_opcode_x64_ORA_ABY; break;
  case k_opcode_SBC: new_uopcode = k_opcode_x64_SBC_ABY; break;
  case k_opcode_STA: new_uopcode = k_opcode_x64_STA_ABY; break;
  case k_opcode_SUB: new_uopcode = k_opcode_x64_SUB_ABY; break;
  default: assert(0); break;
  }
  return new_uopcode;
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
  struct asm_uop* p_nz_flags_uop;
  struct asm_uop* p_inv_uop;
  struct asm_uop* p_addr_check_uop;
  struct asm_uop* p_page_crossing_uop;
  struct asm_uop* p_tmp_uop;
  int32_t uopcode;
  int32_t new_uopcode;
  int is_zpg;
  int is_mode_addr;
  int is_mode_abn;
  int is_rmw;
  int do_set_segment;
  int do_eliminate_load_store;
  uint16_t addr;

  asm_breakdown_from_6502(p_uops,
                          num_uops,
                          &p_main_uop,
                          &p_mode_uop,
                          &p_load_uop,
                          &p_store_uop,
                          &p_load_carry_uop,
                          &p_save_carry_uop,
                          &p_load_overflow_uop,
                          &p_nz_flags_uop,
                          &p_inv_uop,
                          &p_addr_check_uop,
                          &p_page_crossing_uop);
  if (p_main_uop == NULL) {
    return;
  }

  /* Fix up carry flag managment, including for Intel doing borrow instead of
   * carry for subtract.
   */
  if (p_load_carry_uop != NULL) {
    switch (p_main_uop->uopcode) {
    case k_opcode_ADC:
    case k_opcode_ROL_acc:
    case k_opcode_ROL_value:
    case k_opcode_ROR_acc:
    case k_opcode_ROR_value:
      /* The load carry can trash the saved carry and be faster. */
      p_load_carry_uop->backend_tag = 1;
      break;
    case k_opcode_ADD:
    case k_opcode_SUB:
      assert(p_load_carry_uop->is_eliminated);
      break;
    case k_opcode_BCC:
    case k_opcode_BCS:
      break;
    case k_opcode_PHP:
      p_load_carry_uop->is_eliminated = 1;
      break;
    case k_opcode_SBC:
      p_load_carry_uop->uopcode = k_opcode_load_carry_inverted;
      break;
    default:
      assert(0);
      break;
    }
  }
  if (p_save_carry_uop != NULL) {
    switch (p_main_uop->uopcode) {
    case k_opcode_SBC:
    case k_opcode_SUB:
    case k_opcode_CMP:
    case k_opcode_CPX:
    case k_opcode_CPY:
      p_save_carry_uop->uopcode = k_opcode_save_carry_inverted;
      break;
    default:
      break;
    }
  }
  if (p_load_overflow_uop != NULL) {
    switch (p_main_uop->uopcode) {
    case k_opcode_PHP:
      p_load_overflow_uop->is_eliminated = 1;
      break;
    default:
      break;
    }
  }

  /* Many Intel instructions update the save NZ flag state for us. */
  switch (p_main_uop->uopcode) {
  case k_opcode_ADC:
  case k_opcode_ADD:
  case k_opcode_ALR:
  case k_opcode_AND:
  case k_opcode_ASL_acc:
  case k_opcode_ASL_value:
  case k_opcode_DEC_value:
  case k_opcode_DEX:
  case k_opcode_DEY:
  case k_opcode_EOR:
  case k_opcode_INC_value:
  case k_opcode_INX:
  case k_opcode_INY:
  case k_opcode_LSR_acc:
  case k_opcode_LSR_value:
  case k_opcode_ORA:
  /* Not ROL, ROR. rcr and rcl don't set x64 sign / zero flags. */
  case k_opcode_SBC:
  case k_opcode_SLO:
  case k_opcode_SUB:
    /* The dedicated flag setting is eliminated, but the effect still occurs in
     * the x64 instruction.
     */
    assert(p_nz_flags_uop != NULL);
    p_nz_flags_uop->is_merged = 1;
    p_nz_flags_uop->is_eliminated = 1;
  default:
    break;
  }

  /* Intel uses a different instruction sequence for shifts and rotates other
   * than by 1.
   */
  if ((p_mode_uop == NULL) && (p_main_uop->value1 != 1)) {
    switch (p_main_uop->uopcode) {
    case k_opcode_ASL_acc:
      p_main_uop->backend_tag = k_opcode_x64_ASL_ACC_n;
      break;
    case k_opcode_LSR_acc:
      p_main_uop->backend_tag = k_opcode_x64_LSR_ACC_n;
      break;
    case k_opcode_ROL_acc:
      p_main_uop->backend_tag = k_opcode_x64_ROL_ACC_n;
      break;
    case k_opcode_ROR_acc:
      p_main_uop->backend_tag = k_opcode_x64_ROR_ACC_n;
      break;
    default:
      break;
    }
  }

  if (p_mode_uop == NULL) {
    return;
  }

  /* The x64 model does implicit, not explicit, address checks. */
  if (p_addr_check_uop != NULL) {
    p_addr_check_uop->is_eliminated = 1;
  }

  addr = 0;
  is_rmw = 0;
  if ((p_load_uop != NULL) && (p_store_uop != NULL)) {
    is_rmw = 1;
  }
  is_mode_addr = 0;
  is_mode_abn = 0;
  do_set_segment = 0;
  do_eliminate_load_store = 0;

  uopcode = p_mode_uop->uopcode;
  switch (uopcode) {
  case k_opcode_value_load_16bit_wrap:
    /* Mode IND. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->is_eliminated = 1;
    addr = p_mode_uop->value1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_x64_mode_IND;
    p_mode_uop->value1 = addr;
    /* TODO: not correct for hardware register hits, but BRK breaks with IND. */
    p_mode_uop->value2 = K_BBC_MEM_READ_FULL_ADDR;
    break;
  case k_opcode_addr_load_16bit_nowrap:
    /* Dynamic opcode, ABS. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->is_eliminated = 1;
    addr = p_mode_uop->value1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_x64_mode_IND_nowrap;
    p_mode_uop->value1 = addr;
    p_mode_uop->value2 = K_BBC_MEM_READ_IND_ADDR;
    is_mode_addr = 1;
    break;
  case k_opcode_addr_load_8bit:
    /* Dynamic opcode, ZPG. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->is_eliminated = 1;
    addr = p_mode_uop->value1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_x64_mode_IND8;
    p_mode_uop->value1 = addr;
    p_mode_uop->value2 = K_BBC_MEM_READ_IND_ADDR;
    is_mode_addr = 1;
    break;
  case k_opcode_value_set:
    /* Mode IMM. */
    new_uopcode = p_main_uop->uopcode;
    new_uopcode = asm_jit_rewrite_IMM(new_uopcode);
    p_mode_uop->is_eliminated = 1;
    p_main_uop->value1 = p_mode_uop->value1;
    p_main_uop->backend_tag = new_uopcode;
    break;
  case k_opcode_addr_set:
    /* Mode ABS or ZPG. */
    p_mode_uop->is_eliminated = 1;
    addr = p_mode_uop->value1;
    is_zpg = (addr < 0x100);
    new_uopcode = p_main_uop->uopcode;
    if ((new_uopcode == k_opcode_ROL_value) ||
        (new_uopcode == k_opcode_ROR_value)) {
      assert(p_load_uop != NULL);
      assert(p_store_uop != NULL);
      if (is_zpg) new_uopcode = k_opcode_x64_load_ZPG;
      else new_uopcode = k_opcode_x64_load_ABS;
      p_load_uop->backend_tag = new_uopcode;
      p_load_uop->value1 = addr;
      p_load_uop->value2 = K_BBC_MEM_READ_IND_ADDR;
      if (is_zpg) new_uopcode = k_opcode_x64_store_ZPG;
      else new_uopcode = k_opcode_x64_store_ABS;
      p_store_uop->backend_tag = new_uopcode;
      p_store_uop->value1 = addr;
      p_store_uop->value2 = K_BBC_MEM_WRITE_IND_ADDR;
    } else {
      if (is_zpg) new_uopcode = asm_jit_rewrite_ZPG(new_uopcode);
      else new_uopcode = asm_jit_rewrite_ABS(new_uopcode);
      if (p_main_uop->uopcode == k_opcode_BIT) {
        /* Make sure the segment gets applied to the correct uopcode. */
        p_main_uop = p_mode_uop;
        p_main_uop->is_eliminated = 0;
      }
      p_main_uop->backend_tag = new_uopcode;
      p_main_uop->value1 = addr;
      do_set_segment = 1;
      do_eliminate_load_store = 1;
    }
    if (p_inv_uop != NULL) {
      p_inv_uop->backend_tag = k_opcode_x64_write_inv_ABS;
      p_inv_uop->value1 = addr;
    }
    break;
  case k_opcode_addr_load_16bit_wrap:
    /* Mode IDX. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_add_x_8bit);
    /* FALL THROUGH. */
  case k_opcode_addr_add_x_8bit:
    /* Mode ZPX. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->is_eliminated = 1;
    addr = p_mode_uop->value1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_x64_mode_ZPX;
    p_mode_uop->value1 = addr;
    is_mode_addr = 1;
    break;
  case k_opcode_addr_add_y_8bit:
    /* Mode ZPY. */
    p_mode_uop--;
    assert(p_mode_uop->uopcode == k_opcode_addr_set);
    p_mode_uop->is_eliminated = 1;
    addr = p_mode_uop->value1;
    p_mode_uop++;
    p_mode_uop->backend_tag = k_opcode_x64_mode_ZPY;
    p_mode_uop->value1 = addr;
    is_mode_addr = 1;
    break;
  case k_opcode_addr_add_x:
    /* Mode ABX or dyn,X. */
    p_mode_uop->is_eliminated = 1;
    p_mode_uop->is_merged = 1;
    p_tmp_uop = (p_mode_uop - 1);
    if (p_tmp_uop->uopcode == k_opcode_addr_set) {
      /* Mode ABX. */
      p_tmp_uop->is_eliminated = 1;
      addr = p_tmp_uop->value1;
      if (is_rmw) {
        assert(p_load_uop != NULL);
        assert(p_store_uop != NULL);
        p_load_uop->backend_tag = k_opcode_x64_mode_ABX_and_load;
        p_load_uop->value1 = addr;
        p_load_uop->value2 = K_BBC_MEM_READ_IND_ADDR;
        p_store_uop->backend_tag = k_opcode_x64_mode_ABX_store;
        p_store_uop->value1 = addr;
        p_store_uop->value2 = K_BBC_MEM_WRITE_IND_ADDR;
      } else {
        new_uopcode = p_main_uop->uopcode;
        new_uopcode = asm_jit_rewrite_ABX(new_uopcode);
        p_main_uop->backend_tag = new_uopcode;
        p_main_uop->value1 = addr;
        do_eliminate_load_store = 1;
        do_set_segment = 1;
      }
      if (p_inv_uop != NULL) {
        p_inv_uop->backend_tag = k_opcode_x64_write_inv_ABX;
        p_inv_uop->value1 = addr;
      }
      if (p_page_crossing_uop != NULL) {
        p_page_crossing_uop->backend_tag = k_opcode_x64_check_page_crossing_ABX;
        p_page_crossing_uop->value1 = addr;
      }
      is_mode_abn = 1;
    } else {
      /* Mode dyn,X. */
      assert(p_tmp_uop->uopcode == k_opcode_addr_load_16bit_nowrap);
      p_tmp_uop--;
      assert(p_tmp_uop->uopcode == k_opcode_addr_set);
      p_tmp_uop->is_eliminated = 1;
      addr = p_tmp_uop->value1;
      p_tmp_uop++;
      p_tmp_uop->backend_tag = k_opcode_x64_mode_IND_nowrap;
      p_tmp_uop->value1 = addr;
      p_tmp_uop->value2 = K_BBC_MEM_READ_IND_ADDR;
      new_uopcode = p_main_uop->uopcode;
      new_uopcode = asm_jit_rewrite_IDY_X(new_uopcode);
      p_main_uop->backend_tag = new_uopcode;
      if (p_inv_uop != NULL) {
        p_inv_uop->backend_tag = k_opcode_x64_write_inv_IDY_X;
      }
      if (p_page_crossing_uop != NULL) {
        p_page_crossing_uop->backend_tag =
            k_opcode_x64_check_page_crossing_IDY_X;
      }
      do_eliminate_load_store = 1;
    }
    break;
  case k_opcode_addr_add_y:
    /* Mode ABY, IDY or dyn,Y. */
    /* Eliminate k_opcode_addr_add_y, but mark it as merged so the optimizer
     * knows this depends on Y.
     */
    p_mode_uop->is_eliminated = 1;
    p_mode_uop->is_merged = 1;
    p_tmp_uop = (p_mode_uop - 1);
    if (p_tmp_uop->uopcode == k_opcode_addr_set) {
      /* Mode ABY. */
      p_tmp_uop->is_eliminated = 1;
      addr = p_tmp_uop->value1;
      new_uopcode = p_main_uop->uopcode;
      new_uopcode = asm_jit_rewrite_ABY(new_uopcode);
      p_main_uop->backend_tag = new_uopcode;
      p_main_uop->value1 = addr;
      if (p_inv_uop != NULL) {
        p_inv_uop->backend_tag = k_opcode_x64_write_inv_ABY;
        p_inv_uop->value1 = addr;
      }
      if (p_page_crossing_uop != NULL) {
        p_page_crossing_uop->backend_tag = k_opcode_x64_check_page_crossing_ABY;
        p_page_crossing_uop->value1 = addr;
      }
      is_mode_abn = 1;
      do_set_segment = 1;
      do_eliminate_load_store = 1;
    } else if (p_tmp_uop->uopcode == k_opcode_addr_load_16bit_wrap) {
      /* Mode IDY. */
      p_tmp_uop = (p_tmp_uop - 1);
      assert(p_tmp_uop->uopcode == k_opcode_addr_set);
      p_tmp_uop->is_eliminated = 1;
      addr = p_tmp_uop->value1;
      p_tmp_uop = (p_tmp_uop + 1);
      p_tmp_uop->backend_tag = k_opcode_x64_mode_IDY_load;
      p_tmp_uop->value1 = addr;
      new_uopcode = p_main_uop->uopcode;
      new_uopcode = asm_jit_rewrite_IDY(new_uopcode);
      p_main_uop->backend_tag = new_uopcode;
      if (p_inv_uop != NULL) {
        p_inv_uop->backend_tag = k_opcode_x64_write_inv_IDY;
      }
      if (p_page_crossing_uop != NULL) {
        p_page_crossing_uop->backend_tag = k_opcode_x64_check_page_crossing_IDY;
      }
      do_eliminate_load_store = 1;
    } else {
      /* Mode dyn,Y. */
      assert(p_tmp_uop->uopcode == k_opcode_addr_load_16bit_nowrap);
      p_tmp_uop--;
      assert(p_tmp_uop->uopcode == k_opcode_addr_set);
      p_tmp_uop->is_eliminated = 1;
      addr = p_tmp_uop->value1;
      p_tmp_uop++;
      p_tmp_uop->backend_tag = k_opcode_x64_mode_IND_nowrap;
      p_tmp_uop->value1 = addr;
      p_tmp_uop->value2 = K_BBC_MEM_READ_IND_ADDR;
      new_uopcode = p_main_uop->uopcode;
      new_uopcode = asm_jit_rewrite_IDY(new_uopcode);
      p_main_uop->backend_tag = new_uopcode;
      if (p_inv_uop != NULL) {
        p_inv_uop->backend_tag = k_opcode_x64_write_inv_IDY;
      }
      if (p_page_crossing_uop != NULL) {
        p_page_crossing_uop->backend_tag = k_opcode_x64_check_page_crossing_IDY;
      }
      do_eliminate_load_store = 1;
    }
    break;
  case k_opcode_addr_add_constant:
    /* Mode IDY, optimzed where Y is a known constant. */
    p_tmp_uop = (p_mode_uop - 1);
    assert(p_tmp_uop->uopcode == k_opcode_addr_load_16bit_wrap);
    p_tmp_uop = (p_tmp_uop - 1);
    assert(p_tmp_uop->uopcode == k_opcode_addr_set);
    p_tmp_uop->is_eliminated = 1;
    addr = p_tmp_uop->value1;
    p_tmp_uop = (p_tmp_uop + 1);
    p_tmp_uop->backend_tag = k_opcode_x64_mode_IDY_load;
    p_tmp_uop->value1 = addr;
    p_mode_uop->is_eliminated = 1;
    p_mode_uop->is_merged = 1;
    new_uopcode = p_main_uop->uopcode;
    new_uopcode = asm_jit_rewrite_IDY_n(new_uopcode);
    p_main_uop->backend_tag = new_uopcode;
    p_main_uop->value1 = p_mode_uop->value1;
    if (p_inv_uop != NULL) {
      p_inv_uop->backend_tag = k_opcode_x64_write_inv_IDY_n;
      p_inv_uop->value1 = p_mode_uop->value1;
    }
    if (p_page_crossing_uop != NULL) {
      assert(p_page_crossing_uop->uopcode == k_opcode_check_page_crossing_n);
    }
    do_eliminate_load_store = 1;
    break;
  default:
    assert(0);
    break;
  }

  if (is_mode_addr) {
    if (is_rmw) {
      /* Leave it as RMW. */
    } else {
      new_uopcode = p_main_uop->uopcode;
      new_uopcode = asm_jit_rewrite_addr(new_uopcode);
      p_main_uop->backend_tag = new_uopcode;
      do_eliminate_load_store = 1;
    }
  }

  if (do_set_segment) {
    int is_write = 1;
    if (p_load_uop != NULL) {
      is_write = 0;
    }
    p_main_uop->value2 = asm_jit_get_segment(p_asm,
                                             addr,
                                             is_write,
                                             is_mode_abn);
  }

  if (do_eliminate_load_store) {
    assert((p_load_uop != NULL) || (p_store_uop != NULL));
    if (p_load_uop != NULL) {
      p_load_uop->is_eliminated = 1;
    }
    if (p_store_uop != NULL) {
      p_store_uop->is_eliminated = 1;
    }
  }
}

void
asm_emit_jit(struct asm_jit_struct* p_asm,
             struct util_buffer* p_dest_buf,
             struct util_buffer* p_dest_buf_epilog,
             struct asm_uop* p_uop) {
  /* The segment we need to hit for memory accesses.
   * We have different segments:
   * READ
   * WRITE
   * READ INDIRECT
   * WRITE INDIRECT
   * These segments are generally different virtual views on top of the same
   * physical 6502 memory backing buffer. The differences are that writes to
   * ROM area are silently / quickly ignored, and the indirect segments fault
   * if hardware registers are hit.
   * To minimize L1 DTLB issues, we'll generally map all accesses to READ
   * INDIRECT when we know it doesn't make a difference.
   */
  uint32_t delta;
  void* p_trampolines;
  void* p_trampoline_addr = NULL;
  int32_t uopcode = p_uop->uopcode;
  uint32_t value1 = p_uop->value1;
  uint32_t value2 = p_uop->value2;

  (void) p_asm;

  if (p_uop->backend_tag >= 0x1000) {
    uopcode = p_uop->backend_tag;
  }
  assert(uopcode >= 0x100);

  /* Resolve trampoline addresses. */
  /* Resolve any addresses to real pointers. */
  switch (uopcode) {
  case k_opcode_countdown:
  case k_opcode_check_pending_irq:
    p_trampolines = os_alloc_get_mapping_addr(s_p_mapping_trampolines);
    p_trampoline_addr = (p_trampolines + (value1 * K_JIT_TRAMPOLINE_BYTES));
    break;
  default:
    break;
  }

  /* Emit the opcode. */
  switch (uopcode) {
  /* Misc. management opcodes. */
  case k_opcode_add_cycles: ASM_U8(countdown_add); break;
  case k_opcode_check_bcd: ASM(check_bcd); break;
  case k_opcode_check_pending_irq:
    asm_emit_jit_CHECK_PENDING_IRQ(p_dest_buf,
                                   p_dest_buf_epilog,
                                   (uint16_t) value1,
                                   p_trampoline_addr);
    break;
  case k_opcode_countdown:
    asm_emit_jit_check_countdown(p_dest_buf,
                                 p_dest_buf_epilog,
                                 (uint32_t) value2,
                                 (uint16_t) value1,
                                 p_trampoline_addr);
    break;
  case k_opcode_debug:
    asm_emit_jit_call_debug(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_interp:
    asm_emit_jit_jump_interp(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_inturbo:
    asm_emit_jit_call_inturbo(p_dest_buf, (uint16_t) value1);
    break;
  /* Addressing and value opcodes. */
  case k_opcode_addr_add_x: ASM(save_addr_low_byte); ASM(addr_add_x); break;
  case k_opcode_addr_add_y: ASM(save_addr_low_byte); ASM(addr_add_y); break;
  case k_opcode_addr_load_16bit_wrap: ASM(addr_load_16bit_wrap); break;
  case k_opcode_check_page_crossing_n:
    ASM(save_addr_low_byte);
    value2 = K_ASM_TABLE_PAGE_WRAP_CYCLE_INV;
    ASM_ADDR_U32_RAW(check_page_crossing_n);
    ASM(check_page_crossing_adjust);
    break;
  case k_opcode_check_page_crossing_x:
    ASM(check_page_crossing_x);
    ASM(check_page_crossing_adjust);
    break;
  case k_opcode_check_page_crossing_y:
    ASM(check_page_crossing_y);
    ASM(check_page_crossing_adjust);
    break;
  case k_opcode_CLEAR_CARRY: ASM(CLEAR_CARRY); break;
  case k_opcode_flags_nz_a: asm_emit_instruction_A_NZ_flags(p_dest_buf); break;
  case k_opcode_flags_nz_x: asm_emit_instruction_X_NZ_flags(p_dest_buf); break;
  case k_opcode_flags_nz_y: asm_emit_instruction_Y_NZ_flags(p_dest_buf); break;
  case k_opcode_flags_nz_value: ASM(flags_nz_value); break;
  case k_opcode_INVERT_CARRY: ASM(INVERT_CARRY); break;
  case k_opcode_JMP_SCRATCH_n:
    asm_emit_jit_JMP_SCRATCH_n(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_LDA_Z: asm_emit_jit_LDA_Z(p_dest_buf); break;
  case k_opcode_LDX_Z: asm_emit_jit_LDX_Z(p_dest_buf); break;
  case k_opcode_LDY_Z: asm_emit_jit_LDY_Z(p_dest_buf); break;
  case k_opcode_load_carry:
    if (p_uop->backend_tag == 1) {
      ASM(load_carry_for_calc);
    } else {
      ASM(load_carry_for_branch);
    }
    break;
  case k_opcode_load_carry_inverted: ASM(load_carry_inv_for_calc); break;
  case k_opcode_load_overflow: ASM(load_overflow); break;
  case k_opcode_PULL_16: ASM(PULL_16); break;
  case k_opcode_PUSH_16:
    asm_emit_jit_PUSH_16(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_save_carry: ASM(save_carry); break;
  case k_opcode_save_carry_inverted: ASM(save_carry_inv); break;
  case k_opcode_save_overflow: ASM(save_overflow); break;
  case k_opcode_SET_CARRY: ASM(SET_CARRY); break;
  case k_opcode_STOA_IMM:
    asm_emit_jit_STOA_IMM(p_dest_buf, (uint16_t) value1, (uint8_t) value2);
    break;
  case k_opcode_value_load: ASM(value_load); break;
  case k_opcode_value_store: ASM(value_store); break;
  case k_opcode_write_inv: ASM(write_inv); ASM(write_inv_commit); break;
  case k_opcode_ASL_acc: ASM(ASL_ACC); break;
  case k_opcode_ASL_value: ASM(ASL_value); break;
  case k_opcode_BCC: ASM_Bxx(BCC); break;
  case k_opcode_BCS: ASM_Bxx(BCS); break;
  case k_opcode_BEQ: ASM_Bxx(BEQ); break;
  case k_opcode_BIT: asm_emit_instruction_BIT_value(p_dest_buf); break;
  case k_opcode_BMI: ASM_Bxx(BMI); break;
  case k_opcode_BNE: ASM_Bxx(BNE); break;
  case k_opcode_BPL: ASM_Bxx(BPL); break;
  case k_opcode_BVC: ASM_Bxx(BVC); break;
  case k_opcode_BVS: ASM_Bxx(BVS); break;
  case k_opcode_CLC: asm_emit_instruction_CLC(p_dest_buf); break;
  case k_opcode_CLD: asm_emit_instruction_CLD(p_dest_buf); break;
  case k_opcode_CLI: asm_emit_instruction_CLI(p_dest_buf); break;
  case k_opcode_CLV: asm_emit_instruction_CLV(p_dest_buf); break;
  case k_opcode_DEC_value: ASM(DEC_value); break;
  case k_opcode_DEX: asm_emit_instruction_DEX(p_dest_buf); break;
  case k_opcode_DEY: asm_emit_instruction_DEY(p_dest_buf); break;
  case k_opcode_INC_value: ASM(INC_value); break;
  case k_opcode_INX: asm_emit_instruction_INX(p_dest_buf); break;
  case k_opcode_INY: asm_emit_instruction_INY(p_dest_buf); break;
  case k_opcode_JMP: ASM_Bxx(JMP); break;
  case k_opcode_LSR_acc: ASM(LSR_ACC); break;
  case k_opcode_LSR_value: ASM(LSR_value); break;
  case k_opcode_NOP:
    /* We don't really have to emit anything for a NOP, but for now and for
     * good readability, we'll emit a host NOP.
     * We actually emit two host NOPs to cover the size of the invalidation
     * sequence.
     */
    asm_emit_instruction_REAL_NOP(p_dest_buf);
    asm_emit_instruction_REAL_NOP(p_dest_buf);
    break;
  case k_opcode_PHA: asm_emit_instruction_PHA(p_dest_buf); break;
  case k_opcode_PHP: asm_emit_instruction_PHP(p_dest_buf); break;
  case k_opcode_PLA: asm_emit_instruction_PLA(p_dest_buf); break;
  case k_opcode_PLP: asm_emit_instruction_PLP(p_dest_buf); break;
  case k_opcode_ROL_acc: ASM(ROL_ACC); break;
  case k_opcode_ROL_value: ASM(ROL_value); break;
  case k_opcode_ROR_acc: ASM(ROR_ACC); break;
  case k_opcode_ROR_value: ASM(ROR_value); break;
  case k_opcode_SEC: asm_emit_instruction_SEC(p_dest_buf); break;
  case k_opcode_SED: asm_emit_instruction_SED(p_dest_buf); break;
  case k_opcode_SEI: asm_emit_instruction_SEI(p_dest_buf); break;
  case k_opcode_TAX: asm_emit_instruction_TAX(p_dest_buf); break;
  case k_opcode_TAY: asm_emit_instruction_TAY(p_dest_buf); break;
  case k_opcode_TSX: asm_emit_instruction_TSX(p_dest_buf); break;
  case k_opcode_TXA: asm_emit_instruction_TXA(p_dest_buf); break;
  case k_opcode_TXS: asm_emit_instruction_TXS(p_dest_buf); break;
  case k_opcode_TYA: asm_emit_instruction_TYA(p_dest_buf); break;
  case k_opcode_x64_check_page_crossing_ABX:
    value1 &= 0xFF;
    value2 = K_ASM_TABLE_PAGE_WRAP_CYCLE_INV;
    ASM_ADDR_U32_RAW(check_page_crossing_ABX);
    ASM(check_page_crossing_adjust);
    break;
  case k_opcode_x64_check_page_crossing_ABY:
    value1 &= 0xFF;
    value2 = K_ASM_TABLE_PAGE_WRAP_CYCLE_INV;
    ASM_ADDR_U32_RAW(check_page_crossing_ABY);
    ASM(check_page_crossing_adjust);
    break;
  case k_opcode_x64_check_page_crossing_IDY:
    ASM(save_addr_low_byte);
    ASM(check_page_crossing_y);
    ASM(check_page_crossing_adjust);
    break;
  case k_opcode_x64_check_page_crossing_IDY_X:
    ASM(save_addr_low_byte);
    ASM(check_page_crossing_x);
    ASM(check_page_crossing_adjust);
    break;
  case k_opcode_x64_load_ABS: ASM_ADDR_U32(load_ABS); break;
  case k_opcode_x64_load_ZPG: ASM_ADDR_U8(load_ZPG); break;
  case k_opcode_x64_mode_ABX_and_load:
    ASM_ADDR_U32_RAW(mode_abx_and_load);
    break;
  case k_opcode_x64_mode_ABX_store: ASM_ADDR_U32_RAW(mode_abx_store); break;
  case k_opcode_x64_mode_IDY_load:
    ASM_ADDR_U8(mode_IDY_load_mov1);
    value1 = ((value1 + 1) & 0xFF);
    ASM_ADDR_U8(mode_IDY_load_mov2);
    break;
  case k_opcode_x64_mode_IND:
    ASM_ADDR_U32(mode_IND_mov1);
    value1++;
    if ((value1 & 0xFF) == 0) value1 -= 0x100;
    ASM_ADDR_U32(mode_IND_mov2);
    break;
  case k_opcode_x64_mode_IND_nowrap:
    ASM_ADDR_U32(mode_IND_mov1);
    value1++;
    ASM_ADDR_U32(mode_IND_mov2);
    break;
  case k_opcode_x64_mode_IND8: ASM_ADDR_U32(mode_IND_mov1); break;
  case k_opcode_x64_mode_ZPX: asm_emit_jit_MODE_ZPX(p_dest_buf, value1); break;
  case k_opcode_x64_mode_ZPY: asm_emit_jit_MODE_ZPY(p_dest_buf, value1); break;
  case k_opcode_x64_store_ABS: ASM_ADDR_U32(store_ABS); break;
  case k_opcode_x64_store_ZPG: ASM_ADDR_U8(store_ZPG); break;
  case k_opcode_x64_write_inv_ABS:
    value1 = (K_JIT_CONTEXT_OFFSET_JIT_PTRS + (value1 * sizeof(uint32_t)));
    ASM_U32(write_inv_ABS);
    ASM(write_inv_commit);
    break;
  case k_opcode_x64_write_inv_ABX:
    ASM_U32(mode_ABX);
    ASM(write_inv);
    ASM(write_inv_commit);
    break;
  case k_opcode_x64_write_inv_ABY:
    ASM_U32(mode_ABY);
    ASM(write_inv);
    ASM(write_inv_commit);
    break;
  case k_opcode_x64_write_inv_IDY:
    ASM(write_inv_IDY);
    ASM(write_inv_commit);
    break;
  case k_opcode_x64_write_inv_IDY_n:
    value1 *= 4;
    value2 = K_JIT_CONTEXT_OFFSET_JIT_PTRS;
    ASM_ADDR_U32_RAW(write_inv_IDY_n);
    ASM(write_inv_commit);
    break;
  case k_opcode_x64_write_inv_IDY_X:
    ASM(write_inv_IDY_X);
    ASM(write_inv_commit);
    break;
  case k_opcode_x64_ADC_ABS: ASM_ADDR_U32(ADC_ABS); break;
  case k_opcode_x64_ADC_ABX: ASM_ADDR_U32_RAW(ADC_ABX); break;
  case k_opcode_x64_ADC_ABY: ASM_ADDR_U32_RAW(ADC_ABY); break;
  case k_opcode_x64_ADC_addr: ASM(ADC_addr); break;
  case k_opcode_x64_ADC_addr_n: ASM_ADDR_U8(ADC_addr_n); break;
  case k_opcode_x64_ADC_addr_X: ASM(ADC_addr_X); break;
  case k_opcode_x64_ADC_addr_Y: ASM(ADC_addr_Y); break;
  case k_opcode_x64_ADC_IMM: ASM_U8(ADC_IMM); break;
  case k_opcode_x64_ADC_ZPG: ASM_ADDR_U8(ADC_ZPG); break;
  case k_opcode_x64_ADD_ABS: ASM_ADDR_U32(ADD_ABS); break;
  case k_opcode_x64_ADD_ABX: ASM_ADDR_U32_RAW(ADD_ABX); break;
  case k_opcode_x64_ADD_ABY: ASM_ADDR_U32_RAW(ADD_ABY); break;
  case k_opcode_x64_ADD_addr: ASM(ADD_addr); break;
  case k_opcode_x64_ADD_addr_n: ASM_ADDR_U8(ADD_addr_n); break;
  case k_opcode_x64_ADD_addr_X: ASM(ADD_addr_X); break;
  case k_opcode_x64_ADD_addr_Y: ASM(ADD_addr_Y); break;
  case k_opcode_x64_ADD_IMM: ASM_U8(ADD_IMM); break;
  case k_opcode_x64_ADD_ZPG: ASM_ADDR_U8(ADD_ZPG); break;
  case k_opcode_x64_ALR_IMM: ASM_U8(ALR_IMM_and); ASM(ALR_IMM_shr); break;
  case k_opcode_x64_AND_ABS: ASM_ADDR_U32(AND_ABS); break;
  case k_opcode_x64_AND_ABX: ASM_ADDR_U32_RAW(AND_ABX); break;
  case k_opcode_x64_AND_ABY: ASM_ADDR_U32_RAW(AND_ABY); break;
  case k_opcode_x64_AND_addr: ASM(AND_addr); break;
  case k_opcode_x64_AND_addr_n: ASM_ADDR_U8(AND_addr_n); break;
  case k_opcode_x64_AND_addr_X: ASM(AND_addr_X); break;
  case k_opcode_x64_AND_addr_Y: ASM(AND_addr_Y); break;
  case k_opcode_x64_AND_IMM: ASM_U8(AND_IMM); break;
  case k_opcode_x64_AND_ZPG: ASM_ADDR_U8(AND_ZPG); break;
  case k_opcode_x64_ASL_ABS: ASM_ADDR_U32(ASL_ABS); break;
  case k_opcode_x64_ASL_ACC_n: ASM_U8(ASL_ACC_n); break;
  case k_opcode_x64_ASL_ZPG: ASM_ADDR_U8(ASL_ZPG); break;
  case k_opcode_x64_CMP_ABS: ASM_ADDR_U32(CMP_ABS); break;
  case k_opcode_x64_CMP_ABX: ASM_ADDR_U32_RAW(CMP_ABX); break;
  case k_opcode_x64_CMP_ABY: ASM_ADDR_U32_RAW(CMP_ABY); break;
  case k_opcode_x64_CMP_addr: ASM(CMP_addr); break;
  case k_opcode_x64_CMP_addr_n: ASM_ADDR_U8(CMP_addr_n); break;
  case k_opcode_x64_CMP_addr_X: ASM(CMP_addr_X); break;
  case k_opcode_x64_CMP_addr_Y: ASM(CMP_addr_Y); break;
  case k_opcode_x64_CMP_IMM: ASM_U8(CMP_IMM); break;
  case k_opcode_x64_CMP_ZPG: ASM_ADDR_U8(CMP_ZPG); break;
  case k_opcode_x64_CPX_ABS: ASM_ADDR_U32(CPX_ABS); break;
  case k_opcode_x64_CPX_IMM: ASM_U8(CPX_IMM); break;
  case k_opcode_x64_CPX_ZPG: ASM_ADDR_U8(CPX_ZPG); break;
  case k_opcode_x64_CPY_ABS: ASM_ADDR_U32(CPY_ABS); break;
  case k_opcode_x64_CPY_IMM: ASM_U8(CPY_IMM); break;
  case k_opcode_x64_CPY_ZPG: ASM_ADDR_U8(CPY_ZPG); break;
  case k_opcode_x64_DEC_ABS: ASM_ADDR_U32(DEC_ABS); break;
  case k_opcode_x64_DEC_ZPG: ASM_ADDR_U8(DEC_ZPG); break;
  case k_opcode_x64_EOR_ABS: ASM_ADDR_U32(EOR_ABS); break;
  case k_opcode_x64_EOR_ABX: ASM_ADDR_U32_RAW(EOR_ABX); break;
  case k_opcode_x64_EOR_ABY: ASM_ADDR_U32_RAW(EOR_ABY); break;
  case k_opcode_x64_EOR_addr: ASM(EOR_addr); break;
  case k_opcode_x64_EOR_addr_n: ASM_ADDR_U8(EOR_addr_n); break;
  case k_opcode_x64_EOR_addr_X: ASM(EOR_addr_X); break;
  case k_opcode_x64_EOR_addr_Y: ASM(EOR_addr_Y); break;
  case k_opcode_x64_EOR_IMM: ASM_U8(EOR_IMM); break;
  case k_opcode_x64_EOR_ZPG: ASM_ADDR_U8(EOR_ZPG); break;
  case k_opcode_x64_INC_ABS: ASM_ADDR_U32(INC_ABS); break;
  case k_opcode_x64_INC_ZPG: ASM_ADDR_U8(INC_ZPG); break;
  case k_opcode_x64_LDA_addr: ASM(LDA_addr); break;
  case k_opcode_x64_LDA_addr_n: ASM_ADDR_U8(LDA_addr_n); break;
  case k_opcode_x64_LDA_addr_X: ASM(LDA_addr_X); break;
  case k_opcode_x64_LDA_addr_Y: ASM(LDA_addr_Y); break;
  case k_opcode_x64_LDA_ABS: ASM_ADDR_U32(LDA_ABS); break;
  case k_opcode_x64_LDA_ABX: ASM_ADDR_U32_RAW(LDA_ABX); break;
  case k_opcode_x64_LDA_ABY: ASM_ADDR_U32_RAW(LDA_ABY); break;
  case k_opcode_x64_LDA_IMM: ASM_U32(LDA_IMM); break;
  case k_opcode_x64_LDA_ZPG: ASM_ADDR_U8(LDA_ZPG); break;
  case k_opcode_x64_LDX_addr: ASM(LDX_addr); break;
  case k_opcode_x64_LDX_ABS: ASM_ADDR_U32(LDX_ABS); break;
  case k_opcode_x64_LDX_ABY: ASM_ADDR_U32_RAW(LDX_ABY); break;
  case k_opcode_x64_LDX_IMM: ASM_U8(LDX_IMM); break;
  case k_opcode_x64_LDX_ZPG: ASM_ADDR_U8(LDX_ZPG); break;
  case k_opcode_x64_LDY_addr: ASM(LDY_addr); break;
  case k_opcode_x64_LDY_ABS: ASM_ADDR_U32(LDY_ABS); break;
  case k_opcode_x64_LDY_ABX: ASM_ADDR_U32_RAW(LDY_ABX); break;
  case k_opcode_x64_LDY_IMM: ASM_U8(LDY_IMM); break;
  case k_opcode_x64_LDY_ZPG: ASM_ADDR_U8(LDY_ZPG); break;
  case k_opcode_x64_LSR_ABS: ASM_ADDR_U32(LSR_ABS); break;
  case k_opcode_x64_LSR_ACC_n: ASM_U8(LSR_ACC_n); break;
  case k_opcode_x64_LSR_ZPG: ASM_ADDR_U8(LSR_ZPG); break;
  case k_opcode_x64_ORA_ABS: ASM_ADDR_U32(ORA_ABS); break;
  case k_opcode_x64_ORA_ABX: ASM_ADDR_U32_RAW(ORA_ABX); break;
  case k_opcode_x64_ORA_ABY: ASM_ADDR_U32_RAW(ORA_ABY); break;
  case k_opcode_x64_ORA_addr: ASM(ORA_addr); break;
  case k_opcode_x64_ORA_addr_n: ASM_ADDR_U8(ORA_addr_n); break;
  case k_opcode_x64_ORA_addr_X: ASM(ORA_addr_X); break;
  case k_opcode_x64_ORA_addr_Y: ASM(ORA_addr_Y); break;
  case k_opcode_x64_ORA_IMM: ASM_U8(ORA_IMM); break;
  case k_opcode_x64_ORA_ZPG: ASM_ADDR_U8(ORA_ZPG); break;
  case k_opcode_x64_ROL_ACC_n: ASM_U8(ROL_ACC_n); break;
  case k_opcode_x64_ROR_ACC_n: ASM_U8(ROR_ACC_n); break;
  case k_opcode_x64_SAX_ABS: ASM_ADDR_U32(SAX_ABS); break;
  case k_opcode_x64_SBC_ABS: ASM_ADDR_U32(SBC_ABS); break;
  case k_opcode_x64_SBC_ABX: ASM_ADDR_U32_RAW(SBC_ABX); break;
  case k_opcode_x64_SBC_ABY: ASM_ADDR_U32_RAW(SBC_ABY); break;
  case k_opcode_x64_SBC_addr: ASM(SBC_addr); break;
  case k_opcode_x64_SBC_addr_n: ASM_ADDR_U8(SBC_addr_n); break;
  case k_opcode_x64_SBC_addr_X: ASM(SBC_addr_X); break;
  case k_opcode_x64_SBC_addr_Y: ASM(SBC_addr_Y); break;
  case k_opcode_x64_SBC_IMM: ASM_U8(SBC_IMM); break;
  case k_opcode_x64_SBC_ZPG: ASM_ADDR_U8(SBC_ZPG); break;
  case k_opcode_x64_SLO_ABS: asm_emit_jit_SLO_ABS(p_dest_buf, value1); break;
  case k_opcode_x64_STA_addr: ASM(STA_addr); break;
  case k_opcode_x64_STA_addr_n:
    value2 = K_BBC_MEM_WRITE_IND_ADDR;
    ASM_ADDR_U32_RAW(STA_addr_n);
    break;
  case k_opcode_x64_STA_addr_X: ASM(STA_addr_X); break;
  case k_opcode_x64_STA_addr_Y: ASM(STA_addr_Y); break;
  case k_opcode_x64_STA_ABS: ASM_ADDR_U32(STA_ABS); break;
  case k_opcode_x64_STA_ABX: ASM_ADDR_U32_RAW(STA_ABX); break;
  case k_opcode_x64_STA_ABY: ASM_ADDR_U32_RAW(STA_ABY); break;
  case k_opcode_x64_STA_ZPG: ASM_ADDR_U8(STA_ZPG); break;
  case k_opcode_x64_STX_addr: ASM(STX_addr); break;
  case k_opcode_x64_STX_ABS: ASM_ADDR_U32(STX_ABS); break;
  case k_opcode_x64_STX_ZPG: ASM_ADDR_U8(STX_ZPG); break;
  case k_opcode_x64_STY_addr: ASM(STY_addr); break;
  case k_opcode_x64_STY_ABS: ASM_ADDR_U32(STY_ABS); break;
  case k_opcode_x64_STY_ZPG: ASM_ADDR_U8(STY_ZPG); break;
  case k_opcode_x64_SUB_ABS: ASM_ADDR_U32(SUB_ABS); break;
  case k_opcode_x64_SUB_ABX: ASM_ADDR_U32_RAW(SUB_ABX); break;
  case k_opcode_x64_SUB_ABY: ASM_ADDR_U32_RAW(SUB_ABY); break;
  case k_opcode_x64_SUB_addr: ASM(SUB_addr); break;
  case k_opcode_x64_SUB_addr_n: ASM_ADDR_U8(SUB_addr_n); break;
  case k_opcode_x64_SUB_addr_X: ASM(SUB_addr_X); break;
  case k_opcode_x64_SUB_addr_Y: ASM(SUB_addr_Y); break;
  case k_opcode_x64_SUB_IMM: ASM_U8(SUB_IMM); break;
  case k_opcode_x64_SUB_ZPG: ASM_ADDR_U8(SUB_ZPG); break;
  case k_opcode_addr_set:
    assert(0);
    break;
  case k_opcode_value_set:
    assert(0);
    break;
  default:
    assert(0);
    break;
  }
}
