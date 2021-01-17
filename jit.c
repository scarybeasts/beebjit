#include "jit.h"

#include "bbc_options.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "interp.h"
#include "inturbo.h"
#include "memory_access.h"
#include "os_alloc.h"
#include "os_fault.h"
#include "jit_compiler.h"
#include "log.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"

#include "asm/asm_common.h"
#include "asm/asm_defs_host.h"
#include "asm/asm_jit.h"
#include "asm/asm_jit_defs.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct jit_struct {
  struct cpu_driver driver;

  /* C callbacks called by JIT code. */
  void* p_compile_callback;

  /* C pointers used by JIT code. */
  struct inturbo_struct* p_inturbo;

  /* 6502 address -> JIT code pointers. */
  uint32_t jit_ptrs[k_6502_addr_space_size];
  /* 6502 address -> code block. */
  int32_t code_blocks[k_6502_addr_space_size];

  /* Fields not referenced by JIT'ed code. */
  struct os_alloc_mapping* p_mapping_jit;
  struct os_alloc_mapping* p_mapping_trampolines;
  uint8_t* p_jit_base;
  uint8_t* p_jit_trampolines;
  struct jit_compiler* p_compiler;
  struct util_buffer* p_temp_buf;
  struct interp_struct* p_interp;
  uint32_t jit_ptr_no_code;
  uint32_t jit_ptr_dynamic_operand;
  uint8_t jit_invalidation_sequence[2];
  uint8_t* p_opcode_types;
  uint8_t* p_opcode_modes;
  uint8_t* p_opcode_cycles;

  int log_compile;

  uint64_t counter_num_compiles;
  uint64_t counter_num_interps;
  uint64_t counter_num_faults;
  int do_fault_log;
};

static inline uint8_t*
jit_get_jit_block_host_address(struct jit_struct* p_jit, uint16_t addr_6502) {
  uint8_t* p_jit_ptr = (p_jit->p_jit_base +
                        (addr_6502 * K_BBC_JIT_BYTES_PER_BYTE));
  return p_jit_ptr;
}

static inline void
jit_invalidate_host_address(struct jit_struct* p_jit, uint8_t* p_jit_ptr) {
  p_jit_ptr[0] = p_jit->jit_invalidation_sequence[0];
  p_jit_ptr[1] = p_jit->jit_invalidation_sequence[1];
}

static inline int
jit_has_6502_code(struct jit_struct* p_jit, uint16_t addr_6502) {
  if (p_jit->jit_ptrs[addr_6502] == p_jit->jit_ptr_no_code) {
    return 0;
  }
  return 1;
}

static void*
jit_get_block_host_address_callback(void* p, uint16_t addr_6502) {
  struct jit_struct* p_jit = (struct jit_struct*) p;
  return jit_get_jit_block_host_address(p_jit, addr_6502);
}

static void*
jit_get_trampoline_host_address_callback(void* p, uint16_t addr_6502) {
  struct jit_struct* p_jit = (struct jit_struct*) p;
  return (p_jit->p_jit_trampolines + (addr_6502 * K_BBC_JIT_TRAMPOLINE_BYTES));
}

static uint16_t
jit_6502_block_addr_from_host(struct jit_struct* p_jit,
                              uint8_t* p_host_cpu_ip) {
  size_t block_addr_6502;

  uint8_t* p_jit_base = p_jit->p_jit_base;

  block_addr_6502 = (p_host_cpu_ip - p_jit_base);
  block_addr_6502 /= K_BBC_JIT_BYTES_PER_BYTE;

  assert(block_addr_6502 < k_6502_addr_space_size);

  return (uint16_t) block_addr_6502;
}

static int32_t
jit_6502_code_block_from_6502_pc(struct jit_struct* p_jit, uint16_t addr) {
  return p_jit->code_blocks[addr];
}

