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
  int externally_clocked;
  struct bbc_struct* p_bbc;
  struct timing_struct* p_timing;

  uint8_t ORB;
  uint8_t ORA;
  uint8_t DDRB;
  uint8_t DDRA;
  uint8_t SR;
  uint8_t ACR;
  uint8_t PCR;
  uint8_t IFR;
  uint8_t IER;
  uint8_t peripheral_b;
  uint8_t peripheral_a;
  int32_t T1C;
  int32_t T1L;
  int32_t T2C;
  int32_t T2L;
  uint8_t t1_oneshot_fired;
  uint8_t t2_oneshot_fired;
  uint8_t t1_pb7;
};

struct via_struct*
via_create(int id,
           int externally_clocked,
           struct timing_struct* p_timing,
           struct bbc_struct* p_bbc) {
  struct via_struct* p_via = malloc(sizeof(struct via_struct));
  if (p_via == NULL) {
    errx(1, "cannot allocate via_struct");
  }
  (void) memset(p_via, '\0', sizeof(struct via_struct));

  p_via->id = id;
  p_via->externally_clocked = externally_clocked;
  p_via->p_bbc = p_bbc;
  p_via->p_timing = p_timing;

  /* EMU NOTE:
   * We initialize the OR* / DDR* registers to 0. This matches jsbeeb and
   * differs from b-em, which sets them to 0xFF.
   * I think jsbeeb could be correct because it cites a 1977 data sheet,
   * http://archive.6502.org/datasheets/mos_6522_preliminary_nov_1977.pdf
   * And indeed, testing on a real beeb shows jsbeeb is correct:
   * https://stardot.org.uk/forums/viewtopic.php?f=4&t=16081
   */
  p_via->DDRA = 0;
  p_via->DDRB = 0;
  p_via->ORA = 0;
  p_via->ORB = 0;

  p_via->T1C = 0xFFFF;
  p_via->T1L = 0xFFFF;
  p_via->T2C = 0xFFFF;
  p_via->T2L = 0xFFFF;

  /* From the above data sheet:
   * "The interval timer one-shot mode allows generation of a single interrupt
   * for each timer load operation."
   * It's unclear whether "power on" / "reset" counts as an effective timer
   * load or not. Let's copy jsbeeb and b-em and say that it does not.
   */
  p_via->t1_oneshot_fired = 1;
  p_via->t2_oneshot_fired = 1;

  /* EMU NOTE: needs to be initialized to 1 otherwise Planetoid doesn't run. */
  p_via->t1_pb7 = 1;

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
      p_via->peripheral_a &= 0x7F;
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

static uint8_t
via_read_port_a(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    sysvia_update_port_a(p_via);
    return p_via->peripheral_a;
  } else if (p_via->id == k_via_user) {
    /* Printer port, write only. */
    return 0xFF;
  }
  assert(0);
  return 0;
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

static uint8_t
via_read_port_b(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    /* Read is for joystick and CMOS. 0xFF means nothing. */
    return 0xFF;
  } else if (p_via->id == k_via_user) {
    /* Read is for joystick, mouse, user port. 0xFF means nothing. */
    return 0xFF;
  }
  assert(0);
  return 0;
}

static void
via_write_port_b(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    uint8_t old_peripheral_b = p_via->peripheral_b;
    uint8_t orb = p_via->ORB;
    uint8_t ddrb = p_via->DDRB;
    uint8_t port_val = ((orb & ddrb) | ~ddrb);
    uint8_t port_bit = (1 << (port_val & 7));
    int bit_set = ((port_val & 0x08) == 0x08);
    if (bit_set) {
      p_via->peripheral_b |= port_bit;
    } else {
      p_via->peripheral_b &= ~port_bit;
    }
    /* If we're pulling the sound write bit from low to high, send the data
     * value in ORA along to the sound chip.
     */
    if ((port_bit == 1) && bit_set && !(old_peripheral_b & 1)) {
      struct sound_struct* p_sound = bbc_get_sound(p_via->p_bbc);
      sound_sn_write(p_sound, p_via->peripheral_a);
    }
  } else if (p_via->id == k_via_user) {
    /* User port. Ignore. */
  } else {
    assert(0);
  }
}

