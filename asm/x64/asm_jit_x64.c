#include "../asm_jit.h"

#include "../../defs_6502.h"
#include "../../os_alloc.h"
#include "../../util.h"
#include "../asm_common.h"
#include "../asm_defs_host.h"
#include "../asm_jit_defs.h"
/* For REG_MEM_OFFSET. */
#include "asm_defs_registers_x64.h"

#include <assert.h>

static struct os_alloc_mapping* s_p_mapping_trampolines;

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

void
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
asm_jit_supports_optimizer(void) {
  return 1;
}

int
asm_jit_supports_uopcode(int32_t uopcode) {
  (void) uopcode;
  return 1;
}

struct asm_jit_struct*
asm_jit_init(void* p_jit_base) {
  size_t mapping_size;
  uint32_t i;
  void* p_trampolines;
  struct util_buffer* p_temp_buf = util_buffer_create();

  (void) p_jit_base;

  assert(s_p_mapping_trampolines == NULL);

  /* This is the mapping that holds trampolines to jump out of JIT. These
   * one-per-6502-address trampolines enable the core JIT code to be simpler
   * and smaller, at the expense of more complicated bridging between JIT and
   * interp.
   */
  mapping_size = (k_6502_addr_space_size * K_BBC_JIT_TRAMPOLINE_BYTES);
  s_p_mapping_trampolines =
      os_alloc_get_mapping((void*) K_BBC_JIT_TRAMPOLINES_ADDR, mapping_size);
  p_trampolines = os_alloc_get_mapping_addr(s_p_mapping_trampolines);
  os_alloc_make_mapping_read_write_exec(p_trampolines, mapping_size);
  util_buffer_setup(p_temp_buf, p_trampolines, mapping_size);
  asm_fill_with_trap(p_temp_buf);

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    /* Initialize JIT trampoline. */
    util_buffer_setup(
        p_temp_buf,
        (p_trampolines + (i * K_BBC_JIT_TRAMPOLINE_BYTES)),
        K_BBC_JIT_TRAMPOLINE_BYTES);
    asm_emit_jit_jump_interp_trampoline(p_temp_buf, i);
  }

  util_buffer_destroy(p_temp_buf);

  return NULL;
}

void
asm_jit_destroy(struct asm_jit_struct* p_asm) {
  (void) p_asm;

  assert(s_p_mapping_trampolines != NULL);

  os_alloc_free_mapping(s_p_mapping_trampolines);
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
                     uint16_t addr_6502,
                     void* p_fault_addr,
                     int is_write) {
  int inaccessible_indirect_page;
  int ff_fault_fixup;
  int bcd_fault_fixup;
  int stack_wrap_fault_fixup;
  int wrap_indirect_read;
  int wrap_indirect_write;

  (void) p_asm;

  /* The indirect page fault occurs when an indirect addressing mode is used
   * to access 0xF000 - 0xFFFF, primarily of interest due to the hardware
   * registers. Using a fault + fixup here is a good performance boost for the
   * common case.
   * This fault is also encountered in the Windows port, which needs to use it
   * for ROM writes.
   */
  inaccessible_indirect_page = 0;
  /* The 0xFF page wrap fault occurs when a word fetch is performed at the end
   * of a page, where that page wraps. e.g. idx mode fetching the address from
   * 0xFF. Using a fault + fixup here makes the code footprint for idx mode
   * addressing smaller.
   */
  ff_fault_fixup = 0;
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
          ((void*) K_BBC_MEM_READ_FULL_ADDR + K_6502_ADDR_SPACE_SIZE)) {
    ff_fault_fixup = 1;
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
      !ff_fault_fixup &&
      !bcd_fault_fixup &&
      !stack_wrap_fault_fixup &&
      !wrap_indirect_read &&
      !wrap_indirect_write) {
    return 0;
  }

  /* Fault is recognized.
   * Bounce into the interpreter via the trampolines.
   */
  *p_pc =
      (K_BBC_JIT_TRAMPOLINES_ADDR + (addr_6502 * K_BBC_JIT_TRAMPOLINE_BYTES));
  return 1;
}

void
asm_jit_invalidate_code_at(void* p) {
  uint16_t* p_dst = (uint16_t*) p;
  /* call [rdi] */
  *p_dst = 0x17ff;
}

void
asm_emit_jit_invalidated(struct util_buffer* p_buf) {
  /* call [rdi] */
  util_buffer_add_2b(p_buf, 0xff, 0x17);
}

void
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

void
asm_emit_jit_check_countdown_no_save_nz_flags(struct util_buffer* p_buf,
                                              uint32_t count,
                                              void* p_trampoline) {
  void asm_jit_check_countdown_no_save_nz_flags_8bit(void);
  void asm_jit_check_countdown_no_save_nz_flags_8bit_count_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_8bit_jump_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_8bit_END(void);
  void asm_jit_check_countdown_no_save_nz_flags(void);
  void asm_jit_check_countdown_no_save_nz_flags_count_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_jump_patch(void);
  void asm_jit_check_countdown_no_save_nz_flags_END(void);

  size_t offset = util_buffer_get_pos(p_buf);

  if (count <= 127) {
    asm_copy(p_buf,
             asm_jit_check_countdown_no_save_nz_flags_8bit,
             asm_jit_check_countdown_no_save_nz_flags_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_check_countdown_no_save_nz_flags_8bit,
                   asm_jit_check_countdown_no_save_nz_flags_8bit_count_patch,
                   count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown_no_save_nz_flags_8bit,
                   asm_jit_check_countdown_no_save_nz_flags_8bit_jump_patch,
                   p_trampoline);
  } else {
    asm_copy(p_buf,
             asm_jit_check_countdown_no_save_nz_flags,
             asm_jit_check_countdown_no_save_nz_flags_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_check_countdown_no_save_nz_flags,
                  asm_jit_check_countdown_no_save_nz_flags_count_patch,
                  count);
    asm_patch_jump(p_buf,
                   offset,
                   asm_jit_check_countdown_no_save_nz_flags,
                   asm_jit_check_countdown_no_save_nz_flags_jump_patch,
                   p_trampoline);
  }
}

void
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

void
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

void
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
  assert(K_BBC_JIT_TRAMPOLINES_ADDR > K_BBC_JIT_ADDR);

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

void
asm_emit_jit_for_testing(struct util_buffer* p_buf) {
  void asm_jit_for_testing(void);
  void asm_jit_for_testing_END(void);
  asm_copy(p_buf, asm_jit_for_testing, asm_jit_for_testing_END);
}

void
asm_emit_jit_ADD_CYCLES(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_ADD_CYCLES(void);
  void asm_jit_ADD_CYCLES_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_ADD_CYCLES, asm_jit_ADD_CYCLES_END, value);
}

