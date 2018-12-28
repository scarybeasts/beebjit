#include "interp.h"

#include "bbc_options.h"
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
  k_v = 4,
};

struct interp_struct {
  struct state_6502* p_state_6502;
  struct memory_access* p_memory_access;
  struct timing_struct* p_timing;
  struct bbc_options* p_options;

  uint8_t* p_mem_read;
  uint8_t* p_mem_write;
  uint16_t read_callback_above;
  uint16_t write_callback_above;
  uint16_t callback_above;
  int debug_subsystem_active;

  size_t short_instruction_run_timer_id;
  int return_from_loop;
};

static void
interp_instruction_run_timer_callback(void* p) {
  struct interp_struct* p_interp = (struct interp_struct*) p;

  (void) timing_stop_timer(p_interp->p_timing,
                           p_interp->short_instruction_run_timer_id);
  p_interp->return_from_loop = 1;
}

struct interp_struct*
interp_create(struct state_6502* p_state_6502,
              struct memory_access* p_memory_access,
              struct timing_struct* p_timing,
              struct bbc_options* p_options) {
  struct interp_struct* p_interp = malloc(sizeof(struct interp_struct));
  if (p_interp == NULL) {
    errx(1, "couldn't allocate interp_struct");
  }
  (void) memset(p_interp, '\0', sizeof(struct interp_struct));

  p_interp->p_state_6502 = p_state_6502;
  p_interp->p_memory_access = p_memory_access;
  p_interp->p_timing = p_timing;
  p_interp->p_options = p_options;

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
      p_options->p_debug_callback_object);

  p_interp->short_instruction_run_timer_id =
      timing_register_timer(p_timing,
                            interp_instruction_run_timer_callback,
                            p_interp);

  return p_interp;
}

void
interp_destroy(struct interp_struct* p_interp) {
  free(p_interp);
}

static inline void
interp_set_flags(unsigned char flags,
                 unsigned char* zf,
                 unsigned char* nf,
                 unsigned char* cf,
                 unsigned char* of,
                 unsigned char* df,
                 unsigned char* intf) {
  *zf = ((flags & (1 << k_flag_zero)) != 0);
  *nf = ((flags & (1 << k_flag_negative)) != 0);
  *cf = ((flags & (1 << k_flag_carry)) != 0);
  *of = ((flags & (1 << k_flag_overflow)) != 0);
  *df = ((flags & (1 << k_flag_decimal)) != 0);
  *intf = ((flags & (1 << k_flag_interrupt)) != 0);
}

static inline unsigned char
interp_get_flags(unsigned char zf,
                 unsigned char nf,
                 unsigned char cf,
                 unsigned char of,
                 unsigned char df,
                 unsigned char intf) {
  unsigned char flags = 0;
  flags |= (cf << k_flag_carry);
  flags |= (zf << k_flag_zero);
  flags |= (intf << k_flag_interrupt);
  flags |= (df << k_flag_decimal);
  flags |= (of << k_flag_overflow);
  flags |= (nf << k_flag_negative);
  return flags;
}

