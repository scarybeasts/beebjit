#include "via.h"

#include "bbc.h"
#include "sound.h"
#include "state_6502.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct via_struct {
  int id;
  struct bbc_struct* p_bbc;

  unsigned char ORB;
  unsigned char ORA;
  unsigned char DDRB;
  unsigned char DDRA;
  unsigned char SR;
  unsigned char ACR;
  unsigned char PCR;
  unsigned char IFR;
  unsigned char IER;
  unsigned char peripheral_b;
  unsigned char peripheral_a;
  int T1C;
  int T1L;
  int T2C;
  int T2L;
  unsigned char t1_oneshot_fired;
  unsigned char t2_oneshot_fired;
};

struct via_struct*
via_create(int id, struct bbc_struct* p_bbc) {
  struct via_struct* p_via = malloc(sizeof(struct via_struct));
  if (p_via == NULL) {
    errx(1, "cannot allocate via_struct");
  }
  memset(p_via, '\0', sizeof(struct via_struct));

  p_via->id = id;
  p_via->p_bbc = p_bbc;

  /* We initialize the OR* / DDR* registers to 0. This matches jsbeeb and
   * differs from b-em, which sets them to 0xff.
   * This doesn't really matter because the OS will initilize them anyway.
   * I think jsbeeb could be correct because it cites a 1977 data sheet,
   * http://archive.6502.org/datasheets/mos_6522_preliminary_nov_1977.pdf
   */

  p_via->T1C = 0xffff;
  p_via->T1L = 0xffff;
  p_via->T2C = 0xffff;
  p_via->T2L = 0xffff;

  /* From the above data sheet:
   * "The interval timer one-shot mode allows generation of a single interrupt
   * for each timer load operation."
   * It's unclear whether "power on" / "reset" counts as an effective timer
   * load or not. Let's copy jsbeeb and b-em and say that it does not.
   */
  p_via->t1_oneshot_fired = 1;
  p_via->t2_oneshot_fired = 1;

  return p_via;
}

void
via_destroy(struct via_struct* p_via) {
  free(p_via);
}

static void
sysvia_update_port_a(struct via_struct* p_via) {
  struct bbc_struct* p_bbc = p_via->p_bbc;
  unsigned char sdb = p_via->peripheral_a;
  unsigned char keyrow = ((sdb >> 4) & 7);
  unsigned char keycol = (sdb & 0xf);
  int fire = 0;
  if (!(p_via->peripheral_b & 0x08)) {
    if (!bbc_is_key_pressed(p_bbc, keyrow, keycol)) {
      p_via->peripheral_a &= 0x7f;
    }
    if (bbc_is_key_column_pressed(p_bbc, keycol)) {
      fire = 1;
    }
  } else {
    if (bbc_is_any_key_pressed(p_bbc)) {
      fire = 1;
    }
  }
  if (fire) {
    via_raise_interrupt(p_via, k_int_CA2);
  }
}

static unsigned char
via_read_port_a(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    sysvia_update_port_a(p_via);
    return p_via->peripheral_a;
  } else if (p_via->id == k_via_user) {
    /* Printer port, write only. */
    return 0xff;
  }
  assert(0);
}

static void
via_write_port_a(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    unsigned char ora = p_via->ORA;
    unsigned char ddra = p_via->DDRA;
    unsigned char port_val = ((ora & ddra) | ~ddra);
    p_via->peripheral_a = port_val;
    sysvia_update_port_a(p_via);
  } else if (p_via->id == k_via_user) {
    /* Printer port. Ignore. */
  } else {
    assert(0);
  }
}

static unsigned char
via_read_port_b(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    /* Read is for joystick and CMOS. 0xff means nothing. */
    return 0xff;
  } else if (p_via->id == k_via_user) {
    /* Read is for joystick, mouse, user port. 0xff means nothing. */
    return 0xff;
  }
  assert(0);
}

static void
via_write_port_b(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    unsigned char orb = p_via->ORB;
    unsigned char ddrb = p_via->DDRB;
    unsigned char port_val = ((orb & ddrb) | ~ddrb);
    unsigned char port_bit = (1 << (port_val & 7));
    int bit_set = ((port_val & 0x08) == 0x08);
    if (bit_set) {
      p_via->peripheral_b |= port_bit;
    } else {
      p_via->peripheral_b &= ~port_bit;
    }
    if (port_bit == 1) {
      struct sound_struct* p_sound = bbc_get_sound(p_via->p_bbc);
      sound_apply_write_bit_and_data(p_sound, bit_set, p_via->peripheral_a);
    }
  } else if (p_via->id == k_via_user) {
    /* User port. Ignore. */
  } else {
    assert(0);
  }
}