void
asm_emit_jit_ADD_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ADD_ZPG(void);
  void asm_jit_ADD_ZPG_END(void);
  void asm_jit_ADD_ABS(void);
  void asm_jit_ADD_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ADD_ZPG,
                        asm_jit_ADD_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ADD_ABS,
                       asm_jit_ADD_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_ADD_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ADD_ABX(void);
  void asm_jit_ADD_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADD_ABX,
                     asm_jit_ADD_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_ADD_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ADD_ABY(void);
  void asm_jit_ADD_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADD_ABY,
                     asm_jit_ADD_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_ADD_IMM(void);
  void asm_jit_ADD_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_ADD_IMM, asm_jit_ADD_IMM_END, value);
}

void
asm_emit_jit_ADD_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_ADD_SCRATCH(void);
  void asm_jit_ADD_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADD_SCRATCH,
                     asm_jit_ADD_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_ADD_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_ADD_SCRATCH_Y(void);
  void asm_jit_ADD_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_ADD_SCRATCH_Y, asm_jit_ADD_SCRATCH_Y_END);
}

void
asm_emit_jit_ADDR_CHECK(struct util_buffer* p_buf,
                        struct util_buffer* p_buf_epilog,
                        uint16_t addr) {
  /* In the x64 model, indirect address checking is done by faulting and
   * fixup.
   */
  (void) p_buf;
  (void) p_buf_epilog;
  (void) addr;
}

void
asm_emit_jit_CHECK_BCD(struct util_buffer* p_buf,
                       struct util_buffer* p_epilog_buf,
                       uint16_t addr) {
  void asm_jit_CHECK_BCD(void);
  void asm_jit_CHECK_BCD_END(void);
  (void) p_epilog_buf;
  (void) addr;
  asm_copy(p_buf, asm_jit_CHECK_BCD, asm_jit_CHECK_BCD_END);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_n(struct util_buffer* p_buf,
                                           uint8_t n) {
  void asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n(void);
  void asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n_mov_patch(void);
  void asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n,
                asm_jit_CHECK_PAGE_CROSSING_SCRATCH_n_mov_patch,
                (K_ASM_TABLE_PAGE_WRAP_CYCLE_INV + n));
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_X(struct util_buffer* p_buf) {
  void asm_jit_CHECK_PAGE_CROSSING_SCRATCH_X(void);
  void asm_jit_CHECK_PAGE_CROSSING_SCRATCH_X_END(void);
  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_X,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_X_END);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_CHECK_PAGE_CROSSING_SCRATCH_Y(void);
  void asm_jit_CHECK_PAGE_CROSSING_SCRATCH_Y_END(void);
  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_Y,
           asm_jit_CHECK_PAGE_CROSSING_SCRATCH_Y_END);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_X_n(struct util_buffer* p_buf,
                                     uint16_t addr) {
  void asm_jit_CHECK_PAGE_CROSSING_X_n(void);
  void asm_jit_CHECK_PAGE_CROSSING_X_n_mov_patch(void);
  void asm_jit_CHECK_PAGE_CROSSING_X_n_END(void);
  uint32_t value;

  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_X_n,
           asm_jit_CHECK_PAGE_CROSSING_X_n_END);
  value = K_ASM_TABLE_PAGE_WRAP_CYCLE_INV;
  value += (addr & 0xFF);
  asm_patch_int(p_buf,
                offset,
                asm_jit_CHECK_PAGE_CROSSING_X_n,
                asm_jit_CHECK_PAGE_CROSSING_X_n_mov_patch,
                value);
}

void
asm_emit_jit_CHECK_PAGE_CROSSING_Y_n(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_CHECK_PAGE_CROSSING_Y_n(void);
  void asm_jit_CHECK_PAGE_CROSSING_Y_n_mov_patch(void);
  void asm_jit_CHECK_PAGE_CROSSING_Y_n_END(void);
  uint32_t value;

  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf,
           asm_jit_CHECK_PAGE_CROSSING_Y_n,
           asm_jit_CHECK_PAGE_CROSSING_Y_n_END);
  value = K_ASM_TABLE_PAGE_WRAP_CYCLE_INV;
  value += (addr & 0xFF);
  asm_patch_int(p_buf,
                offset,
                asm_jit_CHECK_PAGE_CROSSING_Y_n,
                asm_jit_CHECK_PAGE_CROSSING_Y_n_mov_patch,
                value);
}

void
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

void
asm_emit_jit_CLEAR_CARRY(struct util_buffer* p_buf) {
  void asm_jit_CLEAR_CARRY(void);
  void asm_jit_CLEAR_CARRY_END(void);
  asm_copy(p_buf, asm_jit_CLEAR_CARRY, asm_jit_CLEAR_CARRY_END);
}

void
asm_emit_jit_FLAG_MEM(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_FLAG_MEM(void);
  void asm_jit_FLAG_MEM_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_FLAG_MEM, asm_jit_FLAG_MEM_END);
  asm_patch_int(p_buf,
                (offset - 1),
                asm_jit_FLAG_MEM,
                asm_jit_FLAG_MEM_END,
                (addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_INVERT_CARRY(struct util_buffer* p_buf) {
  void asm_jit_INVERT_CARRY(void);
  void asm_jit_INVERT_CARRY_END(void);
  asm_copy(p_buf, asm_jit_INVERT_CARRY, asm_jit_INVERT_CARRY_END);
}

void
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
                ((K_BBC_JIT_ADDR >> K_BBC_JIT_BYTES_SHIFT) + n));
}

void
asm_emit_jit_LDA_Z(struct util_buffer* p_buf) {
  void asm_jit_LDA_Z(void);
  void asm_jit_LDA_Z_END(void);
  asm_copy(p_buf, asm_jit_LDA_Z, asm_jit_LDA_Z_END);
}

void
asm_emit_jit_LDX_Z(struct util_buffer* p_buf) {
  void asm_jit_LDX_Z(void);
  void asm_jit_LDX_Z_END(void);
  asm_copy(p_buf, asm_jit_LDX_Z, asm_jit_LDX_Z_END);
}

void
asm_emit_jit_LDY_Z(struct util_buffer* p_buf) {
  void asm_jit_LDY_Z(void);
  void asm_jit_LDY_Z_END(void);
  asm_copy(p_buf, asm_jit_LDY_Z, asm_jit_LDY_Z_END);
}

void
asm_emit_jit_LOAD_CARRY_FOR_BRANCH(struct util_buffer* p_buf) {
  void asm_jit_LOAD_CARRY_FOR_BRANCH(void);
  void asm_jit_LOAD_CARRY_FOR_BRANCH_END(void);
  asm_copy(p_buf,
           asm_jit_LOAD_CARRY_FOR_BRANCH,
           asm_jit_LOAD_CARRY_FOR_BRANCH_END);
}

void
asm_emit_jit_LOAD_CARRY_FOR_CALC(struct util_buffer* p_buf) {
  void asm_jit_LOAD_CARRY_FOR_CALC(void);
  void asm_jit_LOAD_CARRY_FOR_CALC_END(void);
  asm_copy(p_buf,
           asm_jit_LOAD_CARRY_FOR_CALC,
           asm_jit_LOAD_CARRY_FOR_CALC_END);
}

