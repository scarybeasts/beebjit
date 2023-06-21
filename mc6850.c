#include "mc6850.h"

#include "bbc_options.h"
#include "log.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>

enum {
  k_serial_acia_status_RDRF = 0x01,
  k_serial_acia_status_TDRE = 0x02,
  k_serial_acia_status_DCD =  0x04,
  k_serial_acia_status_CTS =  0x08,
  k_serial_acia_status_FE =   0x10,
  k_serial_acia_status_OVRN = 0x20,
  k_serial_acia_status_PE =   0x40,
  k_serial_acia_status_IRQ =  0x80,
};

enum {
  k_serial_acia_control_clock_divider_mask = 0x03,
  k_serial_acia_control_clock_divider_64 = 0x02,
  k_serial_acia_control_8_bits = 0x10,
  k_serial_acia_control_TCB_mask = 0x60,
  k_serial_acia_control_RIE = 0x80,
};

enum {
  k_serial_acia_TCB_RTS_and_TIE =   0x20,
  k_serial_acia_TCB_no_RTS_no_TIE = 0x40,
};

enum {
  k_mc6850_state_null = 0,
  k_mc6850_state_need_start = 1,
  k_mc6850_state_need_data = 2,
  k_mc6850_state_need_parity = 3,
  k_mc6850_state_need_stop = 4,
};

struct mc6850_struct {
  struct state_6502* p_state_6502;
  void (*p_transmit_ready_callback)(void* p);
  void* p_transmit_ready_object;
  uint8_t acia_status_for_read;
  uint8_t acia_status;
  uint8_t acia_control;
  uint8_t acia_receive;
  uint8_t acia_transmit;
  int state;
  uint8_t acia_receive_sr;
  uint32_t acia_receive_sr_count;
  int parity_accumulator;
  uint32_t clock_divide_counter;
  int is_sr_parity_error;
  int is_sr_framing_error;
  int is_sr_overflow;
  int is_DCD;

  int log_state;
  int log_bytes;
};

static void
mc6850_update_irq_and_status_read(struct mc6850_struct* p_serial) {
  int do_check_send_int;
  int do_check_receive_int;
  int do_fire_int;
  uint8_t acia_status_for_read;

  int do_fire_send_int = 0;
  int do_fire_receive_int = 0;

  do_check_send_int =
      ((p_serial->acia_control & k_serial_acia_control_TCB_mask) ==
          k_serial_acia_TCB_RTS_and_TIE);
  if (do_check_send_int) {
    do_fire_send_int = !!(p_serial->acia_status & k_serial_acia_status_TDRE);
    do_fire_send_int &= !(p_serial->acia_status & k_serial_acia_status_CTS);
  }

  do_check_receive_int = !!(p_serial->acia_control & k_serial_acia_control_RIE);
  if (do_check_receive_int) {
    do_fire_receive_int = !!(p_serial->acia_status & k_serial_acia_status_RDRF);
    do_fire_receive_int |= !!(p_serial->acia_status & k_serial_acia_status_DCD);
    do_fire_receive_int |=
        !!(p_serial->acia_status & k_serial_acia_status_OVRN);
  }

  do_fire_int = (do_fire_send_int | do_fire_receive_int);

  /* Bit 7 of the control register must be high if we're asserting IRQ. */
  p_serial->acia_status &= ~k_serial_acia_status_IRQ;
  if (do_fire_int) {
    p_serial->acia_status |= k_serial_acia_status_IRQ;
  }

  state_6502_set_irq_level(p_serial->p_state_6502,
                           k_state_6502_irq_serial_acia,
                           do_fire_int);

  /* Update the raw read value for the status register. */
  acia_status_for_read = p_serial->acia_status;

  /* EMU MC6850: "A low CTS indicates that there is a Clear-to-Send from the
   * modem. In the high state, the Transmit Data Register Empty bit is
   * inhibited".
   */
  if (p_serial->acia_status & k_serial_acia_status_CTS) {
    acia_status_for_read &= ~k_serial_acia_status_TDRE;
  }

  /* If the "DCD went high" bit isn't latched, it follows line level. */
  /* EMU MC6850, more verbosely: "It remains high after the DCD input is
   * returned low until cleared by first reading the Status Register and then
   * Data Register or until a master reset occurs. If the DCD input remains
   * high after read status read data or master reset has occurred, the
   * interrupt is cleared, the DCD status bit remains high and will follow
   * the DCD input."
   * Note that on real hardware, only a read of data register seems required
   * to unlatch the DCD high condition.
   */
  if (p_serial->is_DCD) {
    acia_status_for_read |= k_serial_acia_status_DCD;
  }

  /* EMU TODO: MC6850: "Data Carrier Detect being high also causes RDRF to
   * indicate empty."
   * Unclear if that means the line level, latched DCD status, etc.
   */

  p_serial->acia_status_for_read = acia_status_for_read;
}