uint8_t
via_read(struct via_struct* p_via, uint8_t reg) {
  uint8_t ora;
  uint8_t orb;
  uint8_t ddra;
  uint8_t ddrb;
  uint8_t val;
  uint8_t port_val;

  switch (reg) {
  case k_via_ORB:
    assert((p_via->PCR & 0xA0) != 0x20);
    assert(!(p_via->ACR & 0x02));
    orb = p_via->ORB;
    ddrb = p_via->DDRB;
    val = (orb & ddrb);
    port_val = via_read_port_b(p_via);
    val |= (port_val & ~ddrb);
    /* EMU NOTE: PB7 toggling is actually a mix-in of a separately maintained
     * bit, and it's mixed in to both IRB and ORB.
     * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=16081
     */
    if (p_via->ACR & 0x80) {
      val &= 0x7F;
      val |= (p_via->t1_pb7 << 7);
    }
    return val;
  case k_via_ORA:
    assert((p_via->PCR & 0x0A) != 0x02);
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
    return (p_via->T1C & 0xFF);
  case k_via_T1CH:
    return (p_via->T1C >> 8);
  case k_via_T1LL:
    return (p_via->T1L & 0xFF);
  case k_via_T1LH:
    return (p_via->T1L >> 8);
  case k_via_T2CL:
    via_clear_interrupt(p_via, k_int_TIMER2);
    via_time_advance(p_via, 1);
    return (p_via->T2C & 0xFF);
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
    printf("unhandled VIA read %u\n", reg);
    break;
  }
  assert(0);
  return 0;
}

void
via_write(struct via_struct* p_via, uint8_t reg, uint8_t val) {
  switch (reg) {
  case k_via_ORB:
    assert((p_via->PCR & 0xA0) != 0x20);
    assert((p_via->PCR & 0xE0) != 0x80);
    assert((p_via->PCR & 0xE0) != 0xA0);
    p_via->ORB = val;
    via_write_port_b(p_via);
    break;
  case k_via_ORA:
    assert((p_via->PCR & 0x0A) != 0x02);
    assert((p_via->PCR & 0x0E) != 0x08);
    assert((p_via->PCR & 0x0E) != 0x0A);
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
    p_via->T1L = ((p_via->T1L & 0xFF00) | val);
    break;
  case k_via_T1CH:
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xFF));
    p_via->T1C = p_via->T1L;
    p_via->t1_oneshot_fired = 0;
    p_via->t1_pb7 = 0;
    via_clear_interrupt(p_via, k_int_TIMER1);
    break;
  case k_via_T1LH:
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xFF));
    /* EMU NOTE: clear interrupt as per 6522 data sheet.
     * Behavior validated on a real BBC.
     * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=16251
     * Other emulators (b-em, jsbeeb) were only clearing the interrupt when in
     * timer continuous mode, but testing on a real BBC shows it should be
     * cleared always.
     */
    via_clear_interrupt(p_via, k_int_TIMER1);
    break;
  case k_via_T2CL:
    p_via->T2L = ((p_via->T2L & 0xFF00) | val);
    break;
  case k_via_T2CH:
    p_via->T2L = ((val << 8) | (p_via->T2L & 0xFF));
    p_via->T2C = p_via->T2L;
    p_via->t2_oneshot_fired = 0;
    via_clear_interrupt(p_via, k_int_TIMER2);
    break;
  case k_via_SR:
    p_via->SR = val;
    break;
  case k_via_ACR:
    p_via->ACR = val;
    /* EMU NOTE: some emulators re-arm timers when ACR is written to certain
     * modes but after some testing on a real beeb, we don't do anything
     * special here.
     * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=16252
     */
    /*printf("new via %d ACR %x\n", p_via->id, val);*/
    break;
  case k_via_PCR:
    p_via->PCR = val;
    /*printf("new via %d PCR %x\n", p_via->id, val);*/
    break;
  case k_via_IFR:
    p_via->IFR &= ~(val & 0x7F);
    via_check_interrupt(p_via);
    break;
  case k_via_IER:
    if (val & 0x80) {
      p_via->IER |= (val & 0x7F);
    } else {
      p_via->IER &= ~(val & 0x7F);
    }
    via_check_interrupt(p_via);