void
asm_emit_jit_LOAD_CARRY_INV_FOR_CALC(struct util_buffer* p_buf) {
  void asm_jit_LOAD_CARRY_INV_FOR_CALC(void);
  void asm_jit_LOAD_CARRY_INV_FOR_CALC_END(void);
  asm_copy(p_buf,
           asm_jit_LOAD_CARRY_INV_FOR_CALC,
           asm_jit_LOAD_CARRY_INV_FOR_CALC_END);
}

void
asm_emit_jit_LOAD_OVERFLOW(struct util_buffer* p_buf) {
  void asm_jit_LOAD_OVERFLOW(void);
  void asm_jit_LOAD_OVERFLOW_END(void);
  asm_copy(p_buf, asm_jit_LOAD_OVERFLOW, asm_jit_LOAD_OVERFLOW_END);
}

void
asm_emit_jit_LOAD_SCRATCH_8(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_LOAD_SCRATCH_8(void);
  void asm_jit_LOAD_SCRATCH_8_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_LOAD_SCRATCH_8,
                     asm_jit_LOAD_SCRATCH_8_END,
                     (addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_LOAD_SCRATCH_16(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_MODE_IND_16(void);
  void asm_jit_MODE_IND_16_mov1_patch(void);
  void asm_jit_MODE_IND_16_mov2_patch(void);
  void asm_jit_MODE_IND_16_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_MODE_IND_16, asm_jit_MODE_IND_16_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov1_patch,
                (addr - REG_MEM_OFFSET));
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov2_patch,
                ((addr + 1) - REG_MEM_OFFSET));
}

void
asm_emit_jit_MODE_ABX(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_MODE_ABX(void);
  void asm_jit_MODE_ABX_END(void);
  asm_copy_patch_u32(p_buf, asm_jit_MODE_ABX, asm_jit_MODE_ABX_END, addr);
}

void
asm_emit_jit_MODE_ABY(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_MODE_ABY(void);
  void asm_jit_MODE_ABY_END(void);
  asm_copy_patch_u32(p_buf, asm_jit_MODE_ABY, asm_jit_MODE_ABY_END, addr);
}

void
asm_emit_jit_MODE_IND_8(struct util_buffer* p_buf, uint8_t addr) {
  void asm_jit_MODE_IND_8(void);
  void asm_jit_MODE_IND_8_mov1_patch(void);
  void asm_jit_MODE_IND_8_mov2_patch(void);
  void asm_jit_MODE_IND_8_END(void);
  uint16_t next_addr;

  size_t offset = util_buffer_get_pos(p_buf);

  if (addr == 0xFF) {
    next_addr = 0;
  } else {
    next_addr = (addr + 1);
  }

  asm_copy(p_buf, asm_jit_MODE_IND_8, asm_jit_MODE_IND_8_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_MODE_IND_8,
                 asm_jit_MODE_IND_8_mov1_patch,
                 (addr - REG_MEM_OFFSET));
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_MODE_IND_8,
                 asm_jit_MODE_IND_8_mov2_patch,
                 (next_addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_MODE_IND_16(struct util_buffer* p_buf,
                         uint16_t addr,
                         uint32_t segment) {
  void asm_jit_MODE_IND_16(void);
  void asm_jit_MODE_IND_16_mov1_patch(void);
  void asm_jit_MODE_IND_16_mov2_patch(void);
  void asm_jit_MODE_IND_16_END(void);
  uint16_t next_addr;

  size_t offset = util_buffer_get_pos(p_buf);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);

  /* On the 6502, (e.g.) JMP (&10FF) does not fetch across the page boundary. */
  if ((addr & 0xFF) == 0xFF) {
    next_addr = (addr & 0xFF00);
  } else {
    next_addr = (addr + 1);
  }

  asm_copy(p_buf, asm_jit_MODE_IND_16, asm_jit_MODE_IND_16_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov1_patch,
                (addr - REG_MEM_OFFSET + delta));
  asm_patch_int(p_buf,
                offset,
                asm_jit_MODE_IND_16,
                asm_jit_MODE_IND_16_mov2_patch,
                (next_addr - REG_MEM_OFFSET + delta));
}

void
asm_emit_jit_MODE_IND_SCRATCH_8(struct util_buffer* p_buf) {
  void asm_jit_MODE_IND_SCRATCH_8(void);
  void asm_jit_MODE_IND_SCRATCH_8_END(void);
  asm_copy(p_buf, asm_jit_MODE_IND_SCRATCH_8, asm_jit_MODE_IND_SCRATCH_8_END);
}

void
asm_emit_jit_MODE_IND_SCRATCH_16(struct util_buffer* p_buf) {
  void asm_jit_MODE_IND_SCRATCH_16(void);
  void asm_jit_MODE_IND_SCRATCH_16_END(void);
  asm_copy(p_buf, asm_jit_MODE_IND_SCRATCH_16, asm_jit_MODE_IND_SCRATCH_16_END);
}

void
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

void
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

void
asm_emit_jit_PULL_16(struct util_buffer* p_buf) {
  void asm_jit_PULL_16(void);
  void asm_jit_PULL_16_END(void);
  asm_copy(p_buf, asm_jit_PULL_16, asm_jit_PULL_16_END);
}

void
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

void
asm_emit_jit_SAVE_CARRY(struct util_buffer* p_buf) {
  void asm_jit_SAVE_CARRY(void);
  void asm_jit_SAVE_CARRY_END(void);
  asm_copy(p_buf, asm_jit_SAVE_CARRY, asm_jit_SAVE_CARRY_END);
}

void
asm_emit_jit_SAVE_CARRY_INV(struct util_buffer* p_buf) {
  void asm_jit_SAVE_CARRY_INV(void);
  void asm_jit_SAVE_CARRY_INV_END(void);
  asm_copy(p_buf, asm_jit_SAVE_CARRY_INV, asm_jit_SAVE_CARRY_INV_END);
}

void
asm_emit_jit_SAVE_OVERFLOW(struct util_buffer* p_buf) {
  void asm_jit_SAVE_OVERFLOW(void);
  void asm_jit_SAVE_OVERFLOW_END(void);
  asm_copy(p_buf, asm_jit_SAVE_OVERFLOW, asm_jit_SAVE_OVERFLOW_END);
}

void
asm_emit_jit_SET_CARRY(struct util_buffer* p_buf) {
  void asm_jit_SET_CARRY(void);
  void asm_jit_SET_CARRY_END(void);
  asm_copy(p_buf, asm_jit_SET_CARRY, asm_jit_SET_CARRY_END);
}

void
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

void
asm_emit_jit_SUB_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_SUB_ZPG(void);
  void asm_jit_SUB_ZPG_END(void);
  void asm_jit_SUB_ABS(void);
  void asm_jit_SUB_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_SUB_ZPG,
                        asm_jit_SUB_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_SUB_ABS,
                       asm_jit_SUB_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_SUB_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_SUB_IMM(void);
  void asm_jit_SUB_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_SUB_IMM, asm_jit_SUB_IMM_END, value);
}

