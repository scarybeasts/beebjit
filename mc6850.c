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
  k_serial_acia_status_IRQ =  0x80,
};

enum {
  k_serial_acia_control_TCB_mask = 0x60,
  k_serial_acia_control_RIE = 0x80,
};

enum {
  k_serial_acia_TCB_RTS_and_TIE =   0x20,
  k_serial_acia_TCB_no_RTS_no_TIE = 0x40,
};

struct serial_struct {
  struct state_6502* p_state_6502;
  void (*p_transmit_ready_callback)(void* p);
  void* p_transmit_ready_object;
  uint8_t acia_control;
  uint8_t acia_status;
  uint8_t acia_receive;
  uint8_t acia_transmit;
  int is_DCD;
  int is_CTS;

  int log_state;
  int log_bytes;
};

static void
serial_acia_update_irq(struct serial_struct* p_serial) {
  int do_check_send_int;
  int do_check_receive_int;
  int do_fire_int;

  int do_fire_send_int = 0;
  int do_fire_receive_int = 0;

  do_check_send_int =
      ((p_serial->acia_control & k_serial_acia_control_TCB_mask) ==
          k_serial_acia_TCB_RTS_and_TIE);
  if (do_check_send_int) {
    do_fire_send_int = !!(p_serial->acia_status & k_serial_acia_status_TDRE);
    do_fire_send_int &= !p_serial->is_CTS;
  }

  do_check_receive_int = !!(p_serial->acia_control & k_serial_acia_control_RIE);
  if (do_check_receive_int) {
    do_fire_receive_int = !!(p_serial->acia_status & k_serial_acia_status_RDRF);
    do_fire_receive_int |= !!(p_serial->acia_status & k_serial_acia_status_DCD);
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
}

struct serial_struct*
serial_create(struct state_6502* p_state_6502,
              struct bbc_options* p_options) {
  struct serial_struct* p_serial = util_mallocz(sizeof(struct serial_struct));

  p_serial->p_state_6502 = p_state_6502;

  p_serial->log_state = util_has_option(p_options->p_log_flags, "serial:state");
  p_serial->log_bytes = util_has_option(p_options->p_log_flags, "serial:bytes");

  return p_serial;
}

void
serial_destroy(struct serial_struct* p_serial) {
  util_free(p_serial);
}

void
serial_set_transmit_ready_callback(
    struct serial_struct* p_serial,
    void (*p_transmit_ready_callback)(void* p),
    void* p_transmit_ready_object) {
  p_serial->p_transmit_ready_callback = p_transmit_ready_callback;
  p_serial->p_transmit_ready_object = p_transmit_ready_object;
}

void
serial_set_DCD(struct serial_struct* p_serial, int is_DCD) {
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

  serial_acia_update_irq(p_serial);
}

void
serial_set_CTS(struct serial_struct* p_serial, int is_CTS) {
  p_serial->acia_status &= ~k_serial_acia_status_CTS;
  if (is_CTS) {
    p_serial->acia_status |= k_serial_acia_status_CTS;
  }

  serial_acia_update_irq(p_serial);
}

int
serial_get_RTS(struct serial_struct* p_serial) {
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
serial_is_transmit_ready(struct serial_struct* p_serial) {
  return !(p_serial->acia_status & k_serial_acia_status_TDRE);
}

void
serial_receive(struct serial_struct* p_serial, uint8_t byte) {
  if (p_serial->log_bytes) {
    log_do_log(k_log_serial,
               k_log_info,
               "byte received: %d (0x%.2X)",
               byte,
               byte);
  }
  if (p_serial->acia_status & k_serial_acia_status_RDRF) {
    log_do_log(k_log_serial, k_log_unimplemented, "receive buffer full");
  }
  p_serial->acia_status |= k_serial_acia_status_RDRF;
  p_serial->acia_receive = byte;

  serial_acia_update_irq(p_serial);
}

uint8_t
serial_transmit(struct serial_struct* p_serial) {
  assert(serial_is_transmit_ready(p_serial));

  p_serial->acia_status |= k_serial_acia_status_TDRE;
  serial_acia_update_irq(p_serial);

  return p_serial->acia_transmit;
}

void
serial_power_on_reset(struct serial_struct* p_serial) {
  p_serial->acia_receive = 0;
  p_serial->acia_transmit = 0;

  /* Set TDRE (transmit data register empty). Clear everything else. */
  p_serial->acia_status = k_serial_acia_status_TDRE;

  p_serial->acia_control = 0;

  /* Reset of the ACIA cannot change external line levels. Make sure any status
   * bits they affect are kept.
   */
  serial_set_DCD(p_serial, p_serial->is_DCD);
  serial_set_CTS(p_serial, p_serial->is_CTS);
}

uint8_t
serial_acia_read(struct serial_struct* p_serial, uint8_t reg) {
  if (reg == 0) {
    /* Status register. */
    uint8_t ret = p_serial->acia_status;

    /* EMU MC6850: "A low CTS indicates that there is a Clear-to-Send from the
     * modem. In the high state, the Transmit Data Register Empty bit is
     * inhibited".
     */
    if (p_serial->is_CTS) {
      ret &= ~k_serial_acia_status_TDRE;
    }

    /* If the "DCD went high" bit isn't latched, it follows line level. */
    /* EMU MC6850, more verbosely: "It remains high after the DCD input is
     * returned low until cleared by first reading the Status Register and then
     * Data Register or until a master reset occurs. If the DCD input remains
     * high after read status read data or master reset has occurred, the
     * interrupt is cleared, the DCD status bit remains high and will follow
     * the DCD input.
     * Note that on real hardware, only a read of data register seems required
     * to unlatch the DCD high condition.
     */
    if (!(ret & k_serial_acia_status_DCD)) {
      if (p_serial->is_DCD) {
        ret |= k_serial_acia_status_DCD;
      }
    }

    /* EMU TODO: MC6850: "Data Carrier Detect being high also causes RDRF to
     * indicate empty."
     * Unclear if that means the line level, latched DCD status, etc.
     */

    return ret;
  } else {
    /* Data register. */
    p_serial->acia_status &= ~k_serial_acia_status_RDRF;
    p_serial->acia_status &= ~k_serial_acia_status_DCD;

    serial_acia_update_irq(p_serial);

    return p_serial->acia_receive;
  }
}

void
serial_acia_write(struct serial_struct* p_serial, uint8_t reg, uint8_t val) {
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

      serial_power_on_reset(p_serial);
    } else {
      p_serial->acia_control = val;
    }
    if (p_serial->log_state) {
      static const char* p_bitmode_strs[] = {
        "7E2", "7O2", "7E1", "7O1", "8N2", "8N1", "8E1", "8N1",
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

  serial_acia_update_irq(p_serial);
}
