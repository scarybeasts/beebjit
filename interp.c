#include "interp.h"

#include "bbc_options.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "memory_access.h"
#include "state_6502.h"
#include "timing.h"

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_interp_special_debug = 1,
  k_interp_special_callback = 2,
  k_interp_special_countdown = 4,
  k_interp_special_poll_irq = 8,
};

struct interp_struct {
  struct cpu_driver driver;
  int exited;
  uint32_t exit_value;

  uint8_t* p_mem_read;
  uint8_t* p_mem_write;
  uint16_t read_callback_above;
  uint16_t write_callback_above;
  uint16_t callback_above;
  int debug_subsystem_active;
};

static void
interp_destroy(struct cpu_driver* p_cpu_driver) {
  free(p_cpu_driver);
}

static int
interp_enter(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp = (struct interp_struct*) p_cpu_driver;
  int64_t countdown = timing_get_countdown(p_interp->driver.p_timing);

  countdown = interp_enter_with_details(p_interp, countdown, NULL, NULL);
  (void) countdown;

  return p_interp->exited;
}

static int
interp_has_exited(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp = (struct interp_struct*) p_cpu_driver;
  return p_interp->exited;
}

static uint32_t
interp_get_exit_value(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp = (struct interp_struct*) p_cpu_driver;
  assert(p_interp->exited);
  return p_interp->exit_value;
}

static char*
interp_get_address_info(struct cpu_driver* p_cpu_driver, uint16_t addr) {
  (void) p_cpu_driver;
  (void) addr;

  return "ITRP";
}

static void
interp_init(struct cpu_driver* p_cpu_driver) {
  struct interp_struct* p_interp = (struct interp_struct*) p_cpu_driver;
  struct memory_access* p_memory_access = p_cpu_driver->p_memory_access;
  struct bbc_options* p_options = p_cpu_driver->p_options;
  struct cpu_driver_funcs* p_funcs = p_cpu_driver->p_funcs;

  p_funcs->destroy = interp_destroy;
  p_funcs->enter = interp_enter;
  p_funcs->has_exited = interp_has_exited;
  p_funcs->get_exit_value = interp_get_exit_value;
  p_funcs->get_address_info = interp_get_address_info;

  p_interp->p_mem_read = p_memory_access->p_mem_read;
  p_interp->p_mem_write = p_memory_access->p_mem_write;
  p_interp->read_callback_above =
      p_memory_access->memory_read_needs_callback_above(
          p_memory_access->p_callback_obj);
  p_interp->write_callback_above =
      p_memory_access->memory_write_needs_callback_above(
          p_memory_access->p_callback_obj);
  p_interp->callback_above = p_interp->read_callback_above;
  if (p_interp->write_callback_above < p_interp->read_callback_above) {
    p_interp->callback_above = p_interp->write_callback_above;
  }
  /* The code assumes that zero page and stack accesses don't incur special
   * handling.
   */
  assert(p_interp->callback_above >= 0x200);

  p_interp->debug_subsystem_active = p_options->debug_subsystem_active(
      p_options->p_debug_object);

  p_interp->exited = 0;
  p_interp->exit_value = 0;
}

