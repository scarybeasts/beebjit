#include "interp.h"

#include "bbc_options.h"
#include "cpu_driver.h"
#include "debug.h"
#include "defs_6502.h"
#include "log.h"
#include "memory_access.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"

#include <assert.h>
#include <stdint.h>

enum {
  k_interp_special_debug = 1,
  k_interp_special_callback = 2,
  k_interp_special_countdown = 4,
  k_interp_special_poll_irq = 8,
  k_interp_special_entry = 16,
};

struct interp_struct {
  struct cpu_driver driver;
  int is_65c12;
  uint64_t counter_bcd;

  uint8_t* p_mem_read;
  uint8_t* p_mem_write;
  int debug_subsystem_active;
  volatile int* p_debug_interrupt;

  uint8_t callback_intf;
  int callback_do_irq;
};

static void
interp_destroy(struct cpu_driver* p_cpu_driver) {
  assert(p_cpu_driver->p_funcs->get_flags(p_cpu_driver) & k_cpu_flag_exited);

  util_free(p_cpu_driver);
}

static int
interp_enter(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp = (struct interp_struct*) p_cpu_driver;
  int64_t countdown = timing_get_countdown(p_interp->driver.p_timing);

  countdown = interp_enter_with_details(p_interp, countdown, NULL, NULL);
  (void) countdown;

  return !!(p_cpu_driver->flags & k_cpu_flag_exited);
}

static char*
interp_get_address_info(struct cpu_driver* p_cpu_driver, uint16_t addr) {
  (void) p_cpu_driver;
  (void) addr;

  return "ITRP";
}

static inline void
interp_poll_irq_now(int* p_do_irq,
                    struct state_6502* p_state_6502,
                    uint8_t intf) {
  if (!p_state_6502->irq_fire) {
    return;
  }

  if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi) ||
      !intf) {
    *p_do_irq = 1;
  }
}

static void
interp_last_tick_callback(void* p) {
  struct interp_struct* p_interp = (struct interp_struct*) p;

  p_interp->callback_do_irq = 0;
  interp_poll_irq_now(&p_interp->callback_do_irq,
                      p_interp->driver.abi.p_state_6502,
                      p_interp->callback_intf);
}

static void
interp_init(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp = (struct interp_struct*) p_cpu_driver;
  struct memory_access* p_memory_access = p_cpu_driver->p_memory_access;
  struct bbc_options* p_options = p_cpu_driver->p_options;
  struct cpu_driver_funcs* p_funcs = p_cpu_driver->p_funcs;
  struct debug_struct* p_debug = p_cpu_driver->abi.p_debug_object;

  p_funcs->destroy = interp_destroy;
  p_funcs->enter = interp_enter;
  p_funcs->get_address_info = interp_get_address_info;

  p_interp->p_mem_read = p_memory_access->p_mem_read;
  p_interp->p_mem_write = p_memory_access->p_mem_write;

  p_memory_access->memory_client_last_tick_callback = interp_last_tick_callback;
  p_memory_access->p_last_tick_callback_obj = p_interp;

  p_interp->debug_subsystem_active = p_options->debug_subsystem_active(
      p_options->p_debug_object);
  p_interp->p_debug_interrupt = debug_get_interrupt(p_debug);
}

struct cpu_driver*
interp_create(struct cpu_driver_funcs* p_funcs, int is_65c12) {
  struct interp_struct* p_interp = util_mallocz(sizeof(struct interp_struct));

  p_interp->is_65c12 = is_65c12;
  p_funcs->init = interp_init;

  return &p_interp->driver;
}

static inline void
interp_set_flags(uint8_t flags,
                 uint8_t* zf,
                 uint8_t* nf,
                 uint8_t* cf,
                 uint8_t* of,
                 uint8_t* df,
                 uint8_t* intf) {
  *zf = ((flags & (1 << k_flag_zero)) != 0);
  *nf = ((flags & (1 << k_flag_negative)) != 0);
  *cf = ((flags & (1 << k_flag_carry)) != 0);
  *of = ((flags & (1 << k_flag_overflow)) != 0);
  *df = ((flags & (1 << k_flag_decimal)) != 0);
  *intf = ((flags & (1 << k_flag_interrupt)) != 0);
}

static inline uint8_t
interp_get_flags(uint8_t zf,
                 uint8_t nf,
                 uint8_t cf,
                 uint8_t of,
                 uint8_t df,
                 uint8_t intf) {
  uint8_t flags = 0;
  flags |= (cf << k_flag_carry);
  flags |= (zf << k_flag_zero);
  flags |= (intf << k_flag_interrupt);
  flags |= (df << k_flag_decimal);
  flags |= (of << k_flag_overflow);
  flags |= (nf << k_flag_negative);
  return flags;
}

static void
interp_call_debugger(struct interp_struct* p_interp,
                     uint8_t* p_a,
                     uint8_t* p_x,
                     uint8_t* p_y,
                     uint8_t* p_s,
                     uint16_t* p_pc,
                     uint8_t* p_zf,
                     uint8_t* p_nf,
                     uint8_t* p_cf,
                     uint8_t* p_of,
                     uint8_t* p_df,
                     uint8_t* p_intf,
                     uint16_t irq_vector) {
  uint8_t flags;

  struct state_6502* p_state_6502 = p_interp->driver.abi.p_state_6502;
  struct bbc_options* p_options = p_interp->driver.p_options;
  int (*debug_active_at_addr)(void*, uint16_t) =
      p_options->debug_active_at_addr;
  struct cpu_driver* p_cpu_driver = (struct cpu_driver*) p_interp;
  struct debug_struct* p_debug_object = p_cpu_driver->abi.p_debug_object;
  volatile int* p_debug_interrupt = p_interp->p_debug_interrupt;

  if (debug_active_at_addr(p_debug_object, *p_pc) || *p_debug_interrupt) {
    void* (*debug_callback)(struct cpu_driver*, int) =
        p_cpu_driver->abi.p_debug_callback;

    flags = interp_get_flags(*p_zf, *p_nf, *p_cf, *p_of, *p_df, *p_intf);
    state_6502_set_registers(p_state_6502,
                             *p_a,
                             *p_x,
                             *p_y,
                             *p_s,
                             flags,
                             *p_pc);

    debug_callback(p_cpu_driver, irq_vector);

    state_6502_get_registers(p_state_6502, p_a, p_x, p_y, p_s, &flags, p_pc);
    interp_set_flags(flags, p_zf, p_nf, p_cf, p_of, p_df, p_intf);
  }
}

static int
interp_is_branch_opcode(uint8_t opcode) {
  if ((!(opcode & 0x0F)) && (opcode & 0x10)) {
    return 1;
  }
  return 0;
}

static inline void
interp_check_log_bcd(struct interp_struct* p_interp) {
  if ((p_interp->counter_bcd % 1000) == 0) {
    log_do_log(k_log_instruction,
               k_log_info,
               "BCD mode was used (log every 1k)");
  }
  p_interp->counter_bcd++;
}

#define INTERP_TIMING_ADVANCE(num_cycles)                                     \
  countdown -= num_cycles;                                                    \
  countdown = timing_advance_time(p_timing, countdown);                       \

