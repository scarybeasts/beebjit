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

uint32_t
interp_enter(struct interp_struct* p_interp) {
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t s;
  uint8_t flags;
  uint16_t pc;
  uint8_t zf;
  uint8_t nf;
  uint8_t cf;
  uint8_t of;
  uint8_t df;
  uint8_t intf;
  uint8_t tmpf;

  uint8_t opcode;
  uint8_t opmode;
  uint8_t optype;
  uint8_t opmem;
  uint8_t opreg;
  int branch;
  int check_extra_read_cycle;
  uint16_t temp_addr;
  int temp_int;
  uint8_t temp_u8;
  int64_t cycles_this_instruction;
  int64_t countdown;

  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  struct timing_struct* p_timing = p_interp->p_timing;
  struct memory_access* p_memory_access = p_interp->p_memory_access;
  uint8_t* p_mem_read = p_interp->p_mem_read;
  uint8_t* p_mem_write = p_interp->p_mem_write;
  uint8_t* p_stack = (p_mem_write + k_6502_stack_addr);
  uint16_t read_callback_above = p_interp->read_callback_above;
  uint16_t write_callback_above = p_interp->write_callback_above;
  int debug_subsystem_active = p_interp->debug_subsystem_active;
  uint16_t do_irq_vector = 0;
  unsigned char v = 0;
  uint16_t addr = 0;

  p_interp->return_from_loop = 0;
  countdown = timing_get_countdown(p_timing);

  state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
  interp_set_flags(flags, &zf, &nf, &cf, &of, &df, &intf);

  while (1) {
    /* TODO: opcode fetch doesn't consider hardware register access. */
    opcode = p_mem_read[pc];

  force_opcode:
    if (countdown <= 0) {
      uint64_t delta = timing_update_countdown(p_timing, countdown);
      state_6502_add_cycles(p_state_6502, delta);

      countdown = timing_trigger_callbacks(p_timing);

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

    branch = 0;
    opmode = g_opmodes[opcode];
    optype = g_optypes[opcode];
    opreg = g_optype_sets_register[optype];
    opmem = g_opmem[optype];

    /* Cycles, except branch and page crossings. */
    check_extra_read_cycle = (opmem == k_read);
    cycles_this_instruction = g_opcycles[opcode];

    switch (opmode) {
    case k_nil:
    case 0: opmem = k_nomem; pc++; break;
    case k_acc: opreg = k_a; opmem = k_nomem; v = a; pc++; break;
    case k_imm:
    case k_rel: v = p_mem_read[pc + 1]; opmem = k_nomem; pc += 2; break;
    case k_zpg: addr = p_mem_read[pc + 1]; pc += 2; break;
    case k_abs:
      addr = (p_mem_read[pc + 1] | (p_mem_read[(uint16_t) (pc + 2)] << 8));
      pc += 3;
      break;
    case k_zpx:
      addr = p_mem_read[pc + 1];
      addr += x;
      addr &= 0xff;
      pc += 2;
      break;
    case k_zpy:
      addr = p_mem_read[pc + 1];
      addr += y;
      addr &= 0xff;
      pc += 2;
      break;
    case k_abx:
      addr = p_mem_read[pc + 1];
      addr += x;
      cycles_this_instruction += ((addr >> 8) & check_extra_read_cycle);
      addr += (p_mem_read[(uint16_t) (pc + 2)] << 8);
      pc += 3;
      break;
    case k_aby:
      addr = p_mem_read[pc + 1];
      addr += y;
      cycles_this_instruction += ((addr >> 8) & check_extra_read_cycle);
      addr += (p_mem_read[(uint16_t) (pc + 2)] << 8);
      pc += 3;
      break;
    case k_ind:
      addr = (p_mem_read[pc + 1] | (p_mem_read[(uint16_t) (pc + 2)] << 8));
      pc += 3;
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
      v = p_mem_read[pc + 1];
      v += x;
      addr = p_mem_read[v];
      v++;
      addr |= (p_mem_read[v] << 8);
      pc += 2;
      break;
    case k_idy:
      v = p_mem_read[pc + 1];
      addr = p_mem_read[v];
      addr += y;
      cycles_this_instruction += ((addr >> 8) & check_extra_read_cycle);
      v++;
      addr += (p_mem_read[v] << 8);
      pc += 2;
      break;
    default:
      assert(0);
    }

    if (opmem == k_read || opmem == k_rw) {
      if (addr < read_callback_above) {
        v = p_mem_read[addr];
      } else {
        uint64_t delta = timing_update_countdown(p_timing, countdown);
        state_6502_add_cycles(p_state_6502, delta);
        v = p_memory_access->memory_read_callback(
            p_memory_access->p_callback_obj, addr);
        countdown = timing_get_countdown(p_timing);
      }
      if (opmem == k_rw) {
        opreg = k_v;
      }
    }

    switch (optype) {
    case k_kil:
      switch (opcode) {
      case 0x02: /* EXIT */
        return ((y << 16) | (x << 8) | a);
      case 0xf2: /* CRASH */
      {
        volatile unsigned char* p_crash_ptr = (volatile unsigned char*) 0xdead;
        (void) *p_crash_ptr;
      }
      default:
        assert(0);
      }
      break;
    case k_adc:
      temp_int = (a + v + cf);
      if (df) {
        /* Fix up decimal carry on first nibble. */
        /* TODO: incorrect for invalid large BCD numbers, double carries? */
        int decimal_carry = ((a & 0x0f) + (v & 0x0f) + cf);
        if (decimal_carry >= 0x0a) {
          temp_int += 0x06;
        }
      }
      /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */
      of = !!((a ^ temp_int) & (v ^ temp_int) & 0x80);
      if (df) {
        /* In decimal mode, NZ flags are based on this interim value. */
        v = temp_int;
        opreg = k_v;
        if (temp_int >= 0xa0) {
          temp_int += 0x60;
        }
      }
      a = temp_int;
      cf = !!(temp_int & 0x100);
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
      } else {
        /* IRQ. Undo the PC increment. */
        pc -= 2;
      }
      p_stack[s--] = (pc >> 8);
      p_stack[s--] = (pc & 0xff);
      v = interp_get_flags(zf, nf, cf, of, df, intf);
      v |= (temp_u8 | (1 << k_flag_always_set));
      p_stack[s--] = v;
      pc = (p_mem_read[do_irq_vector] |
            (p_mem_read[(uint16_t) (do_irq_vector + 1)] << 8));
      intf = 1;
      do_irq_vector = 0;
      break;
    case k_bvc: branch = (of == 0); break;
    case k_bvs: branch = (of == 1); break;
    case k_clc: cf = 0; break;
    case k_cld: df = 0; break;
    case k_cli:
      intf = 0;
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);
      if (!opcode) {
        goto force_opcode;
      }
      break;
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
      temp_addr = (pc - 1);
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
      interp_check_irq(&opcode, &do_irq_vector, p_state_6502, intf);
      if (!opcode) {
        goto force_opcode;
      }
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
      temp_int = (a + (unsigned char) ~v + cf);
      if (df) {
        /* Fix up decimal carry on first nibble. */
        if (((v & 0x0f) + !cf) > (a & 0x0f)) {
          temp_int -= 0x06;
        }
      }
      /* http://www.righto.com/2012/12/the-6502-overflow-flag-explained.html */
      of = !!((a ^ temp_int) & ((unsigned char) ~v ^ temp_int) & 0x80);
      if (df) {
        /* In decimal mode, NZ flags are based on this interim value. */
        v = temp_int;
        opreg = k_v;
        if ((v + !cf) > a) {
          temp_int -= 0x60;
        }
      }
      a = temp_int;
      cf = !!(temp_int & 0x100);
      break;
    case k_sec: cf = 1; break;
    case k_sed: df = 1; break;
    case k_sei: intf = 1; break;
    /* TODO: SHY also issues a read at the uncarried abx address. Also, it
     * always takes 5 cycles (no extra cycle for abx mode calculation.
     */
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
      if (addr < write_callback_above) {
        p_mem_write[addr] = v;
      } else {
        uint64_t delta = timing_update_countdown(p_timing, countdown);
        state_6502_add_cycles(p_state_6502, delta);
        p_memory_access->memory_write_callback(p_memory_access->p_callback_obj,
                                               addr,
                                               v);
        countdown = timing_get_countdown(p_timing);
      }
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
      /* Taken branches take a cycle longer. */
      cycles_this_instruction++;
      temp_addr = pc;
      pc = (pc + (char) v);
      /* If the taken branch crosses a page boundary, it takes a further cycle
       * longer.
       */
      if ((pc ^ temp_addr) & 0x0100) {
        cycles_this_instruction++;
      }
    }

    /* Need to do this all at once, and last. This is so that we fire timer
     * expiries only at the start of the loop and not while we're accessing
     * hardware registers.
     */
    countdown -= cycles_this_instruction;
  }

  flags = interp_get_flags(zf, nf, cf, of, df, intf);
  state_6502_set_registers(p_state_6502, a, x, y, s, flags, pc);

  return (uint32_t) -1;
}

int64_t
interp_single_instruction(struct interp_struct* p_interp, int64_t countdown) {
  uint32_t ret;

  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  struct timing_struct* p_timing = p_interp->p_timing;

  uint64_t delta = timing_update_countdown(p_timing, countdown);
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
