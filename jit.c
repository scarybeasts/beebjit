#define _GNU_SOURCE /* For REG_RIP */

#include "jit.h"

#include "asm_x64_common.h"
#include "asm_x64_jit.h"
#include "asm_x64_jit_defs.h"
#include "bbc_options.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "interp.h"
#include "memory_access.h"
#include "jit_compiler.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static void* k_jit_addr = (void*) K_BBC_JIT_ADDR;
static const int k_jit_bytes_per_byte = K_BBC_JIT_BYTES_PER_BYTE;
static void* k_jit_trampolines_addr = (void*) K_BBC_JIT_TRAMPOLINES_ADDR;
static const int k_jit_trampoline_bytes_per_byte = K_BBC_JIT_TRAMPOLINE_BYTES;

struct jit_struct {
  struct cpu_driver driver;

  /* C callbacks called by JIT code. */
  void* p_compile_callback;

  /* 6502 address -> JIT code pointers. */
  uint32_t jit_ptrs[k_6502_addr_space_size];

  /* Fields not referenced by JIT'ed code. */
  uint8_t* p_jit_base;
  uint8_t* p_jit_trampolines;
  struct jit_compiler* p_compiler;
  struct util_buffer* p_temp_buf;
  struct util_buffer* p_compile_buf;
  struct interp_struct* p_interp;

  int log_compile;
};

static uint8_t*
jit_get_jit_base_addr(struct jit_struct* p_jit, uint16_t addr_6502) {
  uint8_t* p_jit_ptr = (p_jit->p_jit_base +
                        (addr_6502 * k_jit_bytes_per_byte));
  return p_jit_ptr;
}

static void*
jit_get_block_host_address_callback(void* p, uint16_t addr_6502) {
  struct jit_struct* p_jit = (struct jit_struct*) p;
  return jit_get_jit_base_addr(p_jit, addr_6502);
}

static void*
jit_get_trampoline_host_address_callback(void* p, uint16_t addr_6502) {
  struct jit_struct* p_jit = (struct jit_struct*) p;
  return (p_jit->p_jit_trampolines +
          (addr_6502 * k_jit_trampoline_bytes_per_byte));
}

static uint16_t
jit_get_jit_ptr_block_callback(void* p, uint32_t jit_ptr) {
  size_t ret;
  uint8_t* p_jit_ptr;

  struct jit_struct* p_jit = (struct jit_struct*) p;

  p_jit_ptr = (uint8_t*) (size_t) jit_ptr;

  ret = ((p_jit_ptr - p_jit->p_jit_base) / k_jit_bytes_per_byte);

  assert(ret < k_6502_addr_space_size);

  return ret;
}

static uint16_t
jit_6502_block_addr_from_intel(struct jit_struct* p_jit, uint8_t* p_intel_rip) {
  size_t block_addr_6502;

  uint8_t* p_jit_base = p_jit->p_jit_base;

  block_addr_6502 = (p_intel_rip - p_jit_base);
  block_addr_6502 /= k_jit_bytes_per_byte;

  assert(block_addr_6502 < k_6502_addr_space_size);

  return (uint16_t) block_addr_6502;
}

static uint16_t
jit_6502_block_addr_from_6502(struct jit_struct* p_jit, uint16_t addr) {
  void* p_intel_rip = (void*) (size_t) p_jit->jit_ptrs[addr];
  return jit_6502_block_addr_from_intel(p_jit, p_intel_rip);
}

static void
jit_invalidate_block_address(struct jit_struct* p_jit, uint16_t addr_6502) {
  struct util_buffer* p_buf = p_jit->p_temp_buf;
  uint8_t* p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);

  util_buffer_setup(p_buf, p_jit_ptr, 2);
  asm_x64_emit_jit_call_compile_trampoline(p_buf);
}

static void
jit_invalidate_code_at_address(struct jit_struct* p_jit, uint16_t addr_6502) {
  struct util_buffer* p_buf = p_jit->p_temp_buf;
  void* p_intel_rip = (void*) (size_t) p_jit->jit_ptrs[addr_6502];

  util_buffer_setup(p_buf, p_intel_rip, 2);
  asm_x64_emit_jit_call_compile_trampoline(p_buf);
}

