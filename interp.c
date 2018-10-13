#include "interp.h"

#include "bbc_options.h"
#include "opdefs.h"
#include "state_6502.h"

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_v = 4,
};

struct interp_struct {
  struct state_6502* p_state_6502;
  unsigned char* p_mem;
  struct bbc_options* p_options;
};

struct interp_struct*
interp_create(struct state_6502* p_state_6502,
              unsigned char* p_mem,
              struct bbc_options* p_options) {
  struct interp_struct* p_interp = malloc(sizeof(struct interp_struct));
  if (p_interp == NULL) {
    errx(1, "couldn't allocate interp_struct");
  }
  (void) memset(p_interp, '\0', sizeof(struct interp_struct));

  p_interp->p_state_6502 = p_state_6502;
  p_interp->p_mem = p_mem;
  p_interp->p_options = p_options;

  return p_interp;
}

void
interp_destroy(struct interp_struct* p_interp) {
  free(p_interp);
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
  uint16_t temp_addr;

  volatile unsigned char* p_crash_ptr = 0;
  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  struct bbc_options* p_options = p_interp->p_options;
  unsigned char* p_mem = p_interp->p_mem;
  unsigned char* p_stack = p_mem + k_6502_stack_addr;
  void* (*debug_callback)(void*) = 0;

  state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
  zf = ((flags & (1 << k_flag_zero)) != 0);
  nf = ((flags & (1 << k_flag_negative)) != 0);
  cf = ((flags & (1 << k_flag_carry)) != 0);
  of = ((flags & (1 << k_flag_overflow)) != 0);
  df = ((flags & (1 << k_flag_decimal)) != 0);
  intf = ((flags & (1 << k_flag_interrupt)) != 0);

  (void) df;
  (void) intf;

  if (p_options->debug) {
    debug_callback = p_options->debug_callback;
  }

  while (1) {
    if (debug_callback) {
      state_6502_set_registers(p_state_6502, a, x, y, s, flags, pc);
      debug_callback(p_options->p_debug_callback_object);
      state_6502_get_registers(p_state_6502, &a, &x, &y, &s, &flags, &pc);
    }
    branch = 0;
    opcode = p_mem[pc++];
    opmode = g_opmodes[opcode];
    optype = g_optypes[opcode];
    opreg = g_optype_sets_register[optype];
    opmem = g_opmem[optype];
    switch (opmode) {
    case k_nil:
      break;
    case k_acc:
      opreg = k_a;
      opmem = k_nomem;
      v = a;
      break;
    case k_imm:
    case k_rel:
      v = p_mem[pc++];
      opmem = k_nomem;
      break;
    case k_zpg:
      addr = p_mem[pc++];
      break;
    case k_abs:
      addr = (p_mem[pc] | (p_mem[(uint16_t) (pc + 1)] << 8));
      pc += 2;
      break;
    case k_zpx:
      addr = p_mem[pc++];
      addr += x;
      addr &= 0xff;
      break;
    case k_zpy:
      addr = p_mem[pc++];
      addr += y;
      addr &= 0xff;
      break;
    case k_abx:
      addr = (p_mem[pc] | (p_mem[(uint16_t) (pc + 1)] << 8));
      addr += x;
      pc += 2;
      break;
    case k_aby:
      addr = (p_mem[pc] | (p_mem[(uint16_t) (pc + 1)] << 8));
      addr += y;
      pc += 2;
      break;
    case k_ind:
      addr = (p_mem[pc] | (p_mem[(uint16_t) (pc + 1)] << 8));
      pc += 2;
      v = p_mem[addr];
      /* Indirect fetches wrap at page boundaries. */
      if ((addr & 0xff) == 0xff) {
        addr &= 0xff00;
      } else {
        addr++;
      }
      addr = (v | (p_mem[addr] << 8));
      break;
    case k_idx:
      v = p_mem[pc++];
      v += x;
      addr = p_mem[v];
      v++;
      addr |= (p_mem[v] << 8);
      break;
    case k_idy:
      v = p_mem[pc++];
      addr = p_mem[v];
      v++;
      addr |= (p_mem[v] << 8);
      addr += y;
      break;
    default:
      assert(0);
    }

    if (opmem == k_read || opmem == k_rw) {
      v = p_mem[addr];
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
    case k_and: a &= v; break;
    case k_asl: cf = !!(v & 0x80); v <<= 1; break;
    case k_beq: branch = (zf == 0); break;
    case k_bcc: branch = (cf == 0); break;
    case k_bcs: branch = (cf == 1); break;
    case k_bmi: branch = (nf == 1); break;
    case k_bne: branch = (zf == 1); break;
    case k_bpl: branch = (nf == 0); break;
    case k_bvc: branch = (of == 0); break;
    case k_bvs: branch = (of == 1); break;
    case k_clc: cf = 0; break;
    case k_cld: df = 0; break;
    case k_cli: intf = 0; break;
    case k_clv: of = 0; break;
    case k_cmp: cf = (a >= v); v = (v - a); opreg = k_v; break;
    case k_cpx: cf = (x >= v); v = (v - x); opreg = k_v; break;
    case k_cpy: cf = (y >= v); v = (v - y); opreg = k_v; break;
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
      v = ((1 << k_flag_brk) | (1 << k_flag_always_set));
      v |= (cf << k_flag_carry);
      v |= (zf << k_flag_zero);
      v |= (intf << k_flag_interrupt);
      v |= (df << k_flag_decimal);
      v |= (of << k_flag_overflow);
      v |= (nf << k_flag_negative);
      p_stack[s--] = v;
      break;
    case k_pla: a = p_stack[++s]; break;
    case k_ora: a |= v; break;
    case k_rol: tmpf = cf; cf = !!(v & 0x80); v <<= 1; v |= tmpf; break;
    case k_ror: tmpf = cf; cf = (v & 0x01); v >>= 1; v |= (tmpf << 7); break;
    case k_sec: cf = 1; break;
    case k_sed: df = 1; break;
    case k_sei: intf = 1; break;
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
      assert(0);
    }

    if (opmem == k_write || opmem == k_rw) {
      p_mem[addr] = v;
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
      pc = (pc + (char) v);
    }
  }
}
