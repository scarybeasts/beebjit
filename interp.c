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
};

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

static inline void
interp_update_timing_events(struct timing_struct* p_timing,
                            int64_t* p_next_event_cycles,
                            int64_t* p_last_next_event_cycles) {
  int64_t delta = (*p_last_next_event_cycles - *p_next_event_cycles);
  int64_t next_event_cycles = timing_advance(p_timing, delta);
  *p_next_event_cycles = next_event_cycles;
  *p_last_next_event_cycles = next_event_cycles;
}

static inline uint8_t
interp_read_mem(int64_t* p_next_event_cycles,
                int64_t* p_last_next_event_cycles,
                struct memory_access* p_memory_access,
                struct timing_struct* p_timing,
                uint8_t* p_mem,
                uint16_t addr,
                uint16_t read_callback_mask) {
  uint8_t ret;

  if ((addr & read_callback_mask) == read_callback_mask) {
    ret = p_memory_access->memory_read_callback(
        p_memory_access->p_callback_obj,
        addr);
    /* The special memory callback may have modified when our next event is
     * going to occur.
     */
    interp_update_timing_events(p_timing,
                                p_next_event_cycles,
                                p_last_next_event_cycles);
  } else {
    ret = p_mem[addr];
  }

  return ret;
}

static inline void
interp_write_mem(int64_t* p_next_event_cycles,
                 int64_t* p_last_next_event_cycles,
                 struct memory_access* p_memory_access,
                 struct timing_struct* p_timing,
                 uint8_t* p_mem,
                 uint16_t addr,
                 uint8_t v,
                 uint16_t write_callback_mask) {
  if ((addr & write_callback_mask) == write_callback_mask) {
    p_memory_access->memory_write_callback(p_memory_access->p_callback_obj,
                                           addr,
                                           v);
    /* The special memory callback may have modified when our next event is
     * going to occur.
     */
    interp_update_timing_events(p_timing,
                                p_next_event_cycles,
                                p_last_next_event_cycles);
  } else {
    p_mem[addr] = v;
  }
}

