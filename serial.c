#include "serial.h"

#include "bbc_options.h"
#include "log.h"
#include "os_terminal.h"
#include "state_6502.h"
#include "tape.h"
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

enum {
  k_serial_ula_rs423 = 0x40,
  k_serial_ula_motor = 0x80,
};

struct serial_struct {
  struct state_6502* p_state_6502;
  void (*set_fast_mode_callback)(void* p, int fast);
  void* p_set_fast_mode_object;
  uint8_t acia_control;
  uint8_t acia_status;
  uint8_t acia_receive;
  uint8_t acia_transmit;
  int line_level_DCD;
  int line_level_CTS;

  int serial_ula_rs423_selected;
  int serial_ula_motor_on;

  uint32_t serial_tape_carrier_count;
  int serial_tape_line_level_DCD;

  /* Virtual device connected to RS423. */
  intptr_t handle_input;
  intptr_t handle_output;

  /* Tape device, part of the serial ULA and feeding to the ACIA. */
  struct tape_struct* p_tape;

  int fasttape_flag;
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
    do_fire_send_int &= !p_serial->line_level_CTS;
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

static void
serial_check_line_levels(struct serial_struct* p_serial) {
  int line_level_CTS;
  int line_level_DCD;

  /* CTS. When tape is selected, CTS is always low (meaning active). For RS423,
   * it is high (meaning inactive) unless we've connected a virtual device on
   * the other end.
   */
  if (p_serial->serial_ula_rs423_selected) {
    if (p_serial->handle_output != -1) {
      line_level_CTS = 0;
    } else {
      line_level_CTS = 1;
    }
  } else {
    line_level_CTS = 0;
  }

  /* DCD. In the tape case, it depends on a carrier tone on the tape.
   * For the RS423 case, AUG clearly states: "It will always be low when the
   * RS423 interface is selected".
   */
  if (p_serial->serial_ula_rs423_selected) {
    line_level_DCD = 0;
  } else {
    line_level_DCD = p_serial->serial_tape_line_level_DCD;
  }

  /* The DCD low to high edge causes a bit latch, and potentially an IRQ.
   * DCD going from high to low doesn't affect the status register until the
   * bit latch is cleared by reading the data register.
   */
  if (line_level_DCD && !p_serial->line_level_DCD) {
    if (p_serial->log_state) {
      log_do_log(k_log_serial, k_log_info, "DCD going high");
    }
    p_serial->acia_status |= k_serial_acia_status_DCD;
  }

  p_serial->acia_status &= ~k_serial_acia_status_CTS;
  if (line_level_CTS) {
    p_serial->acia_status |= k_serial_acia_status_CTS;
  }

  p_serial->line_level_DCD = line_level_DCD;
  p_serial->line_level_CTS = line_level_CTS;

  /* Lowering CTS could fire the interrupt for transmit. */
  serial_acia_update_irq(p_serial);
}

struct serial_struct*
serial_create(struct state_6502* p_state_6502,
              int fasttape_flag,
              struct bbc_options* p_options) {
  struct serial_struct* p_serial = util_mallocz(sizeof(struct serial_struct));

  p_serial->p_state_6502 = p_state_6502;
  p_serial->fasttape_flag = fasttape_flag;

  p_serial->handle_input = -1;
  p_serial->handle_output = -1;

  p_serial->log_state = util_has_option(p_options->p_log_flags, "serial:state");
  p_serial->log_bytes = util_has_option(p_options->p_log_flags, "serial:bytes");