static inline void
jit_invalidate_host_block_address(struct jit_struct* p_jit,
                                  uint16_t addr_6502) {
  uint8_t* p_jit_ptr = jit_get_jit_block_host_address(p_jit, addr_6502);

  jit_invalidate_host_address(p_jit, p_jit_ptr);
}

static inline void
jit_invalidate_code_at_address(struct jit_struct* p_jit, uint16_t addr_6502) {
  uint8_t* p_host_cpu_ip = (uint8_t*) (uintptr_t) p_jit->jit_ptrs[addr_6502];

  jit_invalidate_host_address(p_jit, p_host_cpu_ip);
}

static int
jit_interp_instruction_callback(void* p,
                                uint16_t next_pc,
                                uint8_t done_opcode,
                                uint16_t done_addr,
                                int next_is_irq,
                                int irq_pending) {
  int32_t next_block;
  int32_t next_block_prev;

  struct jit_struct* p_jit = (struct jit_struct*) p;
  uint8_t optype = p_jit->p_opcode_types[done_opcode];
  uint8_t opmode = p_jit->p_opcode_modes[done_opcode];
  uint8_t opmem = g_opmem[optype];

  if ((opmem == k_write || opmem == k_rw) && (opmode != k_acc)) {
    /* Any memory writes executed by the interpreter need to invalidate
     * compiled JIT code if they're self-modifying writes.
     */
    jit_invalidate_code_at_address(p_jit, done_addr);
  }

  if (next_is_irq || irq_pending) {
    /* Keep interpreting to handle the IRQ. */
    return 0;
  }

  next_block = jit_6502_code_block_from_6502_pc(p_jit, next_pc);
  if (next_block == -1) {
    /* Always consider an address with no JIT code to be a new block
     * boundary. Without this, an RTI to an uncompiled region will stay stuck
     * in the interpreter.
     */
    return 1;
  }

  next_block_prev = jit_6502_code_block_from_6502_pc(p_jit, (next_pc - 1));
  if (next_block != next_block_prev) {
    /* If the instruction we're about to execute is at the start of a JIT
     * block, bounce back into JIT at this clean boundary.
     */
    return 1;
  }

  /* Keep interpreting. */
  return 0;
}

struct jit_enter_interp_ret {
  int64_t countdown;
  int64_t exited;
};

static void
jit_enter_interp(struct jit_struct* p_jit,
                 struct jit_enter_interp_ret* p_ret,
                 int64_t countdown,
                 uint64_t intel_rflags) {
  uint32_t cpu_driver_flags;

  struct cpu_driver* p_jit_cpu_driver = &p_jit->driver;
  struct jit_compiler* p_compiler = p_jit->p_compiler;
  struct interp_struct* p_interp = p_jit->p_interp;
  struct state_6502* p_state_6502 = p_jit_cpu_driver->abi.p_state_6502;

  p_jit->counter_num_interps++;

  /* Take care of any deferred fault logging. */
  if (p_jit->do_fault_log) {
    p_jit->do_fault_log = 0;
    log_do_log(k_log_jit, k_log_info, "JIT handled fault (log every 1k)");
  }

  /* Bouncing out of the JIT is quite jarring. We need to fixup up any state
   * that was temporarily stale due to optimizations.
   */
  countdown = jit_compiler_fixup_state(p_compiler,
                                       p_state_6502,
                                       countdown,
                                       intel_rflags);

  countdown = interp_enter_with_details(p_interp,
                                        countdown,
                                        jit_interp_instruction_callback,
                                        p_jit);

  cpu_driver_flags = p_jit_cpu_driver->p_funcs->get_flags(p_jit_cpu_driver);
  p_ret->countdown = countdown;
  p_ret->exited = !!(cpu_driver_flags & k_cpu_flag_exited);
}