static void
interp_check_irq(uint8_t* opcode,
                 uint16_t* p_do_irq_vector,
                 struct state_6502* p_state_6502,
                 uint8_t intf) {
  if (!p_state_6502->irq_fire) {
    return;
  }

  /* EMU: if both an NMI and normal IRQ are asserted at the same time, only
   * the NMI should fire. This is confirmed via visual 6502; see:
   * http://forum.6502.org/viewtopic.php?t=1797
   * Note that jsbeeb, b-em and beebem all appear to get this wrong, they
   * will run the 7 cycle interrupt sequence twice in a row, which would
   * be visible as stack and timing artifacts. b2 looks likely to be
   * correct as it is a much more low level 6502 emulation.
   */
  if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi)) {
    state_6502_clear_edge_triggered_irq(p_state_6502, k_state_6502_irq_nmi);
    *p_do_irq_vector = k_6502_vector_nmi;
  } else if (!intf) {
    *p_do_irq_vector = k_6502_vector_irq;
  }
  /* If an IRQ is firing, pull the next opcode to 0 (BRK). This is how the
   * actual 6502 processor works, see: https://www.pagetable.com/?p=410.
   * That decision was made for silicon simplicity; we do the same here for
   * code simplicity.
   */
  if (*p_do_irq_vector) {
    *opcode = 0;
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

  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  struct bbc_options* p_options = p_interp->p_options;
  int (*debug_active_at_addr)(void*, uint16_t) =
      p_options->debug_active_at_addr;
  void* p_debug_callback_object = p_options->p_debug_callback_object;

  if (debug_active_at_addr(p_debug_callback_object, *p_pc)) {
    void* (*debug_callback)(void*, uint16_t) = p_options->debug_callback;

    flags = interp_get_flags(*p_zf, *p_nf, *p_cf, *p_of, *p_df, *p_intf);
    state_6502_set_registers(p_state_6502,
                             *p_a,
                             *p_x,
                             *p_y,
                             *p_s,
                             flags,
                             *p_pc);
    /* TODO: set cycles. */

    debug_callback(p_options->p_debug_callback_object, irq_vector);

    state_6502_get_registers(p_state_6502, p_a, p_x, p_y, p_s, &flags, p_pc);
    interp_set_flags(flags, p_zf, p_nf, p_cf, p_of, p_df, p_intf);
  }
}

#define INTERP_TIMING_ADVANCE(num_cycles)                                     \
  countdown -= num_cycles;                                                    \
  countdown = timing_advance_time(p_timing, &delta, countdown);               \
  state_6502_add_cycles(p_state_6502, delta);

#define INTERP_MEMORY_READ(addr_read)                                         \
  v = memory_read_callback(p_memory_obj, addr_read);                          \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MEMORY_WRITE(addr_write)                                       \
  memory_write_callback(p_memory_obj, addr_write, v);                         \
  countdown = timing_get_countdown(p_timing);

#define INTERP_MODE_ABS_READ()                                                \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    cycles_this_instruction = 4;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(2);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 3;

#define INTERP_MODE_ABS_WRITE()                                               \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  if (addr < callback_above) {                                                \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 4;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(2);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 3;

#define INTERP_MODE_ABS_READ_WRITE_PRE()                                      \
  addr = *(uint16_t*) &p_mem_read[pc + 1];                                    \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(3);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_MEMORY_WRITE(addr);                                                \
  }

#define INTERP_MODE_ABS_READ_WRITE_POST()                                     \
  INTERP_LOAD_NZ_FLAGS(v);                                                    \
  if (addr < callback_above) {                                                \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 3;

#define INTERP_MODE_ABr_READ(reg_name)                                        \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    cycles_this_instruction = 4;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      INTERP_TIMING_ADVANCE(3);                                               \
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);          \
      INTERP_MEMORY_READ(addr - 0x100);                                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(2);                                               \
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);          \
      INTERP_TIMING_ADVANCE(1);                                               \
    }                                                                         \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 3;

#define INTERP_MODE_ABr_WRITE(reg_name)                                       \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + reg_name);                                              \
  if (addr < callback_above) {                                                \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 5;                                              \
  } else {                                                                    \
    addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                       \
    INTERP_TIMING_ADVANCE(3);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_MEMORY_READ(addr_temp);                                            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 3;

#define INTERP_MODE_ABX_READ_WRITE_PRE()                                      \
  addr_temp = *(uint16_t*) &p_mem_read[pc + 1];                               \
  addr = (addr_temp + x);                                                     \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
  } else {                                                                    \
    addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                       \
    INTERP_TIMING_ADVANCE(3);                                                 \
    INTERP_MEMORY_READ(addr_temp);                                            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_MEMORY_WRITE(addr);                                                \
  }