struct mc6850_struct*
mc6850_create(struct state_6502* p_state_6502,
              struct bbc_options* p_options) {
  struct mc6850_struct* p_serial = util_mallocz(sizeof(struct mc6850_struct));

  p_serial->p_state_6502 = p_state_6502;

  p_serial->log_state = util_has_option(p_options->p_log_flags, "serial:state");
  p_serial->log_bytes = util_has_option(p_options->p_log_flags, "serial:bytes");

  return p_serial;
}

void
mc6850_destroy(struct mc6850_struct* p_serial) {
  util_free(p_serial);
}

void
mc6850_set_transmit_ready_callback(
    struct mc6850_struct* p_serial,
    void (*p_transmit_ready_callback)(void* p),
    void* p_transmit_ready_object) {
  p_serial->p_transmit_ready_callback = p_transmit_ready_callback;
  p_serial->p_transmit_ready_object = p_transmit_ready_object;
}

void
mc6850_set_DCD(struct mc6850_struct* p_serial, int is_DCD) {
  /* The DCD low to high edge causes a bit latch, and potentially an IRQ.
   * DCD going from high to low doesn't affect the status register until the
   * bit latch is cleared by reading the data register.
   */
  if (is_DCD && !p_serial->is_DCD) {
    if (p_serial->log_state) {
      log_do_log(k_log_serial, k_log_info, "DCD going high");
    }
    p_serial->acia_status |= k_serial_acia_status_DCD;
  }
  p_serial->is_DCD = is_DCD;

  mc6850_update_irq_and_status_read(p_serial);
}

void
mc6850_set_CTS(struct mc6850_struct* p_serial, int is_CTS) {
  p_serial->acia_status &= ~k_serial_acia_status_CTS;
  if (is_CTS) {
    p_serial->acia_status |= k_serial_acia_status_CTS;
  }

  mc6850_update_irq_and_status_read(p_serial);
}

int
mc6850_get_RTS(struct mc6850_struct* p_serial) {
  if ((p_serial->acia_control & k_serial_acia_control_TCB_mask) ==
          k_serial_acia_TCB_no_RTS_no_TIE) {
    return 0;
  }
  /* TODO: seems wrong to have this here. It's a carry-over from the old
   * logic in what is now serial_ula_tick().
   */
  if (p_serial->acia_status & k_serial_acia_status_RDRF) {
    return 0;
  }

  return 1;
}

int
mc6850_is_transmit_ready(struct mc6850_struct* p_serial) {
  return !(p_serial->acia_status & k_serial_acia_status_TDRE);
}

int
mc6850_receive(struct mc6850_struct* p_serial, uint8_t byte) {
  if (p_serial->log_bytes) {
    log_do_log(k_log_serial,
               k_log_info,
               "byte received: %d (0x%.2X)",
               byte,
               byte);
  }
  if (p_serial->acia_status & k_serial_acia_status_RDRF) {
    log_do_log(k_log_serial, k_log_info, "receive buffer full");
    return 0;
  }
  p_serial->acia_status |= k_serial_acia_status_RDRF;
  p_serial->acia_status &= ~k_serial_acia_status_FE;
  p_serial->acia_status &= ~k_serial_acia_status_PE;
  p_serial->acia_receive = byte;

  mc6850_update_irq_and_status_read(p_serial);

  return 1;
}