struct cpu_driver*
interp_create(struct cpu_driver_funcs* p_funcs) {
  struct interp_struct* p_interp = malloc(sizeof(struct interp_struct));
  if (p_interp == NULL) {
    errx(1, "couldn't allocate interp_struct");
  }
  (void) memset(p_interp, '\0', sizeof(struct interp_struct));

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

  if (debug_active_at_addr(p_debug_object, *p_pc)) {
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

#define INTERP_TIMING_ADVANCE(num_cycles)                                     \
  countdown -= num_cycles;                                                    \
  countdown = timing_advance_time(p_timing, countdown);                       \

#define INTERP_MEMORY_READ(addr_read)                                         \
  v = memory_read_callback(p_memory_obj, addr_read);                          \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MEMORY_WRITE(addr_write)                                       \
  memory_write_callback(p_memory_obj, addr_write, v);                         \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MODE_ABS_READ(INSTR)                                           \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  pc += 3;                                                                    \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 4;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(2);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABS_WRITE(INSTR)                                          \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  pc += 3;                                                                    \
  if (addr < callback_above) {                                                \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 4;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(2);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABS_READ_WRITE(INSTR)                                     \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  pc += 3;                                                                    \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(3);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_MEMORY_WRITE(addr);                                                \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABr_READ(INSTR, reg_name)                                 \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  pc += 3;                                                                    \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 4;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      INTERP_TIMING_ADVANCE(3);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_MEMORY_READ(addr - 0x100);                                       \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(2);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    }                                                                         \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABr_WRITE(INSTR, reg_name)                                \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  pc += 3;                                                                    \
  if (addr < callback_above) {                                                \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 5;                                              \
  } else {                                                                    \
    addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                       \
    INTERP_TIMING_ADVANCE(3);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_MEMORY_READ(addr_temp);                                            \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ABX_READ_WRITE(INSTR)                                     \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + x);                                                     \
  pc += 3;                                                                    \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 7;                                              \
  } else {                                                                    \
    addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                       \
    INTERP_TIMING_ADVANCE(3);                                                 \
    INTERP_MEMORY_READ(addr_temp);                                            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_MEMORY_WRITE(addr);                                                \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDX_READ(INSTR)                                           \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  pc += 2;                                                                    \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
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
  if (addr < callback_above) {                                                \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDY_READ(INSTR)                                           \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  pc += 2;                                                                    \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    INSTR;                                                                    \
    cycles_this_instruction = 5;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      INTERP_TIMING_ADVANCE(4);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_MEMORY_READ(addr - 0x100);                                       \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(3);                                               \
      interp_poll_irq_now(&do_irq, p_state_6502, intf);                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    }                                                                         \
    INTERP_MEMORY_READ(addr);                                                 \
    INSTR;                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_IDY_WRITE(INSTR)                                          \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  pc += 2;                                                                    \
  if (addr < callback_above) {                                                \
    INSTR;                                                                    \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                       \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_MEMORY_READ(addr_temp);                                            \
    interp_poll_irq_now(&do_irq, p_state_6502, intf);                         \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INSTR;                                                                    \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    goto check_irq;                                                           \
  }

#define INTERP_MODE_ZPr_READ(reg_name)                                        \
  addr = p_mem_read[pc + 1];                                                  \
  addr += reg_name;                                                           \
  addr &= 0xFF;                                                               \
  v = p_mem_read[addr];                                                       \
  cycles_this_instruction = 4;                                                \
  pc += 2;

#define INTERP_MODE_ZPr_WRITE(reg_name)                                       \
  addr = p_mem_read[pc + 1];                                                  \
  addr += reg_name;                                                           \
  addr &= 0xFF;                                                               \
  p_mem_write[addr] = v;                                                      \
  cycles_this_instruction = 4;                                                \
  pc += 2;

#define INTERP_MODE_ZPX_READ_WRITE()                                          \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  v = p_mem_read[addr];                                                       \
  cycles_this_instruction = 6;                                                \
  pc += 2;

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
  if (df) {                                                                   \
    /* Fix up decimal carry on first nibble. */                               \
    /* TODO: incorrect for invalid large BCD numbers, double carries? */      \
    int decimal_carry = ((a & 0x0F) + (v & 0x0F) + cf);                       \
    if (decimal_carry >= 0x0A) {                                              \
      temp_int += 0x06;                                                       \
    }                                                                         \
  }                                                                           \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  of = !!((a ^ temp_int) & (v ^ temp_int) & 0x80);                            \
  /* In decimal mode, NZ flags are based on this interim value. */            \
  INTERP_LOAD_NZ_FLAGS((temp_int & 0xFF));                                    \
  if (df) {                                                                   \
    if (temp_int >= 0xA0) {                                                   \
      temp_int += 0x60;                                                       \
    }                                                                         \
  }                                                                           \
  cf = !!(temp_int & 0x100);                                                  \
  a = temp_int;

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

#define INTERP_INSTR_DEC()                                                    \
  v--;                                                                        \
  INTERP_LOAD_NZ_FLAGS(v);

#define INTERP_INSTR_EOR()                                                    \
  a ^= v;                                                                     \
  INTERP_LOAD_NZ_FLAGS(a);

#define INTERP_INSTR_INC()                                                    \
  v++;                                                                        \
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

#define INTERP_INSTR_SBC()                                                    \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  /* "SBC simply takes the ones complement of the second value and then       \
   * performs an ADC"                                                         \
   */                                                                         \
  temp_int = (a + (uint8_t) ~v + cf);                                         \
  if (df) {                                                                   \
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

#define INTERP_INSTR_STA()                                                    \
  v = a;

#define INTERP_INSTR_STX()                                                    \
  v = x;

#define INTERP_INSTR_STY()                                                    \
  v = y;

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

  struct state_6502* p_state_6502 = p_interp->driver.abi.p_state_6502;
  struct timing_struct* p_timing = p_interp->driver.p_timing;
  struct memory_access* p_memory_access = p_interp->driver.p_memory_access;
  uint8_t (*memory_read_callback)(void*, uint16_t) =
      p_memory_access->memory_read_callback;
  void (*memory_write_callback)(void*, uint16_t, uint8_t) =
      p_memory_access->memory_write_callback;
  void* p_memory_obj = p_memory_access->p_callback_obj;
  uint8_t* p_mem_read = p_interp->p_mem_read;
  uint8_t* p_mem_write = p_interp->p_mem_write;
  uint8_t* p_stack = (p_mem_write + k_6502_stack_addr);
  uint16_t callback_above = p_interp->callback_above;
  int64_t cycles_this_instruction = 0;
  uint8_t opcode = 0;
  int special_checks = 0;
  uint16_t addr = 0;
  int do_irq = 0;

  assert(countdown >= 0);
  assert(!p_interp->exited);

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
      /* EMU NOTE: if an NMI hits early enough in the 7-cycle interrupt / BRK
       * sequence for a non-NMI interrupt, the NMI overrides and in the case of
       * BRK the BRK can go missing!
       */
      INTERP_TIMING_ADVANCE(3);
      if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi)) {
        state_6502_clear_edge_triggered_irq(p_state_6502, k_state_6502_irq_nmi);
        addr = k_6502_vector_nmi;
      }
      p_stack[s--] = (pc >> 8);
      p_stack[s--] = (pc & 0xFF);
      v = interp_get_flags(zf, nf, cf, of, df, intf);
      v |= (temp_u8 | (1 << k_flag_always_set));
      p_stack[s--] = v;
      pc = (p_mem_read[addr] | (p_mem_read[(uint16_t) (addr + 1)] << 8));
      intf = 1;
      do_irq = 0;
      cycles_this_instruction = 4;
      break;
    case 0x01: /* ORA idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_ORA());
      break;
    case 0x02: /* Extension: EXIT */
      p_interp->exited = 1;
      p_interp->exit_value = ((y << 16) | (x << 8) | a);
      return countdown;
    case 0x04: /* NOP zp */ /* Undocumented. */
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x05: /* ORA zp */
      addr = p_mem_read[pc + 1];
      a |= p_mem_read[addr];
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x06: /* ASL zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_ASL();
      p_mem_write[addr] = v;
      pc += 2;
      cycles_this_instruction = 5;
      break;
    case 0x07: /* SLO zp */ /* Undocumented. */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      cf = !!(v & 0x80);
      v <<= 1;
      p_mem_write[addr] = v;
      a |= v;
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 5;
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
    case 0x0B: /* ANC imm */ /* Undocumented. */
      v = p_mem_read[pc + 1];
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
      cf = nf;
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0x0D: /* ORA abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_ORA());
      break;
    case 0x0E: /* ASL abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_ASL());
      break;
    case 0x10: /* BPL */
      INTERP_INSTR_BRANCH(!nf);
      break;
    case 0x11: /* ORA idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_ORA());
      break;
    case 0x15: /* ORA zpx */
      INTERP_MODE_ZPr_READ(x);
      a |= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x16: /* ASL zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_ASL();
      p_mem_write[addr] = v;
      break;
    case 0x18: /* CLC */
      cf = 0;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x19: /* ORA aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_ORA(), y);
      break;
    case 0x1D: /* ORA abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_ORA(), x);
      break;
    case 0x1E: /* ASL abx */
      INTERP_MODE_ABX_READ_WRITE(INTERP_INSTR_ASL());
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
    case 0x24: /* BIT zpg */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_BIT();
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x25: /* AND zp */
      addr = p_mem_read[pc + 1];
      a &= p_mem_read[addr];
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x26: /* ROL zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_ROL();
      p_mem_write[addr] = v;
      pc += 2;
      cycles_this_instruction = 5;
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
    case 0x2C: /* BIT abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_BIT());
      break;
    case 0x2D: /* AND abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_AND());
      break;
    case 0x2E: /* ROL abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_ROL());
      break;
    case 0x30: /* BMI */
      INTERP_INSTR_BRANCH(nf);
      break;
    case 0x31: /* AND idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_AND());
      break;
    case 0x35: /* AND zpx */
      INTERP_MODE_ZPr_READ(x);
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x36: /* ROL zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_ROL();
      p_mem_write[addr] = v;
      break;
    case 0x38: /* SEC */
      cf = 1;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x39: /* AND aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_AND(), y);
      break;
    case 0x3D: /* AND abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_AND(), x);
      break;
    case 0x3E: /* ROL abx */
      INTERP_MODE_ABX_READ_WRITE(INTERP_INSTR_ROL());
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
    case 0x45: /* EOR zp */
      addr = p_mem_read[pc + 1];
      a ^= p_mem_read[addr];
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x46: /* LSR zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_LSR();
      p_mem_write[addr] = v;
      pc += 2;
      cycles_this_instruction = 5;
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
    case 0x4B: /* ALR imm */ /* Undocumented. */
      v = p_mem_read[pc + 1];
      a &= v;
      cf = (a & 0x01);
      a >>= 1;
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 2;
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
    case 0x50: /* BVC */
      INTERP_INSTR_BRANCH(!of);
      break;
    case 0x51: /* EOR idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_EOR());
      break;
    case 0x55: /* EOR zpx */
      INTERP_MODE_ZPr_READ(x);
      a ^= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x56: /* LSR zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_LSR();
      p_mem_write[addr] = v;
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
    case 0x5D: /* EOR abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_EOR(), x);
      break;
    case 0x5E: /* LSR abx */
      INTERP_MODE_ABX_READ_WRITE(INTERP_INSTR_LSR());
      break;
    case 0x60: /* RTS */
      pc = p_stack[++s];
      pc |= (p_stack[++s] << 8);
      pc++;
      cycles_this_instruction = 6;
      break;
    case 0x61: /* ADC idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_ADC());
      break;
    case 0x65: /* ADC zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_ADC();
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x66: /* ROR zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_ROR();
      p_mem_write[addr] = v;
      pc += 2;
      cycles_this_instruction = 5;
      break;
    case 0x68: /* PLA */
      a = p_stack[++s];
      INTERP_LOAD_NZ_FLAGS(a);
      pc++;
      cycles_this_instruction = 4;
      break;
    case 0x69: /* ADC imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_ADC();
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0x6A: /* ROR A */
      v = a;
      INTERP_INSTR_ROR();
      a = v;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x6C: /* JMP ind */
      addr = *(uint16_t*) &p_mem_read[pc + 1];
      addr_temp = ((addr + 1) & 0xFF);
      addr_temp |= (addr & 0xFF00);
      pc = p_mem_read[addr];
      pc |= (p_mem_read[addr_temp] << 8);
      cycles_this_instruction = 5;
      break;
    case 0x6D: /* ADC abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_ADC());
      break;
    case 0x6E: /* ROR abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_ROR());
      break;
    case 0x70: /* BVS */
      INTERP_INSTR_BRANCH(of);
      break;
    case 0x71: /* ADC idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_ADC());
      break;
    case 0x75: /* ADC zpx */
      INTERP_MODE_ZPr_READ(x);
      INTERP_INSTR_ADC();
      break;
    case 0x76: /* ROR zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      INTERP_INSTR_ROR();
      p_mem_write[addr] = v;
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
      INTERP_MODE_ABr_READ(INTERP_INSTR_ADC(), y);
      break;
    case 0x7D: /* ADC abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_ADC(), x);
      break;
    case 0x7E: /* ROR abx */
      INTERP_MODE_ABX_READ_WRITE(INTERP_INSTR_ROR());
      break;
    case 0x81: /* STA idx */
      INTERP_MODE_IDX_WRITE(INTERP_INSTR_STA());
      break;
    case 0x84: /* STY zp */
      addr = p_mem_read[pc + 1];
      p_mem_write[addr] = y;
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x85: /* STA zp */
      addr = p_mem_read[pc + 1];
      p_mem_write[addr] = a;
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x86: /* STX zp */
      addr = p_mem_read[pc + 1];
      p_mem_write[addr] = x;
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x87: /* SAX zp */ /* Undocumented. */
      addr = p_mem_read[pc + 1];
      p_mem_write[addr] = (a & x);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0x88: /* DEY */
      y--;
      INTERP_LOAD_NZ_FLAGS(y);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x8A: /* TXA */
      a = x;
      INTERP_LOAD_NZ_FLAGS(a);
      pc++;
      cycles_this_instruction = 2;
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
    case 0x90: /* BCC */
      INTERP_INSTR_BRANCH(!cf);
      break;
    case 0x91: /* STA idy */
      INTERP_MODE_IDY_WRITE(INTERP_INSTR_STA());
      break;
    case 0x94: /* STY zpx */
      v = y;
      INTERP_MODE_ZPr_WRITE(x);
      break;
    case 0x95: /* STA zpx */
      v = a;
      INTERP_MODE_ZPr_WRITE(x);
      break;
    case 0x96: /* STX zpy */
      v = x;
      INTERP_MODE_ZPr_WRITE(y);
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
    case 0x9C: /* SHY abx */ /* Undocumented. */
      INTERP_MODE_ABr_WRITE(INTERP_INSTR_SHY(), x);
      break;
    case 0x9D: /* STA abx */
      INTERP_MODE_ABr_WRITE(INTERP_INSTR_STA(), x);
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
    case 0xA4: /* LDY zp */
      addr = p_mem_read[pc + 1];
      y = p_mem_read[addr];
      INTERP_LOAD_NZ_FLAGS(y);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0xA5: /* LDA zp */
      addr = p_mem_read[pc + 1];
      a = p_mem_read[addr];
      INTERP_LOAD_NZ_FLAGS(a);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0xA6: /* LDX zp */
      addr = p_mem_read[pc + 1];
      x = p_mem_read[addr];
      INTERP_LOAD_NZ_FLAGS(x);
      pc += 2;
      cycles_this_instruction = 3;
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
    case 0xAC: /* LDY abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_LDY());
      break;
    case 0xAD: /* LDA abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_LDA());
      break;
    case 0xAE: /* LDX abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_LDX());
      break;
    case 0xB0: /* BCS */
      INTERP_INSTR_BRANCH(cf);
      break;
    case 0xB1: /* LDA idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_LDA());
      break;
    case 0xB4: /* LDY zpx */
      INTERP_MODE_ZPr_READ(x);
      y = v;
      INTERP_LOAD_NZ_FLAGS(y);
      break;
    case 0xB5: /* LDA zpx */
      INTERP_MODE_ZPr_READ(x);
      a = v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0xB6: /* LDX zpy */
      INTERP_MODE_ZPr_READ(y);
      x = v;
      INTERP_LOAD_NZ_FLAGS(x);
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
    case 0xBC: /* LDY abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_LDY(), x);
      break;
    case 0xBD: /* LDA abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_LDA(), x);
      break;
    case 0xBE: /* LDX aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_LDX(), y);
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
    case 0xC4: /* CPY zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_CMP(y);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0xC5: /* CMP zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_CMP(a);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0xC6: /* DEC zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      v--;
      p_mem_write[addr] = v;
      INTERP_LOAD_NZ_FLAGS(v);
      pc += 2;
      cycles_this_instruction = 5;
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
    case 0xCC: /* CPY abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_CMP(y));
      break;
    case 0xCD: /* CMP abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_CMP(a));
      break;
    case 0xCE: /* DEC abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_DEC());
      break;
    case 0xD0: /* BNE */
      INTERP_INSTR_BRANCH(!zf);
      break;
    case 0xD1: /* CMP idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_CMP(a));
      break;
    case 0xD5: /* CMP zpx */
      INTERP_MODE_ZPr_READ(x);
      INTERP_INSTR_CMP(a);
      break;
    case 0xD6: /* DEC zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      v--;
      p_mem_write[addr] = v;
      INTERP_LOAD_NZ_FLAGS(v);
      break;
    case 0xD8: /* CLD */
      df = 0;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xD9: /* CMP aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_CMP(a), y);
      break;
    case 0xDC: /* NOP abx */ /* Undocumented. */
      INTERP_MODE_ABr_READ(INTERP_INSTR_NOP(), x);
      break;
    case 0xDD: /* CMP abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_CMP(a), x);
      break;
    case 0xDE: /* DEC abx */
      INTERP_MODE_ABX_READ_WRITE(INTERP_INSTR_DEC());
      break;
    case 0xE0: /* CPX imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_CMP(x);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xE1: /* SBC idx */
      INTERP_MODE_IDX_READ(INTERP_INSTR_SBC());
      break;
    case 0xE4: /* CPX zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_CMP(x);
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0xE5: /* SBC zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      INTERP_INSTR_SBC();
      pc += 2;
      cycles_this_instruction = 3;
      break;
    case 0xE6: /* INC zp */
      addr = p_mem_read[pc + 1];
      v = p_mem_read[addr];
      v++;
      p_mem_write[addr] = v;
      INTERP_LOAD_NZ_FLAGS(v);
      pc += 2;
      cycles_this_instruction = 5;
      break;
    case 0xE8: /* INX */
      x++;
      INTERP_LOAD_NZ_FLAGS(x);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xE9: /* SBC imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_SBC();
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xEA: /* NOP */
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xEC: /* CPX abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_CMP(x));
      break;
    case 0xED: /* SBC abs */
      INTERP_MODE_ABS_READ(INTERP_INSTR_SBC());
      break;
    case 0xEE: /* INC abs */
      INTERP_MODE_ABS_READ_WRITE(INTERP_INSTR_INC());
      break;
    case 0xF0: /* BEQ */
      INTERP_INSTR_BRANCH(zf);
      break;
    case 0xF1: /* SBC idy */
      INTERP_MODE_IDY_READ(INTERP_INSTR_SBC());
      break;
    case 0xF2: /* Extension: CRASH */
      *((volatile uint8_t*) 0xdead) = '\x41';
      break;
    case 0xF4: /* NOP zpx */ /* Undocumented. */
      pc += 2;
      cycles_this_instruction = 4;
      break;
    case 0xF5: /* SBC zpx */
      INTERP_MODE_ZPr_READ(x);
      INTERP_INSTR_SBC();
      break;
    case 0xF6: /* INC zpx */
      INTERP_MODE_ZPX_READ_WRITE();
      v++;
      p_mem_write[addr] = v;
      INTERP_LOAD_NZ_FLAGS(v);
      break;
    case 0xF8: /* SED */
      df = 1;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xF9: /* SBC aby */
      INTERP_MODE_ABr_READ(INTERP_INSTR_SBC(), y);
      break;
    case 0xFD: /* SBC abx */
      INTERP_MODE_ABr_READ(INTERP_INSTR_SBC(), x);
      break;
    case 0xFE: /* INC abx */
      INTERP_MODE_ABX_READ_WRITE(INTERP_INSTR_INC());
      break;
    default:
      __builtin_trap();
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

      if (cycles_this_instruction <= 2) {
        INTERP_TIMING_ADVANCE(0);
        interp_poll_irq_now(&do_irq, p_state_6502, intf);
        /* TODO: remove? */
        INTERP_TIMING_ADVANCE(cycles_this_instruction);
      } else if (interp_is_branch_opcode(opcode)) {
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
      } else {
        INTERP_TIMING_ADVANCE(cycles_this_instruction - 2);
        interp_poll_irq_now(&do_irq, p_state_6502, intf);
        INTERP_TIMING_ADVANCE(2);
      }
    } else if (!countdown) {
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
    if (state_6502_check_and_do_reset(p_state_6502)) {
      state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
      interp_set_flags(flags, &zf, &nf, &cf, &of, &df, &intf);
    }

    /* The instruction callback fires after an instruction executes. */
    if (instruction_callback && cycles_this_instruction) {
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

    if (do_irq) {
      opcode = 0x00;
    } else {
      /* TODO: opcode fetch doesn't consider hardware register access,
       * i.e. JMP $FE6A will have incorrect timings.
       */
      opcode = p_mem_read[pc];
    }

    /* The debug callout fires before the next instruction executes. */
    if (p_interp->debug_subsystem_active) {
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