#define INTERP_MEMORY_READ(addr_read)                                         \
  v = memory_read_callback(p_memory_obj, addr_read, pc, 0);                   \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MEMORY_READ_POLL_IRQ(addr_read)                                \
  p_interp->callback_intf = intf;                                             \
  assert((p_interp->callback_do_irq = -1) == -1);                             \
  v = memory_read_callback(p_memory_obj, addr_read, pc, 1);                   \
  do_irq = p_interp->callback_do_irq;                                         \
  assert(do_irq != -1);                                                       \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MEMORY_WRITE(addr_write)                                       \
  if (memory_write_callback(p_memory_obj, addr_write, v, pc, 0) != 0) {       \
    write_callback_from =                                                     \
        p_memory_access->memory_write_needs_callback_from(p_memory_obj);      \
    read_callback_from =                                                      \
        p_memory_access->memory_read_needs_callback_from(p_memory_obj);       \
    assert(read_callback_from >= write_callback_from);                        \
  }                                                                           \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MEMORY_WRITE_POLL_IRQ(addr_write)                              \
  p_interp->callback_intf = intf;                                             \
  assert((p_interp->callback_do_irq = -1) == -1);                             \
  if (memory_write_callback(p_memory_obj, addr_write, v, pc, 1) != 0) {       \
    write_callback_from =                                                     \
        p_memory_access->memory_write_needs_callback_from(p_memory_obj);      \
    read_callback_from =                                                      \
        p_memory_access->memory_read_needs_callback_from(p_memory_obj);       \
    assert(read_callback_from >= write_callback_from);                        \
  }                                                                           \
  do_irq = p_interp->callback_do_irq;                                         \
  assert(do_irq != -1);                                                       \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MODE_ABS_READ(INSTR)                                           \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  pc += 3;                                                                    \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 4;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(2);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_65c12_BCD_ABS_READ(INSTR)                                 \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  pc += 3;                                                                    \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 5;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(3);                                                 \
    INTERP_MEMORY_READ_POLL_IRQ(addr);                                        \
    INSTR;                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABS_WRITE(INSTR)                                          \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  pc += 3;                                                                    \
  if (addr < write_callback_from) {                                           \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 4;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(2);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABS_READ_WRITE(INSTR)                                     \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  pc += 3;                                                                    \
  if (addr < write_callback_from) {                                           \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(3);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    if (is_65c12) {                                                           \
      INTERP_MEMORY_READ_POLL_IRQ(addr);                                      \
    } else {                                                                  \
      INTERP_MEMORY_WRITE_POLL_IRQ(addr);                                     \
    }                                                                         \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABr_READ(INSTR, reg_name)                                 \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  pc += 3;                                                                    \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 4;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      if (is_65c12) {                                                         \
        INTERP_TIMING_ADVANCE(3);                                             \
        interp_poll_irq_now(&do_irq, p_state_6502, intf);                     \
        INTERP_TIMING_ADVANCE(1);                                             \
      } else {                                                                \
        INTERP_TIMING_ADVANCE(3);                                             \
        INTERP_MEMORY_READ_POLL_IRQ(addr - 0x100);                            \
      }                                                                       \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(2);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    }                                                                         \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_65c12_BCD_ABr_READ(INSTR, reg_name)                       \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  pc += 3;                                                                    \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 5;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      INTERP_TIMING_ADVANCE(4);                                               \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(3);                                               \
    }                                                                         \
    INTERP_MEMORY_READ_POLL_IRQ(addr);                                        \
    INSTR;                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABr_WRITE(INSTR, reg_name)                                \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  pc += 3;                                                                    \
  if (addr < write_callback_from) {                                           \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 5;                                              \
  } else {                                                                    \
    if (is_65c12) {                                                           \
      INTERP_TIMING_ADVANCE(3);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    } else {                                                                  \
      addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                     \
      INTERP_TIMING_ADVANCE(3);                                               \
      INTERP_MEMORY_READ_POLL_IRQ(addr_temp);                                 \
    }                                                                         \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABr_READ_WRITE(INSTR, reg_name)                           \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  pc += 3;                                                                    \
  if (addr < write_callback_from) {                                           \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 7;                                              \
  } else {                                                                    \
    if (is_65c12) {                                                           \
      INTERP_TIMING_ADVANCE(4);                                               \
      INTERP_MEMORY_READ(addr);                                               \
      INTERP_MEMORY_READ_POLL_IRQ(addr);                                      \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(3);                                               \
      addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                     \
      INTERP_MEMORY_READ(addr_temp);                                          \
      INTERP_MEMORY_READ(addr);                                               \
      INTERP_MEMORY_WRITE_POLL_IRQ(addr);                                     \
    }                                                                         \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

/* An optimization on the 65c12 only. */
#define INTERP_MODE_ABX_READ_WRITE_6_CYC(INSTR)                               \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + x);                                                     \
  pc += 3;                                                                    \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  if (addr < write_callback_from) {                                           \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(3 + page_crossing);                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_MEMORY_READ_POLL_IRQ(addr);                                        \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDX_READ(INSTR)                                           \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  pc += 2;                                                                    \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_65c12_BCD_IDX_READ(INSTR)                                 \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  pc += 2;                                                                    \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 7;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(5);                                                 \
    INTERP_MEMORY_READ_POLL_IRQ(addr);                                        \
    INSTR;                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDX_WRITE(INSTR)                                          \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  pc += 2;                                                                    \
  if (addr < write_callback_from) {                                           \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDX_READ_WRITE(INSTR)                                     \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  pc += 2;                                                                    \
  if (addr < write_callback_from) {                                           \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 8;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(5);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_MEMORY_WRITE_POLL_IRQ(addr);                                       \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ID_READ(INSTR)                                            \
  addr = p_mem_read[pc + 1];                                                  \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  pc += 2;                                                                    \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 5;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(3);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ID_WRITE(INSTR)                                           \
  addr = p_mem_read[pc + 1];                                                  \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  pc += 2;                                                                    \
  if (addr < write_callback_from) {                                           \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 5;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(3);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDY_READ(INSTR)                                           \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  pc += 2;                                                                    \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 5;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      if (is_65c12) {                                                         \
        INTERP_TIMING_ADVANCE(4);                                             \
        interp_poll_irq_now(&do_irq, p_state_6502, intf);                     \
        INTERP_TIMING_ADVANCE(1);                                             \
      } else {                                                                \
        INTERP_TIMING_ADVANCE(4);                                             \
        INTERP_MEMORY_READ_POLL_IRQ(addr - 0x100);                            \
      }                                                                       \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(3);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    }                                                                         \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_65c12_BCD_IDY_READ(INSTR)                                 \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  pc += 2;                                                                    \
  if (addr < read_callback_from) {                                            \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 6;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      INTERP_TIMING_ADVANCE(5);                                               \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(4);                                               \
    }                                                                         \
    INTERP_MEMORY_READ_POLL_IRQ(addr);                                        \
    INSTR;                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
  }

#define INTERP_MODE_IDY_WRITE(INSTR)                                          \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  pc += 2;                                                                    \
  if (addr < write_callback_from) {                                           \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    if (is_65c12) {                                                           \
      INTERP_TIMING_ADVANCE(4);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    } else {                                                                  \
      addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                     \
      INTERP_TIMING_ADVANCE(4);                                               \
      INTERP_MEMORY_READ_POLL_IRQ(addr_temp);                                 \
    }                                                                         \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDY_READ_WRITE(INSTR)                                     \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  pc += 2;                                                                    \
  if (addr < write_callback_from) {                                           \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 8;                                              \
  } else {                                                                    \
    addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                       \
    INTERP_TIMING_ADVANCE(4);                                                 \
    INTERP_MEMORY_READ(addr_temp);                                            \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_MEMORY_WRITE_POLL_IRQ(addr);                                       \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ZPG_READ(INSTR)                                           \
  addr = p_mem_read[pc + 1];                                                  \
  pc += 2;                                                                    \
  v = p_mem_read[addr];                                                       \
  INSTR;                                                                      \
  cycles_this_instruction = 3;

#define INTERP_MODE_ZPG_READ_WRITE(INSTR)                                     \
  addr = p_mem_read[pc + 1];                                                  \
  pc += 2;                                                                    \
  v = p_mem_read[addr];                                                       \
  INSTR;                                                                      \
  p_mem_write[addr] = v;                                                      \
  cycles_this_instruction = 5;

#define INTERP_MODE_ZPG_WRITE(INSTR)                                          \
  addr = p_mem_read[pc + 1];                                                  \
  pc += 2;                                                                    \
  INSTR;                                                                      \
  p_mem_write[addr] = v;                                                      \
  cycles_this_instruction = 3;

#define INTERP_MODE_ZPr_READ(INSTR, reg_name)                                 \
  addr = p_mem_read[pc + 1];                                                  \
  pc += 2;                                                                    \
  addr += reg_name;                                                           \
  addr &= 0xFF;                                                               \
  v = p_mem_read[addr];                                                       \
  INSTR;                                                                      \
  cycles_this_instruction = 4;

#define INTERP_MODE_ZPr_WRITE(INSTR, reg_name)                                \
  addr = p_mem_read[pc + 1];                                                  \
  pc += 2;                                                                    \
  addr += reg_name;                                                           \
  addr &= 0xFF;                                                               \
  INSTR;                                                                      \
  p_mem_write[addr] = v;                                                      \
  cycles_this_instruction = 4;

#define INTERP_MODE_ZPX_READ_WRITE(INSTR)                                     \
  addr = p_mem_read[pc + 1];                                                  \
  pc += 2;                                                                    \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  v = p_mem_read[addr];                                                       \
  INSTR;                                                                      \
  p_mem_write[addr] = v;                                                      \
  cycles_this_instruction = 5;

#define INTERP_LOAD_NZ_FLAGS(reg_name)                                        \
  nf = !!(reg_name & 0x80);                                                   \
  zf = (reg_name == 0);

#define INTERP_INSTR_BRANCH(condition)                                        \
  v = p_mem_read[pc + 1];                                                     \
  cycles_this_instruction = 2;                                                \
  pc += 2;                                                                    \
  if (condition) {                                                            \
    addr_temp = pc;                                                           \
    cycles_this_instruction++;                                                \
    pc = (uint16_t) ((int) addr_temp + (int8_t) v);                           \
    /* Add a cycle if the branch crossed a page. */                           \
    if ((pc >> 8) ^ (addr_temp >> 8)) {                                       \
      cycles_this_instruction++;                                              \
    }                                                                         \
  }

#define INTERP_INSTR_ADC()                                                    \
  temp_int = (a + v + cf);                                                    \
  zf = !(temp_int & 0xFF);                                                    \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  of = !!((a ^ temp_int) & (v ^ temp_int) & 0x80);                            \
  nf = !!(temp_int & 0x80);                                                   \
  cf = (temp_int >= 0x100);                                                   \
  a = temp_int;

#define INTERP_INSTR_BCD_ADC()                                                \
  temp_int = (a + v + cf);                                                    \
  zf = !(temp_int & 0xFF);                                                    \
  if (df) {                                                                   \
    interp_check_log_bcd(p_interp);                                           \
    /* Fix up decimal carry on first nibble. */                               \
    int decimal_carry = ((a & 0x0F) + (v & 0x0F) + cf);                       \
    if (decimal_carry >= 0x0A) {                                              \
      temp_int += 0x06;                                                       \
      if (decimal_carry >= 0x1A) {                                            \
        temp_int -= 0x10;                                                     \
      }                                                                       \
    }                                                                         \
  }                                                                           \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  of = !!((a ^ temp_int) & (v ^ temp_int) & 0x80);                            \
  nf = !!(temp_int & 0x80);                                                   \
  if (df) {                                                                   \
    if (temp_int >= 0xA0) {                                                   \
      temp_int += 0x60;                                                       \
    }                                                                         \
  }                                                                           \
  cf = (temp_int >= 0x100);                                                   \
  a = temp_int;

#define INTERP_INSTR_AHX()                                                    \
  v = (a & x & ((addr >> 8) + 1));

#define INTERP_INSTR_AND()                                                    \
  a &= v;                                                                     \
  INTERP_LOAD_NZ_FLAGS(a);

#define INTERP_INSTR_ASL()                                                    \
  cf = !!(v & 0x80);                                                          \
  v <<= 1;                                                                    \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_BIT()                                                    \
  zf = !(a & v);                                                              \
  nf = !!(v & 0x80);                                                          \
  of = !!(v & 0x40);

#define INTERP_INSTR_CMP(reg_name)                                            \
  cf = (reg_name >= v);                                                       \
  v = (reg_name - v);                                                         \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_DCP()                                                    \
  v--;                                                                        \
  INTERP_LOAD_NZ_FLAGS((a - v));                                              \
  cf = (a >= v);

#define INTERP_INSTR_DEC()                                                    \
  v--;                                                                        \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_EOR()                                                    \
  a ^= v;                                                                     \
  INTERP_LOAD_NZ_FLAGS(a);

#define INTERP_INSTR_INC()                                                    \
  v++;                                                                        \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_KIL()                                                    \
  util_bail("KIL");

#define INTERP_INSTR_LAX()                                                    \
  a = v;                                                                      \
  x = v;                                                                      \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_LDA()                                                    \
  a = v;                                                                      \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_LDX()                                                    \
  x = v;                                                                      \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_LDY()                                                    \
  y = v;                                                                      \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_LSR()                                                    \
  cf = (v & 0x01);                                                            \
  v >>= 1;                                                                    \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_NOP()

#define INTERP_INSTR_ORA()                                                    \
  a |= v;                                                                     \
  INTERP_LOAD_NZ_FLAGS(a);

#define INTERP_INSTR_RLA()                                                    \
  temp_int = cf;                                                              \
  cf = !!(v & 0x80);                                                          \
  v <<= 1;                                                                    \
  v |= temp_int;                                                              \
  a &= v;                                                                     \
  INTERP_LOAD_NZ_FLAGS(a);

#define INTERP_INSTR_ROL()                                                    \
  temp_int = cf;                                                              \
  cf = !!(v & 0x80);                                                          \
  v <<= 1;                                                                    \
  v |= temp_int;                                                              \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_ROR()                                                    \
  temp_int = cf;                                                              \
  cf = (v & 0x01);                                                            \
  v >>= 1;                                                                    \
  v |= (temp_int << 7);                                                       \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_RRA()                                                    \
  temp_int = cf;                                                              \
  cf = (v & 0x01);                                                            \
  v >>= 1;                                                                    \
  v |= (temp_int << 7);                                                       \
  a &= v;                                                                     \
  if (df) {                                                                   \
    INTERP_INSTR_BCD_ADC();                                                   \
  } else {                                                                    \
    INTERP_INSTR_ADC();                                                       \
  }

#define INTERP_INSTR_SAX()                                                    \
  v = (a & x);

#define INTERP_INSTR_SBC()                                                    \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  /* "SBC simply takes the ones complement of the second value and then       \
   * performs an ADC"                                                         \
   */                                                                         \
  temp_int = (a + (uint8_t) ~v + cf);                                         \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  of = !!((a ^ temp_int) & ((uint8_t) ~v ^ temp_int) & 0x80);                 \
  /* In decimal mode, NZ flags are based on this interim value. */            \
  INTERP_LOAD_NZ_FLAGS((temp_int & 0xFF));                                    \
  cf = !!(temp_int & 0x100);                                                  \
  a = temp_int;

#define INTERP_INSTR_BCD_SBC()                                                \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  /* "SBC simply takes the ones complement of the second value and then       \
   * performs an ADC"                                                         \
   */                                                                         \
  temp_int = (a + (uint8_t) ~v + cf);                                         \
  if (df) {                                                                   \
    interp_check_log_bcd(p_interp);                                           \
    /* Fix up decimal carry on first nibble. */                               \
    if (((v & 0x0F) + !cf) > (a & 0x0F)) {                                    \
      temp_int -= 0x06;                                                       \
    }                                                                         \
  }                                                                           \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  of = !!((a ^ temp_int) & ((uint8_t) ~v ^ temp_int) & 0x80);                 \
  /* In decimal mode, NZ flags are based on this interim value. */            \
  INTERP_LOAD_NZ_FLAGS((temp_int & 0xFF));                                    \
  if (df) {                                                                   \
    if ((v + !cf) > a) {                                                      \
      temp_int -= 0x60;                                                       \
    }                                                                         \
  }                                                                           \
  cf = !!(temp_int & 0x100);                                                  \
  a = temp_int;

#define INTERP_INSTR_SHY()                                                    \
  v = (y & ((addr_temp >> 8) + 1));

#define INTERP_INSTR_SLO()                                                    \
  cf = !!(v & 0x80);                                                          \
  v <<= 1;                                                                    \
  a |= v;                                                                     \
  INTERP_LOAD_NZ_FLAGS(a);

#define INTERP_INSTR_SRE()                                                    \
  cf = (v & 0x01);                                                            \
  v >>= 1;                                                                    \
  a ^= v;                                                                     \
  INTERP_LOAD_NZ_FLAGS(a);

#define INTERP_INSTR_STA()                                                    \
  v = a;

#define INTERP_INSTR_STX()                                                    \
  v = x;

#define INTERP_INSTR_STY()                                                    \
  v = y;

#define INTERP_INSTR_STZ()                                                    \
  v = 0;

#define INTERP_INSTR_TRB()                                                    \
  zf = !(v & a);                                                              \
  v &= ~a;

#define INTERP_INSTR_TSB()                                                    \
  zf = !(v & a);                                                              \
  v |= a;

int64_t
interp_enter_with_details(struct interp_struct* p_interp,
                          int64_t countdown,
                          int (*instruction_callback)(void* p,
                                                      uint16_t next_pc,
                                                      uint8_t done_opcode,
                                                      uint16_t done_addr,
                                                      int next_is_irq,
                                                      int irq_pending),
                          void* p_callback_context) {
  uint16_t pc;
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t s;
  uint8_t flags;
  uint8_t zf;
  uint8_t nf;
  uint8_t cf;
  uint8_t of;
  uint8_t df;
  uint8_t intf;

  int temp_int;
  uint8_t temp_u8;
  int page_crossing;
  uint16_t addr_temp;
  uint8_t v;
  int poll_irq;
  uint32_t cpu_driver_flags;
  uint16_t read_callback_from;
  uint16_t write_callback_from;
  int is_nmi;

  struct state_6502* p_state_6502 = p_interp->driver.abi.p_state_6502;
  struct timing_struct* p_timing = p_interp->driver.p_timing;
  struct memory_access* p_memory_access = p_interp->driver.p_memory_access;
  uint8_t (*memory_read_callback)(void*, uint16_t, uint16_t, int) =
      p_memory_access->memory_read_callback;
  int (*memory_write_callback)(void*, uint16_t, uint8_t, uint16_t, int) =
      p_memory_access->memory_write_callback;
  void* p_memory_obj = p_memory_access->p_callback_obj;
  uint8_t* p_mem_read = p_interp->p_mem_read;
  uint8_t* p_mem_write = p_interp->p_mem_write;
  uint8_t* p_stack = (p_mem_write + k_6502_stack_addr);
  volatile int* p_debug_interrupt = p_interp->p_debug_interrupt;
  int64_t cycles_this_instruction = 0;
  uint8_t opcode = 0;
  int special_checks = k_interp_special_entry;
  uint16_t addr = 0;
  int do_irq = 0;
  int is_65c12 = p_interp->is_65c12;

  assert(countdown >= 0);

  read_callback_from =
      p_memory_access->memory_read_needs_callback_from(p_memory_obj);
  write_callback_from =
      p_memory_access->memory_write_needs_callback_from(p_memory_obj);
  /* We use write_callback_from for read-modify-write. */
  assert(read_callback_from >= write_callback_from);
  /* The code assumes that zero page and stack accesses don't incur special
   * handling.
   */
  assert(write_callback_from >= 0x200);

  state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
  interp_set_flags(flags, &zf, &nf, &cf, &of, &df, &intf);

  if (p_interp->debug_subsystem_active) {
    special_checks |= k_interp_special_debug;
  }
  if (instruction_callback) {
    special_checks |= k_interp_special_callback;
  }

  /* Jump in at the checks / fetch. Checking for countdown==0 on entry is
   * required because e.g. JIT mode will bounce in this way sometimes.
   */
  goto do_special_checks;

  while (1) {
    switch (opcode) {
    case 0x00: /* BRK */
      /* EMU NOTE: if both an NMI and normal IRQ are asserted at the same time,        * only the NMI should fire. This is confirmed via visual 6502; see:
       * http://forum.6502.org/viewtopic.php?t=1797
       * Note that jsbeeb, b-em and beebem all appear to get this wrong, they
       * will run the 7 cycle interrupt sequence twice in a row, which would
       * be visible as stack and timing artifacts. b2 looks likely to be
       * correct as it is a much more low level 6502 emulation.
       */
      temp_u8 = 0;
      addr = k_6502_vector_irq;
      if (!do_irq) {
        /* It's a BRK, not an IRQ. */
        temp_u8 = (1 << k_flag_brk);
        pc += 2;
      }
      INTERP_TIMING_ADVANCE(3);
      v = interp_get_flags(zf, nf, cf, of, df, intf);
      v |= (temp_u8 | (1 << k_flag_always_set));
      is_nmi = state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi);
      if (is_65c12) {
        /* CMOS */
        /* The CMOS part clears DF. */
        df = 0;
        /* The CMOS part allegedly clears up BRK vs. NMI. Needs testing. */
        if (!do_irq && is_nmi) {
          is_nmi = 0;
          /* NOTE: unsure if this is necessary or if it should be an assert. */
          special_checks |= k_interp_special_poll_irq;
        }
      }
      /* EMU NOTE: for the NMOS part, if an NMI hits early enough in the 7-cycle
       * BRK sequence, the NMI overrides and the BRK can go missing!
       */
      if (is_nmi) {
        state_6502_clear_edge_triggered_irq(p_state_6502, k_state_6502_irq_nmi);
        addr = k_6502_vector_nmi;
      }
      p_stack[s--] = (pc >> 8);
      p_stack[s--] = (pc & 0xFF);
      p_stack[s--] = v;
      pc = (p_mem_read[addr] | (p_mem_read[(uint16_t) (addr + 1)] << 8));
      intf = 1;
      do_irq = 0;
      cycles_this_instruction = 4;
      break;
    case 0x01: /* ORA idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_ORA());
      break;
    case 0x02: /* KIL */ /* Undocumented. */ /* NOP imm */
      if (is_65c12) {
        pc += 2;
        cycles_this_instruction = 2;
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x03: /* SLO idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDX_READ_WRITE(INTERP_INSTR_SLO());
      }
      break;
    case 0x04: /* NOP zpg */ /* Undocumented. */ /* TSB zpg */
      if (is_65c12) {
        INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_TSB());
      } else {
        pc += 2;
        cycles_this_instruction = 3;
      }
      break;
    case 0x05: /* ORA zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_ORA());
      break;
    case 0x06: /* ASL zpg */
      INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_ASL());
      break;
    case 0x07: /* SLO zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_SLO());
      }
      break;
    case 0x08: /* PHP */
      v = interp_get_flags(zf, nf, cf, of, df, intf);
      v |= ((1 << k_flag_brk) | (1 << k_flag_always_set));
      p_stack[s--] = v;
      pc++;
      cycles_this_instruction = 3;
      break;
    case 0x09: /* ORA imm */
      a |= p_mem_read[pc + 1];
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0x0A: /* ASL A */
      v = a;
      INTERP_INSTR_ASL();
      a = v;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x0B: /* ANC imm */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        v = p_mem_read[pc + 1];
        a &= v;
        INTERP_LOAD_NZ_FLAGS(a);
        cf = nf;
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0x0C: /* NOP abs */ /* Undocumented. */ /* TSB abs */
      if (is_65c12) {
        INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_TSB());
      } else {
        INTERP_MODE_ABS_READ(INTERP_INSTR_NOP());
      }
      break;
    case 0x0D: /* ORA abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_ORA());
      break;
    case 0x0E: /* ASL abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_ASL());
      break;
    case 0x0F: /* SLO abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_SLO());
      }
      break;
    case 0x10: /* BPL */
      INTERP_INSTR_BRANCH(!nf);
      break;
    case 0x11: /* ORA idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_ORA());
      break;
    case 0x12: /* KIL */ /* Undocumented. */ /* ORA id */
      if (is_65c12) {
        INTERP_MODE_ID_READ(INTERP_INSTR_ORA());
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x13: /* SLO idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDY_READ_WRITE(INTERP_INSTR_SLO());
      }
      break;
    case 0x14: /* NOP zpx */ /* Undocumented. */ /* TRB zpg */
      if (is_65c12) {
        INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_TRB());
      } else {
        pc += 2;
        cycles_this_instruction = 4;
      }
      break;
    case 0x15: /* ORA zpx */
      INTERP_MODE_ZPr_READ(INTERP_INSTR_ORA(), x);
      break;
    case 0x16: /* ASL zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_ASL();
      p_mem_write[addr] = v;
      break;
    case 0x17: /* SLO zpx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPX_READ_WRITE(INTERP_INSTR_SLO());
      }
      break;
    case 0x18: /* CLC */
      cf = 0;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x19: /* ORA aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_ORA(), y);
      break;
    case 0x1A: /* NOP */ /* Undocumented. */ /* INC A */
      if (is_65c12) {
        a++;
        INTERP_LOAD_NZ_FLAGS(a);
        pc++;
        cycles_this_instruction = 2;
      } else {
        pc++;
        cycles_this_instruction = 2;
      }
      break;
    case 0x1B: /* SLO aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_SLO(), y);
      }
      break;
    case 0x1C: /* NOP abx */ /* Undocumented. */ /* TRB abs */
      if (is_65c12) {
        INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_TRB());
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_NOP(), x);
      }
      break;
    case 0x1D: /* ORA abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_ORA(), x);
      break;
    case 0x1E: /* ASL abx */
      if (is_65c12) {
        INTERP_MODE_ABX_READ_WRITE_6_CYC(INTERP_INSTR_ASL());
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_ASL(), x);
      }
      break;
    case 0x1F: /* SLO abx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_SLO(), x);
      }
      break;
    case 0x20: /* JSR */
      addr = *(uint16_t*) &p_mem_read[pc + 1];
      addr_temp = (pc + 2);
      p_stack[s--] = (addr_temp >> 8);
      p_stack[s--] = (addr_temp & 0xFF);
      pc = addr;
      cycles_this_instruction = 6;
      break;
    case 0x21: /* AND idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_AND());
      break;
    case 0x22: /* KIL */ /* Undocumented. */ /* NOP imm */
      if (is_65c12) {
        pc += 2;
        cycles_this_instruction = 2;
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x23: /* RLA idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDX_READ_WRITE(INTERP_INSTR_RLA());
      }
      break;
    case 0x24: /* BIT zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_BIT());
      break;
    case 0x25: /* AND zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_AND());
      break;
    case 0x26: /* ROL zpg */
      INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_ROL());
      break;
    case 0x27: /* RLA zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_RLA());
      }
      break;
    case 0x28: /* PLP */
      /* PLP fiddles with the interrupt disable flag so we need to tick it
       * out to get the correct ordering and behavior.
       */
      INTERP_TIMING_ADVANCE(2);
      interp_poll_irq_now(&do_irq, p_state_6502, intf);
      v = p_stack[++s];
      interp_set_flags(v, &zf, &nf, &cf, &of, &df, &intf);
      pc++;
      INTERP_TIMING_ADVANCE(2);
      goto check_irq;
    case 0x29: /* AND imm */
      v = p_mem_read[pc + 1];
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0x2A: /* ROL A */
      v = a;
      INTERP_INSTR_ROL();
      a = v;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x2B: /* ANC imm */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        v = p_mem_read[pc + 1];
        a &= v;
        INTERP_LOAD_NZ_FLAGS(a);
        cf = nf;
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0x2C: /* BIT abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_BIT());
      break;
    case 0x2D: /* AND abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_AND());
      break;
    case 0x2E: /* ROL abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_ROL());
      break;
    case 0x2F: /* RLA abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_RLA());
      }
      break;
    case 0x30: /* BMI */
      INTERP_INSTR_BRANCH(nf);
      break;
    case 0x31: /* AND idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_AND());
      break;
    case 0x32: /* KIL */ /* Undocumented. */ /* AND id */
      if (is_65c12) {
        INTERP_MODE_ID_READ(INTERP_INSTR_AND());
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x33: /* RLA idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDY_READ_WRITE(INTERP_INSTR_RLA());
      }
      break;
    case 0x34: /* NOP zpx */ /* Undocumented. */ /* BIT zpx */
      if (is_65c12) {
        INTERP_MODE_ZPr_READ(INTERP_INSTR_BIT(), x);
      } else {
        pc += 2;
        cycles_this_instruction = 4;
      }
      break;
    case 0x35: /* AND zpx */
      INTERP_MODE_ZPr_READ(INTERP_INSTR_AND(), x);
      break;
    case 0x36: /* ROL zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_ROL();
      p_mem_write[addr] = v;
      break;
    case 0x37: /* RLA zpx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPX_READ_WRITE(INTERP_INSTR_RLA());
      }
      break;
    case 0x38: /* SEC */
      cf = 1;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x39: /* AND aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_AND(), y);
      break;
    case 0x3A: /* NOP */ /* Undocumented. */ /* DEC A */
      if (is_65c12) {
        a--;
        INTERP_LOAD_NZ_FLAGS(a);
        pc++;
        cycles_this_instruction = 2;
      } else {
        pc++;
        cycles_this_instruction = 2;
      }
      break;
    case 0x3B: /* RLA aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_RLA(), y);
      }
      break;
    case 0x3C: /* NOP abx */ /* Undocumented. */ /* BIT abx */
      if (is_65c12) {
        INTERP_MODE_ABr_READ(INTERP_INSTR_BIT(), x);
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_NOP(), x);
      }
      break;
    case 0x3D: /* AND abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_AND(), x);
      break;
    case 0x3E: /* ROL abx */
      if (is_65c12) {
        INTERP_MODE_ABX_READ_WRITE_6_CYC(INTERP_INSTR_ROL());
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_ROL(), x);
      }
      break;
    case 0x3F: /* RLA abx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_RLA(), x);
      }
      break;
    case 0x40: /* RTI */
      /* RTI fiddles with the interrupt disable flag so we need to tick it
       * out to get the correct ordering and behavior.
       */
      INTERP_TIMING_ADVANCE(4);
      v = p_stack[++s];
      interp_set_flags(v, &zf, &nf, &cf, &of, &df, &intf);
      pc = p_stack[++s];
      pc |= (p_stack[++s] << 8);
      interp_poll_irq_now(&do_irq, p_state_6502, intf);
      INTERP_TIMING_ADVANCE(2);
      goto check_irq;
    case 0x41: /* EOR idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_EOR());
      break;
    case 0x42: /* KIL */ /* Undocumented. */ /* NOP imm */
      if (is_65c12) {
        pc += 2;
        cycles_this_instruction = 2;
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x43: /* SRE idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDX_READ_WRITE(INTERP_INSTR_SRE());
      }
      break;
    case 0x44: /* NOP zpg */ /* Undocumented. */
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x45: /* EOR zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_EOR());
      break;
    case 0x46: /* LSR zpg */
      INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_LSR());
      break;
    case 0x47: /* SRE zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_SRE());
      }
      break;
    case 0x48: /* PHA */
      p_stack[s--] = a;
      pc++;
      cycles_this_instruction = 3;
      break;
    case 0x49: /* EOR imm */
      a ^= p_mem_read[pc + 1];
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0x4A: /* LSR A */
      v = a;
      INTERP_INSTR_LSR();
      a = v;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x4B: /* ALR imm */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        v = p_mem_read[pc + 1];
        a &= v;
        cf = (a & 0x01);
        a >>= 1;
        INTERP_LOAD_NZ_FLAGS(a);
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0x4C: /* JMP abs */
      pc = *(uint16_t*) &p_mem_read[pc + 1];
      cycles_this_instruction = 3;
      break;
    case 0x4D: /* EOR abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_EOR());
      break;
    case 0x4E: /* LSR abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_LSR());
      break;
    case 0x4F: /* SRE abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_SRE());
      }
      break;
    case 0x50: /* BVC */
      INTERP_INSTR_BRANCH(!of);
      break;
    case 0x51: /* EOR idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_EOR());
      break;
    case 0x52: /* KIL */ /* Undocumented. */ /* EOR id */
      if (is_65c12) {
        INTERP_MODE_ID_READ(INTERP_INSTR_EOR());
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x53: /* SRE idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDY_READ_WRITE(INTERP_INSTR_SRE());
      }
      break;
    case 0x54: /* NOP zpx */ /* Undocumented. */
    case 0xD4:
    case 0xF4:
      pc += 2;
      cycles_this_instruction = 4;
      break;
    case 0x55: /* EOR zpx */
      INTERP_MODE_ZPr_READ(INTERP_INSTR_EOR(), x);
      break;
    case 0x56: /* LSR zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_LSR();
      p_mem_write[addr] = v;
      break;
    case 0x57: /* SRE zpx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPX_READ_WRITE(INTERP_INSTR_SRE());
      }
      break;
    case 0x58: /* CLI */
      /* CLI enables interrupts but this takes effect after the IRQ poll
       * point.
       */
      interp_poll_irq_now(&do_irq, p_state_6502, intf);
      intf = 0;
      pc++;
      INTERP_TIMING_ADVANCE(2);
      goto check_irq;
    case 0x59: /* EOR aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_EOR(), y);
      break;
    case 0x5A: /* NOP */ /* Undocumented. */ /* PHY */
      if (is_65c12) {
        p_stack[s--] = y;
        pc++;
        cycles_this_instruction = 3;
      } else {
        pc++;
        cycles_this_instruction = 2;
      }
      break;
    case 0x5B: /* SRE aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_SRE(), y);
      }
      break;
    case 0x5C: /* NOP abx */ /* Undocumented. */ /* NOP abs (8) */
      if (is_65c12) {
        util_bail("NOP abs (8)");
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_NOP(), x);
      }
      break;
    case 0x5D: /* EOR abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_EOR(), x);
      break;
    case 0x5E: /* LSR abx */
      if (is_65c12) {
        INTERP_MODE_ABX_READ_WRITE_6_CYC(INTERP_INSTR_LSR());
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_LSR(), x);
      }
      break;
    case 0x5F: /* SRE abx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_SRE(), x);
      }
      break;
    case 0x60: /* RTS */
      pc = p_stack[++s];
      pc |= (p_stack[++s] << 8);
      pc++;
      cycles_this_instruction = 6;
      break;
    case 0x61: /* ADC idx */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_IDX_READ(INTERP_INSTR_BCD_ADC());
        } else {
          INTERP_MODE_IDX_READ(INTERP_INSTR_BCD_ADC());
        }
      } else {
        INTERP_MODE_IDX_READ(INTERP_INSTR_ADC());
      }
      break;
    case 0x62: /* KIL */ /* Undocumented. */ /* NOP imm */
      if (is_65c12) {
        pc += 2;
        cycles_this_instruction = 2;
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x63: /* RRA idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDX_READ_WRITE(INTERP_INSTR_RRA());
      }
      break;
    case 0x64: /* NOP zpg */ /* Undocumented. */ /* STZ zpg */
      if (is_65c12) {
        INTERP_MODE_ZPG_WRITE(INTERP_INSTR_STZ());
      } else {
        pc += 2;
        cycles_this_instruction = 3;
      }
      break;
    case 0x65: /* ADC zpg */
      if (df) {
        INTERP_MODE_ZPG_READ(INTERP_INSTR_BCD_ADC());
        if (is_65c12) {
          cycles_this_instruction = 4;
        }
      } else {
        INTERP_MODE_ZPG_READ(INTERP_INSTR_ADC());
      }
      break;
    case 0x66: /* ROR zpg */
      INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_ROR());
      break;
    case 0x67: /* RRA zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_RRA());
      }
      break;
    case 0x68: /* PLA */
      a = p_stack[++s];
      INTERP_LOAD_NZ_FLAGS(a);
      pc++;
      cycles_this_instruction = 4;
      break;
    case 0x69: /* ADC imm */
      v = p_mem_read[pc + 1];
      pc += 2;
      cycles_this_instruction = 2;
      if (df) {
        if (is_65c12) {
          cycles_this_instruction = 3;
        }
        INTERP_INSTR_BCD_ADC();
      } else {
        INTERP_INSTR_ADC();
      }
      break;
    case 0x6A: /* ROR A */
      v = a;
      INTERP_INSTR_ROR();
      a = v;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x6B: /* ARR imm */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ARR imm");
      }
      break;
    case 0x6C: /* JMP ind */
      addr = *(uint16_t*) &p_mem_read[pc + 1];
      if (is_65c12) {
        pc = *(uint16_t*) &p_mem_read[addr];
        cycles_this_instruction = 6;
      } else {
        addr_temp = ((addr + 1) & 0xFF);
        addr_temp |= (addr & 0xFF00);
        pc = p_mem_read[addr];
        pc |= (p_mem_read[addr_temp] << 8);
        cycles_this_instruction = 5;
      }
      break;
    case 0x6D: /* ADC abs */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_ABS_READ(INTERP_INSTR_BCD_ADC());
        } else {
          INTERP_MODE_ABS_READ(INTERP_INSTR_BCD_ADC());
        }
      } else {
        INTERP_MODE_ABS_READ(INTERP_INSTR_ADC());
      }
      break;
    case 0x6E: /* ROR abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_ROR());
      break;
    case 0x6F: /* RRA abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_RRA());
      }
      break;
    case 0x70: /* BVS */
      INTERP_INSTR_BRANCH(of);
      break;
    case 0x71: /* ADC idy */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_IDY_READ(INTERP_INSTR_BCD_ADC());
        } else {
          INTERP_MODE_IDY_READ(INTERP_INSTR_BCD_ADC());
        }
      } else {
        INTERP_MODE_IDY_READ(INTERP_INSTR_ADC());
      }
      break;
    case 0x72: /* KIL */ /* Undocumented. */ /* ADC id */
      if (is_65c12) {
        if (df) {
          INTERP_MODE_ID_READ(INTERP_INSTR_BCD_ADC());
        } else {
          INTERP_MODE_ID_READ(INTERP_INSTR_ADC());
        }
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x73: /* RRA idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDY_READ_WRITE(INTERP_INSTR_RRA());
      }
      break;
    case 0x74: /* NOP zpx */ /* Undocumented. */ /* STZ zpx */
      if (is_65c12) {
        INTERP_MODE_ZPr_WRITE(INTERP_INSTR_STZ(), x);
      } else {
        pc += 2;
        cycles_this_instruction = 4;
      }
      break;
    case 0x75: /* ADC zpx */
      if (df) {
        INTERP_MODE_ZPr_READ(INTERP_INSTR_BCD_ADC(), x);
        if (is_65c12) {
          cycles_this_instruction = 5;
        }
      } else {
        INTERP_MODE_ZPr_READ(INTERP_INSTR_ADC(), x);
      }
      break;
    case 0x76: /* ROR zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_ROR();
      p_mem_write[addr] = v;
      break;
    case 0x77: /* RRA zpx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPX_READ_WRITE(INTERP_INSTR_RRA());
      }
      break;
    case 0x78: /* SEI */
      /* SEI disables interrupts but this takes effect after the IRQ poll
       * point.
       */
      interp_poll_irq_now(&do_irq, p_state_6502, intf);
      intf = 1;
      pc++;
      INTERP_TIMING_ADVANCE(2);
      goto check_irq;
    case 0x79: /* ADC aby */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_ABr_READ(INTERP_INSTR_BCD_ADC(), y);
        } else {
          INTERP_MODE_ABr_READ(INTERP_INSTR_BCD_ADC(), y);
        }
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_ADC(), y);
      }
      break;
    case 0x7A: /* NOP */ /* Undocumented. */ /* PLY */
      if (is_65c12) {
        y = p_stack[++s];
        INTERP_LOAD_NZ_FLAGS(y);
        pc++;
        cycles_this_instruction = 4;
      } else {
        pc++;
        cycles_this_instruction = 2;
      }
      break;
    case 0x7B: /* RRA aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_RRA(), y);
      }
      break;
    case 0x7C: /* NOP abx */ /* Undocumented. */ /* JMP iax */
      if (is_65c12) {
        addr = *(uint16_t*) &p_mem_read[pc + 1];
        addr += x;
        pc = *(uint16_t*) &p_mem_read[addr];
        cycles_this_instruction = 6;
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_NOP(), x);
      }
      break;
    case 0x7D: /* ADC abx */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_ABr_READ(INTERP_INSTR_BCD_ADC(), x);
        } else {
          INTERP_MODE_ABr_READ(INTERP_INSTR_BCD_ADC(), x);
        }
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_ADC(), x);
      }
      break;
    case 0x7E: /* ROR abx */
      if (is_65c12) {
        INTERP_MODE_ABX_READ_WRITE_6_CYC(INTERP_INSTR_ROR());
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_ROR(), x);
      }
      break;
    case 0x7F: /* RRA abx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_RRA(), x);
      }
      break;
    case 0x80: /* NOP imm */ /* Undocumented. */ /* BRA */
      if (is_65c12) {
        INTERP_INSTR_BRANCH(1);
      } else {
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0x81: /* STA idx */
      INTERP_MODE_IDX_WRITE(INTERP_INSTR_STA());
      break;
    case 0x82: /* NOP imm */ /* Undocumented. */
    case 0xC2:
    case 0xE2:
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0x83: /* SAX idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDX_WRITE(INTERP_INSTR_SAX());
      }
      break;
    case 0x84: /* STY zpg */
      INTERP_MODE_ZPG_WRITE(INTERP_INSTR_STY());
      break;
    case 0x85: /* STA zpg */
      INTERP_MODE_ZPG_WRITE(INTERP_INSTR_STA());
      break;
    case 0x86: /* STX zpg */
      INTERP_MODE_ZPG_WRITE(INTERP_INSTR_STX());
      break;
    case 0x87: /* SAX zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPG_WRITE(INTERP_INSTR_SAX());
      }
      break;
    case 0x88: /* DEY */
      y--;
      INTERP_LOAD_NZ_FLAGS(y);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x89: /* NOP imm */ /* Undocumented. */ /* BIT imm */
      if (is_65c12) {
        v = p_mem_read[pc + 1];
        INTERP_INSTR_BIT();
        pc += 2;
        cycles_this_instruction = 2;
      } else {
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0x8A: /* TXA */
      a = x;
      INTERP_LOAD_NZ_FLAGS(a);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x8B: /* XAA */ /* Undocumented and unstable. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        /* Battle Tank hit this! */
        v = p_mem_read[pc + 1];
        /* EMU NOTE: Using 0xEE for the magic constant as per jsbeeb and b-em,
         * but on a Model B issue 3, I'm seeing the instability; magic constant
         * is usually 0xE8 but sometimes 0x68.
         * See: http://visual6502.org/wiki/index.php?title=6502_Opcode_8B_%28XAA,_ANE%29
         */
        a = ((a | 0xEE) & x & v);
        INTERP_LOAD_NZ_FLAGS(a);
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0x8C: /* STY abs */
      INTERP_MODE_ABS_WRITE(INTERP_INSTR_STY());
      break;
    case 0x8D: /* STA abs */
      INTERP_MODE_ABS_WRITE(INTERP_INSTR_STA());
      break;
    case 0x8E: /* STX abs */
      INTERP_MODE_ABS_WRITE(INTERP_INSTR_STX());
      break;
    case 0x8F: /* SAX abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABS_WRITE(INTERP_INSTR_SAX());
      }
      break;
    case 0x90: /* BCC */
      INTERP_INSTR_BRANCH(!cf);
      break;
    case 0x91: /* STA idy */
      INTERP_MODE_IDY_WRITE(INTERP_INSTR_STA());
      break;
    case 0x92: /* KIL */ /* Undocumented. */ /* STA id */
      if (is_65c12) {
        INTERP_MODE_ID_WRITE(INTERP_INSTR_STA());
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0x93: /* AHX idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDY_WRITE(INTERP_INSTR_AHX());
      }
      break;
    case 0x94: /* STY zpx */
      INTERP_MODE_ZPr_WRITE(INTERP_INSTR_STY(), x);
      break;
    case 0x95: /* STA zpx */
      INTERP_MODE_ZPr_WRITE(INTERP_INSTR_STA(), x);
      break;
    case 0x96: /* STX zpy */
      INTERP_MODE_ZPr_WRITE(INTERP_INSTR_STX(), y);
      break;
    case 0x97: /* SAX zpy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPr_WRITE(INTERP_INSTR_SAX(), y);
      }
      break;
    case 0x98: /* TYA */
      a = y;
      INTERP_LOAD_NZ_FLAGS(a);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x99: /* STA aby */
      INTERP_MODE_ABr_WRITE(INTERP_INSTR_STA(), y);
      break;
    case 0x9A: /* TXS */
      s = x;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x9B: /* TAS aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("TAS aby");
      }
      break;
    case 0x9C: /* SHY abx */ /* Undocumented. */ /* STZ abs */
      if (is_65c12) {
        INTERP_MODE_ABS_WRITE(INTERP_INSTR_STZ());
      } else {
        INTERP_MODE_ABr_WRITE(INTERP_INSTR_SHY(), x);
      }
      break;
    case 0x9D: /* STA abx */
      INTERP_MODE_ABr_WRITE(INTERP_INSTR_STA(), x);
      break;
    case 0x9E: /* SHX aby */ /* Undocumented. */ /* STZ abx */
      if (is_65c12) {
        INTERP_MODE_ABr_WRITE(INTERP_INSTR_STZ(), x);
      } else {
        util_bail("SHX aby");
      }
      break;
    case 0x9F: /* AHX aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_WRITE(INTERP_INSTR_AHX(), y);
      }
      break;
    case 0xA0: /* LDY imm */
      y = p_mem_read[pc + 1];
      INTERP_LOAD_NZ_FLAGS(y);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xA1: /* LDA idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_LDA());
      break;
    case 0xA2: /* LDX imm */
      x = p_mem_read[pc + 1];
      INTERP_LOAD_NZ_FLAGS(x);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xA3: /* LAX idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDX_READ(INTERP_INSTR_LAX());
      }
      break;
    case 0xA4: /* LDY zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_LDY());
      break;
    case 0xA5: /* LDA zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_LDA());
      break;
    case 0xA6: /* LDX zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_LDX());
      break;
    case 0xA7: /* LAX zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPG_READ(INTERP_INSTR_LAX());
      }
      break;
    case 0xA8: /* TAY */
      y = a;
      INTERP_LOAD_NZ_FLAGS(y);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xA9: /* LDA imm */
      a = p_mem_read[pc + 1];
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xAA: /* TAX */
      x = a;
      INTERP_LOAD_NZ_FLAGS(x);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xAB: /* LAX imm */ /* Undocumented and unstable. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        /* Dune Rider hit this! */
        v = p_mem_read[pc + 1];
        /* EMU NOTE: Not mixing in the 0xEE magic constant as per jsbeeb and
         * b-em, because the Model B issue 3 I have seems to do a plain AND with
         * no shenanigans or variance.
         */
        INTERP_INSTR_LAX();
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0xAC: /* LDY abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_LDY());
      break;
    case 0xAD: /* LDA abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_LDA());
      break;
    case 0xAE: /* LDX abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_LDX());
      break;
    case 0xAF: /* LAX abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABS_READ(INTERP_INSTR_LAX());
      }
      break;
    case 0xB0: /* BCS */
      INTERP_INSTR_BRANCH(cf);
      break;
    case 0xB1: /* LDA idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_LDA());
      break;
    case 0xB2: /* KIL */ /* Undocumented. */ /* LDA id */
      if (is_65c12) {
        INTERP_MODE_ID_READ(INTERP_INSTR_LDA());
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0xB3: /* LAX idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDY_READ(INTERP_INSTR_LAX());
      }
      break;
    case 0xB4: /* LDY zpx */
      INTERP_MODE_ZPr_READ(INTERP_INSTR_LDY(), x);
      break;
    case 0xB5: /* LDA zpx */
      INTERP_MODE_ZPr_READ(INTERP_INSTR_LDA(), x);
      break;
    case 0xB6: /* LDX zpy */
      INTERP_MODE_ZPr_READ(INTERP_INSTR_LDX(), y);
      break;
    case 0xB7: /* LAX zpy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPr_READ(INTERP_INSTR_LAX(), y);
      }
      break;
    case 0xB8: /* CLV */
      of = 0;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xB9: /* LDA aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_LDA(), y);
      break;
    case 0xBA: /* TSX */
      x = s;
      INTERP_LOAD_NZ_FLAGS(x);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xBB: /* LAS aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("LAS aby");
      }
      break;
    case 0xBC: /* LDY abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_LDY(), x);
      break;
    case 0xBD: /* LDA abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_LDA(), x);
      break;
    case 0xBE: /* LDX aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_LDX(), y);
      break;
    case 0xBF: /* LAX aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_LAX(), y);
      }
      break;
    case 0xC0: /* CPY imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_CMP(y);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xC1: /* CMP idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_CMP(a));
      break;
    case 0xC3: /* DCP idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDX_READ_WRITE(INTERP_INSTR_DCP());
      }
      break;
    case 0xC4: /* CPY zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_CMP(y));
      break;
    case 0xC5: /* CMP zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_CMP(a));
      break;
    case 0xC6: /* DEC zpg */
      INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_DEC());
      break;
    case 0xC7: /* DCP zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_DCP());
      }
      break;
    case 0xC8: /* INY */
      y++;
      INTERP_LOAD_NZ_FLAGS(y);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xC9: /* CMP imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_CMP(a);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xCA: /* DEX */
      x--;
      INTERP_LOAD_NZ_FLAGS(x);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xCB: /* AXS imm */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        v = p_mem_read[pc + 1];
        x = (a & x);
        cf = (x >= v);
        x = (x - v);
        INTERP_LOAD_NZ_FLAGS(x);
        pc += 2;
        cycles_this_instruction = 2;
      }
      break;
    case 0xCC: /* CPY abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_CMP(y));
      break;
    case 0xCD: /* CMP abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_CMP(a));
      break;
    case 0xCE: /* DEC abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_DEC());
      break;
    case 0xCF: /* DCP abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_DCP());
      }
      break;
    case 0xD0: /* BNE */
      INTERP_INSTR_BRANCH(!zf);
      break;
    case 0xD1: /* CMP idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_CMP(a));
      break;
    case 0xD2: /* KIL */ /* Undocumented. */ /* CMP id */
      if (is_65c12) {
        INTERP_MODE_ID_READ(INTERP_INSTR_CMP(a));
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0xD3: /* DCP idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_IDY_READ_WRITE(INTERP_INSTR_DCP());
      }
      break;
    case 0xD5: /* CMP zpx */
      INTERP_MODE_ZPr_READ(INTERP_INSTR_CMP(a), x);
      break;
    case 0xD6: /* DEC zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      v--;
      p_mem_write[addr] = v;
      INTERP_LOAD_NZ_FLAGS(v);
      break;
    case 0xD7: /* DCP zpx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ZPX_READ_WRITE(INTERP_INSTR_DCP());
      }
      break;
    case 0xD8: /* CLD */
      df = 0;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xD9: /* CMP aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_CMP(a), y);
      break;
    case 0xDA: /* NOP */ /* Undocumented. */ /* PHX */
      if (is_65c12) {
        p_stack[s--] = x;
        pc++;
        cycles_this_instruction = 3;
      } else {
        pc++;
        cycles_this_instruction = 2;
      }
      break;
    case 0xDB: /* DCP aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_DCP(), y);
      }
      break;
    case 0xDC: /* NOP abx */ /* NOP abs */ /* Both undocumented. */
    case 0xFC:
      if (is_65c12) {
        INTERP_MODE_ABS_READ(INTERP_INSTR_NOP());
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_NOP(), x);
      }
      break;
    case 0xDD: /* CMP abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_CMP(a), x);
      break;
    case 0xDE: /* DEC abx */
      INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_DEC(), x);
      break;
    case 0xDF: /* DCP abx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_DCP(), x);
      }
      break;
    case 0xE0: /* CPX imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_CMP(x);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xE1: /* SBC idx */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_IDX_READ(INTERP_INSTR_BCD_SBC());
        } else {
          INTERP_MODE_IDX_READ(INTERP_INSTR_BCD_SBC());
        }
      } else {
        INTERP_MODE_IDX_READ(INTERP_INSTR_SBC());
      }
      break;
    case 0xE3: /* ISC idx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ISC idx");
      }
      break;
    case 0xE4: /* CPX zpg */
      INTERP_MODE_ZPG_READ(INTERP_INSTR_CMP(x));
      break;
    case 0xE5: /* SBC zpg */
      if (df) {
        INTERP_MODE_ZPG_READ(INTERP_INSTR_BCD_SBC());
        if (is_65c12) {
          cycles_this_instruction = 4;
        }
      } else {
        INTERP_MODE_ZPG_READ(INTERP_INSTR_SBC());
      }
      break;
    case 0xE6: /* INC zpg */
      INTERP_MODE_ZPG_READ_WRITE(INTERP_INSTR_INC());
      break;
    case 0xE7: /* ISC zpg */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ISC zpg");
      }
      break;
    case 0xE8: /* INX */
      x++;
      INTERP_LOAD_NZ_FLAGS(x);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xE9: /* SBC imm */
      v = p_mem_read[pc + 1];
      pc += 2;
      cycles_this_instruction = 2;
      if (df) {
        if (is_65c12) {
          cycles_this_instruction = 3;
        }
        INTERP_INSTR_BCD_SBC();
      } else {
        INTERP_INSTR_SBC();
      }
      break;
    case 0xEA: /* NOP */
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xEB: /* SBC imm */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        v = p_mem_read[pc + 1];
        pc += 2;
        cycles_this_instruction = 2;
        if (df) {
          INTERP_INSTR_BCD_SBC();
        } else {
          INTERP_INSTR_SBC();
        }
      }
      break;
    case 0xEC: /* CPX abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_CMP(x));
      break;
    case 0xED: /* SBC abs */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_ABS_READ(INTERP_INSTR_BCD_SBC());
        } else {
          INTERP_MODE_ABS_READ(INTERP_INSTR_BCD_SBC());
        }
      } else {
        INTERP_MODE_ABS_READ(INTERP_INSTR_SBC());
      }
      break;
    case 0xEE: /* INC abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_INC());
      break;
    case 0xEF: /* ISC abs */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ISC abs");
      }
      break;
    case 0xF0: /* BEQ */
      INTERP_INSTR_BRANCH(zf);
      break;
    case 0xF1: /* SBC idy */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_IDY_READ(INTERP_INSTR_BCD_SBC());
        } else {
          INTERP_MODE_IDY_READ(INTERP_INSTR_BCD_SBC());
        }
      } else {
        INTERP_MODE_IDY_READ(INTERP_INSTR_SBC());
      }
      break;
    case 0xF2: /* KIL */ /* Undocumented. */ /* SBC id */
      if (is_65c12) {
        if (df) {
          INTERP_MODE_ID_READ(INTERP_INSTR_BCD_SBC());
        } else {
          INTERP_MODE_ID_READ(INTERP_INSTR_SBC());
        }
      } else {
        INTERP_INSTR_KIL();
      }
      break;
    case 0xF3: /* ISC idy */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ISC idy");
      }
      break;
    case 0xF5: /* SBC zpx */
      if (df) {
        INTERP_MODE_ZPr_READ(INTERP_INSTR_BCD_SBC(), x);
        if (is_65c12) {
          cycles_this_instruction = 5;
        }
      } else {
        INTERP_MODE_ZPr_READ(INTERP_INSTR_SBC(), x);
      }
      break;
    case 0xF6: /* INC zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      v++;
      p_mem_write[addr] = v;
      INTERP_LOAD_NZ_FLAGS(v);
      break;
    case 0xF7: /* ISC zpx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ISC zpx");
      }
      break;
    case 0xF8: /* SED */
      df = 1;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xF9: /* SBC aby */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_ABr_READ(INTERP_INSTR_BCD_SBC(), y);
        } else {
          INTERP_MODE_ABr_READ(INTERP_INSTR_BCD_SBC(), y);
        }
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_SBC(), y);
      }
      break;
    case 0xFA: /* NOP */ /* Undocumented. */ /* PLX */
      if (is_65c12) {
        x = p_stack[++s];
        INTERP_LOAD_NZ_FLAGS(x);
        pc++;
        cycles_this_instruction = 4;
      } else {
        pc++;
        cycles_this_instruction = 2;
      }
      break;
    case 0xFB: /* ISC aby */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ISC aby");
      }
      break;
    case 0xFD: /* SBC abx */
      if (df) {
        if (is_65c12) {
          INTERP_MODE_65c12_BCD_ABr_READ(INTERP_INSTR_BCD_SBC(), x);
        } else {
          INTERP_MODE_ABr_READ(INTERP_INSTR_BCD_SBC(), x);
        }
      } else {
        INTERP_MODE_ABr_READ(INTERP_INSTR_SBC(), x);
      }
      break;
    case 0xFE: /* INC abx */
      INTERP_MODE_ABr_READ_WRITE(INTERP_INSTR_INC(), x);
      break;
    case 0xFF: /* ISC abx */ /* Undocumented. */ /* NOP1 */
      if (is_65c12) {
        pc++;
        cycles_this_instruction = 1;
      } else {
        util_bail("ISC abx");
      }
      break;
    default:
      log_do_log(k_log_instruction,
                 k_log_unimplemented,
                 "pc $%.4x opcode $%.2x",
                 pc,
                 opcode);
      __builtin_trap();
      break;
    }

    countdown -= cycles_this_instruction;

