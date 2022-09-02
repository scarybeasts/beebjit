#include "via.h"

#include "bbc.h"
#include "cmos.h"
#include "keyboard.h"
#include "sound.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"

#include <assert.h>

enum {
  k_via_ORB =   0x0,
  k_via_ORA =   0x1,
  k_via_DDRB =  0x2,
  k_via_DDRA =  0x3,
  k_via_T1CL =  0x4,
  k_via_T1CH =  0x5,
  k_via_T1LL =  0x6,
  k_via_T1LH =  0x7,
  k_via_T2CL =  0x8,
  k_via_T2CH =  0x9,
  k_via_SR =    0xA,
  k_via_ACR =   0xB,
  k_via_PCR =   0xC,
  k_via_IFR =   0xD,
  k_via_IER =   0xE,
  k_via_ORAnh = 0xF,
};

enum {
  k_int_CA2 =    0x01,
  k_int_CA1 =    0x02,
  k_int_CB2 =    0x08,
  k_int_CB1 =    0x10,
  k_int_TIMER1 = 0x40,
  k_int_TIMER2 = 0x20,
};

struct via_struct {
  int id;
  int externally_clocked;
  struct bbc_struct* p_bbc;
  struct timing_struct* p_timing;
  uint32_t t1_timer_id;
  uint32_t t2_timer_id;

  void (*p_CB2_changed_callback)(void* p, int level, int output);
  void* p_CB2_changed_object;
  void (*p_timing_advancer)(void* p, uint64_t ticks);
  void* p_timing_advancer_object;

  uint8_t IRA;
  uint8_t IRB;
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
  int CA1;
  int CA2;
  int CB1;
  int CB2;
};

static void
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
    interrupt = k_state_6502_irq_via_1;
  } else {
    interrupt = k_state_6502_irq_via_2;
  }
  state_6502_set_irq_level(p_state_6502, interrupt, level);
}

static void
via_set_IFR(struct via_struct* p_via, uint8_t val) {
  int was_CA2 = (p_via->IFR & k_int_CA2);
  p_via->IFR = val;
  /* NOTE: special case hack here to handle keyboard correctly while maintaining
   * speed.
   * If we're lowering the keyboard interrupt, give it a chance to raise again
   * immediately. Real hardware behaves this way when the keyboard is in
   * auto-scan mode and a key is down because the CA2 input line is toggling
   * on->off->on->off. It toggles as the auto-scan iterates across columns and
   * hits the one with the key down once per iteration.
   */
  if ((p_via->id == k_via_system) && was_CA2 && !(val & k_int_CA2)) {
    via_update_port_a(p_via);
  }
  via_check_interrupt(p_via);
}

static void
via_raise_interrupt(struct via_struct* p_via, uint8_t val) {
  assert(!(val & 0x80));
  via_set_IFR(p_via, (p_via->IFR | val));
}

static void
via_clear_interrupt(struct via_struct* p_via, uint8_t val) {
  assert(!(val & 0x80));
  via_set_IFR(p_via, (p_via->IFR & ~val));
}