static int
jit_interp_instruction_callback(void* p,
                                uint16_t next_pc,
                                uint8_t done_opcode,
                                uint16_t done_addr,
                                int next_is_irq,
                                int irq_pending) {
  uint16_t next_block;
  uint16_t next_block_prev;

  struct jit_struct* p_jit = (struct jit_struct*) p;
  uint8_t opmode = g_opmodes[done_opcode];
  uint8_t optype = g_optypes[done_opcode];
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

  next_block = jit_6502_block_addr_from_6502(p_jit, next_pc);
  if (next_block == 0xFFFF) {
    /* Always consider an address with no JIT code to be a new block
     * boundary. Without this, an RTI to an uncompiled region will stay stuck
     * in the interpreter.
     */
    return 1;
  }

  next_block_prev = jit_6502_block_addr_from_6502(p_jit, (next_pc - 1));
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

static struct jit_enter_interp_ret
jit_enter_interp(struct jit_struct* p_jit,
                 int64_t countdown,
                 uint64_t intel_rflags) {
  struct jit_enter_interp_ret ret;

  struct cpu_driver* p_jit_cpu_driver = &p_jit->driver;
  struct jit_compiler* p_compiler = p_jit->p_compiler;
  struct interp_struct* p_interp = p_jit->p_interp;
  struct state_6502* p_state_6502 = p_jit_cpu_driver->abi.p_state_6502;

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

  ret.countdown = countdown;
  ret.exited = p_jit_cpu_driver->p_funcs->has_exited(p_jit_cpu_driver);

  return ret;
}

static void
jit_destroy(struct cpu_driver* p_cpu_driver) {
  size_t mapping_size;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_cpu_driver = (struct cpu_driver*) p_jit->p_interp;

  p_interp_cpu_driver->p_funcs->destroy(p_interp_cpu_driver);

  util_buffer_destroy(p_jit->p_compile_buf);
  util_buffer_destroy(p_jit->p_temp_buf);

  jit_compiler_destroy(p_jit->p_compiler);

  mapping_size = (k_6502_addr_space_size * k_jit_bytes_per_byte);
  util_free_guarded_mapping(k_jit_addr, mapping_size);
  mapping_size = (k_6502_addr_space_size * k_jit_trampoline_bytes_per_byte);
  util_free_guarded_mapping(k_jit_trampolines_addr, mapping_size);
}

static int
jit_enter(struct cpu_driver* p_cpu_driver) {
  int exited;
  uint32_t uint_start_addr;
  int64_t countdown;

  struct timing_struct* p_timing = p_cpu_driver->p_timing;
  uint16_t addr_6502 = state_6502_get_pc(p_cpu_driver->abi.p_state_6502);
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint8_t* p_start_addr = jit_get_jit_base_addr(p_jit, addr_6502);

  uint_start_addr = (uint32_t) (size_t) p_start_addr;

  countdown = timing_get_countdown(p_timing);

  exited = asm_x64_asm_enter(p_jit, uint_start_addr, countdown);
  assert(exited == 1);

  return exited;
}

static int
jit_has_exited(struct cpu_driver* p_cpu_driver) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_driver = (struct cpu_driver*) p_jit->p_interp;

  return p_interp_driver->p_funcs->has_exited(p_interp_driver);
}

static uint32_t
jit_get_exit_value(struct cpu_driver* p_cpu_driver) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct cpu_driver* p_interp_driver = (struct cpu_driver*) p_jit->p_interp;

  return p_interp_driver->p_funcs->get_exit_value(p_interp_driver);
}

static void
jit_memory_range_invalidate(struct cpu_driver* p_cpu_driver,
                            uint16_t addr,
                            uint16_t len) {
  uint32_t i;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint32_t addr_end = (addr + len);

  if (p_jit->log_compile) {
    printf("LOG:JIT:invalidate range $%.4X-$%.4X\n", addr, (addr_end - 1));
  }

  assert(addr_end >= addr);

  for (i = addr; i < addr_end; ++i) {
    jit_invalidate_code_at_address(p_jit, i);
    jit_invalidate_block_address(p_jit, i);
  }

  jit_compiler_memory_range_invalidate(p_jit->p_compiler, addr, len);
}

static char*
jit_get_address_info(struct cpu_driver* p_cpu_driver, uint16_t addr) {
  static char block_addr_buf[5];

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint16_t block_addr_6502 = jit_6502_block_addr_from_6502(p_jit, addr);

  (void) snprintf(block_addr_buf,
                  sizeof(block_addr_buf),
                  "%.4X",
                  block_addr_6502);

  return block_addr_buf;
}