void
asm_emit_jit_WRITE_INV_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_WRITE_INV_ABS(void);
  void asm_jit_WRITE_INV_ABS_offset_patch(void);
  void asm_jit_WRITE_INV_ABS_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_WRITE_INV_ABS, asm_jit_WRITE_INV_ABS_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_WRITE_INV_ABS,
                asm_jit_WRITE_INV_ABS_offset_patch,
                (K_JIT_CONTEXT_OFFSET_JIT_PTRS + (addr * sizeof(uint32_t))));
}

void
asm_emit_jit_WRITE_INV_SCRATCH(struct util_buffer* p_buf) {
  void asm_jit_WRITE_INV_SCRATCH(void);
  void asm_jit_WRITE_INV_SCRATCH_END(void);
  asm_copy(p_buf, asm_jit_WRITE_INV_SCRATCH, asm_jit_WRITE_INV_SCRATCH_END);
}

void
asm_emit_jit_WRITE_INV_SCRATCH_n(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_WRITE_INV_SCRATCH_n_8bit(void);
  void asm_jit_WRITE_INV_SCRATCH_n_8bit_lea_patch(void);
  void asm_jit_WRITE_INV_SCRATCH_n_8bit_END(void);
  void asm_jit_WRITE_INV_SCRATCH_n_32bit(void);
  void asm_jit_WRITE_INV_SCRATCH_n_32bit_lea_patch(void);
  void asm_jit_WRITE_INV_SCRATCH_n_32bit_END(void);

  size_t offset = util_buffer_get_pos(p_buf);

  if (value < 0x80) {
    asm_copy(p_buf,
             asm_jit_WRITE_INV_SCRATCH_n_8bit,
             asm_jit_WRITE_INV_SCRATCH_n_8bit_END);
    asm_patch_byte(p_buf,
                   offset,
                   asm_jit_WRITE_INV_SCRATCH_n_8bit,
                   asm_jit_WRITE_INV_SCRATCH_n_8bit_lea_patch,
                   value);
  } else {
    asm_copy(p_buf,
             asm_jit_WRITE_INV_SCRATCH_n_32bit,
             asm_jit_WRITE_INV_SCRATCH_n_32bit_END);
    asm_patch_int(p_buf,
                  offset,
                  asm_jit_WRITE_INV_SCRATCH_n_32bit,
                  asm_jit_WRITE_INV_SCRATCH_n_32bit_lea_patch,
                  value);
  }
}

void
asm_emit_jit_WRITE_INV_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_WRITE_INV_SCRATCH_Y(void);
  void asm_jit_WRITE_INV_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_WRITE_INV_SCRATCH_Y, asm_jit_WRITE_INV_SCRATCH_Y_END);
}

void
asm_emit_jit_ADC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ADC_ZPG(void);
  void asm_jit_ADC_ZPG_END(void);
  void asm_jit_ADC_ABS(void);
  void asm_jit_ADC_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ADC_ZPG,
                        asm_jit_ADC_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ADC_ABS,
                       asm_jit_ADC_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_ADC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ADC_ABX(void);
  void asm_jit_ADC_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADC_ABX,
                     asm_jit_ADC_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_ADC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ADC_ABY(void);
  void asm_jit_ADC_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADC_ABY,
                     asm_jit_ADC_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_ADC_IMM(void);
  void asm_jit_ADC_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_ADC_IMM, asm_jit_ADC_IMM_END, value);
}

void
asm_emit_jit_ADC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_ADC_SCRATCH(void);
  void asm_jit_ADC_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ADC_SCRATCH,
                     asm_jit_ADC_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_ADC_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_ADC_SCRATCH_Y(void);
  void asm_jit_ADC_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_ADC_SCRATCH_Y, asm_jit_ADC_SCRATCH_Y_END);
}

void
asm_emit_jit_ALR_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_ALR_IMM(void);
  void asm_jit_ALR_IMM_patch_byte(void);
  void asm_jit_ALR_IMM_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ALR_IMM, asm_jit_ALR_IMM_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_ALR_IMM,
                 asm_jit_ALR_IMM_patch_byte,
                 value);
}

void
asm_emit_jit_AND_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_AND_ZPG(void);
  void asm_jit_AND_ZPG_END(void);
  void asm_jit_AND_ABS(void);
  void asm_jit_AND_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_AND_ZPG,
                        asm_jit_AND_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_AND_ABS,
                       asm_jit_AND_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_AND_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_AND_ABX(void);
  void asm_jit_AND_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_AND_ABX,
                     asm_jit_AND_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_AND_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_AND_ABY(void);
  void asm_jit_AND_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_AND_ABY,
                     asm_jit_AND_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_AND_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_AND_IMM(void);
  void asm_jit_AND_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_AND_IMM, asm_jit_AND_IMM_END, value);
}

void
asm_emit_jit_AND_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_AND_SCRATCH(void);
  void asm_jit_AND_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_AND_SCRATCH,
                     asm_jit_AND_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_AND_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_AND_SCRATCH_Y(void);
  void asm_jit_AND_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_AND_SCRATCH_Y, asm_jit_AND_SCRATCH_Y_END);
}

void
asm_emit_jit_ASL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ASL_ZPG(void);
  void asm_jit_ASL_ZPG_END(void);
  void asm_jit_ASL_ABS(void);
  void asm_jit_ASL_ABS_END(void);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ASL_ZPG,
                        asm_jit_ASL_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ASL_ABS,
                       asm_jit_ASL_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_ASL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ASL_ABS_RMW(void);
  void asm_jit_ASL_ABS_RMW_mov1_patch(void);
  void asm_jit_ASL_ABS_RMW_mov2_patch(void);
  void asm_jit_ASL_ABS_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ASL_ABS_RMW, asm_jit_ASL_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABS_RMW,
                asm_jit_ASL_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABS_RMW,
                asm_jit_ASL_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_ASL_ABX(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ASL_ABX(void);
  void asm_jit_ASL_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ASL_ABX,
                     asm_jit_ASL_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_ASL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ASL_ABX_RMW(void);
  void asm_jit_ASL_ABX_RMW_mov1_patch(void);
  void asm_jit_ASL_ABX_RMW_mov2_patch(void);
  void asm_jit_ASL_ABX_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ASL_ABX_RMW, asm_jit_ASL_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABX_RMW,
                asm_jit_ASL_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ASL_ABX_RMW,
                asm_jit_ASL_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_ASL_ACC(struct util_buffer* p_buf) {
  void asm_jit_ASL_ACC(void);
  void asm_jit_ASL_ACC_END(void);
  asm_copy(p_buf, asm_jit_ASL_ACC, asm_jit_ASL_ACC_END);
}

void
asm_emit_jit_ASL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  void asm_jit_ASL_ACC_n(void);
  void asm_jit_ASL_ACC_n_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_ASL_ACC_n, asm_jit_ASL_ACC_n_END, n);
}