unsigned char
via_read(struct via_struct* p_via, size_t reg) {
  unsigned char ora;
  unsigned char orb;
  unsigned char ddra;
  unsigned char ddrb;
  unsigned char val;
  unsigned char port_val;

  switch (reg) {
  case k_via_ORB:
    assert((p_via->PCR & 0xa0) != 0x20);
    assert(!(p_via->ACR & 0x02));
    orb = p_via->ORB;
    ddrb = p_via->DDRB;
    val = (orb & ddrb);
    port_val = via_read_port_b(p_via);
    val |= (port_val & ~ddrb);
    return val;
  case k_via_ORA:
    assert((p_via->PCR & 0x0a) != 0x02);
    via_clear_interrupt(p_via, k_int_CA1);
    via_clear_interrupt(p_via, k_int_CA2);
    /* Fall through. */
  case k_via_ORAnh:
    assert(!(p_via->ACR & 0x01));
    ora = p_via->ORA;
    ddra = p_via->DDRA;
    val = (ora & ddra);
    port_val = via_read_port_a(p_via);
    val |= (port_val & ~ddra);
    return val;
  case k_via_DDRB:
    return p_via->DDRB;
  case k_via_T1CL:
    via_clear_interrupt(p_via, k_int_TIMER1);
    via_time_advance(p_via, 1);
    return (p_via->T1C & 0xff);
  case k_via_T1CH:
    return (p_via->T1C >> 8);
  case k_via_T1LL:
    return (p_via->T1L & 0xff);
  case k_via_T1LH:
    return (p_via->T1L >> 8);
  case k_via_T2CL:
    via_clear_interrupt(p_via, k_int_TIMER2);
    via_time_advance(p_via, 1);
    return (p_via->T2C & 0xff);
  case k_via_T2CH:
    return (p_via->T2C >> 8);
  case k_via_SR:
    return p_via->SR;
  case k_via_ACR:
    return p_via->ACR;
  case k_via_PCR:
    return p_via->PCR;
  case k_via_IFR:
    return p_via->IFR;
  case k_via_IER:
    return (p_via->IER | 0x80);
  default:
    printf("unhandled VIA read %zu\n", reg);
    assert(0);
  }
}

void
via_write(struct via_struct* p_via, size_t reg, unsigned char val) {
  switch (reg) {
  case k_via_ORB:
    assert((p_via->PCR & 0xa0) != 0x20);
    assert((p_via->PCR & 0xe0) != 0x80);
    assert((p_via->PCR & 0xe0) != 0xa0);
    p_via->ORB = val;
    via_write_port_b(p_via);
    break;
  case k_via_ORA:
    assert((p_via->PCR & 0x0a) != 0x02);
    assert((p_via->PCR & 0x0e) != 0x08);
    assert((p_via->PCR & 0x0e) != 0x0a);
    p_via->ORA = val;
    via_write_port_a(p_via);
    break;
  case k_via_DDRB:
    p_via->DDRB = val;
    via_write_port_b(p_via);
    break;
  case k_via_DDRA:
    p_via->DDRA = val;
    via_write_port_a(p_via);
    break;
  case k_via_T1CL:
  case k_via_T1LL:
    /* Not an error: writing to either T1CL or T1LL updates just T1LL. */
    p_via->T1L = ((p_via->T1L & 0xff00) | val);
    break;
  case k_via_T1CH:
    if ((p_via->ACR & 0xc0) == 0x80) {
      /* For one-shot timers with PB7 enabled, the output starts low. */
      p_via->ORB &= ~0x80;
    }
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xff));
    p_via->T1C = p_via->T1L;
    p_via->t1_oneshot_fired = 0;
    via_clear_interrupt(p_via, k_int_TIMER1);
    break;
  case k_via_T1LH:
    /* TODO: clear timer interrupt if acr & 0x40. */
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xff));
    break;
  case k_via_T2CL:
    p_via->T2L = ((p_via->T2L & 0xff00) | val);
    break;
  case k_via_T2CH:
    p_via->T2L = ((val << 8) | (p_via->T2L & 0xff));
    p_via->T2C = p_via->T2L;
    p_via->t2_oneshot_fired = 0;
    via_clear_interrupt(p_via, k_int_TIMER2);
    break;
  case k_via_SR:
    p_via->SR = val;
    break;
  case k_via_ACR:
    p_via->ACR = val;
    /*printf("new via %d ACR %x\n", p_via->id, val);*/
    break;
  case k_via_PCR:
    p_via->PCR = val;
    /*printf("new via %d PCR %x\n", p_via->id, val);*/
    break;
  case k_via_IFR:
    p_via->IFR &= ~(val & 0x7f);
    via_check_interrupt(p_via);
    break;
  case k_via_IER:
    if (val & 0x80) {
      p_via->IER |= (val & 0x7f);
    } else {
      p_via->IER &= ~(val & 0x7f);
    }
    via_check_interrupt(p_via);
/*    printf("new sysvia IER %x\n", p_bbc->sysvia_IER);*/
    break;
  case k_via_ORAnh:
    p_via->ORA = val;
    via_write_port_a(p_via);
    break;
  default:
    printf("unhandled VIA write %zu\n", reg);
    assert(0);
    break;
  }
}

void
via_raise_interrupt(struct via_struct* p_via, unsigned char val) {
  assert(!(val & 0x80));
  p_via->IFR |= val;
  via_check_interrupt(p_via);
}

void
via_clear_interrupt(struct via_struct* p_via, unsigned char val) {
  assert(!(val & 0x80));
  p_via->IFR &= ~val;
  via_check_interrupt(p_via);
}