static void
mc6850_clear_receive_state(struct mc6850_struct* p_serial) {
  p_serial->acia_receive_sr = 0;
  p_serial->acia_receive_sr_count = 0;
  p_serial->parity_accumulator = 0;
  p_serial->clock_divide_counter = 0;
  p_serial->is_sr_parity_error = 0;
  p_serial->is_sr_framing_error = 0;
}

static void
mc6850_transfer_sr_to_receive(struct mc6850_struct* p_serial) {
  int ok = mc6850_receive(p_serial, p_serial->acia_receive_sr);
  if (!ok) {
    /* Overflow condition. Data wasn't read in time. Overflow condition will be
     * raised once the existing data is read.
     */
    p_serial->is_sr_overflow = 1;
  } else {
    if (p_serial->is_sr_parity_error) {
      p_serial->acia_status |= k_serial_acia_status_PE;
    }
    if (p_serial->is_sr_framing_error) {
      p_serial->acia_status |= k_serial_acia_status_FE;
    }
  }
  mc6850_clear_receive_state(p_serial);

  mc6850_update_irq_and_status_read(p_serial);
}

void
mc6850_receive_bit(struct mc6850_struct* p_serial, int bit) {
  /* Implement 300 baud tapes here by skipping bits if our clock divider is 64
   * instead of the usual 16.
   */
  p_serial->clock_divide_counter++;
  p_serial->clock_divide_counter &= 3;
  if ((p_serial->acia_control & k_serial_acia_control_clock_divider_mask) ==
          k_serial_acia_control_clock_divider_64) {
    if (p_serial->clock_divide_counter != 0) {
      return;
    }
  }

  switch (p_serial->state) {
  case k_mc6850_state_need_start:
    if (bit == 0) {
      assert(p_serial->acia_receive_sr == 0);
      assert(p_serial->acia_receive_sr_count == 0);
      assert(p_serial->parity_accumulator == 0);
      assert(p_serial->is_sr_parity_error == 0);
      assert(p_serial->is_sr_framing_error == 0);
      p_serial->state = k_mc6850_state_need_data;
    }
    break;
  case k_mc6850_state_need_data:
    if (bit) {
      p_serial->acia_receive_sr |= (1 << p_serial->acia_receive_sr_count);
      p_serial->parity_accumulator = !p_serial->parity_accumulator;
    }
    p_serial->acia_receive_sr_count++;
    if (p_serial->acia_control & k_serial_acia_control_8_bits) {
      if (p_serial->acia_receive_sr_count == 8) {
        if (p_serial->acia_control & 0x08) {
          p_serial->state = k_mc6850_state_need_parity;
        } else {
          p_serial->state = k_mc6850_state_need_stop;
        }
      }
    } else {
      if (p_serial->acia_receive_sr_count == 7) {
        p_serial->state = k_mc6850_state_need_parity;
      }
    }
    break;
  case k_mc6850_state_need_parity:
    if (bit) {
      p_serial->parity_accumulator = !p_serial->parity_accumulator;
    }
    if (p_serial->parity_accumulator !=
            ((p_serial->acia_control & 0x04) >> 2)) {
      log_do_log(k_log_serial, k_log_warning, "incorrect parity bit");
      p_serial->is_sr_parity_error = 1;
    }
    p_serial->state = k_mc6850_state_need_stop;
    break;
  case k_mc6850_state_need_stop:
    if (bit != 1) {
      log_do_log(k_log_serial, k_log_warning, "incorrect stop bit");
      p_serial->is_sr_framing_error = 1;
    }
    mc6850_transfer_sr_to_receive(p_serial);
    p_serial->state = k_mc6850_state_need_start;
    break;
  default:
    assert(0);
    break;
  }
}

uint8_t
mc6850_transmit(struct mc6850_struct* p_serial) {
  assert(mc6850_is_transmit_ready(p_serial));

  p_serial->acia_status |= k_serial_acia_status_TDRE;
  mc6850_update_irq_and_status_read(p_serial);

  return p_serial->acia_transmit;
}