void
asm_emit_jit_ASL_scratch(struct util_buffer* p_buf) {
  void asm_jit_ASL_scratch(void);
  void asm_jit_ASL_scratch_END(void);
  asm_copy(p_buf, asm_jit_ASL_scratch, asm_jit_ASL_scratch_END);
}

void
asm_emit_jit_BCC(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BCC(void);
  void asm_jit_BCC_END(void);
  void asm_jit_BCC_8bit(void);
  void asm_jit_BCC_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BCC,
                    asm_jit_BCC_END,
                    asm_jit_BCC_8bit,
                    asm_jit_BCC_8bit_END);
}

void
asm_emit_jit_BCS(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BCS(void);
  void asm_jit_BCS_END(void);
  void asm_jit_BCS_8bit(void);
  void asm_jit_BCS_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BCS,
                    asm_jit_BCS_END,
                    asm_jit_BCS_8bit,
                    asm_jit_BCS_8bit_END);
}

void
asm_emit_jit_BEQ(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BEQ(void);
  void asm_jit_BEQ_END(void);
  void asm_jit_BEQ_8bit(void);
  void asm_jit_BEQ_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BEQ,
                    asm_jit_BEQ_END,
                    asm_jit_BEQ_8bit,
                    asm_jit_BEQ_8bit_END);
}

void
asm_emit_jit_BIT(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_BIT_ZPG(void);
  void asm_jit_BIT_ZPG_END(void);
  void asm_jit_BIT_ABS(void);
  void asm_jit_BIT_ABS_END(void);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_BIT_ZPG,
                        asm_jit_BIT_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_BIT_ABS,
                       asm_jit_BIT_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
  asm_emit_instruction_BIT_common(p_buf);
}

void
asm_emit_jit_BMI(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BMI(void);
  void asm_jit_BMI_END(void);
  void asm_jit_BMI_8bit(void);
  void asm_jit_BMI_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BMI,
                    asm_jit_BMI_END,
                    asm_jit_BMI_8bit,
                    asm_jit_BMI_8bit_END);
}

void
asm_emit_jit_BNE(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BNE(void);
  void asm_jit_BNE_END(void);
  void asm_jit_BNE_8bit(void);
  void asm_jit_BNE_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BNE,
                    asm_jit_BNE_END,
                    asm_jit_BNE_8bit,
                    asm_jit_BNE_8bit_END);
}

void
asm_emit_jit_BPL(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BPL(void);
  void asm_jit_BPL_END(void);
  void asm_jit_BPL_8bit(void);
  void asm_jit_BPL_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BPL,
                    asm_jit_BPL_END,
                    asm_jit_BPL_8bit,
                    asm_jit_BPL_8bit_END);
}

void
asm_emit_jit_BVC(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BVC(void);
  void asm_jit_BVC_END(void);
  void asm_jit_BVC_8bit(void);
  void asm_jit_BVC_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BVC,
                    asm_jit_BVC_END,
                    asm_jit_BVC_8bit,
                    asm_jit_BVC_8bit_END);
}

void
asm_emit_jit_BVS(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_BVS(void);
  void asm_jit_BVS_END(void);
  void asm_jit_BVS_8bit(void);
  void asm_jit_BVS_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_BVS,
                    asm_jit_BVS_END,
                    asm_jit_BVS_8bit,
                    asm_jit_BVS_8bit_END);
}

void
asm_emit_jit_CMP_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_CMP_ZPG(void);
  void asm_jit_CMP_ZPG_END(void);
  void asm_jit_CMP_ABS(void);
  void asm_jit_CMP_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_CMP_ZPG,
                        asm_jit_CMP_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_CMP_ABS,
                       asm_jit_CMP_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_CMP_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_CMP_ABX(void);
  void asm_jit_CMP_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_CMP_ABX,
                     asm_jit_CMP_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_CMP_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_CMP_ABY(void);
  void asm_jit_CMP_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_CMP_ABY,
                     asm_jit_CMP_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_CMP_IMM(void);
  void asm_jit_CMP_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_CMP_IMM, asm_jit_CMP_IMM_END, value);
}

void
asm_emit_jit_CMP_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_CMP_SCRATCH(void);
  void asm_jit_CMP_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_CMP_SCRATCH,
                     asm_jit_CMP_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_CMP_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_CMP_SCRATCH_Y(void);
  void asm_jit_CMP_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_CMP_SCRATCH_Y, asm_jit_CMP_SCRATCH_Y_END);
}

void
asm_emit_jit_CPX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_CPX_ZPG(void);
  void asm_jit_CPX_ZPG_END(void);
  void asm_jit_CPX_ABS(void);
  void asm_jit_CPX_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_CPX_ZPG,
                        asm_jit_CPX_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_CPX_ABS,
                       asm_jit_CPX_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_CPX_IMM(void);
  void asm_jit_CPX_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_CPX_IMM, asm_jit_CPX_IMM_END, value);
}

void
asm_emit_jit_CPY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_CPY_ZPG(void);
  void asm_jit_CPY_ZPG_END(void);
  void asm_jit_CPY_ABS(void);
  void asm_jit_CPY_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_CPY_ZPG,
                        asm_jit_CPY_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_CPY_ABS,
                       asm_jit_CPY_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_CPY_IMM(void);
  void asm_jit_CPY_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_CPY_IMM, asm_jit_CPY_IMM_END, value);
}

void
asm_emit_jit_DEC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_DEC_ZPG(void);
  void asm_jit_DEC_ZPG_END(void);
  void asm_jit_DEC_ABS(void);
  void asm_jit_DEC_ABS_END(void);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_DEC_ZPG,
                        asm_jit_DEC_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_DEC_ABS,
                       asm_jit_DEC_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_DEC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_DEC_ABS_RMW(void);
  void asm_jit_DEC_ABS_RMW_mov1_patch(void);
  void asm_jit_DEC_ABS_RMW_mov2_patch(void);
  void asm_jit_DEC_ABS_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_DEC_ABS_RMW, asm_jit_DEC_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABS_RMW,
                asm_jit_DEC_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABS_RMW,
                asm_jit_DEC_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_DEC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_DEC_ABX(void);
  void asm_jit_DEC_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_DEC_ABX,
                     asm_jit_DEC_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_DEC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_DEC_ABX_RMW(void);
  void asm_jit_DEC_ABX_RMW_mov1_patch(void);
  void asm_jit_DEC_ABX_RMW_mov2_patch(void);
  void asm_jit_DEC_ABX_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_DEC_ABX_RMW, asm_jit_DEC_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABX_RMW,
                asm_jit_DEC_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_DEC_ABX_RMW,
                asm_jit_DEC_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_DEC_scratch(struct util_buffer* p_buf) {
  void asm_jit_DEC_scratch(void);
  void asm_jit_DEC_scratch_END(void);
  asm_copy(p_buf, asm_jit_DEC_scratch, asm_jit_DEC_scratch_END);
}