/*    printf("new sysvia IER %x\n", p_bbc->sysvia_IER);*/
    break;
  case k_via_ORAnh:
    p_via->ORA = val;
    via_write_port_a(p_via);
    break;
  default:
    printf("unhandled VIA write %u\n", reg);
    assert(0);
    break;
  }
}

void
via_raise_interrupt(struct via_struct* p_via, uint8_t val) {
  assert(!(val & 0x80));
  p_via->IFR |= val;
  via_check_interrupt(p_via);
}

void
via_clear_interrupt(struct via_struct* p_via, uint8_t val) {
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
                  uint8_t* p_ORA,
                  uint8_t* p_ORB,
                  uint8_t* p_DDRA,
                  uint8_t* p_DDRB,
                  uint8_t* p_SR,
                  uint8_t* p_ACR,
                  uint8_t* p_PCR,
                  uint8_t* p_IFR,
                  uint8_t* p_IER,
                  uint8_t* p_peripheral_a,
                  uint8_t* p_peripheral_b,
                  int32_t* p_T1C,
                  int32_t* p_T1L,
                  int32_t* p_T2C,
                  int32_t* p_T2L,
                  uint8_t* p_t1_oneshot_fired,
                  uint8_t* p_t2_oneshot_fired,
                  uint8_t* p_t1_pb7) {
  *p_ORA = p_via->ORA;
  *p_ORB = p_via->ORB;
  *p_DDRA = p_via->DDRA;
  *p_DDRB = p_via->DDRB;
  *p_SR = p_via->SR;
  *p_ACR = p_via->ACR;
  *p_PCR = p_via->PCR;
  *p_IFR = p_via->IFR;
  *p_IER = p_via->IER;
  *p_peripheral_a = p_via->peripheral_a;
  *p_peripheral_b = p_via->peripheral_b;
  *p_T1C = p_via->T1C;
  *p_T1L = p_via->T1L;
  *p_T2C = p_via->T2C;
  *p_T2L = p_via->T2L;
  *p_t1_oneshot_fired = p_via->t1_oneshot_fired;
  *p_t2_oneshot_fired = p_via->t2_oneshot_fired;
  *p_t1_pb7 = p_via->t1_pb7;
}

void via_set_registers(struct via_struct* p_via,
                       uint8_t ORA,
                       uint8_t ORB,
                       uint8_t DDRA,
                       uint8_t DDRB,
                       uint8_t SR,
                       uint8_t ACR,
                       uint8_t PCR,
                       uint8_t IFR,
                       uint8_t IER,
                       uint8_t peripheral_a,
                       uint8_t peripheral_b,
                       int32_t T1C,
                       int32_t T1L,
                       int32_t T2C,
                       int32_t T2L,
                       uint8_t t1_oneshot_fired,
                       uint8_t t2_oneshot_fired,
                       uint8_t t1_pb7) {
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
  p_via->t1_pb7 = t1_pb7;
}

uint8_t*
via_get_peripheral_b_ptr(struct via_struct* p_via) {
  return &p_via->peripheral_b;
}

void
via_time_advance(struct via_struct* p_via, uint64_t us) {
  p_via->T1C -= us;
  if (p_via->T1C < 0) {
    if (!p_via->t1_oneshot_fired) {
      via_raise_interrupt(p_via, k_int_TIMER1);
      /* EMU NOTE: PB7 is maintained regardless of whether PB7 mode is active.
       * Confirmed on a real beeb.
       * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=16263
       */
      p_via->t1_pb7 = !p_via->t1_pb7;
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