static int64_t
jit_compile(struct jit_struct* p_jit,
            uint8_t* p_intel_rip,
            int64_t countdown,
            uint64_t intel_rflags) {
  uint8_t* p_jit_ptr;
  uint8_t* p_block_ptr;
  uint16_t addr_6502;
  int is_new;

  struct state_6502* p_state_6502 = p_jit->driver.abi.p_state_6502;
  struct jit_compiler* p_compiler = p_jit->p_compiler;
  struct util_buffer* p_compile_buf = p_jit->p_compile_buf;
  uint16_t block_addr_6502 = jit_6502_block_addr_from_intel(p_jit, p_intel_rip);

  p_block_ptr = jit_get_jit_base_addr(p_jit, block_addr_6502);
  if (p_block_ptr == p_intel_rip) {
    is_new = 1;
    addr_6502 = block_addr_6502;
  } else {
    is_new = 0;
    /* Host IP is inside a code block; find the corresponding 6502 address. */
    addr_6502 = block_addr_6502;
    while (1) {
      p_jit_ptr = (uint8_t*) (size_t) p_jit->jit_ptrs[addr_6502];
      assert(jit_6502_block_addr_from_intel(p_jit, p_jit_ptr) ==
             block_addr_6502);
      if (p_jit_ptr == p_intel_rip) {
        break;
      }
      addr_6502++;
    }
  }

  /* Bouncing out of the JIT is quite jarring. We need to fixup up any state
   * that was temporarily stale due to optimizations.
   */
  p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);
  p_state_6502->reg_pc = addr_6502;
  if (!is_new) {
    countdown = jit_compiler_fixup_state(p_compiler,
                                         p_state_6502,
                                         countdown,
                                         intel_rflags);
  }

  util_buffer_setup(p_compile_buf, p_jit_ptr, k_jit_bytes_per_byte);

  if (p_jit->log_compile) {
    const char* p_new_or_inval;
    if (is_new) {
      p_new_or_inval = "new";
    } else {
      p_new_or_inval = "inval";
    }
    printf("LOG:JIT:compile @$%.4X [rip @%p], %s\n",
           addr_6502,
           p_intel_rip,
           p_new_or_inval);
  }

  if ((addr_6502 < 0xFF) &&
      !jit_compiler_is_compiling_for_code_in_zero_page(p_compiler)) {
    printf("LOG:JIT:compiling zero page code; handled but slightly slower\n");

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
    printf("LOG:JIT:compiling stack page code; self-modify here not handled\n");
    printf("LOG:JIT:happens a lot (including Exile); will probably work ok\n");
  }

  jit_compiler_compile_block(p_compiler, p_compile_buf, addr_6502);

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
sigsegv_reraise(void* p_rip, void* p_addr) {
  int ret;
  struct sigaction sa;
  char hex_buf[16];

  static const char* p_msg = "SIGSEGV: rip ";
  static const char* p_msg2 = ", addr ";
  static const char* p_msg3 = "\n";

  (void) memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = SIG_DFL;
  (void) sigaction(SIGSEGV, &sa, NULL);

  ret = write(2, p_msg, strlen(p_msg));
  jit_safe_hex_convert(&hex_buf[0], p_rip);
  ret = write(2, hex_buf, sizeof(hex_buf));
  ret = write(2, p_msg2, strlen(p_msg2));
  jit_safe_hex_convert(&hex_buf[0], p_addr);
  ret = write(2, hex_buf, sizeof(hex_buf));
  ret = write(2, p_msg3, strlen(p_msg3));
  (void) ret;

  (void) raise(SIGSEGV);
  _exit(1);
}