do_special_checks:
    /* The invariant here, across all modes, is that countdown expiries fire as
     * soon as they can. If a countdown fires at the end of an instuction, it
     * fires before the next instruction executes.
     */
    special_checks |= ((countdown <= 0) * k_interp_special_countdown);

    if (!special_checks) {
      /* No countdown expired or other special situation, just fetch the next
       * opcode without drama.
       */
      opcode = p_mem_read[pc];
      continue;
    }

    poll_irq = (special_checks & k_interp_special_poll_irq);
    if (countdown <= 0) {
      special_checks &= ~k_interp_special_countdown;
      if (countdown < 0) {
        /* Expiry within the instruction that just finished. Need to poll IRQ
         * point of this instruction.
         */
        poll_irq = 1;
      }
    }

    /* Instructions requiring full tick-by-tick execution -- notably,
     * hardware register accesses -- are handled separately.
     * For the remaining instructions, the only sub-instruction aspect which
     * makes a difference is when the interrupt decision is made, which
     * usually (but not always) occurs just before the last instuction cycle.
     * "Just before" means that we effectively need to check interrupts
     * before the penultimate cycle because an interrupt that is asserted
     * at the start of the last cycle is not soon enough to be detected.
     */
    if (poll_irq) {
      special_checks &= ~k_interp_special_poll_irq;

      countdown += cycles_this_instruction;
      assert(cycles_this_instruction);
      assert(countdown >= 0);

      if (cycles_this_instruction == 2) {
        INTERP_TIMING_ADVANCE(0);
        interp_poll_irq_now(&do_irq, p_state_6502, intf);
        INTERP_TIMING_ADVANCE(cycles_this_instruction);
      } else if (interp_is_branch_opcode(opcode) && !is_65c12) {
        /* Quirky IRQ poll point handling only applies to NMOS 6502.
         * See: https://stardot.org.uk/forums/viewtopic.php?f=3&t=15631
         */
        /* NOTE: branch & not taken case handled above for cycles == 2. */
        INTERP_TIMING_ADVANCE(0);
        /* EMU NOTE: Taken branches have a different interrupt poll location. */
        if (cycles_this_instruction == 3) {
          /* Branch taken, no page crossing, 3 cycles. Interrupt polling done
           * after first cycle, not second cycle. Given that the interrupt
           * needs to be already asserted prior to polling, we poll interrupts
           * at the start of the 3 cycle sequence.
           */
          interp_poll_irq_now(&do_irq, p_state_6502, intf);
          INTERP_TIMING_ADVANCE(3);
        } else {
          /* Branch taken page crossing, 4 cycles. Interrupt polling after
           * first cycle _and_ after third cycle.
           * Reference: https://wiki.nesdev.com/w/index.php/CPU_interrupts
           */
          interp_poll_irq_now(&do_irq, p_state_6502, intf);
          INTERP_TIMING_ADVANCE(2);
          interp_poll_irq_now(&do_irq, p_state_6502, intf);
          INTERP_TIMING_ADVANCE(2);
        }
      } else if (cycles_this_instruction > 2) {
        INTERP_TIMING_ADVANCE(cycles_this_instruction - 2);
        interp_poll_irq_now(&do_irq, p_state_6502, intf);
        INTERP_TIMING_ADVANCE(2);
      } else {
        /* 1-cycle instructions, notably the 65c12 1 byte NOP, don't poll
         * IRQs.
         * See https://stardot.org.uk/forums/viewtopic.php?f=54&t=20411&p=286803
         */
        INTERP_TIMING_ADVANCE(cycles_this_instruction);
      }
    } else if (countdown == 0) {
      /* Make sure to always run timer callbacks at the instruction boundary. */
      INTERP_TIMING_ADVANCE(0);
    }