void
asm_emit_jit_EOR_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_EOR_ZPG(void);
  void asm_jit_EOR_ZPG_END(void);
  void asm_jit_EOR_ABS(void);
  void asm_jit_EOR_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_EOR_ZPG,
                        asm_jit_EOR_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_EOR_ABS,
                       asm_jit_EOR_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_EOR_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_EOR_ABX(void);
  void asm_jit_EOR_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_EOR_ABX,
                     asm_jit_EOR_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_EOR_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_EOR_ABY(void);
  void asm_jit_EOR_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_EOR_ABY,
                     asm_jit_EOR_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_EOR_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_EOR_IMM(void);
  void asm_jit_EOR_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_EOR_IMM, asm_jit_EOR_IMM_END, value);
}

void
asm_emit_jit_EOR_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_EOR_SCRATCH(void);
  void asm_jit_EOR_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_EOR_SCRATCH,
                     asm_jit_EOR_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_EOR_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_EOR_SCRATCH_Y(void);
  void asm_jit_EOR_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_EOR_SCRATCH_Y, asm_jit_EOR_SCRATCH_Y_END);
}

void
asm_emit_jit_INC_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_INC_ZPG(void);
  void asm_jit_INC_ZPG_END(void);
  void asm_jit_INC_ABS(void);
  void asm_jit_INC_ABS_END(void);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_INC_ZPG,
                        asm_jit_INC_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_INC_ABS,
                       asm_jit_INC_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_INC_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_INC_ABS_RMW(void);
  void asm_jit_INC_ABS_RMW_mov1_patch(void);
  void asm_jit_INC_ABS_RMW_mov2_patch(void);
  void asm_jit_INC_ABS_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_INC_ABS_RMW, asm_jit_INC_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABS_RMW,
                asm_jit_INC_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABS_RMW,
                asm_jit_INC_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_INC_ABX(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_INC_ABX(void);
  void asm_jit_INC_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_INC_ABX,
                     asm_jit_INC_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_INC_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_INC_ABX_RMW(void);
  void asm_jit_INC_ABX_RMW_mov1_patch(void);
  void asm_jit_INC_ABX_RMW_mov2_patch(void);
  void asm_jit_INC_ABX_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_INC_ABX_RMW, asm_jit_INC_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABX_RMW,
                asm_jit_INC_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_INC_ABX_RMW,
                asm_jit_INC_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_INC_scratch(struct util_buffer* p_buf) {
  void asm_jit_INC_scratch(void);
  void asm_jit_INC_scratch_END(void);
  asm_copy(p_buf, asm_jit_INC_scratch, asm_jit_INC_scratch_END);
}

void
asm_emit_jit_JMP(struct util_buffer* p_buf, void* p_target) {
  void asm_jit_JMP(void);
  void asm_jit_JMP_END(void);
  void asm_jit_JMP_8bit(void);
  void asm_jit_JMP_8bit_END(void);
  asm_emit_jit_jump(p_buf,
                    p_target,
                    asm_jit_JMP,
                    asm_jit_JMP_END,
                    asm_jit_JMP_8bit,
                    asm_jit_JMP_8bit_END);
}

void
asm_emit_jit_LDA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDA_ZPG(void);
  void asm_jit_LDA_ZPG_END(void);
  void asm_jit_LDA_ABS(void);
  void asm_jit_LDA_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LDA_ZPG,
                        asm_jit_LDA_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LDA_ABS,
                       asm_jit_LDA_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_LDA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDA_ABX(void);
  void asm_jit_LDA_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDA_ABX,
                     asm_jit_LDA_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_LDA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDA_ABY(void);
  void asm_jit_LDA_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDA_ABY,
                     asm_jit_LDA_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_LDA_IMM(void);
  void asm_jit_LDA_IMM_END(void);
  asm_copy_patch_u32(p_buf, asm_jit_LDA_IMM, asm_jit_LDA_IMM_END, value);
}

void
asm_emit_jit_LDA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_LDA_SCRATCH(void);
  void asm_jit_LDA_SCRATCH_END(void);
  asm_copy_patch_byte(p_buf,
                      asm_jit_LDA_SCRATCH,
                      asm_jit_LDA_SCRATCH_END,
                      (offset - REG_MEM_OFFSET));
}

void
asm_emit_jit_LDA_SCRATCH_X(struct util_buffer* p_buf) {
  void asm_jit_LDA_SCRATCH_X(void);
  void asm_jit_LDA_SCRATCH_X_END(void);
  asm_copy(p_buf, asm_jit_LDA_SCRATCH_X, asm_jit_LDA_SCRATCH_X_END);
}

void
asm_emit_jit_LDA_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_LDA_SCRATCH_Y(void);
  void asm_jit_LDA_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_LDA_SCRATCH_Y, asm_jit_LDA_SCRATCH_Y_END);
}

void
asm_emit_jit_LDX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDX_ZPG(void);
  void asm_jit_LDX_ZPG_END(void);
  void asm_jit_LDX_ABS(void);
  void asm_jit_LDX_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LDX_ZPG,
                        asm_jit_LDX_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LDX_ABS,
                       asm_jit_LDX_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_LDX_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDX_ABY(void);
  void asm_jit_LDX_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDX_ABY,
                     asm_jit_LDX_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_LDX_IMM(void);
  void asm_jit_LDX_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_LDX_IMM, asm_jit_LDX_IMM_END, value);
}

void
asm_emit_jit_LDX_scratch(struct util_buffer* p_buf) {
  void asm_jit_LDX_scratch(void);
  void asm_jit_LDX_scratch_END(void);
  asm_copy(p_buf, asm_jit_LDX_scratch, asm_jit_LDX_scratch_END);
}

void
asm_emit_jit_LDY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDY_ZPG(void);
  void asm_jit_LDY_ZPG_END(void);
  void asm_jit_LDY_ABS(void);
  void asm_jit_LDY_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LDY_ZPG,
                        asm_jit_LDY_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LDY_ABS,
                       asm_jit_LDY_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_LDY_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_LDY_ABX(void);
  void asm_jit_LDY_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_LDY_ABX,
                     asm_jit_LDY_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_LDY_IMM(void);
  void asm_jit_LDY_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_LDY_IMM, asm_jit_LDY_IMM_END, value);
}

void
asm_emit_jit_LDY_scratch(struct util_buffer* p_buf) {
  void asm_jit_LDY_scratch(void);
  void asm_jit_LDY_scratch_END(void);
  asm_copy(p_buf, asm_jit_LDY_scratch, asm_jit_LDY_scratch_END);
}