static void
jit_destroy(struct cpu_driver* p_cpu_driver) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_inturbo_cpu_driver =
      (struct cpu_driver*) p_jit->p_inturbo;
  struct cpu_driver* p_interp_cpu_driver = (struct cpu_driver*) p_jit->p_interp;

  p_inturbo_cpu_driver->p_funcs->destroy(p_inturbo_cpu_driver);
  p_interp_cpu_driver->p_funcs->destroy(p_interp_cpu_driver);

  util_buffer_destroy(p_jit->p_temp_buf);

  jit_compiler_destroy(p_jit->p_compiler);

  os_alloc_free_mapping(p_jit->p_mapping_jit);
  os_alloc_free_mapping(p_jit->p_mapping_trampolines);

  os_alloc_free_aligned(p_cpu_driver);
}

static int
jit_enter(struct cpu_driver* p_cpu_driver) {
  int exited;
  uint32_t uint_start_addr;
  int64_t countdown;

  struct timing_struct* p_timing = p_cpu_driver->p_timing;
  struct state_6502* p_state_6502 = p_cpu_driver->abi.p_state_6502;
  uint16_t addr_6502 = state_6502_get_pc(p_state_6502);
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint8_t* p_start_addr = jit_get_jit_block_host_address(p_jit, addr_6502);
  void* p_mem_base = (void*) K_BBC_MEM_READ_IND_ADDR;

  uint_start_addr = (uint32_t) (size_t) p_start_addr;

  countdown = timing_get_countdown(p_timing);

  /* The memory must be aligned to at least 0x100 so that our register access
   * tricks work.
   */
  assert(((uintptr_t) p_mem_base & 0xff) == 0);

  exited = asm_enter(p_jit, uint_start_addr, countdown, p_mem_base);
  assert(exited == 1);

  return exited;
}

static void
jit_set_reset_callback(struct cpu_driver* p_cpu_driver,
                       void (*do_reset_callback)(void* p, uint32_t flags),
                       void* p_do_reset_callback_object) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_driver = (struct cpu_driver*) p_jit->p_interp;

  p_interp_driver->p_funcs->set_reset_callback(p_interp_driver,
                                               do_reset_callback,
                                               p_do_reset_callback_object);
}

static void
jit_apply_flags(struct cpu_driver* p_cpu_driver,
                uint32_t flags_set,
                uint32_t flags_clear) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_driver = (struct cpu_driver*) p_jit->p_interp;

  p_interp_driver->p_funcs->apply_flags(p_interp_driver,
                                        flags_set,
                                        flags_clear);
}

static uint32_t
jit_get_flags(struct cpu_driver* p_cpu_driver) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_driver = (struct cpu_driver*) p_jit->p_interp;

  return p_interp_driver->p_funcs->get_flags(p_interp_driver);
}


static uint32_t
jit_get_exit_value(struct cpu_driver* p_cpu_driver) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_driver = (struct cpu_driver*) p_jit->p_interp;

  return p_interp_driver->p_funcs->get_exit_value(p_interp_driver);
}

static void
jit_set_exit_value(struct cpu_driver* p_cpu_driver, uint32_t exit_value) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_driver = (struct cpu_driver*) p_jit->p_interp;

  p_interp_driver->p_funcs->set_exit_value(p_interp_driver, exit_value);
}

static void
jit_memory_range_invalidate(struct cpu_driver* p_cpu_driver,
                            uint16_t addr,
                            uint32_t len) {
  uint32_t i;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint32_t addr_end = (addr + len);

  assert(len <= k_6502_addr_space_size);
  assert(addr_end <= k_6502_addr_space_size);

  if (p_jit->log_compile) {
    log_do_log(k_log_jit,
               k_log_info,
               "invalidate range $%.4X-$%.4X",
               addr,
               (addr_end - 1));
  }

  assert(addr_end >= addr);

  for (i = addr; i < addr_end; ++i) {
    jit_invalidate_code_at_address(p_jit, i);
    jit_invalidate_host_block_address(p_jit, i);
    p_jit->jit_ptrs[i] = p_jit->jit_ptr_no_code;
    p_jit->code_blocks[i] = -1;
  }

  jit_compiler_memory_range_invalidate(p_jit->p_compiler, addr, len);
}

