#include "via.h"

#include "bbc.h"
#include "sound.h"
#include "state_6502.h"
#include "timing.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_via_tick_rate = 1000000; /* 1Mhz */

struct via_struct {
  int id;
  int externally_clocked;
  struct bbc_struct* p_bbc;
  struct timing_struct* p_timing;
  size_t t1_timer_id;
  size_t t2_timer_id;

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
  uint16_t T1L;
  uint16_t T2L;
  uint8_t t1_pb7;
};

static void
via_set_t1c_raw(struct via_struct* p_via, int32_t val) {
  size_t id = p_via->t1_timer_id;
  /* Add 2 to val because VIA timers fire at -1 and timing_* fires at 0, and
   * raw deals in 2Mhz cycles.
   */
  val += 2;
  (void) timing_set_timer_value(p_via->p_timing, id, val);
}

static void
via_set_t1c(struct via_struct* p_via, int32_t val) {
  via_set_t1c_raw(p_via, (val << 1));
}

static int32_t
via_get_t1c_raw(struct via_struct* p_via) {
  size_t id = p_via->t1_timer_id;
  int64_t val = timing_get_timer_value(p_via->p_timing, id);

  val -= 2;

  /* If interrupts aren't firing, the timer will decrement indefinitely so we
   * have to fix it up with all of the re-latches.
   */
  if (val < -2) {
    /* T1 (latch 4) counts 4... 3... 2... 1... 0... -1... 4... */
    uint64_t delta = (-val - 4);
    /* TODO: if T1L changed, this is incorrect. */
    uint64_t relatch_cycles = ((p_via->T1L + 2) << 1);
    uint64_t relatches = (delta / relatch_cycles);
    relatches++;
    val += (relatches * relatch_cycles);

    via_set_t1c_raw(p_via, val);
  }

  return val;
}

static int32_t
via_get_t1c(struct via_struct* p_via) {
  int32_t val = via_get_t1c_raw(p_via);
  /* TODO: add assert invariant that accesses are always done VIA mid cycle? */
  val >>= 1;
  return val;
}

static void
via_set_t2c_raw(struct via_struct* p_via, int32_t val) {
  size_t id = p_via->t2_timer_id;
  /* Add 2 to val because VIA timers fire at -1 and timing_* fires at 0, and
   * raw deals in 2Mhz cycles.
   */
  val += 2;
  (void) timing_set_timer_value(p_via->p_timing, id, val);
}

static void
via_set_t2c(struct via_struct* p_via, int32_t val) {
  via_set_t2c_raw(p_via, (val << 1));
}

static int32_t
via_get_t2c_raw(struct via_struct* p_via) {
  size_t id = p_via->t2_timer_id;
  int64_t val = timing_get_timer_value(p_via->p_timing, id);

  val -= 2;

  /* If interrupts aren't firing, the timer will decrement indefinitely so we
   * have to fix it up with all of the re-latches.
   */
  if (val < -2) {
    /* T2 counts 4... 3... 2... 1... 0... FFFF... FFFE... */
    uint64_t delta = (-val - 4);
    uint64_t relatch_cycles = (0x10000 << 1); /* -2 -> 0xFFFE */
    uint64_t relatches = (delta / relatch_cycles);
    relatches++;
    val += (relatches * relatch_cycles);

    via_set_t2c_raw(p_via, val);
  }

  return val;
}

static int32_t
via_get_t2c(struct via_struct* p_via) {
  int32_t val = via_get_t2c_raw(p_via);
  val >>= 1;
  return val;
}

static void
via_do_fire_t1(struct via_struct* p_via) {
  struct timing_struct* p_timing = p_via->p_timing;
  uint32_t timer_id = p_via->t1_timer_id;
  assert(timing_get_firing(p_timing, timer_id));

  via_raise_interrupt(p_via, k_int_TIMER1);
  /* EMU NOTE: PB7 is maintained regardless of whether PB7 mode is active.
   * Confirmed on a real beeb.
   * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=16263
   */
  p_via->t1_pb7 = !p_via->t1_pb7;

  /* If we're in one-shot mode, flag the timer hit so we don't assert an
   * interrupt again until T1CH has been re-written.
   */
  if (!(p_via->ACR & 0x40)) {
    timing_set_firing(p_timing, timer_id, 0);
  } else {
    int64_t delta = (p_via->T1L + 2);
    (void) timing_adjust_timer_value(p_timing, NULL, timer_id, (delta << 1));
  }
}