void
via_check_interrupt(struct via_struct* p_via) {
  int level;
  int interrupt;
  struct state_6502* p_state_6502 = bbc_get_6502(p_via->p_bbc);

  assert(!(p_via->IER & 0x80));

  if (p_via->IER & p_via->IFR) {
    p_via->IFR |= 0x80;
    level = 1;
  } else {
    p_via->IFR &= ~0x80;
    level = 0;
  }
  if (p_via->id == k_via_system) {
    interrupt = k_state_6502_irq_1;
  } else {
    interrupt = k_state_6502_irq_2;
  }
  state_6502_set_irq_level(p_state_6502, interrupt, level);
}

void
via_get_registers(struct via_struct* p_via,
                  unsigned char* ORA,
                  unsigned char* ORB,
                  unsigned char* DDRA,
                  unsigned char* DDRB,
                  unsigned char* SR,
                  unsigned char* ACR,
                  unsigned char* PCR,
                  unsigned char* IFR,
                  unsigned char* IER,
                  unsigned char* peripheral_a,
                  unsigned char* peripheral_b,
                  int* T1C,
                  int* T1L,
                  int* T2C,
                  int* T2L,
                  unsigned char* t1_oneshot_fired,
                  unsigned char* t2_oneshot_fired) {
  *ORA = p_via->ORA;
  *ORB = p_via->ORB;
  *DDRA = p_via->DDRA;
  *DDRB = p_via->DDRB;
  *SR = p_via->SR;
  *ACR = p_via->ACR;
  *PCR = p_via->PCR;
  *IFR = p_via->IFR;
  *IER = p_via->IER;
  *peripheral_a = p_via->peripheral_a;
  *peripheral_b = p_via->peripheral_b;
  *T1C = p_via->T1C;
  *T1L = p_via->T1L;
  *T2C = p_via->T2C;
  *T2L = p_via->T2L;
  *t1_oneshot_fired = p_via->t1_oneshot_fired;
  *t2_oneshot_fired = p_via->t2_oneshot_fired;
}

void via_set_registers(struct via_struct* p_via,
                       unsigned char ORA,
                       unsigned char ORB,
                       unsigned char DDRA,
                       unsigned char DDRB,
                       unsigned char SR,
                       unsigned char ACR,
                       unsigned char PCR,
                       unsigned char IFR,
                       unsigned char IER,
                       unsigned char peripheral_a,
                       unsigned char peripheral_b,
                       int T1C,
                       int T1L,
                       int T2C,
                       int T2L,
                       unsigned char t1_oneshot_fired,
                       unsigned char t2_oneshot_fired) {
  p_via->ORA = ORA;
  p_via->ORB = ORB;
  p_via->DDRA = DDRA;
  p_via->DDRB = DDRB;
  p_via->SR = SR;
  p_via->ACR = ACR;
  p_via->PCR = PCR;
  p_via->IFR = IFR;
  p_via->IER = IER;
  p_via->peripheral_a = peripheral_a;
  p_via->peripheral_b = peripheral_b;
  p_via->T1C = T1C;
  p_via->T1L = T1L;
  p_via->T2C = T2C;
  p_via->T2L = T2L;
  p_via->t1_oneshot_fired = t1_oneshot_fired;
  p_via->t2_oneshot_fired = t2_oneshot_fired;
}

unsigned char*
via_get_peripheral_b_ptr(struct via_struct* p_via) {
  return &p_via->peripheral_b;
}

void
via_time_advance(struct via_struct* p_via, size_t us) {
  p_via->T1C -= us;
  if (p_via->T1C < 0) {
    if (!p_via->t1_oneshot_fired) {
      via_raise_interrupt(p_via, k_int_TIMER1);
      if (p_via->ACR & 0x80) {
        /* If PB7 output mode is active, toggle PB7. */
        p_via->ORB ^= 0x80;
      }
    }
    /* If we're in one-shot mode, flag the timer hit so we don't assert an
     * interrupt again until T1CH has been re-written.
     */
    if (!(p_via->ACR & 0x40)) {
      p_via->t1_oneshot_fired = 1;
    }
    while (p_via->T1C < 0) {
      p_via->T1C += (p_via->T1L + 1);
    }
  }

  /* If TIMER2 is in pulse counting mode, it doesn't decrement. */
  if (p_via->ACR & 0x20) {
    return;
  }

  p_via->T2C -= us;
  if (p_via->T2C < 0) {
    if (!p_via->t2_oneshot_fired) {
      via_raise_interrupt(p_via, k_int_TIMER2);
    }
    p_via->t2_oneshot_fired = 1;
    while (p_via->T2C < 0) {
      /* NOTE: I'm suspicious of this value's correctness. It's copied from
       * jsbeeb and is effectively 65536us. I think corresponds to an effective
       * TIMER2 latch value of 0xFFFF, and a symmetrical arrangement with
       * jsbeeb's TIMER1 would involve an increment of "latch + 2us", or
       * 0x10001. I don't have a real BBC to test.
       */
      p_via->T2C += 0x10000;
    }
  }
}