static char*
jit_get_address_info(struct cpu_driver* p_cpu_driver, uint16_t addr) {
  static char block_addr_buf[5];

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  int32_t block_addr_6502 = jit_6502_code_block_from_6502_pc(p_jit, addr);

  (void) snprintf(block_addr_buf,
                  sizeof(block_addr_buf),
                  "%.4X",
                  (uint16_t) block_addr_6502);

  return block_addr_buf;
}

static void
jit_get_custom_counters(struct cpu_driver* p_cpu_driver,
                        uint64_t* p_c1,
                        uint64_t* p_c2) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;

  *p_c1 = p_jit->counter_num_compiles;
  *p_c2 = p_jit->counter_num_interps;
}

struct jit_host_ip_details {
  int exact_match;
  int32_t pc_6502;
  int32_t block_6502;
  void* p_invalidation_code_block;
};

static void
jit_get_6502_details_from_host_ip(struct jit_struct* p_jit,
                                  struct jit_host_ip_details* p_details,
                                  void* p_host_ip) {
  uint16_t host_block_6502;
  int32_t code_block_6502;
  uint16_t i_pc_6502;
  uint16_t pc_6502;
  uint32_t jit_ptr;
  void* p_jit_ptr;
  void* p_last_jit_ptr;
  int exact_match = -1;

  p_details->exact_match = -1;
  p_details->pc_6502 = -1;
  p_details->block_6502 = -1;
  p_details->p_invalidation_code_block = NULL;

  host_block_6502 = jit_6502_block_addr_from_host(p_jit, p_host_ip);
  code_block_6502 = p_jit->code_blocks[host_block_6502];

  if (((uintptr_t) p_host_ip & (K_BBC_JIT_BYTES_PER_BYTE - 1)) == 0) {
    /* A block compile request. Still need to check if this splits an existing
     * block.
     */
    if (code_block_6502 != -1) {
      p_details->p_invalidation_code_block =
          jit_get_jit_block_host_address(p_jit, code_block_6502);
    }
    return;
  }

  assert(code_block_6502 != -1);
  i_pc_6502 = code_block_6502;
  pc_6502 = i_pc_6502;
  p_last_jit_ptr = NULL;
  exact_match = 0;
  while (1) {
    if (p_jit->code_blocks[i_pc_6502] != code_block_6502) {
      break;
    }
    jit_ptr = p_jit->jit_ptrs[i_pc_6502];
    assert(jit_ptr != p_jit->jit_ptr_no_code);
    p_jit_ptr = (void*) (uintptr_t) jit_ptr;
    if (jit_ptr == p_jit->jit_ptr_dynamic_operand) {
      /* Just continue. */
    } else if (p_jit_ptr == p_host_ip) {
      pc_6502 = i_pc_6502;
      exact_match = 1;
      break;
    } else {
      if (p_jit_ptr > p_host_ip) {
        break;
      }
      if (p_jit_ptr != p_last_jit_ptr) {
        p_last_jit_ptr = p_jit_ptr;
        pc_6502 = i_pc_6502;
      }
    }
    i_pc_6502++;
  }

  p_details->exact_match = exact_match;
  p_details->block_6502 = code_block_6502;
  p_details->pc_6502 = pc_6502;
  p_details->p_invalidation_code_block =
      jit_get_jit_block_host_address(p_jit, code_block_6502);
}