static void
mc6850_reset(struct mc6850_struct* p_serial) {
  int is_CTS = !!(p_serial->acia_status & k_serial_acia_status_CTS);

  p_serial->acia_receive = 0;
  p_serial->acia_transmit = 0;

  p_serial->state = k_mc6850_state_need_start;
  mc6850_clear_receive_state(p_serial);
  p_serial->is_sr_overflow = 0;

  /* Set TDRE (transmit data register empty). Clear everything else. */
  p_serial->acia_status = k_serial_acia_status_TDRE;

  p_serial->acia_control = 0;

  /* Reset of the ACIA cannot change external line levels. Make sure any status
   * bits they affect are kept.
   */
  /* These calls call mc6850_update_irq_and_status_read(). */
  mc6850_set_DCD(p_serial, p_serial->is_DCD);
  mc6850_set_CTS(p_serial, is_CTS);
}

void
mc6850_power_on_reset(struct mc6850_struct* p_serial) {
  p_serial->is_DCD = 0;
  p_serial->acia_status = 0;
  mc6850_reset(p_serial);
}

uint8_t
mc6850_read(struct mc6850_struct* p_serial, uint8_t reg) {
  if (reg == 0) {
    /* Status register. */
    return p_serial->acia_status_for_read;
  } else {
    /* Data register. */
    p_serial->acia_status &= ~k_serial_acia_status_DCD;
    p_serial->acia_status &= ~k_serial_acia_status_OVRN;
    if (p_serial->is_sr_overflow) {
      assert(p_serial->acia_status & k_serial_acia_status_RDRF);
      /* MC6850: "The Overrun does not occur in the Status Register until the
       * valid character prior to Overrun has been read.
       */
      p_serial->is_sr_overflow = 0;
      p_serial->acia_status |= k_serial_acia_status_OVRN;
      /* RDRF remains asserted. */
    } else {
      p_serial->acia_status &= ~k_serial_acia_status_RDRF;
    }

    mc6850_update_irq_and_status_read(p_serial);

    return p_serial->acia_receive;
  }
}

void
mc6850_write(struct mc6850_struct* p_serial, uint8_t reg, uint8_t val) {
  if (reg == 0) {
    /* Control register. */
    if ((val & 0x03) == 0x03) {
      /* Master reset. */
      /* EMU NOTE: the data sheet says, "Master reset does not affect other
       * Control Register bits.", however this does not seem to be fully
       * correct. If there's an interrupt active at reset time, it is cleared
       * after reset. This suggests it could be clearing CR, including the
       * interrupt select bits. We clear CR, and this is sufficient to get the
       * Frak tape to load.
       * (After a master reset, the chip actually seems to be dormant until CR
       * is written. One specific example is that TDRE is not indicated until
       * CR is written -- a corner case we do not yet implement.)
       */
      if (p_serial->log_state) {
        log_do_log(k_log_serial, k_log_info, "reset");
      }

      mc6850_reset(p_serial);
    } else {
      p_serial->acia_control = val;
    }
    if (p_serial->log_state) {
      static const char* p_bitmode_strs[] = {
        "7E2", "7O2", "7E1", "7O1", "8N2", "8N1", "8E1", "8O1",
      };
      static const char* p_divider_strs[] = {
        "/1", "/16", "/64", "RESET",
      };
      const char* p_bitmode_str =
          p_bitmode_strs[(p_serial->acia_control >> 2) & 0x07];
      const char* p_divider_str = p_divider_strs[p_serial->acia_control & 0x03];
      log_do_log(k_log_serial,
                 k_log_info,
                 "control register now: $%.2X [%s] [%s]",
                 p_serial->acia_control,
                 p_bitmode_str,
                 p_divider_str);
    }
  } else {
    /* Data register, transmit byte. */
    assert(reg == 1);
    if (!(p_serial->acia_status & k_serial_acia_status_TDRE)) {
      log_do_log(k_log_serial, k_log_unimplemented, "transmit buffer full");
    }

    p_serial->acia_transmit = val;
    p_serial->acia_status &= ~k_serial_acia_status_TDRE;

    if (p_serial->p_transmit_ready_callback) {
      p_serial->p_transmit_ready_callback(p_serial->p_transmit_ready_object);
    }
  }

  mc6850_update_irq_and_status_read(p_serial);
}