#define INTERP_MODE_ABX_READ_WRITE_POST()                                     \
  INTERP_LOAD_NZ_FLAGS(v);                                                    \
  if (addr < callback_above) {                                                \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 7;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 3;

#define INTERP_MODE_IDX_READ()                                                \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 2;

#define INTERP_MODE_IDX_WRITE()                                               \
  addr = p_mem_read[pc + 1];                                                  \
  addr += x;                                                                  \
  addr &= 0xFF;                                                               \
  addr = ((p_mem_read[(uint8_t) (addr + 1)] << 8) | p_mem_read[addr]);        \
  if (addr < callback_above) {                                                \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 2;

#define INTERP_MODE_IDY_READ()                                                \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  page_crossing = !!((addr_temp >> 8) ^ (addr >> 8));                         \
  if (addr < callback_above) {                                                \
    v = p_mem_read[addr];                                                     \
    cycles_this_instruction = 5;                                              \
    cycles_this_instruction += page_crossing;                                 \
  } else {                                                                    \
    if (page_crossing) {                                                      \
      INTERP_TIMING_ADVANCE(4);                                               \
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);          \
      INTERP_MEMORY_READ(addr - 0x100);                                       \
      INTERP_TIMING_ADVANCE(1);                                               \
    } else {                                                                  \
      INTERP_TIMING_ADVANCE(3);                                               \
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);          \
      INTERP_TIMING_ADVANCE(1);                                               \
    }                                                                         \
    INTERP_MEMORY_READ(addr);                                                 \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 2;

#define INTERP_MODE_IDY_WRITE()                                               \
  addr_temp = p_mem_read[pc + 1];                                             \
  addr_temp = ((p_mem_read[(uint8_t) (addr_temp + 1)] << 8) |                 \
      p_mem_read[addr_temp]);                                                 \
  addr = (addr_temp + y);                                                     \
  if (addr < callback_above) {                                                \
    p_mem_write[addr] = v;                                                    \
    cycles_this_instruction = 6;                                              \
  } else {                                                                    \
    addr_temp = ((addr & 0xFF) | (addr_temp & 0xFF00));                       \
    INTERP_TIMING_ADVANCE(4);                                                 \
    interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);            \
    INTERP_MEMORY_READ(addr_temp);                                            \
    INTERP_TIMING_ADVANCE(1);                                                 \
    INTERP_MEMORY_WRITE(addr);                                                \
    INTERP_TIMING_ADVANCE(1);                                                 \
    cycles_this_instruction = 0;                                              \
    if (do_irq_vector) goto force_opcode;                                     \
  }                                                                           \
  pc += 2;

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
  cycles_this_instruction = 5;                                                \
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

#define INTERP_INSTR_LSR()                                                    \
  cf = (v & 0x01);                                                            \
  v >>= 1;                                                                    \
  INTERP_LOAD_NZ_FLAGS(v);

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
  temp_int = (a + (unsigned char) ~v + cf);                                   \
  if (df) {                                                                   \
    /* Fix up decimal carry on first nibble. */                               \
    if (((v & 0x0F) + !cf) > (a & 0x0F)) {                                    \
      temp_int -= 0x06;                                                       \
    }                                                                         \
  }                                                                           \
  /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */   \
  of = !!((a ^ temp_int) & ((unsigned char) ~v ^ temp_int) & 0x80);           \
  /* In decimal mode, NZ flags are based on this interim value. */            \
  INTERP_LOAD_NZ_FLAGS((temp_int & 0xFF));                                    \
  if (df) {                                                                   \
    if ((v + !cf) > a) {                                                      \
      temp_int -= 0x60;                                                       \
    }                                                                         \
  }                                                                           \
  cf = !!(temp_int & 0x100);                                                  \
  a = temp_int;