void
interp_enter(struct interp_struct* p_interp) {
  unsigned char a;
  unsigned char x;
  unsigned char y;
  unsigned char s;
  unsigned char flags;
  uint16_t pc;
  unsigned char zf;
  unsigned char nf;
  unsigned char cf;
  unsigned char of;
  unsigned char df;
  unsigned char intf;
  unsigned char tmpf;

  unsigned char opcode;
  unsigned char opmode;
  unsigned char optype;
  unsigned char opmem;
  unsigned char opreg;
  unsigned char v;
  uint16_t addr;
  int branch;
  int check_extra_read_cycle;
  uint16_t temp_addr;
  int tmp_int;

  volatile unsigned char* p_crash_ptr = 0;
  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  struct memory_access* p_memory_access = p_interp->p_memory_access;
  struct timing_struct* p_timing = p_interp->p_timing;
  struct bbc_options* p_options = p_interp->p_options;
  unsigned char* p_mem_read = p_memory_access->p_mem_read;
  unsigned char* p_mem_write = p_memory_access->p_mem_write;
  unsigned char* p_stack = (p_mem_write + k_6502_stack_addr);
  uint16_t read_callback_mask =
      p_memory_access->memory_read_needs_callback_mask(
          p_memory_access->p_callback_obj);
  uint16_t write_callback_mask =
      p_memory_access->memory_write_needs_callback_mask(
          p_memory_access->p_callback_obj);
  unsigned int* p_irq = &p_state_6502->irq_fire;
  void* p_debug_callback_object = p_options->p_debug_callback_object;
  int debug_subsystem_active = p_options->debug_subsystem_active(
      p_debug_callback_object);
  size_t* p_debug_counter_ptr = p_options->debug_get_counter_ptr(
      p_debug_callback_object);
  void* (*debug_callback)(void*) = p_options->debug_callback;
  int (*debug_active_at_addr)(void*, uint16_t) =
      p_options->debug_active_at_addr;
  int (*debug_counter_at_addr)(void*, uint16_t) =
      p_options->debug_counter_at_addr;
  int64_t next_event_cycles = timing_advance(p_timing, 0);
  int64_t last_next_event_cycles = next_event_cycles;

  v = 0;
  addr = 0;

  state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
  interp_set_flags(flags, &zf, &nf, &cf, &of, &df, &intf);

  while (1) {
    if (debug_subsystem_active) {
      if (debug_counter_at_addr(p_debug_callback_object, pc)) {
        if (!*p_debug_counter_ptr) {
          __builtin_trap();
        }
        *p_debug_counter_ptr = (*p_debug_counter_ptr - 1);
      }

      if (debug_active_at_addr(p_debug_callback_object, pc)) {
        flags = interp_get_flags(zf, nf, cf, of, df, intf);
        state_6502_set_registers(p_state_6502, a, x, y, s, flags, pc);
        /* TODO: set cycles. */

        debug_callback(p_options->p_debug_callback_object);

        state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
        interp_set_flags(flags, &zf, &nf, &cf, &of, &df, &intf);
      }
    }

    branch = 0;
    /* TODO: opcode fetch doesn't consider hardware register access. */
    opcode = p_mem_read[pc++];
    opmode = g_opmodes[opcode];
    optype = g_optypes[opcode];
    opreg = g_optype_sets_register[optype];
    opmem = g_opmem[optype];

    /* Cycles, except branch and page crossings. */
    check_extra_read_cycle = (opmem == k_read);
    next_event_cycles -= g_opcycles[opcode];

    switch (opmode) {
    case k_nil:
    case 0: opmem = k_nomem; break;
    case k_acc: opreg = k_a; opmem = k_nomem; v = a; break;
    case k_imm:
    case k_rel: v = p_mem_read[pc++]; opmem = k_nomem; break;
    case k_zpg: addr = p_mem_read[pc++]; break;
    case k_abs:
      addr = (p_mem_read[pc] | (p_mem_read[(uint16_t) (pc + 1)] << 8));
      pc += 2;
      break;
    case k_zpx: addr = p_mem_read[pc++]; addr += x; addr &= 0xff; break;
    case k_zpy: addr = p_mem_read[pc++]; addr += y; addr &= 0xff; break;
    case k_abx:
      addr = p_mem_read[pc];
      addr += x;
      next_event_cycles -= ((addr >> 8) & check_extra_read_cycle);
      addr += (p_mem_read[(uint16_t) (pc + 1)] << 8);
      pc += 2;
      break;
    case k_aby:
      addr = p_mem_read[pc];
      addr += y;
      next_event_cycles -= ((addr >> 8) & check_extra_read_cycle);
      addr += (p_mem_read[(uint16_t) (pc + 1)] << 8);
      pc += 2;
      break;
    case k_ind:
      addr = (p_mem_read[pc] | (p_mem_read[(uint16_t) (pc + 1)] << 8));
      pc += 2;
      v = p_mem_read[addr];
      /* Indirect fetches wrap at page boundaries. */
      if ((addr & 0xff) == 0xff) {
        addr &= 0xff00;
      } else {
        addr++;
      }
      addr = (v | (p_mem_read[addr] << 8));
      break;
    case k_idx:
      v = p_mem_read[pc++];
      v += x;
      addr = p_mem_read[v];
      v++;
      addr |= (p_mem_read[v] << 8);
      break;
    case k_idy:
      v = p_mem_read[pc++];
      addr = p_mem_read[v];
      addr += y;
      next_event_cycles -= ((addr >> 8) & check_extra_read_cycle);
      v++;
      addr += (p_mem_read[v] << 8);
      break;
    default:
      assert(0);
    }

    if (opmem == k_read || opmem == k_rw) {
      v = interp_read_mem(&next_event_cycles,
                          &last_next_event_cycles,
                          p_memory_access,
                          p_timing,
                          p_mem_read,
                          addr,
                          read_callback_mask);
      if (opmem == k_rw) {
        opreg = k_v;
      }
    }

    switch (optype) {
    case k_kil:
      switch (opcode) {
      case 0x02: /* EXIT */
        return;
      case 0xf2: /* CRASH */
        (void) *p_crash_ptr;
      default:
        assert(0);
      }
      break;
    case k_adc:
      tmp_int = (a + v + cf);
      if (df) {
        /* Fix up decimal carry on first nibble. */
        int decimal_carry = ((a & 0x0f) + (v & 0x0f) + cf);
        if (decimal_carry >= 0x0a) {
          tmp_int += 0x06;
        }
      }
      /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */
      of = !!((a ^ tmp_int) & (v ^ tmp_int) & 0x80);
      if (df) {
        /* In decimal mode, NZ flags are based on this interim value. */
        v = tmp_int;
        opreg = k_v;
        if (tmp_int >= 0xa0) {
          tmp_int += 0x60;
        }
      }
      a = tmp_int;
      cf = !!(tmp_int & 0x100);
      break;
    case k_alr: a &= v; cf = (a & 0x01); a >>= 1; break;
    case k_and: a &= v; break;
    case k_asl: cf = !!(v & 0x80); v <<= 1; break;
    case k_bcc: branch = (cf == 0); break;
    case k_bcs: branch = (cf == 1); break;
    case k_beq: branch = (zf == 1); break;
    case k_bit: zf = !(a & v); nf = !!(v & 0x80); of = !!(v & 0x40); break;
    case k_bmi: branch = (nf == 1); break;
    case k_bne: branch = (zf == 0); break;
    case k_bpl: branch = (nf == 0); break;
    case k_brk:
      temp_addr = pc + 1;
      p_stack[s--] = (temp_addr >> 8);
      p_stack[s--] = (temp_addr & 0xff);
      v = interp_get_flags(zf, nf, cf, of, df, intf);
      v |= ((1 << k_flag_brk) | (1 << k_flag_always_set));
      p_stack[s--] = v;
      pc = (p_mem_read[k_6502_vector_irq] |
            (p_mem_read[k_6502_vector_irq + 1] << 8));
      intf = 1;
      break;
    case k_bvc: branch = (of == 0); break;
    case k_bvs: branch = (of == 1); break;
    case k_clc: cf = 0; break;
    case k_cld: df = 0; break;
    case k_cli: intf = 0; break;
    case k_clv: of = 0; break;
    case k_cmp: cf = (a >= v); v = (a - v); opreg = k_v; break;
    case k_cpx: cf = (x >= v); v = (x - v); opreg = k_v; break;
    case k_cpy: cf = (y >= v); v = (y - v); opreg = k_v; break;
    case k_dec: v--; break;
    case k_dex: x--; break;
    case k_dey: y--; break;
    case k_eor: a ^= v; break;
    case k_inc: v++; break;
    case k_inx: x++; break;
    case k_iny: y++; break;
    case k_jmp: pc = addr; break;
    case k_jsr:
      temp_addr = pc - 1;
      p_stack[s--] = (temp_addr >> 8);
      p_stack[s--] = (temp_addr & 0xff);
      pc = addr;
      break;
    case k_lda: a = v; break;
    case k_ldx: x = v; break;
    case k_ldy: y = v; break;
    case k_lsr: cf = (v & 0x01); v >>= 1; break;
    case k_nop: break;
    case k_pha: p_stack[s--] = a; break;
    case k_php:
      v = interp_get_flags(zf, nf, cf, of, df, intf);
      v |= ((1 << k_flag_brk) | (1 << k_flag_always_set));
      p_stack[s--] = v;
      break;
    case k_pla: a = p_stack[++s]; break;
    case k_plp:
      v = p_stack[++s];
      interp_set_flags(v, &zf, &nf, &cf, &of, &df, &intf);
      break;
    case k_ora: a |= v; break;
    case k_rol: tmpf = cf; cf = !!(v & 0x80); v <<= 1; v |= tmpf; break;
    case k_ror: tmpf = cf; cf = (v & 0x01); v >>= 1; v |= (tmpf << 7); break;
    case k_rti:
      v = p_stack[++s];
      interp_set_flags(v, &zf, &nf, &cf, &of, &df, &intf);
      pc = p_stack[++s];
      pc |= (p_stack[++s] << 8);
      break;
    case k_rts: pc = p_stack[++s]; pc |= (p_stack[++s] << 8); pc++; break;
    case k_sax: v = (a & x); break;
    case k_sbc:
      /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */
      /* "SBC simply takes the ones complement of the second value and then
       * performs an ADC"
       */
      tmp_int = (a + (unsigned char) ~v + cf);
      if (df) {
        /* Fix up decimal carry on first nibble. */
        if (((v & 0x0f) + !cf) > (a & 0x0f)) {
          tmp_int -= 0x06;
        }
      }
      /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */
      of = !!((a ^ tmp_int) & ((unsigned char) ~v ^ tmp_int) & 0x80);
      if (df) {
        /* In decimal mode, NZ flags are based on this interim value. */
        v = tmp_int;
        opreg = k_v;
        if ((v + !cf) > a) {
          tmp_int -= 0x60;
        }
      }
      a = tmp_int;
      cf = !!(tmp_int & 0x100);
      break;
    case k_sec: cf = 1; break;
    case k_sed: df = 1; break;
    case k_sei: intf = 1; break;
    /* TODO: SHY also issues a read at the uncarried abx address. */
    case k_shy: v = (y & ((addr >> 8) + 1)); break;
    case k_slo: cf = !!(v & 0x80); v <<= 1; a |= v; break;
    case k_sta: v = a; break;
    case k_stx: v = x; break;
    case k_sty: v = y; break;
    case k_tax: x = a; break;
    case k_tay: y = a; break;
    case k_tsx: x = s; break;
    case k_txa: a = x; break;
    case k_txs: s = x; break;
    case k_tya: a = y; break;
    default:
      printf("Unknown opcode: %x @ post-inc PC $%.4x\n", opcode, pc);
      assert(0);
    }

    if (opmem == k_write || opmem == k_rw) {
      interp_write_mem(&next_event_cycles,
                       &last_next_event_cycles,
                       p_memory_access,
                       p_timing,
                       p_mem_write,
                       addr,
                       v,
                       write_callback_mask);
    }
    if (opmode == k_acc) {
      a = v;
    }

    switch (opreg) {
    case 0: break;
    case k_a: zf = (a == 0); nf = !!(a & 0x80); break;
    case k_x: zf = (x == 0); nf = !!(x & 0x80); break;
    case k_y: zf = (y == 0); nf = !!(y & 0x80); break;
    case k_v: zf = (v == 0); nf = !!(v & 0x80); break;
    default:
      assert(0);
    }

    if (branch) {
      next_event_cycles--;
      temp_addr = pc;
      pc = (pc + (char) v);
      if ((pc ^ temp_addr) & 0x0100) {
        next_event_cycles--;
      }
    }
    if (next_event_cycles <= 0) {
      interp_update_timing_events(p_timing,
                                  &next_event_cycles,
                                  &last_next_event_cycles);
    }
    /* TODO: only check IRQs after we've handled an event, or after a CLI /
     * PLP.
     */
    if (*p_irq) {
      uint16_t vector = 0;
      /* EMU: if both an NMI and normal IRQ are asserted at the same time, only
       * the NMI should fire. This is confirmed via visual 6502; see:
       * http://forum.6502.org/viewtopic.php?t=1797
       * Note that jsbeeb, b-em and beebem all appear to get this wrong, they
       * will run the 7 cycle interrupt sequence twice in a row, which would
       * be visible as stack and timing artifacts. b2 looks likely to be
       * correct as it is a much more low level 6502 emulation.
       */
      if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi)) {
        vector = k_6502_vector_nmi;
      } else if (!intf) {
        vector = k_6502_vector_irq;
      }
      /* EMU NOTE: if an NMI hits early enough in the 7-cycle interrupt
       * sequence for a non-NMI interrupt, the NMI should take precendence.
       * Probably not worth emulating unless we can come up with a
       * deterministic way to fire an NMI to trigger this.
       */
      if (vector) {
        p_stack[s--] = (pc >> 8);
        p_stack[s--] = (pc & 0xff);
        v = interp_get_flags(zf, nf, cf, of, df, intf);
        v |= (1 << k_flag_always_set);
        p_stack[s--] = v;
        pc = (p_mem_read[vector] | (p_mem_read[(uint16_t) (vector + 1)] << 8));
        intf = 1;
        next_event_cycles -= 7;
        if (next_event_cycles <= 0) {
          interp_update_timing_events(p_timing,
                                      &next_event_cycles,
                                      &last_next_event_cycles);
        }
      }
    }
  }
}