  return p_serial;
}

void
serial_destroy(struct serial_struct* p_serial) {
  struct tape_struct* p_tape = p_serial->p_tape;

  tape_set_status_callback(p_tape, NULL, NULL);

  if (tape_is_playing(p_tape)) {
    tape_stop(p_tape);
  }

  util_free(p_serial);
}

void
serial_set_fast_mode_callback(struct serial_struct* p_serial,
                              void (*set_fast_mode_callback)(void* p, int fast),
                              void* p_set_fast_mode_object) {
  p_serial->set_fast_mode_callback = set_fast_mode_callback;
  p_serial->p_set_fast_mode_object = p_set_fast_mode_object;
}

void
serial_set_io_handles(struct serial_struct* p_serial,
                      intptr_t handle_input,
                      intptr_t handle_output) {
  p_serial->handle_input = handle_input;
  p_serial->handle_output = handle_output;
}

static void
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

static void
serial_tape_receive_status(void* p, int carrier, int32_t byte) {
  struct serial_struct* p_serial = (struct serial_struct*) p;

  if (!carrier) {
    p_serial->serial_tape_carrier_count = 0;
    p_serial->serial_tape_line_level_DCD = 0;
  } else {
    p_serial->serial_tape_carrier_count++;
    /* The tape hardware doesn't raise DCD until the carrier tone has persisted      * for a while. The BBC service manual opines,
     * "The DCD flag in the 6850 should change 0.1 to 0.4 seconds after a
     * continuous tone appears".
     * We use ~0.17s, measured on an issue 3 model B.
     * Star Drifter doesn't load without this.
     * Testing on real hardware, DCD is blipped, it lowers about 210us after it      * raises, even though the carrier tone may be continuing.
     */
    if (p_serial->serial_tape_carrier_count == 20) {
      p_serial->serial_tape_line_level_DCD = 1;
    } else {
      p_serial->serial_tape_line_level_DCD = 0;
    }
  }

  serial_check_line_levels(p_serial);

  if ((byte >= 0) && !p_serial->serial_ula_rs423_selected) {
    serial_receive(p_serial, byte);
  }
}

void
serial_set_tape(struct serial_struct* p_serial, struct tape_struct* p_tape) {
  assert(p_serial->p_tape == NULL);
  p_serial->p_tape = p_tape;

  tape_set_status_callback(p_tape, serial_tape_receive_status, p_serial);
}

static void
serial_acia_power_on_reset(struct serial_struct* p_serial) {
  /* Power on reset is currently the same as writing master reset to CR. */
  if (p_serial->log_state) {
    log_do_log(k_log_serial, k_log_info, "reset");
  }

  p_serial->acia_receive = 0;
  p_serial->acia_transmit = 0;

  /* Set TDRE (transmit data register empty). Clear everything else. */
  p_serial->acia_status = k_serial_acia_status_TDRE;

  p_serial->acia_control = 0;

  /* Reset of the ACIA cannot change external line levels. Make sure they are
   * kept.
   */
  serial_check_line_levels(p_serial);

  serial_acia_update_irq(p_serial);
}

static void
serial_ula_power_on_reset(struct serial_struct* p_serial) {
  if (p_serial->serial_ula_motor_on) {
    tape_stop(p_serial->p_tape);
  }
  assert(p_serial->serial_tape_carrier_count == 0);
  assert(p_serial->serial_tape_line_level_DCD == 0);

  p_serial->serial_ula_motor_on = 0;
  p_serial->serial_ula_rs423_selected = 0;
}

void
serial_power_on_reset(struct serial_struct* p_serial) {
  serial_acia_power_on_reset(p_serial);
  serial_ula_power_on_reset(p_serial);

  serial_check_line_levels(p_serial);
}

void
serial_tick(struct serial_struct* p_serial) {
  if (!p_serial->serial_ula_rs423_selected) {
    return;
  }

  /* Check for external serial input. */
  if (p_serial->handle_input != -1) {
    int do_receive = ((p_serial->acia_control &
                       k_serial_acia_control_TCB_mask) !=
                      k_serial_acia_TCB_no_RTS_no_TIE);
    do_receive &= !(p_serial->acia_status & k_serial_acia_status_RDRF);
    if (do_receive) {
      /* TODO: this doesn't seem correct. The serial connection may not be via
       * a host terminal?
       */
      int has_bytes = os_terminal_has_readable_bytes(p_serial->handle_input);
      if (has_bytes) {
        uint8_t val;
        int ret = os_terminal_handle_read_byte(p_serial->handle_input, &val);
        if (ret) {
          /* Rewrite \n to \r for BBC style input. */
          if (val == '\n') {
            val = '\r';
          }
          serial_receive(p_serial, val);
        }
      }
    }
  }

  /* Check for external serial output. */
  if (p_serial->handle_output != -1) {
    int do_send = !(p_serial->acia_status & k_serial_acia_status_TDRE);
    if (do_send) {
      uint8_t val = p_serial->acia_transmit;
      /* NOTE: no suppression of \r in the BBC stream's newlines. */
      /* This may block; we rely on the host end to be faster than our BBC! */
      int ret = os_terminal_handle_write_byte(p_serial->handle_output, val);
      if (ret) {
        p_serial->acia_status |= k_serial_acia_status_TDRE;
        serial_acia_update_irq(p_serial);
      }
    }
  }
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
    if (p_serial->line_level_CTS) {
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
      if (p_serial->line_level_DCD) {
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
      serial_acia_power_on_reset(p_serial);
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

    if (!p_serial->serial_ula_rs423_selected) {
      /* If the tape is selected, just consume the byte immediately. It's needed
       * to get Pro Boxing Simulator loading.
       */
      p_serial->acia_status |= k_serial_acia_status_TDRE;
    } else {
      /* Clear TDRE (transmit data register empty). */
      p_serial->acia_status &= ~k_serial_acia_status_TDRE;
    }
  }

  serial_acia_update_irq(p_serial);
}

uint8_t
serial_ula_read(struct serial_struct* p_serial) {
  /* EMU NOTE: returns 0 on a real beeb, but appears to have side effects.
   * The side effect is as if 0xFE had been written to this same register.
   * Rich Talbot-Watkins came up with a good theory as to why: the serial ULA
   * doesn't have a read/write bit, so selecting it will always write. And
   * on a 6502 read cycle, the bus value will be the high byte of the address,
   * 0xFE of 0xFE10 in this case.
   */
  serial_ula_write(p_serial, 0xFE);

  return 0;
}

void
serial_ula_write(struct serial_struct* p_serial, uint8_t val) {
  static const char* p_rate_strs[] = {
    "1228.8k", "76.8k", "307.2k", "9.6k", "614.4k", "19.2k", "153.6k", "4.8k"
  };
  int rs423_or_tape = !!(val & k_serial_ula_rs423);
  int motor_on = !!(val & k_serial_ula_motor);

  if (p_serial->log_state) {
    log_do_log(k_log_serial,
               k_log_info,
               "serial ULA control: [R %s] [T %s] [%s] [%s]",
               p_rate_strs[(val >> 3) & 7],
               p_rate_strs[val & 7],
               (rs423_or_tape ? "RS423" : "TAPE"),
               (motor_on ? "MOTOR" : ""));
  }

  if (motor_on && !p_serial->serial_ula_motor_on) {
    tape_play(p_serial->p_tape);
    if (p_serial->fasttape_flag) {
      if (p_serial->set_fast_mode_callback != NULL) {
        p_serial->set_fast_mode_callback(p_serial->p_set_fast_mode_object, 1);
      }
    }
  } else if (!motor_on && p_serial->serial_ula_motor_on) {
    tape_stop(p_serial->p_tape);
    if (p_serial->fasttape_flag) {
      if (p_serial->set_fast_mode_callback != NULL) {
        p_serial->set_fast_mode_callback(p_serial->p_set_fast_mode_object, 0);
      }
    }
  }

  p_serial->serial_ula_motor_on = motor_on;
  p_serial->serial_ula_rs423_selected = rs423_or_tape;

  /* Selecting the ACIA's connection between RS423 vs. tape will update the
   * physical line levels, as can switching off the tape motor if tape is
   * selected.
   */
  serial_check_line_levels(p_serial);
}