uint32_t
interp_enter(struct interp_struct* p_interp) {
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
  int64_t cycles_this_instruction;
  int64_t countdown;
  uint64_t delta;
  int page_crossing;
  uint16_t addr;
  uint16_t addr_temp;
  uint8_t opcode;
  uint8_t v;

  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  struct timing_struct* p_timing = p_interp->p_timing;
  struct memory_access* p_memory_access = p_interp->p_memory_access;
  uint8_t (*memory_read_callback)(void*, uint16_t) =
      p_memory_access->memory_read_callback;
  void (*memory_write_callback)(void*, uint16_t, uint8_t) =
      p_memory_access->memory_write_callback;
  void* p_memory_obj = p_memory_access->p_callback_obj;
  uint8_t* p_mem_read = p_interp->p_mem_read;
  uint8_t* p_mem_write = p_interp->p_mem_write;
  uint8_t* p_stack = (p_mem_write + k_6502_stack_addr);
  uint16_t callback_above = p_interp->callback_above;
  int debug_subsystem_active = p_interp->debug_subsystem_active;
  uint16_t do_irq_vector = 0;

  p_interp->return_from_loop = 0;
  countdown = timing_get_countdown(p_timing);

  state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
  interp_set_flags(flags, &zf, &nf, &cf, &of, &df, &intf);

  while (1) {
    /* TODO: opcode fetch doesn't consider hardware register access,
     * i.e. JMP $FE6A will have incorrect timings.
     */
    opcode = p_mem_read[pc];

  force_opcode:
    if (countdown <= 0) {
      INTERP_TIMING_ADVANCE(0);

      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);

      /* Note that we stay in the interpreter loop to handle the IRQ if one
       * has arisen, otherwise it would get lost.
       */
      if (p_interp->return_from_loop) {
        size_t timer_id = p_interp->short_instruction_run_timer_id;
        if (!do_irq_vector) {
          break;
        }
        countdown = timing_start_timer(p_timing, timer_id, 0);
      }
    }

    if (debug_subsystem_active) {
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
                           do_irq_vector);
    }

    switch (opcode) {
    case 0x00: /* BRK */
      /* EMU NOTE: if an NMI hits early enough in the 7-cycle interrupt / BRK
       * sequence for a non-NMI interrupt, the NMI should take precendence.
       * Probably not worth emulating unless we can come up with a
       * deterministic way to fire an NMI to trigger this.
       * (Need to investigate disc controller NMI timing on a real beeb.)
       */
      temp_u8 = 0;
      if (!do_irq_vector) {
        /* It's a BRK, not an IRQ. */
        temp_u8 = (1 << k_flag_brk);
        do_irq_vector = k_6502_vector_irq;
        pc += 2;
      }
      p_stack[s--] = (pc >> 8);
      p_stack[s--] = (pc & 0xFF);
      v = interp_get_flags(zf, nf, cf, of, df, intf);
      v |= (temp_u8 | (1 << k_flag_always_set));
      p_stack[s--] = v;
      pc = (p_mem_read[do_irq_vector] |
          (p_mem_read[(uint16_t) (do_irq_vector + 1)] << 8));
      intf = 1;
      do_irq_vector = 0;
      break;
    case 0x01: /* ORA idx */
      INTERP_MODE_IDX_READ();
      a |= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x02: /* Extension: EXIT */
      return ((y << 16) | (x << 8) | a);
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
    case 0x0D: /* ORA abs */
      INTERP_MODE_ABS_READ();
      a |= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x0E: /* ASL abs */
      INTERP_MODE_ABS_READ_WRITE_PRE();
      INTERP_INSTR_ASL();
      INTERP_MODE_ABS_READ_WRITE_POST();
      break;
    case 0x10: /* BPL */
      INTERP_INSTR_BRANCH(!nf);
      break;
    case 0x11: /* ORA idy */
      INTERP_MODE_IDY_READ();
      a |= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x12: /* Extension: CYCLES */
      INTERP_TIMING_ADVANCE(0);
      a = (state_6502_get_cycles(p_state_6502) & 0xFF);
      pc++;
      cycles_this_instruction = 1;
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
      INTERP_MODE_ABr_READ(y);
      a |= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x1D: /* ORA abx */
      INTERP_MODE_ABr_READ(x);
      a |= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x1E: /* ASL abx */
      INTERP_MODE_ABX_READ_WRITE_PRE();
      INTERP_INSTR_ASL();
      INTERP_MODE_ABX_READ_WRITE_POST();
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
      INTERP_MODE_IDX_READ();
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x22: /* Extension: CYCLES_RESET */
      INTERP_TIMING_ADVANCE(0);
      state_6502_set_cycles(p_state_6502, 0);
      pc++;
      cycles_this_instruction = 1;
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
      v = p_stack[++s];
      interp_set_flags(v, &zf, &nf, &cf, &of, &df, &intf);
      pc++;
      cycles_this_instruction = 4;
      /* TODO: buggy. */
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);
      if (!opcode) {
        goto force_opcode;
      }
      break;
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
      INTERP_MODE_ABS_READ();
      INTERP_INSTR_BIT();
      break;
    case 0x2D: /* AND abs */
      INTERP_MODE_ABS_READ();
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x2E: /* ROL abs */
      INTERP_MODE_ABS_READ_WRITE_PRE();
      INTERP_INSTR_ROL();
      INTERP_MODE_ABS_READ_WRITE_POST();
      break;
    case 0x30: /* BMI */
      INTERP_INSTR_BRANCH(nf);
      break;
    case 0x31: /* AND idy */
      INTERP_MODE_IDY_READ();
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
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
      INTERP_MODE_ABr_READ(y);
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x3D: /* AND abx */
      INTERP_MODE_ABr_READ(x);
      a &= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x3E: /* ROL abx */
      INTERP_MODE_ABX_READ_WRITE_PRE();
      INTERP_INSTR_ROL();
      INTERP_MODE_ABX_READ_WRITE_POST();
      break;
    case 0x40: /* RTI */
      v = p_stack[++s];
      interp_set_flags(v, &zf, &nf, &cf, &of, &df, &intf);
      pc = p_stack[++s];
      pc |= (p_stack[++s] << 8);
      cycles_this_instruction = 6;
      break;
    case 0x41: /* EOR idx */
      INTERP_MODE_IDX_READ();
      a ^= v;
      INTERP_LOAD_NZ_FLAGS(a);
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
    case 0x4C: /* JMP abs */
      pc = *(uint16_t*) &p_mem_read[pc + 1];
      cycles_this_instruction = 3;
      break;
    case 0x4D: /* EOR abs */
      INTERP_MODE_ABS_READ();
      a ^= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x4E: /* LSR abs */
      INTERP_MODE_ABS_READ_WRITE_PRE();
      INTERP_INSTR_LSR();
      INTERP_MODE_ABS_READ_WRITE_POST();
      break;
    case 0x50: /* BVC */
      INTERP_INSTR_BRANCH(!of);
      break;
    case 0x51: /* EOR idy */
      INTERP_MODE_IDY_READ();
      a ^= v;
      INTERP_LOAD_NZ_FLAGS(a);
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
      intf = 0;
      pc++;
      cycles_this_instruction = 2;
      /* TODO: buggy. */
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);
      if (!opcode) {
        goto force_opcode;
      }
      break;
    case 0x59: /* EOR aby */
      INTERP_MODE_ABr_READ(y);
      a ^= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x5D: /* EOR abx */
      INTERP_MODE_ABr_READ(x);
      a ^= v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0x5E: /* LSR abx */
      INTERP_MODE_ABX_READ_WRITE_PRE();
      INTERP_INSTR_LSR();
      INTERP_MODE_ABX_READ_WRITE_POST();
      break;
    case 0x60: /* RTS */
      pc = p_stack[++s];
      pc |= (p_stack[++s] << 8);
      pc++;
      cycles_this_instruction = 6;
      break;
    case 0x61: /* ADC idx */
      INTERP_MODE_IDX_READ();
      INTERP_INSTR_ADC();
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
      INTERP_MODE_ABS_READ();
      INTERP_INSTR_ADC();
      break;
    case 0x6E: /* ROR abs */
      INTERP_MODE_ABS_READ_WRITE_PRE();
      INTERP_INSTR_ROR();
      INTERP_MODE_ABS_READ_WRITE_POST();
      break;
    case 0x70: /* BVS */
      INTERP_INSTR_BRANCH(of);
      break;
    case 0x71: /* ADC idy */
      INTERP_MODE_IDY_READ();
      INTERP_INSTR_ADC();
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
      intf = 1;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x79: /* ADC aby */
      INTERP_MODE_ABr_READ(y);
      INTERP_INSTR_ADC();
      break;
    case 0x7D: /* ADC abx */
      INTERP_MODE_ABr_READ(x);
      INTERP_INSTR_ADC();
      break;
    case 0x7E: /* ROR abx */
      INTERP_MODE_ABX_READ_WRITE_PRE();
      INTERP_INSTR_ROR();
      INTERP_MODE_ABX_READ_WRITE_POST();
      break;
    case 0x81: /* STA idx */
      v = a;
      INTERP_MODE_IDX_WRITE();
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
      v = y;
      INTERP_MODE_ABS_WRITE();
      break;
    case 0x8D: /* STA abs */
      v = a;
      INTERP_MODE_ABS_WRITE();
      break;
    case 0x8E: /* STX abs */
      v = x;
      INTERP_MODE_ABS_WRITE();
      break;
    case 0x90: /* BCC */
      INTERP_INSTR_BRANCH(!cf);
      break;
    case 0x91: /* STA idy */
      v = a;
      INTERP_MODE_IDY_WRITE();
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
      v = a;
      INTERP_MODE_ABr_WRITE(y);
      break;
    case 0x9A: /* TXS */
      s = x;
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0x9D: /* STA abx */
      v = a;
      INTERP_MODE_ABr_WRITE(x);
      break;
    case 0xA0: /* LDY imm */
      y = p_mem_read[pc + 1];
      INTERP_LOAD_NZ_FLAGS(y);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xA1: /* LDA idx */
      INTERP_MODE_IDX_READ();
      a = v;
      INTERP_LOAD_NZ_FLAGS(a);
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
      INTERP_MODE_ABS_READ();
      y = v;
      INTERP_LOAD_NZ_FLAGS(y);
      break;
    case 0xAD: /* LDA abs */
      INTERP_MODE_ABS_READ();
      a = v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0xAE: /* LDX abs */
      INTERP_MODE_ABS_READ();
      x = v;
      INTERP_LOAD_NZ_FLAGS(x);
      break;
    case 0xB0: /* BCS */
      INTERP_INSTR_BRANCH(cf);
      break;
    case 0xB1: /* LDA idy */
      INTERP_MODE_IDY_READ();
      a = v;
      INTERP_LOAD_NZ_FLAGS(a);
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
      INTERP_MODE_ABr_READ(y);
      a = v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0xBA: /* TSX */
      x = s;
      INTERP_LOAD_NZ_FLAGS(x);
      pc++;
      cycles_this_instruction = 2;
      break;
    case 0xBC: /* LDY abx */
      INTERP_MODE_ABr_READ(x);
      y = v;
      INTERP_LOAD_NZ_FLAGS(y);
      break;
    case 0xBD: /* LDA abx */
      INTERP_MODE_ABr_READ(x);
      a = v;
      INTERP_LOAD_NZ_FLAGS(a);
      break;
    case 0xBE: /* LDX aby */
      INTERP_MODE_ABr_READ(y);
      x = v;
      INTERP_LOAD_NZ_FLAGS(x);
      break;
    case 0xC0: /* CPY imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_CMP(y);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xC1: /* CMP idx */
      INTERP_MODE_IDX_READ();
      INTERP_INSTR_CMP(a);
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
      INTERP_MODE_ABS_READ();
      INTERP_INSTR_CMP(y);
      break;
    case 0xCD: /* CMP abs */
      INTERP_MODE_ABS_READ();
      INTERP_INSTR_CMP(a);
      break;
    case 0xCE: /* DEC abs */
      INTERP_MODE_ABS_READ_WRITE_PRE();
      v--;
      INTERP_MODE_ABS_READ_WRITE_POST();
      break;
    case 0xD0: /* BNE */
      INTERP_INSTR_BRANCH(!zf);
      break;
    case 0xD1: /* CMP idy */
      INTERP_MODE_IDY_READ();
      INTERP_INSTR_CMP(a);
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
      INTERP_MODE_ABr_READ(y);
      INTERP_INSTR_CMP(a);
      break;
    case 0xDD: /* CMP abx */
      INTERP_MODE_ABr_READ(x);
      INTERP_INSTR_CMP(a);
      break;
    case 0xDE: /* DEC abx */
      INTERP_MODE_ABX_READ_WRITE_PRE();
      v--;
      INTERP_MODE_ABX_READ_WRITE_POST();
      break;
    case 0xE0: /* CPX imm */
      v = p_mem_read[pc + 1];
      INTERP_INSTR_CMP(x);
      pc += 2;
      cycles_this_instruction = 2;
      break;
    case 0xE1: /* SBC idx */
      INTERP_MODE_IDX_READ();
      INTERP_INSTR_SBC();
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
      INTERP_MODE_ABS_READ();
      INTERP_INSTR_CMP(x);
      break;
    case 0xED: /* SBC abs */
      INTERP_MODE_ABS_READ();
      INTERP_INSTR_SBC();
      break;
    case 0xEE: /* INC abs */
      INTERP_MODE_ABS_READ_WRITE_PRE();
      v++;
      INTERP_MODE_ABS_READ_WRITE_POST();
      break;
    case 0xF0: /* BEQ */
      INTERP_INSTR_BRANCH(zf);
      break;
    case 0xF1: /* SBC idy */
      INTERP_MODE_IDY_READ();
      INTERP_INSTR_SBC();
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
      INTERP_MODE_ABr_READ(y);
      INTERP_INSTR_SBC();
      break;
    case 0xFD: /* SBC abx */
      INTERP_MODE_ABr_READ(x);
      INTERP_INSTR_SBC();
      break;
    case 0xFE: /* INC abx */
      INTERP_MODE_ABX_READ_WRITE_PRE();
      v++;
      INTERP_MODE_ABX_READ_WRITE_POST();
      break;
    default:
      __builtin_trap();
    }

    countdown -= cycles_this_instruction;
  }

  flags = interp_get_flags(zf, nf, cf, of, df, intf);
  state_6502_set_registers(p_state_6502, a, x, y, s, flags, pc);

  return (uint32_t) -1;
}

int64_t
interp_single_instruction(struct interp_struct* p_interp, int64_t countdown) {
  uint32_t ret;
  uint64_t delta;

  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  struct timing_struct* p_timing = p_interp->p_timing;

  (void) timing_advance_time(p_timing, &delta, countdown);
  state_6502_add_cycles(p_state_6502, delta);

  /* Set a timer to fire after 1 instruction and stop the interpreter loop. */
  (void) timing_start_timer(p_timing,
                            p_interp->short_instruction_run_timer_id,
                            1);

  ret = interp_enter(p_interp);
  (void) ret;
  assert(ret == (uint32_t) -1);

  countdown = timing_get_countdown(p_timing);
  return countdown;
}
