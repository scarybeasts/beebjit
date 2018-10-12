#include "interp.h"

#include "opdefs.h"
#include "state_6502.h"

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct interp_struct {
  struct state_6502* p_state_6502;
  unsigned char* p_mem;
};

struct interp_struct*
interp_create(struct state_6502* p_state_6502, unsigned char* p_mem) {
  struct interp_struct* p_interp = malloc(sizeof(struct interp_struct));
  if (p_interp == NULL) {
    errx(1, "couldn't allocate interp_struct");
  }
  (void) memset(p_interp, '\0', sizeof(struct interp_struct));

  p_interp->p_state_6502 = p_state_6502;
  p_interp->p_mem = p_mem;

  return p_interp;
}

void
interp_destroy(struct interp_struct* p_interp) {
  free(p_interp);
}

void
interp_enter(struct interp_struct* p_interp) {
  unsigned char opcode;
  unsigned char opmode;
  unsigned char optype;
  unsigned char opreg;
  unsigned char v;
  int branch;

  volatile unsigned char* p_crash_ptr = 0;
  struct state_6502* p_state_6502 = p_interp->p_state_6502;
  unsigned char* p_mem = p_interp->p_mem;
  uint16_t pc = p_state_6502->reg_pc;
  unsigned char a = p_state_6502->reg_a;
  unsigned char x = p_state_6502->reg_x;
  unsigned char y = p_state_6502->reg_y;
  unsigned char flags = p_state_6502->reg_flags;
  unsigned char zf = ((flags & (1 << k_flag_zero)) != 0);
  unsigned char nf = ((flags & (1 << k_flag_negative)) != 0);

  while (1) {
    branch = 0;
    opcode = p_mem[pc++];
    opmode = g_opmodes[opcode];
    optype = g_optypes[opcode];
    opreg = g_optype_sets_register[optype];
    switch (opmode) {
    case k_nil:
      break;
    case k_imm:
    case k_rel:
      v = p_mem[pc++];
      break;
    default:
      assert(0);
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
    case k_beq: branch = (zf == 0); break;
    case k_bmi: branch = (nf == 1); break;
    case k_bne: branch = (zf == 1); break;
    case k_bpl: branch = (nf == 0); break;
    case k_lda: a = v; break;
    case k_nop: break;
    case k_ora: a |= v; break;
    case k_tax: x = a; break;
    case k_tay: y = a; break;
    case k_txa: a = x; break;
    case k_tya: a = y; break;
    default:
      assert(0);
    }

    switch (opreg) {
    case 0: break;
    case k_a: zf = (a == 0); nf = ((a & 0x80) != 0); break;
    case k_x: zf = (x == 0); nf = ((x & 0x80) != 0); break;
    case k_y: zf = (y == 0); nf = ((y & 0x80) != 0); break;
    default:
      assert(0);
    }

    if (branch) {
      pc = (pc + (char) v);
    }
  }
}