void
asm_emit_jit_LSR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_LSR_ZPG(void);
  void asm_jit_LSR_ZPG_END(void);
  void asm_jit_LSR_ABS(void);
  void asm_jit_LSR_ABS_END(void);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_LSR_ZPG,
                        asm_jit_LSR_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_LSR_ABS,
                       asm_jit_LSR_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_LSR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_LSR_ABS_RMW(void);
  void asm_jit_LSR_ABS_RMW_mov1_patch(void);
  void asm_jit_LSR_ABS_RMW_mov2_patch(void);
  void asm_jit_LSR_ABS_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_LSR_ABS_RMW, asm_jit_LSR_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABS_RMW,
                asm_jit_LSR_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABS_RMW,
                asm_jit_LSR_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_LSR_ABX(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_LSR_ABX(void);
  void asm_jit_LSR_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_LSR_ABX,
                     asm_jit_LSR_ABX_END,
                     (addr + K_BBC_MEM_READ_IND_ADDR));
}

void
asm_emit_jit_LSR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_LSR_ABX_RMW(void);
  void asm_jit_LSR_ABX_RMW_mov1_patch(void);
  void asm_jit_LSR_ABX_RMW_mov2_patch(void);
  void asm_jit_LSR_ABX_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_LSR_ABX_RMW, asm_jit_LSR_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABX_RMW,
                asm_jit_LSR_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_LSR_ABX_RMW,
                asm_jit_LSR_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_LSR_ACC(struct util_buffer* p_buf) {
  void asm_jit_LSR_ACC(void);
  void asm_jit_LSR_ACC_END(void);
  asm_copy(p_buf, asm_jit_LSR_ACC, asm_jit_LSR_ACC_END);
}

void
asm_emit_jit_LSR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  void asm_jit_LSR_ACC_n(void);
  void asm_jit_LSR_ACC_n_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_LSR_ACC_n, asm_jit_LSR_ACC_n_END, n);
}

void
asm_emit_jit_LSR_scratch(struct util_buffer* p_buf) {
  void asm_jit_LSR_scratch(void);
  void asm_jit_LSR_scratch_END(void);
  asm_copy(p_buf, asm_jit_LSR_scratch, asm_jit_LSR_scratch_END);
}

void
asm_emit_jit_ORA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ORA_ZPG(void);
  void asm_jit_ORA_ZPG_END(void);
  void asm_jit_ORA_ABS(void);
  void asm_jit_ORA_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ORA_ZPG,
                        asm_jit_ORA_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ORA_ABS,
                       asm_jit_ORA_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_ORA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ORA_ABX(void);
  void asm_jit_ORA_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ORA_ABX,
                     asm_jit_ORA_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_ORA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_ORA_ABY(void);
  void asm_jit_ORA_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ORA_ABY,
                     asm_jit_ORA_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_ORA_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_ORA_IMM(void);
  void asm_jit_ORA_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_ORA_IMM, asm_jit_ORA_IMM_END, value);
}

void
asm_emit_jit_ORA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_ORA_SCRATCH(void);
  void asm_jit_ORA_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_ORA_SCRATCH,
                     asm_jit_ORA_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_ORA_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_ORA_SCRATCH_Y(void);
  void asm_jit_ORA_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_ORA_SCRATCH_Y, asm_jit_ORA_SCRATCH_Y_END);
}

void
asm_emit_jit_ROL_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROL_ZPG(void);
  void asm_jit_ROL_ZPG_END(void);
  void asm_jit_ROL_ABS(void);
  void asm_jit_ROL_ABS_END(void);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ROL_ZPG,
                        asm_jit_ROL_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ROL_ABS,
                       asm_jit_ROL_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_ROL_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROL_ABS_RMW(void);
  void asm_jit_ROL_ABS_RMW_mov1_patch(void);
  void asm_jit_ROL_ABS_RMW_mov2_patch(void);
  void asm_jit_ROL_ABS_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROL_ABS_RMW, asm_jit_ROL_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABS_RMW,
                asm_jit_ROL_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABS_RMW,
                asm_jit_ROL_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_ROL_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROL_ABX_RMW(void);
  void asm_jit_ROL_ABX_RMW_mov1_patch(void);
  void asm_jit_ROL_ABX_RMW_mov2_patch(void);
  void asm_jit_ROL_ABX_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROL_ABX_RMW, asm_jit_ROL_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABX_RMW,
                asm_jit_ROL_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROL_ABX_RMW,
                asm_jit_ROL_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_ROL_ACC(struct util_buffer* p_buf) {
  void asm_jit_ROL_ACC(void);
  void asm_jit_ROL_ACC_END(void);
  asm_copy(p_buf, asm_jit_ROL_ACC, asm_jit_ROL_ACC_END);
}

void
asm_emit_jit_ROL_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  void asm_jit_ROL_ACC_n(void);
  void asm_jit_ROL_ACC_n_END(void);
  asm_copy_patch_byte(p_buf,
                          asm_jit_ROL_ACC_n,
                          asm_jit_ROL_ACC_n_END,
                          n);
}

void
asm_emit_jit_ROL_scratch(struct util_buffer* p_buf) {
  void asm_jit_ROL_scratch(void);
  void asm_jit_ROL_scratch_END(void);
  asm_copy(p_buf, asm_jit_ROL_scratch, asm_jit_ROL_scratch_END);
}

void
asm_emit_jit_ROR_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROR_ZPG(void);
  void asm_jit_ROR_ZPG_END(void);
  void asm_jit_ROR_ABS(void);
  void asm_jit_ROR_ABS_END(void);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_ROR_ZPG,
                        asm_jit_ROR_ZPG_END,
                        (addr - REG_MEM_OFFSET));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_ROR_ABS,
                       asm_jit_ROR_ABS_END,
                       (addr - REG_MEM_OFFSET));
  }
}

void
asm_emit_jit_ROR_ABS_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROR_ABS_RMW(void);
  void asm_jit_ROR_ABS_RMW_mov1_patch(void);
  void asm_jit_ROR_ABS_RMW_mov2_patch(void);
  void asm_jit_ROR_ABS_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROR_ABS_RMW, asm_jit_ROR_ABS_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABS_RMW,
                asm_jit_ROR_ABS_RMW_mov1_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_READ_FULL));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABS_RMW,
                asm_jit_ROR_ABS_RMW_mov2_patch,
                (addr - REG_MEM_OFFSET + K_BBC_MEM_OFFSET_TO_WRITE_FULL));
}