static void
via_do_fire_t2(struct via_struct* p_via) {
  struct timing_struct* p_timing = p_via->p_timing;
  uint32_t timer_id = p_via->t2_timer_id;
  assert(timing_get_firing(p_timing, timer_id));

  via_raise_interrupt(p_via, k_int_TIMER2);
  timing_set_firing(p_timing, timer_id, 0);
}

static void
via_t1_fired(void* p) {
  struct via_struct* p_via = (struct via_struct*) p;
  int64_t val = via_get_t1c(p_via);

  (void) val;
  assert(val == -1);
  assert(!p_via->externally_clocked);

  via_do_fire_t1(p_via);
}

static void
via_t2_fired(void* p) {
  struct via_struct* p_via = (struct via_struct*) p;
  int64_t val = via_get_t2c(p_via);

  (void) val;
  assert(val == -1);
  assert(!p_via->externally_clocked);
  assert(!(p_via->ACR & 0x20)); /* Shouldn't fire in pulse counting mode. */

  via_do_fire_t2(p_via);
}

static int
via_is_t1_firing(struct via_struct* p_via) {
  int32_t val;
  struct timing_struct* p_timing = p_via->p_timing;
  uint32_t timer_id = p_via->t1_timer_id;
  if (!timing_get_firing(p_timing, timer_id)) {
    return 0;
  }

  val = via_get_t1c_raw(p_via);
  return (val == -1);
}

static int
via_is_t2_firing(struct via_struct* p_via) {
  int32_t val;
  struct timing_struct* p_timing = p_via->p_timing;
  uint32_t timer_id = p_via->t2_timer_id;
  if (!timing_get_firing(p_timing, timer_id)) {
    return 0;
  }

  val = via_get_t2c_raw(p_via);
  return (val == -1);
}

struct via_struct*
via_create(int id,
           int externally_clocked,
           struct timing_struct* p_timing,
           struct bbc_struct* p_bbc) {
  uint32_t t1_timer_id;
  uint32_t t2_timer_id;

  struct via_struct* p_via = malloc(sizeof(struct via_struct));

  if (p_via == NULL) {
    errx(1, "cannot allocate via_struct");
  }
  (void) memset(p_via, '\0', sizeof(struct via_struct));

  p_via->id = id;
  p_via->externally_clocked = externally_clocked;
  p_via->p_bbc = p_bbc;
  p_via->p_timing = p_timing;

  /* Hardcoded assumption that CPU is clocked 2x VIA (2Mhz vs. 1Mhz). */
  assert((k_via_tick_rate * 2) == timing_get_tick_rate(p_timing));

  t1_timer_id = timing_register_timer(p_timing, via_t1_fired, p_via);
  t2_timer_id = timing_register_timer(p_timing, via_t2_fired, p_via);
  p_via->t1_timer_id = t1_timer_id;
  p_via->t2_timer_id = t2_timer_id;

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

  via_set_t1c(p_via, 0xFFFF);
  p_via->T1L = 0xFFFF;
  via_set_t2c(p_via, 0xFFFF);
  p_via->T2L = 0xFFFF;

  /* EMU NOTE: needs to be initialized to 1 otherwise Planetoid doesn't run. */
  p_via->t1_pb7 = 1;

  if (!externally_clocked) {
    timing_start_timer(p_timing, t1_timer_id);
    timing_start_timer(p_timing, t2_timer_id);
  }

  /* From the above data sheet:
   * "The interval timer one-shot mode allows generation of a single interrupt
   * for each timer load operation."
   * It's unclear whether "power on" / "reset" counts as an effective timer
   * load or not. Let's copy jsbeeb and b-em and say that it does not.
   */
  timing_set_firing(p_timing, t1_timer_id, 0);
  timing_set_firing(p_timing, t2_timer_id, 0);

  return p_via;
}

void
via_destroy(struct via_struct* p_via) {
  free(p_via);
}

static void
via_time_advance(struct via_struct* p_via, uint64_t ticks) {
  int32_t t1c;
  int32_t t2c;

  struct timing_struct* p_timing = p_via->p_timing;

  assert(p_via->externally_clocked);

  t1c = via_get_t1c(p_via);
  t1c -= ticks;
  via_set_t1c(p_via, t1c);

  if (t1c < 0) {
    if (timing_get_firing(p_timing, p_via->t1_timer_id)) {
      via_do_fire_t1(p_via);
    }
    t1c = via_get_t1c(p_via);
  }

  /* If TIMER2 is in pulse counting mode, it doesn't decrement. */
  if (p_via->ACR & 0x20) {
    return;
  }

  t2c = via_get_t2c(p_via);
  t2c -= ticks;
  via_set_t2c(p_via, t2c);

  if (t2c < 0) {
    if (timing_get_firing(p_timing, p_via->t2_timer_id)) {
      via_do_fire_t2(p_via);
    }
    t2c = via_get_t2c(p_via);
  }
}