static void
jit_handle_sigsegv(int signum, siginfo_t* p_siginfo, void* p_void) {
  int inaccessible_indirect_page;
  int ff_page_wrap;
  struct jit_struct* p_jit;
  uint16_t block_addr_6502;
  uint16_t addr_6502;
  uint16_t i_addr_6502;
  void* p_last_jit_ptr;

  void* p_addr = p_siginfo->si_addr;
  ucontext_t* p_context = (ucontext_t*) p_void;
  void* p_rip = (void*) p_context->uc_mcontext.gregs[REG_RIP];
  void* p_jit_end = (k_jit_addr +
                     (k_6502_addr_space_size * k_jit_bytes_per_byte));

  /* Crash unless it's fault we clearly recognize. */
  if (signum != SIGSEGV || p_siginfo->si_code != SEGV_ACCERR) {
    sigsegv_reraise(p_rip, p_addr);
  }

  /* Crash unless the faulting instruction is in the JIT region. */
  if ((p_rip < k_jit_addr) || (p_rip >= p_jit_end)) {
    sigsegv_reraise(p_rip, p_addr);
  }

  /* Bail unless it's a clearly recognized fault. */
  /* The indirect page fault occurs when an indirect addressing mode is used
   * to access 0xF000 - 0xFFFF, primarily of interest due to the hardware
   * registers. Using a fault + fixup here is a good performance boost for the
   * common case.
   */
  inaccessible_indirect_page = 0;
  /* The 0xFF page wrap fault occurs when a word fetch is performed at the end
   * of a page, where that page wraps. e.g. idx mode fetching the address from
   * 0xFF. Using a fault + fixup here makes the code footprint for idx mode
   * addressing smaller.
   */
  ff_page_wrap = 0;
  if ((p_addr >= ((void*) K_BBC_MEM_READ_IND_ADDR +
                  K_BBC_MEM_INACCESSIBLE_OFFSET)) &&
      (p_addr < ((void*) K_BBC_MEM_READ_IND_ADDR + k_6502_addr_space_size))) {
    inaccessible_indirect_page = 1;
  }
  if ((p_addr >= ((void*) K_BBC_MEM_WRITE_IND_ADDR +
                  K_BBC_MEM_INACCESSIBLE_OFFSET)) &&
      (p_addr < ((void*) K_BBC_MEM_WRITE_IND_ADDR + k_6502_addr_space_size))) {
    inaccessible_indirect_page = 1;
  }
  if (p_addr == ((void*) K_BBC_MEM_READ_ADDR + K_6502_ADDR_SPACE_SIZE)) {
    ff_page_wrap = 1;
  }

  if (!inaccessible_indirect_page && !ff_page_wrap) {
    sigsegv_reraise(p_rip, p_addr);
  }

  p_jit = (struct jit_struct*) p_context->uc_mcontext.gregs[REG_RDI];
  /* Sanity check it is really a jit struct. */
  if (p_jit->p_compile_callback != jit_compile) {
    sigsegv_reraise(p_rip, p_addr);
  }
  if (p_jit->p_jit_trampolines != (void*) K_BBC_JIT_TRAMPOLINES_ADDR) {
    sigsegv_reraise(p_rip, p_addr);
  }

  /* NOTE -- may call assert() which isn't async safe but faulting context is
   * raw asm, shouldn't be a disaster.
   */
  block_addr_6502 = jit_6502_block_addr_from_intel(p_jit, p_rip);

  /* Walk the code pointers in the block and do a non-exact match because the
   * faulting instruction won't be the start of the 6502 opcode. (That may
   * be e.g. the MODE_IND uop as part of the idy addressin mode.
   */
  addr_6502 = 0;
  i_addr_6502 = block_addr_6502;
  p_last_jit_ptr = NULL;
  while (1) {
    void* p_jit_ptr;
    if (!i_addr_6502) {
      sigsegv_reraise(p_rip, p_addr);
    }
    p_jit_ptr = (void*) (size_t) p_jit->jit_ptrs[i_addr_6502];
    if (p_jit_ptr > p_rip) {
      break;
    }
    if (p_jit_ptr != p_last_jit_ptr) {
      p_last_jit_ptr = p_jit_ptr;
      addr_6502 = i_addr_6502;
    }
    i_addr_6502++;
  }

  /* Bounce into the interpreter via the trampolines. */
  p_context->uc_mcontext.gregs[REG_RIP] =
      (K_BBC_JIT_TRAMPOLINES_ADDR + (addr_6502 * K_BBC_JIT_TRAMPOLINE_BYTES));
}