check_irq:
    if (!do_irq) {
      /* An IRQ may have been raised or unblocked after the poll point
       * (including at the instruction boundary). If an IRQ is asserted,
       * make sure to check the next poll point to see if it needs to fire.
       */
      if (p_state_6502->irq_fire &&
          (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi) ||
           !intf)) {
        special_checks |= k_interp_special_poll_irq;
      }
    }

    /* Advancing the timing or hardware register access may have triggered
     * exit or reset.
     */
    cpu_driver_flags = p_interp->driver.flags;
    if (cpu_driver_flags != 0) {
      if (cpu_driver_flags & k_cpu_flag_exited) {
        break;
      }
      if (cpu_driver_flags & (k_cpu_flag_soft_reset | k_cpu_flag_hard_reset)) {
        void (*do_reset_callback)(void* p, uint32_t flags) =
            p_interp->driver.do_reset_callback;
        if (do_reset_callback != NULL) {
          do_reset_callback(p_interp->driver.p_do_reset_callback_object,
                            cpu_driver_flags);
          state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
          interp_set_flags(flags, &zf, &nf, &cf, &of, &df, &intf);
          do_irq = 0;

          countdown = timing_get_countdown(p_timing);
        }
      }
    }

    /* The instruction callback fires after an instruction executes. */
    if (instruction_callback && (!(special_checks & k_interp_special_entry))) {
      int irq_pending = !!(special_checks & k_interp_special_poll_irq);
      /* This passes the just executed opcode and addr, but the next pc. */
      if (instruction_callback(p_callback_context,
                               pc,
                               opcode,
                               addr,
                               do_irq,
                               irq_pending)) {
        /* The instruction callback can elect to exit the interpreter. */
        break;
      }
    }

    special_checks &= ~k_interp_special_entry;

    if (do_irq) {
      opcode = 0x00;
    } else {
      /* TODO: opcode fetch doesn't consider hardware register access,
       * i.e. JMP $FE6A will have incorrect timings.
       */
      opcode = p_mem_read[pc];
    }

    /* The debug callout fires before the next instruction executes. */
    if (p_interp->debug_subsystem_active || *p_debug_interrupt) {
      INTERP_TIMING_ADVANCE(0);
      interp_call_debugger(p_interp,
                           &a,
                           &x,
                           &y,
                           &s,
                           &pc,
                           &zf,
                           &nf,
                           &cf,
                           &of,
                           &df,
                           &intf,
                           do_irq);
    }
  }

  flags = interp_get_flags(zf, nf, cf, of, df, intf);
  state_6502_set_registers(p_state_6502, a, x, y, s, flags, pc);

  return countdown;
}

void
interp_testing_unexit(struct interp_struct* p_interp) {
  p_interp->driver.flags &= ~k_cpu_flag_exited;
}