void
via_apply_wall_time_delta(struct via_struct* p_via, uint64_t delta) {
  if (!p_via->externally_clocked) {
    return;
  }
  via_time_advance(p_via, delta);
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
  } else {
    via_clear_interrupt(p_via, k_int_CA2);
  }
}

uint8_t
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

uint8_t
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
  int32_t t1_val;
  int32_t t2_val;

  /* We're at the VIA start-cycle. Will T1/T2 interrupt fire at the mid cycle?
   * Work it out now because we can't tell after advancing the timing.
   */
  int t1_firing = via_is_t1_firing(p_via);
  int t2_firing = via_is_t2_firing(p_via);

  /* Advance to the VIA mid-cycle.
   * EMU NOTE: do this first before processing the read. Interrupts fire at
   * the mid-cycle and the read needs any results from that to be correct.
   * Of note, if an interrupt fires the same VIA cycle as an IFR read, IFR
   * reflects the just-hit interrupt on a real BBC.
   */
  struct timing_struct* p_timing = p_via->p_timing;
  int64_t countdown = timing_get_countdown(p_timing);
  countdown--;
  (void) timing_advance_time(p_timing, countdown);

  assert(state_6502_get_cycles(bbc_get_6502(p_via->p_bbc)) & 1);

  t1_val = via_get_t1c(p_via);
  if (t1_firing) {
    /* If the timer is firing, return -1. Need to force this because the raw
     * timer value is set to the relatch value plus one which must not be
     * exposed.
     */
    t1_val = -1;
  }
  t2_val = via_get_t2c(p_via);

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
    if (!t1_firing) {
      via_clear_interrupt(p_via, k_int_TIMER1);
    }
    return (((uint16_t) t1_val) & 0xFF);
  case k_via_T1CH:
    return (((uint16_t) t1_val) >> 8);
  case k_via_T1LL:
    return (p_via->T1L & 0xFF);
  case k_via_T1LH:
    return (p_via->T1L >> 8);
  case k_via_T2CL:
    if (!t2_firing) {
      via_clear_interrupt(p_via, k_int_TIMER2);
    }
    return (((uint16_t) t2_val) & 0xFF);
  case k_via_T2CH:
    return (((uint16_t) t2_val) >> 8);
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
  uint32_t t2_timer_id;
  int32_t timer_val;
  int32_t t1_val;
  int32_t t2_val;

  /* We're at the VIA start-cycle. Will T1/T2 interrupt fire at the mid cycle?
   * Work it out now because we can't tell after advancing the timing.
   */
  int t1_firing = via_is_t1_firing(p_via);
  int t2_firing = via_is_t2_firing(p_via);

  /* Advance to the VIA mid-cycle. */
  struct timing_struct* p_timing = p_via->p_timing;
  int64_t countdown = timing_get_countdown(p_timing);
  countdown--;
  (void) timing_advance_time(p_timing, countdown);

  assert(state_6502_get_cycles(bbc_get_6502(p_via->p_bbc)) & 1);

  /* This is a bit subtle but we need to read the T1C value in order to force
   * the deferred calculation of timer value for one shot timers that have shot.
   * Such a timer decrements indefinitely and negatively without timer events
   * firing.
   * The deferred calculation needs to know what the effective T1L value was
   * during the run. Since via_write may change T1L, do the deferred
   * calculation first.
   */
  t1_val = via_get_t1c(p_via);
  (void) t1_val;

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
    if (!t1_firing) {
      via_clear_interrupt(p_via, k_int_TIMER1);
    }
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xFF));
    timer_val = p_via->T1L;
    /* Increment the value because it must take effect in 1 tick. */
    timer_val++;
    via_set_t1c(p_via, timer_val);
    timing_set_firing(p_via->p_timing, p_via->t1_timer_id, 1);
    /* EMU TODO: does this behave differently if t1_firing as well? */
    p_via->t1_pb7 = 0;
    break;
  case k_via_T1LH:
    /* EMU NOTE: clear interrupt as per 6522 data sheet.
     * Behavior validated on a real BBC.
     * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=16251
     * Other emulators (b-em, jsbeeb) were only clearing the interrupt when in
     * timer continuous mode, but testing on a real BBC shows it should be
     * cleared always.
     */
    /* EMU TODO: assuming the same logic of not canceling interrupts applies
     * here for T1LH writes vs. IFR, but it's untest on a real BBC.
     */
    if (!t1_firing) {
      via_clear_interrupt(p_via, k_int_TIMER1);
    }
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xFF));
    break;
  case k_via_T2CL:
    p_via->T2L = ((p_via->T2L & 0xFF00) | val);
    break;
  case k_via_T2CH:
    if (!t2_firing) {
      via_clear_interrupt(p_via, k_int_TIMER2);
    }
    p_via->T2L = ((val << 8) | (p_via->T2L & 0xFF));
    timer_val = p_via->T2L;
    /* Increment the value because it must take effect in 1 tick. */
    if (!(p_via->ACR & 0x20)) {
      timer_val++;
    }
    via_set_t2c(p_via, timer_val);
    timing_set_firing(p_via->p_timing, p_via->t2_timer_id, 1);
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
     * See: tests.ssd:VIA.AC1
     */
    /* EMU NOTE: there's an very quirky special case if ACR if written to
     * one-shot the same cycle there's a T1 expiry. The one-shot is applied
     * to the just-expired timer. And the inverse is not true: turning on
     * continuous mode the same cycle as a T1 expiry still results in one-shot.
     * See: tests.ssd:VIA.AC3
     * See: tests.ssd:VIA.AC2
     */
    if (t1_firing && (!(val & 0x40))) {
      timing_set_firing(p_timing, p_via->t1_timer_id, 0);
    }

    if (!p_via->externally_clocked) {
      t2_timer_id = p_via->t2_timer_id;
      if (val & 0x20) {
        /* Stop T2 if that bit is set. */
        if (timing_timer_is_running(p_timing, t2_timer_id)) {
          t2_val = via_get_t2c(p_via);
          /* The value freezes after ticking one more time. */
          via_set_t2c(p_via, (t2_val - 1));
          (void) timing_stop_timer(p_timing, t2_timer_id);
        }
      } else {
        /* Otherwise start it. */
        if (!(timing_timer_is_running(p_timing, t2_timer_id))) {
          t2_val = via_get_t2c(p_via);
          /* The value starts ticking next cycle. */
          via_set_t2c(p_via, (t2_val + 1));
          (void) timing_start_timer(p_timing, t2_timer_id);
        }
      }
    }
    /*printf("new via %d ACR %x\n", p_via->id, val);*/
    break;
  case k_via_PCR:
    p_via->PCR = val;
    /*printf("new via %d PCR %x\n", p_via->id, val);*/
    break;
  case k_via_IFR:
    p_via->IFR &= ~(val & 0x7F);
    if (t1_firing) {
      /* Timer firing wins over a write to IFR. */
      p_via->IFR |= k_int_TIMER1;
    }
    /* EMU TODO: assuming the same logic applies for T2, although it's untested
     * on a real BBC.
     */
    if (t2_firing) {
      /* Timer firing wins over a write to IFR. */
      p_via->IFR |= k_int_TIMER2;
    }
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
                  int32_t* p_T1C_raw,
                  int32_t* p_T1L,
                  int32_t* p_T2C_raw,
                  int32_t* p_T2L,
                  uint8_t* p_t1_oneshot_fired,
                  uint8_t* p_t2_oneshot_fired,
                  uint8_t* p_t1_pb7) {
  struct timing_struct* p_timing = p_via->p_timing;

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
  *p_T1C_raw = via_get_t1c_raw(p_via);
  *p_T1L = p_via->T1L;
  *p_T2C_raw = via_get_t2c_raw(p_via);
  *p_T2L = p_via->T2L;
  *p_t1_oneshot_fired = !timing_get_firing(p_timing, p_via->t1_timer_id);
  *p_t2_oneshot_fired = !timing_get_firing(p_timing, p_via->t2_timer_id);
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
                       int32_t T1C_raw,
                       int32_t T1L,
                       int32_t T2C_raw,
                       int32_t T2L,
                       uint8_t t1_oneshot_fired,
                       uint8_t t2_oneshot_fired,
                       uint8_t t1_pb7) {
  struct timing_struct* p_timing = p_via->p_timing;

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
  via_set_t1c_raw(p_via, T1C_raw);
  p_via->T1L = T1L;
  via_set_t2c_raw(p_via, T2C_raw);
  p_via->T2L = T2L;
  timing_set_firing(p_timing, p_via->t1_timer_id, !t1_oneshot_fired);
  timing_set_firing(p_timing, p_via->t2_timer_id, !t2_oneshot_fired);
  p_via->t1_pb7 = t1_pb7;
}

uint8_t*
via_get_peripheral_b_ptr(struct via_struct* p_via) {
  return &p_via->peripheral_b;
}