static int64_t
jit_compile(struct jit_struct* p_jit,
            uint8_t* p_host_cpu_ip,
            int64_t countdown,
            uint64_t intel_rflags) {
  struct jit_host_ip_details details;
  uint32_t bytes_6502_compiled;
  uint16_t addr_6502;
  uint16_t addr_6502_next;
  uint16_t clear_ptrs_addr_6502;

  struct state_6502* p_state_6502 = p_jit->driver.abi.p_state_6502;
  struct jit_compiler* p_compiler = p_jit->p_compiler;
  int is_invalidation = 0;
  int has_6502_code = 0;
  int is_block_continuation = 0;

  p_jit->counter_num_compiles++;

  jit_get_6502_details_from_host_ip(p_jit, &details, p_host_cpu_ip);

  if (details.p_invalidation_code_block) {
    jit_invalidate_host_address(p_jit, details.p_invalidation_code_block);
  }

  if (details.pc_6502 != -1) {
    assert(details.exact_match == 1);
    addr_6502 = details.pc_6502;
    is_invalidation = 1;
  } else {
    addr_6502 = jit_6502_block_addr_from_host(p_jit, p_host_cpu_ip);
  }

  /* Bouncing out of the JIT is quite jarring. We need to fixup up any state
   * that was temporarily stale due to optimizations.
   */
  p_state_6502->reg_pc = addr_6502;
  if (is_invalidation) {
    countdown = jit_compiler_fixup_state(p_compiler,
                                         p_state_6502,
                                         countdown,
                                         intel_rflags);
  }

  if ((addr_6502 < 0xFF) &&
      !jit_compiler_is_compiling_for_code_in_zero_page(p_compiler)) {
    log_do_log(k_log_jit,
               k_log_unusual,
               "compiling zero page code @$%.2X",
               addr_6502);

    /* Invalidate all existing compiled code because if it writes to the zero
     * page, it isn't doing self-modified code correctly.
     */
    jit_memory_range_invalidate(&p_jit->driver,
                                0,
                                (k_6502_addr_space_size - 1));

    jit_compiler_set_compiling_for_code_in_zero_page(p_compiler, 1);
  } else if ((addr_6502 >= 0x100) && (addr_6502 <= 0x1FF)) {
    /* TODO: doesn't handle case where zero page code spills into stack page
     * code.
     */
    log_do_log(k_log_jit,
               k_log_unimplemented,
               "compiling stack page code @$%.4X; self-modify not handled",
               addr_6502);
  }

  if (p_jit->log_compile) {
    has_6502_code = jit_has_6502_code(p_jit, addr_6502);
    is_block_continuation = jit_compiler_is_block_continuation(p_compiler,
                                                               addr_6502);
  }

  bytes_6502_compiled = jit_compiler_compile_block(p_compiler,
                                                   is_invalidation,
                                                   addr_6502);

  /* Clear any leftover JIT pointers from a previous block at the same
   * location. Also clean out subsequent block metadata if that block was
   * trampled on.
   */
  addr_6502_next = (addr_6502 + bytes_6502_compiled);
  clear_ptrs_addr_6502 = addr_6502_next;
  while (1) {
    int32_t code_block = p_jit->code_blocks[clear_ptrs_addr_6502];
    if ((code_block == -1) || (code_block >= addr_6502_next)) {
      break;
    }
    p_jit->code_blocks[clear_ptrs_addr_6502] = -1;
    p_jit->jit_ptrs[clear_ptrs_addr_6502] = p_jit->jit_ptr_no_code;
    clear_ptrs_addr_6502++;
  }

  if (p_jit->log_compile) {
    const char* p_text;
    uint16_t addr_6502_end = (addr_6502 + bytes_6502_compiled - 1);
    if (is_invalidation) {
      p_text = "inval";
    } else if (is_block_continuation) {
      p_text = "cont";
    } else if (has_6502_code) {
      p_text = "split";
    } else {
      p_text = "new";
    }
    log_do_log(k_log_jit,
               k_log_info,
               "compile @$%.4X-$%.4X [rip @%p], %s",
               addr_6502,
               addr_6502_end,
               p_host_cpu_ip,
               p_text);
  }

  return countdown;
}