void
asm_emit_jit_ROR_ABX_RMW(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_ROR_ABX_RMW(void);
  void asm_jit_ROR_ABX_RMW_mov1_patch(void);
  void asm_jit_ROR_ABX_RMW_mov2_patch(void);
  void asm_jit_ROR_ABX_RMW_END(void);
  size_t offset = util_buffer_get_pos(p_buf);

  asm_copy(p_buf, asm_jit_ROR_ABX_RMW, asm_jit_ROR_ABX_RMW_END);
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABX_RMW,
                asm_jit_ROR_ABX_RMW_mov1_patch,
                (addr + K_BBC_MEM_READ_FULL_ADDR));
  asm_patch_int(p_buf,
                offset,
                asm_jit_ROR_ABX_RMW,
                asm_jit_ROR_ABX_RMW_mov2_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
asm_emit_jit_ROR_ACC(struct util_buffer* p_buf) {
  void asm_jit_ROR_ACC(void);
  void asm_jit_ROR_ACC_END(void);
  asm_copy(p_buf, asm_jit_ROR_ACC, asm_jit_ROR_ACC_END);
}

void
asm_emit_jit_ROR_ACC_n(struct util_buffer* p_buf, uint8_t n) {
  void asm_jit_ROR_ACC_n(void);
  void asm_jit_ROR_ACC_n_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_ROR_ACC_n, asm_jit_ROR_ACC_n_END, n);
}

void
asm_emit_jit_ROR_scratch(struct util_buffer* p_buf) {
  void asm_jit_ROR_scratch(void);
  void asm_jit_ROR_scratch_END(void);
  asm_copy(p_buf, asm_jit_ROR_scratch, asm_jit_ROR_scratch_END);
}

void
asm_emit_jit_SAX_ABS(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_SAX_ABS(void);
  void asm_jit_SAX_ABS_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_SAX_ABS,
                     asm_jit_SAX_ABS_END,
                     (addr - REG_MEM_OFFSET));
}

void
asm_emit_jit_SBC_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_SBC_ZPG(void);
  void asm_jit_SBC_ZPG_END(void);
  void asm_jit_SBC_ABS(void);
  void asm_jit_SBC_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_SBC_ZPG,
                        asm_jit_SBC_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_SBC_ABS,
                       asm_jit_SBC_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_SBC_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_SBC_ABX(void);
  void asm_jit_SBC_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_SBC_ABX,
                     asm_jit_SBC_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_SBC_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_SBC_ABY(void);
  void asm_jit_SBC_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_SBC_ABY,
                     asm_jit_SBC_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_SBC_IMM(struct util_buffer* p_buf, uint8_t value) {
  void asm_jit_SBC_IMM(void);
  void asm_jit_SBC_IMM_END(void);
  asm_copy_patch_byte(p_buf, asm_jit_SBC_IMM, asm_jit_SBC_IMM_END, value);
}

void
asm_emit_jit_SBC_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_SBC_SCRATCH(void);
  void asm_jit_SBC_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_SBC_SCRATCH,
                     asm_jit_SBC_SCRATCH_END,
                     (K_BBC_MEM_READ_IND_ADDR + offset));
}

void
asm_emit_jit_SBC_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_SBC_SCRATCH_Y(void);
  void asm_jit_SBC_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_SBC_SCRATCH_Y, asm_jit_SBC_SCRATCH_Y_END);
}

void
asm_emit_jit_SHY_ABX(struct util_buffer* p_buf, uint16_t addr) {
  void asm_jit_SHY_ABX(void);
  void asm_jit_SHY_ABX_byte_patch(void);
  void asm_jit_SHY_ABX_mov_patch(void);
  void asm_jit_SHY_ABX_END(void);
  size_t offset = util_buffer_get_pos(p_buf);
  uint8_t value = ((addr >> 8) + 1);

  asm_copy(p_buf, asm_jit_SHY_ABX, asm_jit_SHY_ABX_END);
  asm_patch_byte(p_buf,
                 offset,
                 asm_jit_SHY_ABX,
                 asm_jit_SHY_ABX_byte_patch,
                 value);
  asm_patch_int(p_buf,
                offset,
                asm_jit_SHY_ABX,
                asm_jit_SHY_ABX_mov_patch,
                (addr + K_BBC_MEM_WRITE_FULL_ADDR));
}

void
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

void
asm_emit_jit_STA_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STA_ZPG(void);
  void asm_jit_STA_ZPG_END(void);
  void asm_jit_STA_ABS(void);
  void asm_jit_STA_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_STA_ZPG,
                        asm_jit_STA_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(
        p_buf,
        asm_jit_STA_ABS,
        asm_jit_STA_ABS_END,
        (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_STA_ABX(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STA_ABX(void);
  void asm_jit_STA_ABX_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_STA_ABX,
                     asm_jit_STA_ABX_END,
                     (addr + segment));
}

void
asm_emit_jit_STA_ABY(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STA_ABY(void);
  void asm_jit_STA_ABY_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_STA_ABY,
                     asm_jit_STA_ABY_END,
                     (addr + segment));
}

void
asm_emit_jit_STA_SCRATCH(struct util_buffer* p_buf, uint8_t offset) {
  void asm_jit_STA_SCRATCH(void);
  void asm_jit_STA_SCRATCH_END(void);
  asm_copy_patch_u32(p_buf,
                     asm_jit_STA_SCRATCH,
                     asm_jit_STA_SCRATCH_END,
                     (K_BBC_MEM_WRITE_IND_ADDR + offset));
}

void
asm_emit_jit_STA_SCRATCH_Y(struct util_buffer* p_buf) {
  void asm_jit_STA_SCRATCH_Y(void);
  void asm_jit_STA_SCRATCH_Y_END(void);
  asm_copy(p_buf, asm_jit_STA_SCRATCH_Y, asm_jit_STA_SCRATCH_Y_END);
}

void
asm_emit_jit_STX_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STX_ZPG(void);
  void asm_jit_STX_ZPG_END(void);
  void asm_jit_STX_ABS(void);
  void asm_jit_STX_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_STX_ZPG,
                        asm_jit_STX_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_STX_ABS,
                       asm_jit_STX_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_STX_scratch(struct util_buffer* p_buf) {
  void asm_jit_STX_scratch(void);
  void asm_jit_STX_scratch_END(void);
  asm_copy(p_buf, asm_jit_STX_scratch, asm_jit_STX_scratch_END);
}

void
asm_emit_jit_STY_ABS(struct util_buffer* p_buf,
                     uint16_t addr,
                     uint32_t segment) {
  void asm_jit_STY_ZPG(void);
  void asm_jit_STY_ZPG_END(void);
  void asm_jit_STY_ABS(void);
  void asm_jit_STY_ABS_END(void);
  uint32_t delta = (segment - K_BBC_MEM_READ_IND_ADDR);
  if (addr < 0x100) {
    asm_copy_patch_byte(p_buf,
                        asm_jit_STY_ZPG,
                        asm_jit_STY_ZPG_END,
                        (addr - REG_MEM_OFFSET + delta));
  } else {
    asm_copy_patch_u32(p_buf,
                       asm_jit_STY_ABS,
                       asm_jit_STY_ABS_END,
                       (addr - REG_MEM_OFFSET + delta));
  }
}

void
asm_emit_jit_STY_scratch(struct util_buffer* p_buf) {
  void asm_jit_STY_scratch(void);
  void asm_jit_STY_scratch_END(void);
  asm_copy(p_buf, asm_jit_STY_scratch, asm_jit_STY_scratch_END);
}