static void
jit_init(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp;
  size_t i;
  size_t mapping_size;
  uint8_t* p_jit_base;
  uint8_t* p_jit_trampolines;
  struct util_buffer* p_temp_buf;
  struct sigaction sa;
  int ret;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  struct state_6502* p_state_6502 = p_cpu_driver->abi.p_state_6502;
  struct memory_access* p_memory_access = p_cpu_driver->p_memory_access;
  struct timing_struct* p_timing = p_cpu_driver->p_timing;
  struct bbc_options* p_options = p_cpu_driver->p_options;
  void* p_debug_object = p_options->p_debug_object;
  int debug = p_options->debug_active_at_addr(p_debug_object, 0xFFFF);
  struct cpu_driver_funcs* p_funcs = p_cpu_driver->p_funcs;

  p_jit->log_compile = util_has_option(p_options->p_log_flags, "jit:compile");

  p_funcs->destroy = jit_destroy;
  p_funcs->enter = jit_enter;
  p_funcs->has_exited = jit_has_exited;
  p_funcs->get_exit_value = jit_get_exit_value;
  p_funcs->memory_range_invalidate = jit_memory_range_invalidate;
  p_funcs->get_address_info = jit_get_address_info;

  p_cpu_driver->abi.p_util_private = asm_x64_jit_compile_trampoline;
  p_jit->p_compile_callback = jit_compile;

  /* The JIT mode uses an interpreter to handle complicated situations,
   * such as IRQs, hardware accesses, etc.
   */
  p_interp = (struct interp_struct*) cpu_driver_alloc(k_cpu_mode_interp,
                                                      p_state_6502,
                                                      p_memory_access,
                                                      p_timing,
                                                      p_options);
  if (p_interp == NULL) {
    errx(1, "couldn't allocate interp_struct");
  }
  p_jit->p_interp = p_interp;

  p_jit->driver.abi.p_interp_callback = jit_enter_interp;
  p_jit->driver.abi.p_interp_object = p_jit;

  /* This is the mapping that holds the dynamically JIT'ed code. */
  mapping_size = (k_6502_addr_space_size * k_jit_bytes_per_byte);
  p_jit_base = util_get_guarded_mapping(k_jit_addr, mapping_size);
  util_make_mapping_read_write_exec(p_jit_base, mapping_size);
  /* Fill with int3. */
  (void) memset(p_jit_base, '\xcc', mapping_size);

  /* This is the mapping that holds trampolines to jump out of JIT. These
   * one-per-6502-address trampolines enable the core JIT code to be simpler
   * and smaller, at the expense of more complicated bridging between JIT and
   * interp.
   */
  mapping_size = (k_6502_addr_space_size * k_jit_trampoline_bytes_per_byte);
  p_jit_trampolines = util_get_guarded_mapping(k_jit_trampolines_addr,
                                               mapping_size);
  util_make_mapping_read_write_exec(p_jit_trampolines, mapping_size);
  /* Fill with int3. */
  (void) memset(p_jit_trampolines, '\xcc', mapping_size);

  p_jit->p_jit_base = p_jit_base;
  p_jit->p_jit_trampolines = p_jit_trampolines;
  p_jit->p_compiler = jit_compiler_create(
      p_memory_access,
      jit_get_block_host_address_callback,
      jit_get_trampoline_host_address_callback,
      jit_get_jit_ptr_block_callback,
      p_jit,
      &p_jit->jit_ptrs[0],
      p_options,
      debug);
  p_temp_buf = util_buffer_create();
  p_jit->p_temp_buf = p_temp_buf;
  p_jit->p_compile_buf = util_buffer_create();

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    /* Initialize JIT code. */
    jit_invalidate_block_address(p_jit, i);
    /* Initialize JIT trampoline. */
    util_buffer_setup(
        p_temp_buf,
        (p_jit_trampolines + (i * k_jit_trampoline_bytes_per_byte)),
        k_jit_trampoline_bytes_per_byte);
    asm_x64_emit_jit_jump_interp_trampoline(p_temp_buf, i);
  }

  /* Ah the horrors, a SIGSEGV handler! This actually enables a ton of
   * optimizations by using faults for very uncommon conditions, such that the
   * fast path doesn't need certain checks.
   */
  (void) memset(&sa, '\0', sizeof(sa));
  sa.sa_sigaction = jit_handle_sigsegv;
  sa.sa_flags = (SA_SIGINFO | SA_NODEFER);
  ret = sigaction(SIGSEGV, &sa, &sa);
  if (ret != 0) {
    errx(1, "sigaction failed");
  }
  if (sa.sa_sigaction != NULL && sa.sa_sigaction != jit_handle_sigsegv) {
    errx(1, "conflicting SIGSEGV handler");
  }
}

struct cpu_driver*
jit_create(struct cpu_driver_funcs* p_funcs) {
  struct cpu_driver* p_cpu_driver = malloc(sizeof(struct jit_struct));
  if (p_cpu_driver == NULL) {
    errx(1, "cannot allocate jit_struct");
  }
  (void) memset(p_cpu_driver, '\0', sizeof(struct jit_struct));

  p_funcs->init = jit_init;

  return p_cpu_driver;
}