static void
jit_safe_hex_convert(char* p_buf, void* p_ptr) {
  size_t i;
  size_t val = (size_t) p_ptr;
  for (i = 0; i < 8; ++i) {
    char c1 = (val & 0x0f);
    char c2 = ((val & 0xf0) >> 4);

    if (c1 < 10) {
      c1 = '0' + c1;
    } else {
      c1 = 'a' + (c1 - 10);
    }
    if (c2 < 10) {
      c2 = '0' + c2;
    } else {
      c2 = 'a' + (c2 - 10);
    }

    p_buf[16 - 2 - (i * 2) + 1] = c1;
    p_buf[16 - 2 - (i * 2) ] = c2;

    val >>= 8;
  }
}

static void
fault_reraise(void* p_rip, void* p_addr) {
  int ret;
  char hex_buf[16];

  static const char* p_msg = "FAULT: rip ";
  static const char* p_msg2 = ", addr ";
  static const char* p_msg3 = "\n";

  ret = write(2, p_msg, strlen(p_msg));
  jit_safe_hex_convert(&hex_buf[0], p_rip);
  ret = write(2, hex_buf, sizeof(hex_buf));
  ret = write(2, p_msg2, strlen(p_msg2));
  jit_safe_hex_convert(&hex_buf[0], p_addr);
  ret = write(2, hex_buf, sizeof(hex_buf));
  ret = write(2, p_msg3, strlen(p_msg3));
  (void) ret;

  os_fault_bail();
}

static void
jit_handle_fault(uintptr_t* p_host_rip,
                 uintptr_t host_fault_addr,
                 int is_exec,
                 int is_write,
                 uintptr_t host_rdi) {
  int inaccessible_indirect_page;
  int ff_fault_fixup;
  int bcd_fault_fixup;
  int stack_wrap_fault_fixup;
  int wrap_indirect_read;
  int wrap_indirect_write;
  struct jit_struct* p_jit;
  struct jit_host_ip_details details;
  uint16_t addr_6502;

  void* p_jit_end = ((void*) K_BBC_JIT_ADDR +
                     (k_6502_addr_space_size * K_BBC_JIT_BYTES_PER_BYTE));
  void* p_fault_rip = (void*) *p_host_rip;
  void* p_fault_addr = (void*) host_fault_addr;

  /* Crash unless the faulting instruction is in the JIT region. */
  if ((p_fault_rip < (void*) K_BBC_JIT_ADDR) || (p_fault_rip >= p_jit_end)) {
    fault_reraise(p_fault_rip, p_fault_addr);
  }

  /* Fault in instruction fetch would be bad! */
  if (is_exec) {
    fault_reraise(p_fault_rip, p_fault_addr);
  }

  /* Bail unless it's a clearly recognized fault. */
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
    fault_reraise(p_fault_rip, p_fault_addr);
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
    fault_reraise(p_fault_rip, p_fault_addr);
  }

  p_jit = (struct jit_struct*) host_rdi;
  /* Sanity check it is really a jit struct. */
  if (p_jit->p_compile_callback != jit_compile) {
    fault_reraise(p_fault_rip, p_fault_addr);
  }
  if (p_jit->p_jit_trampolines != (void*) K_BBC_JIT_TRAMPOLINES_ADDR) {
    fault_reraise(p_fault_rip, p_fault_addr);
  }

  if ((p_jit->counter_num_faults % 1000) == 0) {
    /* We shouldn't call logging in the fault context (re-entrancy etc.) so set
     * a flag to take care of it later.
     */
    p_jit->do_fault_log = 1;
  }
  p_jit->counter_num_faults++;

  /* NOTE -- may call assert() which isn't async safe but faulting context is
   * raw asm, shouldn't be a disaster.
   */
  jit_get_6502_details_from_host_ip(p_jit, &details, p_fault_rip);
  assert(details.block_6502 != -1);
  assert(details.pc_6502 != -1);

  /* Bounce into the interpreter via the trampolines. */
  addr_6502 = details.pc_6502;
  *p_host_rip =
      (K_BBC_JIT_TRAMPOLINES_ADDR + (addr_6502 * K_BBC_JIT_TRAMPOLINE_BYTES));
}