static void
via_set_t1c_raw(struct via_struct* p_via, int32_t val) {
  uint32_t id = p_via->t1_timer_id;
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
  uint32_t id = p_via->t1_timer_id;
  int64_t val = timing_get_timer_value(p_via->p_timing, id);

  val -= 2;

  /* If interrupts aren't firing, the timer will decrement indefinitely so we
   * have to fix it up with all of the re-latches.
   */
  if (val < -2) {
    /* T1 (latch 4) counts 4... 3... 2... 1... 0... -1... 4... */
    uint64_t delta = (-val - 4);
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
  uint32_t id = p_via->t2_timer_id;
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
  uint32_t id = p_via->t2_timer_id;
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
via_is_t1_firing(struct via_struct* p_via, int32_t ticks_add) {
  int32_t val;
  struct timing_struct* p_timing = p_via->p_timing;
  uint32_t timer_id = p_via->t1_timer_id;
  if (!timing_get_firing(p_timing, timer_id)) {
    return 0;
  }

  val = via_get_t1c_raw(p_via);
  return ((val - ticks_add) == -1);
}

static int
via_is_t2_firing(struct via_struct* p_via, int32_t ticks_add) {
  int32_t val;
  struct timing_struct* p_timing = p_via->p_timing;
  uint32_t timer_id = p_via->t2_timer_id;
  if (!timing_get_firing(p_timing, timer_id)) {
    return 0;
  }

  val = via_get_t2c_raw(p_via);
  return ((val - ticks_add) == -1);
}

struct via_struct*
via_create(int id,
           int externally_clocked,
           struct timing_struct* p_timing,
           struct bbc_struct* p_bbc) {
  struct via_struct* p_via = util_mallocz(sizeof(struct via_struct));

  p_via->id = id;
  p_via->externally_clocked = externally_clocked;
  p_via->p_bbc = p_bbc;
  p_via->p_timing = p_timing;

  p_via->t1_timer_id = timing_register_timer(p_timing, via_t1_fired, p_via);
  p_via->t2_timer_id = timing_register_timer(p_timing, via_t2_fired, p_via);

  return p_via;
}

void
via_destroy(struct via_struct* p_via) {
  util_free(p_via);
}

void
via_power_on_reset(struct via_struct* p_via) {
  struct timing_struct* p_timing = p_via->p_timing;
  uint32_t t1_timer_id = p_via->t1_timer_id;
  uint32_t t2_timer_id = p_via->t2_timer_id;

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
  p_via->SR = 0;
  p_via->ACR = 0;
  p_via->PCR = 0;
  p_via->IFR = 0;
  p_via->IER = 0;
  p_via->CA1 = 0;
  p_via->CA2 = 0;
  p_via->CB1 = 0;
  p_via->CB2 = 0;
  /* EMU: the input registers seem to be initialized to 0xFF on a real
   * machine.
   */
  p_via->IRA = 0xFF;
  p_via->IRB = 0xFF;

  /* Nothing attached to the buses, or nothing actively pulling pins low to
   * start with.
   */
  p_via->peripheral_a = 0xFF;
  p_via->peripheral_b = 0xFF;

  /* EMU NOTE: needs to be initialized to 1 otherwise Planetoid doesn't run. */
  p_via->t1_pb7 = 1;

  if (!p_via->externally_clocked) {
    if (!timing_timer_is_running(p_timing, t1_timer_id)) {
      timing_start_timer(p_timing, t1_timer_id);
    }
    if (!timing_timer_is_running(p_timing, t2_timer_id)) {
      timing_start_timer(p_timing, t2_timer_id);
    }
  }

  /* From the above data sheet:
   * "The interval timer one-shot mode allows generation of a single interrupt
   * for each timer load operation."
   * It's unclear whether "power on" / "reset" counts as an effective timer
   * load or not. Let's copy jsbeeb and b-em and say that it does not.
   */
  timing_set_firing(p_timing, t1_timer_id, 0);
  timing_set_firing(p_timing, t2_timer_id, 0);

  /* EMU: the counter values appear to be quasi-random on a real machine, but
   * we'll initialize them to 0xFFFF for deterministic behavior.
   */
  via_set_t1c(p_via, 0xFFFF);
  p_via->T1L = 0xFFFF;
  via_set_t2c(p_via, 0xFFFF);
  p_via->T2L = 0xFFFF;
}

void
via_set_CB2_changed_callback(struct via_struct* p_via,
                             void (*p_CB2_changed_callback)
                                 (void* p, int level, int output),
                             void* p_CB2_changed_object) {
  p_via->p_CB2_changed_callback = p_CB2_changed_callback;
  p_via->p_CB2_changed_object = p_CB2_changed_object;
}

void
via_set_timing_advancer(struct via_struct* p_via,
                        void (*p_timing_advancer)(void* p, uint64_t ticks),
                        void* p_timing_advancer_object) {
  p_via->p_timing_advancer = p_timing_advancer;
  p_via->p_timing_advancer_object = p_timing_advancer_object;
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

static uint8_t
via_calculate_port_a(struct via_struct* p_via) {
  uint8_t ora = p_via->ORA;
  uint8_t ddra = p_via->DDRA;
  uint8_t val = (ora & ddra);
  uint8_t peripheral_a = p_via->peripheral_a;
  val |= (peripheral_a & ~ddra);

  /* Our peripheral (currently just the keyboard) is always capable of driving
   * a bus level low, even for pins configured as outputs.
   */
  val &= peripheral_a;

  return val;
}

static uint8_t
via_calculate_port_b(struct via_struct* p_via) {
  uint8_t orb = p_via->ORB;
  uint8_t ddrb = p_via->DDRB;
  uint8_t val = (orb & ddrb);
  val |= (p_via->peripheral_b & ~ddrb);

  return val;
}

static void
sysvia_update_port_a(struct via_struct* p_via) {
  struct bbc_struct* p_bbc = p_via->p_bbc;
  struct keyboard_struct* p_keyboard = bbc_get_keyboard(p_bbc);
  struct cmos_struct* p_cmos = bbc_get_cmos(p_bbc);
  uint8_t bus_val = via_calculate_port_a(p_via);
  uint8_t keyrow = ((bus_val >> 4) & 7);
  uint8_t keycol = (bus_val & 0xf);
  int fire = 0;
  uint8_t IC32 = bbc_get_IC32(p_bbc);
  uint8_t peripheral_a = 0xFF;

  if (p_cmos != NULL) {
    peripheral_a &= cmos_get_bus_value(p_cmos);
  }

  if (!(IC32 & 0x08)) {
    if (!keyboard_bbc_is_key_pressed(p_keyboard, keyrow, keycol)) {
      peripheral_a &= 0x7F;
    }
    if (keyboard_bbc_is_key_column_pressed(p_keyboard, keycol)) {
      fire = 1;
    }
  } else {
    if (keyboard_bbc_is_any_key_pressed(p_keyboard)) {
      /* Make sure the CA2 line toggles. This is how real hardware behaves in
       * auto-scan mode because it iterates across key columns and will hit
       * the active column as well as the inactive columns.
       */
      via_set_CA2(p_via, 0);
      fire = 1;
    }
  }

  p_via->peripheral_a = peripheral_a;

  via_set_CA2(p_via, fire);

  if (!(IC32 & 1)) {
    struct sound_struct* p_sound = bbc_get_sound(p_via->p_bbc);
    /* Make sure the bus value is uptodate with any keyboard action. */
    bus_val &= peripheral_a;
    sound_sn_write(p_sound, bus_val);
  }
}

static void
sysvia_update_port_b(struct via_struct* p_via) {
  struct bbc_struct* p_bbc = p_via->p_bbc;
  struct cmos_struct* p_cmos = bbc_get_cmos(p_bbc);
  uint8_t old_IC32 = bbc_get_IC32(p_bbc);
  uint8_t bus_val = via_calculate_port_b(p_via);
  uint8_t port_bit = (1 << (bus_val & 7));
  int bit_set = ((bus_val & 0x08) == 0x08);
  uint8_t IC32 = old_IC32;

  if (bit_set) {
    IC32 |= port_bit;
  } else {
    IC32 &= ~port_bit;
  }

  if (p_cmos != NULL) {
    cmos_update_external_inputs(p_cmos,
                                bus_val,
                                via_calculate_port_a(p_via),
                                IC32);
  }

  bbc_set_IC32(p_bbc, IC32);
}

void
via_update_port_a(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    sysvia_update_port_a(p_via);
  } else if (p_via->id == k_via_user) {
    /* Printer port. Ignore. */
  } else {
    assert(0);
  }
}

void
via_update_port_b(struct via_struct* p_via) {
  if (p_via->id == k_via_system) {
    sysvia_update_port_b(p_via);
  } else if (p_via->id == k_via_user) {
    /* User port. Ignore. */
  } else {
    assert(0);
  }
}

static void
via_advance_ticks(struct via_struct* p_via, uint64_t ticks) {
  p_via->p_timing_advancer(p_via->p_timing_advancer_object, ticks);
}

static void
via_load_T1(struct via_struct* p_via) {
  int32_t timer_val = p_via->T1L;
  /* Increment the value because it must take effect in 1 tick. */
  timer_val++;
  via_set_t1c(p_via, timer_val);
}

static uint8_t
via_read_internal(struct via_struct* p_via, uint8_t reg, int is_raw) {
  uint8_t orb;
  uint8_t ddrb;
  uint8_t port_val;
  int32_t t1_val;
  int32_t t2_val;
  uint8_t ret;

  /* Will T1/T2 interrupt fire at the mid cycle?
   * Work it out now because we can't tell after advancing the timing.
   */
  int t1_firing = 0;
  int t2_firing = 0;
  uint32_t ticks = (state_6502_get_cycles(bbc_get_6502(p_via->p_bbc)) & 1);
  if ((reg == k_via_T1CL) || (reg == k_via_T1CH)) {
    t1_firing = via_is_t1_firing(p_via, ticks);
  } else if (reg == k_via_T2CL) {
    t2_firing = via_is_t2_firing(p_via, ticks);
  }

  /* Advance to the VIA mid-cycle.
   * EMU NOTE: do this first before processing the read. Interrupts fire at
   * the mid-cycle and the read needs any results from that to be correct.
   * Of note, if an interrupt fires the same VIA cycle as an IFR read, IFR
   * reflects the just-hit interrupt on a real BBC.
   */
  if (!is_raw) {
    via_advance_ticks(p_via, (ticks + 1));
  }

  switch (reg) {
  case k_via_ORB:
    /* Independent interrupt not supported yet. */
    assert((p_via->PCR & 0xA0) != 0x20);

    if (!is_raw) {
      via_clear_interrupt(p_via, k_int_CB1);
      via_clear_interrupt(p_via, k_int_CB2);
    }

    /* A read of VIA port B mixes input and output as indicated by DDRB. */
    orb = p_via->ORB;
    ddrb = p_via->DDRB;
    ret = (orb & ddrb);
    if (p_via->ACR & 0x02) {
      port_val = p_via->IRB;
    } else {
      port_val = via_calculate_port_b(p_via);
    }
    ret |= (port_val & ~ddrb);

    /* EMU NOTE: PB7 toggling is actually a mix-in of a separately maintained
     * bit, and it's mixed in to both IRB and ORB.
     * See: https://stardot.org.uk/forums/viewtopic.php?f=4&t=16081
     */
    if (p_via->ACR & 0x80) {
      ret &= 0x7F;
      ret |= (p_via->t1_pb7 << 7);
    }
    break;
  case k_via_ORA:
    /* Independent interrupt not supported yet. */
    assert((p_via->PCR & 0x0A) != 0x02);
    if (!is_raw) {
      via_clear_interrupt(p_via, k_int_CA1);
      via_clear_interrupt(p_via, k_int_CA2);
    }
  /* Fall through. */
  case k_via_ORAnh:
    /* A read of VIA port A reads the current pins levels, or uses the pin
     * levels at the last latch, regardless of input vs. output configuration.
     */
    if (p_via->ACR & 0x01) {
      ret = p_via->IRA;
    } else {
      ret = via_calculate_port_a(p_via);
    }
    break;
  case k_via_DDRB:
    ret = p_via->DDRB;
    break;
  case k_via_DDRA:
    ret = p_via->DDRA;
    break;
  case k_via_T1CL:
    if (!t1_firing && !is_raw) {
      via_clear_interrupt(p_via, k_int_TIMER1);
    }
    t1_val = via_get_t1c(p_via);
    if (t1_firing) {
      /* If the timer is firing, return -1. Need to force this because the raw
       * timer value is set to the relatch value plus one which must not be
       * exposed.
       */
      t1_val = -1;
    }
    ret = (((uint16_t) t1_val) & 0xFF);
    break;
  case k_via_T1CH:
    t1_val = via_get_t1c(p_via);
    if (t1_firing) {
      /* If the timer is firing, return -1. Need to force this because the raw
       * timer value is set to the relatch value plus one which must not be
       * exposed.
       */
      t1_val = -1;
    }
    ret = (((uint16_t) t1_val) >> 8);
    break;
  case k_via_T1LL:
    ret = (p_via->T1L & 0xFF);
    break;
  case k_via_T1LH:
    ret = (p_via->T1L >> 8);
    break;
  case k_via_T2CL:
    if (!t2_firing && !is_raw) {
      via_clear_interrupt(p_via, k_int_TIMER2);
    }
    t2_val = via_get_t2c(p_via);
    ret = ((uint16_t) t2_val & 0xFF);
    break;
  case k_via_T2CH:
    t2_val = via_get_t2c(p_via);
    ret = ((uint16_t) t2_val >> 8);
    break;
  case k_via_SR:
    ret = p_via->SR;
    break;
  case k_via_ACR:
    ret = p_via->ACR;
    break;
  case k_via_PCR:
    ret = p_via->PCR;
    break;
  case k_via_IFR:
    ret = p_via->IFR;
    break;
  case k_via_IER:
    ret = (p_via->IER | 0x80);
    break;
  default:
    assert(0);
    ret = 0;
    break;
  }

  if (!is_raw) {
    via_advance_ticks(p_via, 1);
  }

  return ret;
}

uint8_t
via_read(struct via_struct* p_via, uint8_t reg) {
  return via_read_internal(p_via, reg, 0);
}

uint8_t
via_read_raw(struct via_struct* p_via, uint8_t reg) {
  return via_read_internal(p_via, reg, 1);
}

void
via_write_internal(struct via_struct* p_via,
                   uint8_t reg,
                   uint8_t val,
                   int is_raw) {
  uint32_t t2_timer_id;
  int32_t timer_val;
  int32_t t1_val;
  int32_t t2_val;
  uint8_t new_IFR;

  /* Will T1/T2 interrupt fire at the mid cycle?
   * Work it out now because we can't tell after advancing the timing.
   */
  uint32_t ticks = (state_6502_get_cycles(bbc_get_6502(p_via->p_bbc)) & 1);
  int t1_firing = via_is_t1_firing(p_via, ticks);
  int t2_firing = via_is_t2_firing(p_via, ticks);
  struct timing_struct* p_timing = p_via->p_timing;

  /* TODO: the way things have worked out, it looks like we'll have less
   * complexity if we apply the write right away and then fix up. There will
   * likely be less fixing up that way around.
   */
  /* Advance to the VIA mid-cycle. */
  if (!is_raw) {
    via_advance_ticks(p_via, (ticks + 1));
  }

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
    /* Independent interrupt not supported yet. */
    assert((p_via->PCR & 0xA0) != 0x20);
    /* Handshake mode not supported yet. */
    assert((p_via->PCR & 0xE0) != 0x80);
    /* Pulse output not supported yet. */
    assert((p_via->PCR & 0xE0) != 0xA0);
    p_via->ORB = val;
    via_clear_interrupt(p_via, k_int_CB1);
    via_clear_interrupt(p_via, k_int_CB2);
    via_update_port_b(p_via);
    break;
  case k_via_ORA:
    /* Independent interrupt not supported yet. */
    assert((p_via->PCR & 0x0A) != 0x02);
    /* Handshake mode not supported yet. */
    assert((p_via->PCR & 0x0E) != 0x08);
    /* Pulse output not supported yet. */
    assert((p_via->PCR & 0x0E) != 0x0A);
    via_clear_interrupt(p_via, k_int_CA1);
    via_clear_interrupt(p_via, k_int_CA2);
  /* Fall through. */
  case k_via_ORAnh:
    p_via->ORA = val;
    via_update_port_a(p_via);
    break;
  case k_via_DDRB:
    p_via->DDRB = val;
    via_update_port_b(p_via);
    break;
  case k_via_DDRA:
    p_via->DDRA = val;
    via_update_port_a(p_via);
    break;
  case k_via_T1CL:
  case k_via_T1LL:
    /* Not an error: writing to either T1CL or T1LL updates just T1LL. */
    p_via->T1L = ((p_via->T1L & 0xFF00) | val);
    /* EMU NOTE: If we reloaded the timer from the latch at the same VIA cycle
     * as a write to change the latch, the newly written value must take effect.
     * Finally hit by the second(?) stage Nightshade tape loader at $7300.
     */
    if (t1_firing && (p_via->ACR & 0x40)) {
      via_load_T1(p_via);
    }
    break;
  case k_via_T1CH:
    if (!t1_firing) {
      via_clear_interrupt(p_via, k_int_TIMER1);
    }
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xFF));
    via_load_T1(p_via);
    timing_set_firing(p_timing, p_via->t1_timer_id, 1);
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
    p_via->T1L = ((val << 8) | (p_via->T1L & 0xFF));
    if (!t1_firing) {
      via_clear_interrupt(p_via, k_int_TIMER1);
    } else if (p_via->ACR & 0x40) {
      via_load_T1(p_via);
    }
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
    timing_set_firing(p_timing, p_via->t2_timer_id, 1);
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
    break;
  case k_via_PCR:
    p_via->PCR = val;
    if ((val & 0xE0) == 0xC0) {
      via_set_CB2(p_via, 0);
    } else if (val & 0x80) {
      via_set_CB2(p_via, 1);
    }
    break;
  case k_via_IFR:
    new_IFR = (p_via->IFR & ~(val & 0x7F));
    if (t1_firing) {
      /* Timer firing wins over a write to IFR. */
      new_IFR |= k_int_TIMER1;
    }
    /* EMU TODO: assuming the same logic applies for T2, although it's untested
     * on a real BBC.
     */
    if (t2_firing) {
      /* Timer firing wins over a write to IFR. */
      new_IFR |= k_int_TIMER2;
    }
    via_set_IFR(p_via, new_IFR);
    break;
  case k_via_IER:
    if (val & 0x80) {
      p_via->IER |= (val & 0x7F);
    } else {
      p_via->IER &= ~(val & 0x7F);
    }
    via_check_interrupt(p_via);
    break;
  default:
    assert(0);
    break;
  }

  if (!is_raw) {
    via_advance_ticks(p_via, 1);
  }
}

void
via_write(struct via_struct* p_via, uint8_t reg, uint8_t val) {
  return via_write_internal(p_via, reg, val, 0);
}

void
via_write_raw(struct via_struct* p_via, uint8_t reg, uint8_t val) {
  return via_write_internal(p_via, reg, val, 1);
}

void
via_get_all_CAB(struct via_struct* p_via,
                int* p_CA1,
                int* p_CA2,
                int* p_CB1,
                int* p_CB2) {
  *p_CA1 = p_via->CA1;
  *p_CA2 = p_via->CA2;
  *p_CB1 = p_via->CB1;
  *p_CB2 = p_via->CB2;
}

void
via_set_CA1(struct via_struct* p_via, int level) {
  int trigger_level;

  if (level == p_via->CA1) {
    return;
  }

  trigger_level = !!(p_via->PCR & 0x01);
  if (level == trigger_level) {
    p_via->IRA = p_via->peripheral_a;
    via_raise_interrupt(p_via, k_int_CA1);
    assert((p_via->PCR & 0x0C) != 0x08);
  }
  p_via->CA1 = level;
}

void
via_set_CA2(struct via_struct* p_via, int level) {
  int trigger_level;

  if (level == p_via->CA2) {
    return;
  }

  /* TODO: here and CA1, don't IRQ in output mode. */
  trigger_level = !!(p_via->PCR & 0x04);
  if (level == trigger_level) {
    via_raise_interrupt(p_via, k_int_CA2);
  }
  p_via->CA2 = level;
}

void
via_set_CB1(struct via_struct* p_via, int level) {
  int trigger_level;

  if (level == p_via->CB1) {
    return;
  }

  trigger_level = !!(p_via->PCR & 0x10);
  if (level == trigger_level) {
    p_via->IRB = p_via->peripheral_b;
    via_raise_interrupt(p_via, k_int_CB1);
    assert((p_via->PCR & 0xC0) != 0x80);
  }
  p_via->CB1 = level;
}

void
via_set_CB2(struct via_struct* p_via, int level) {
  int trigger_level;
  int output;

  if (level == p_via->CB2) {
    return;
  }

  p_via->CB2 = level;

  output = !!(p_via->PCR & 0x80);

  if (p_via->p_CB2_changed_callback) {
    p_via->p_CB2_changed_callback(p_via->p_CB2_changed_object, level, output);
  }

  if (output) {
    return;
  }

  trigger_level = !!(p_via->PCR & 0x40);
  if (level == trigger_level) {
    via_raise_interrupt(p_via, k_int_CB2);
  }
}

void
via_set_peripheral_b(struct via_struct* p_via, uint8_t val) {
  p_via->peripheral_b = val;
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