static void
jit_init(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp;
  struct inturbo_struct* p_inturbo;
  size_t i;
  size_t mapping_size;
  uint8_t* p_jit_base;
  uint8_t* p_jit_trampolines;
  struct util_buffer* p_temp_buf;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct state_6502* p_state_6502 = p_cpu_driver->abi.p_state_6502;
  struct memory_access* p_memory_access = p_cpu_driver->p_memory_access;
  struct timing_struct* p_timing = p_cpu_driver->p_timing;
  struct bbc_options* p_options = p_cpu_driver->p_options;
  void* p_debug_object = p_options->p_debug_object;
  int debug = p_options->debug_active_at_addr(p_debug_object, 0xFFFF);
  struct cpu_driver_funcs* p_funcs = p_cpu_driver->p_funcs;

  p_jit->log_compile = util_has_option(p_options->p_log_flags, "jit:compile");
  p_funcs->get_opcode_maps(p_cpu_driver,
                           &p_jit->p_opcode_types,
                           &p_jit->p_opcode_modes,
                           &p_jit->p_opcode_cycles);

  p_funcs->destroy = jit_destroy;
  p_funcs->enter = jit_enter;
  p_funcs->set_reset_callback = jit_set_reset_callback;
  p_funcs->apply_flags = jit_apply_flags;
  p_funcs->get_flags = jit_get_flags;
  p_funcs->get_exit_value = jit_get_exit_value;
  p_funcs->set_exit_value = jit_set_exit_value;
  p_funcs->memory_range_invalidate = jit_memory_range_invalidate;
  p_funcs->get_address_info = jit_get_address_info;
  p_funcs->get_custom_counters = jit_get_custom_counters;

  p_cpu_driver->abi.p_util_private = asm_jit_compile_trampoline;
  p_jit->p_compile_callback = jit_compile;

  /* The JIT mode uses an interpreter to handle complicated situations,
   * such as IRQs, hardware accesses, etc.
   */
  p_interp = (struct interp_struct*) cpu_driver_alloc(k_cpu_mode_interp,
                                                      0,
                                                      p_state_6502,
                                                      p_memory_access,
                                                      p_timing,
                                                      p_options);
  if (p_interp == NULL) {
    util_bail("couldn't allocate interp_struct");
  }
  cpu_driver_init((struct cpu_driver*) p_interp);
  p_jit->p_interp = p_interp;

  /* The JIT mode uses an inturbo to handle opcodes that are self-modified
   * continually.
   */
  p_inturbo = (struct inturbo_struct*) cpu_driver_alloc(k_cpu_mode_inturbo,
                                                        0,
                                                        p_state_6502,
                                                        p_memory_access,
                                                        p_timing,
                                                        p_options);
  if (p_inturbo == NULL) {
    util_bail("couldn't allocate inturbo_struct");
  }
  inturbo_set_interp(p_inturbo, p_interp);
  /* Enable inturbo ret mode, which means we can call it to interpret a single
   * instruction and it will ret right back to us after every instruction.
   */
  inturbo_set_ret_mode(p_inturbo);
  inturbo_set_do_write_invalidation(p_inturbo, &p_jit->jit_ptrs[0]);
  cpu_driver_init((struct cpu_driver*) p_inturbo);
  p_jit->p_inturbo = p_inturbo;

  p_jit->driver.abi.p_interp_callback = jit_enter_interp;
  p_jit->driver.abi.p_interp_object = p_jit;

  /* This is the mapping that holds the dynamically JIT'ed code. */
  mapping_size = (k_6502_addr_space_size * K_BBC_JIT_BYTES_PER_BYTE);
  p_jit->p_mapping_jit = os_alloc_get_mapping((void*) K_BBC_JIT_ADDR,
                                              mapping_size);
  p_jit_base = os_alloc_get_mapping_addr(p_jit->p_mapping_jit);
  os_alloc_make_mapping_read_write_exec(p_jit_base, mapping_size);
  /* Fill with int3. */
  (void) memset(p_jit_base, '\xcc', mapping_size);

  /* This is the mapping that holds trampolines to jump out of JIT. These
   * one-per-6502-address trampolines enable the core JIT code to be simpler
   * and smaller, at the expense of more complicated bridging between JIT and
   * interp.
   */
  mapping_size = (k_6502_addr_space_size * K_BBC_JIT_TRAMPOLINE_BYTES);
  p_jit->p_mapping_trampolines =
      os_alloc_get_mapping((void*) K_BBC_JIT_TRAMPOLINES_ADDR, mapping_size);
  p_jit_trampolines = os_alloc_get_mapping_addr(p_jit->p_mapping_trampolines);
  os_alloc_make_mapping_read_write_exec(p_jit_trampolines, mapping_size);
  /* Fill with int3. */
  (void) memset(p_jit_trampolines, '\xcc', mapping_size);

  p_jit->p_jit_base = p_jit_base;
  p_jit->p_jit_trampolines = p_jit_trampolines;
  p_jit->p_compiler = jit_compiler_create(
      p_timing,
      p_memory_access,
      jit_get_block_host_address_callback,
      jit_get_trampoline_host_address_callback,
      p_jit,
      &p_jit->jit_ptrs[0],
      &p_jit->code_blocks[0],
      p_options,
      debug,
      p_jit->p_opcode_types,
      p_jit->p_opcode_modes,
      p_jit->p_opcode_cycles);
  p_temp_buf = util_buffer_create();
  p_jit->p_temp_buf = p_temp_buf;
  p_jit->jit_ptr_no_code =
      (uint32_t) (size_t) jit_get_jit_block_host_address(
          p_jit, (k_6502_addr_space_size - 1));
  p_jit->jit_ptr_dynamic_operand =
      (uint32_t) (size_t) jit_get_jit_block_host_address(
          p_jit, (k_6502_addr_space_size - 2));

  util_buffer_setup(p_temp_buf, &p_jit->jit_invalidation_sequence[0], 2);
  asm_emit_jit_call_compile_trampoline(p_temp_buf);

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    /* Initialize JIT trampoline. */
    util_buffer_setup(
        p_temp_buf,
        (p_jit_trampolines + (i * K_BBC_JIT_TRAMPOLINE_BYTES)),
        K_BBC_JIT_TRAMPOLINE_BYTES);
    asm_emit_jit_jump_interp_trampoline(p_temp_buf, i);
  }

  /* Ah the horrors, a fault / SIGSEGV handler! This actually enables a ton of
   * optimizations by using faults for very uncommon conditions, such that the
   * fast path doesn't need certain checks.
   */
  os_fault_register_handler(jit_handle_fault);

  /* NOTE: the JIT code space hasn't been set up with the invalidation markers.
   * Power-on reset has the responsibility of marking the entire address space
   * as invalidated.
   */
}

struct cpu_driver*
jit_create(struct cpu_driver_funcs* p_funcs) {
  struct cpu_driver* p_cpu_driver;
  size_t alignment;

  if (!asm_jit_is_enabled()) {
    return NULL;
  }

  asm_jit_test_preconditions();

  /* Align the structure to a multiple of the L1 DTLB bucket stride. This is
   * because the structure contains pointers read by JIT code and we want
   * deterministic performance.
   */
  alignment = (4096 * (64 / 4));

  p_cpu_driver =
      (struct cpu_driver*) os_alloc_get_aligned(alignment,
                                                sizeof(struct jit_struct));
  (void) memset(p_cpu_driver, '\0', sizeof(struct jit_struct));

  p_funcs->init = jit_init;

  return p_cpu_driver;
}

#include "test-jit.c"
