#include "serial_ula.h"

#include "bbc_options.h"
#include "log.h"
#include "mc6850.h"
#include "os_terminal.h"
#include "tape.h"
#include "util.h"

enum {
  k_serial_ula_rs423 = 0x40,
  k_serial_ula_motor = 0x80,
};

struct serial_ula_struct {
  struct mc6850_struct* p_serial;
  struct tape_struct* p_tape;
  void (*set_fast_mode_callback)(void* p, int fast);
  void* p_set_fast_mode_object;
  /* Virtual device connected to RS423. */
  intptr_t handle_input;
  intptr_t handle_output;

  int is_rs423_selected;
  int is_motor_on;
  uint64_t tape_carrier_count;
  int is_tape_DCD;

  int is_fasttape;
  int log_state;
};

static void
serial_ula_transmit_ready_callback(void* p) {
  struct serial_ula_struct* p_serial_ula = (struct serial_ula_struct*) p;

  if (p_serial_ula->is_rs423_selected) {
    return;
  }

  /* If the tape is selected, just consume the byte immediately. It's needed
   * to get Pro Boxing Simulator loading.
   */
  (void) mc6850_transmit(p_serial_ula->p_serial);
}

struct serial_ula_struct*
serial_ula_create(struct mc6850_struct* p_serial,
                  struct tape_struct* p_tape,
                  int is_fasttape,
                  struct bbc_options* p_options) {
  struct serial_ula_struct* p_serial_ula =
      util_mallocz(sizeof(struct serial_ula_struct));

  p_serial_ula->p_serial = p_serial;
  p_serial_ula->p_tape = p_tape;
  p_serial_ula->is_fasttape = is_fasttape;

  p_serial_ula->handle_input = -1;
  p_serial_ula->handle_output = -1;

  p_serial_ula->log_state = util_has_option(p_options->p_log_flags,
                                            "serial:state");

  tape_set_serial_ula(p_tape, p_serial_ula);
  mc6850_set_transmit_ready_callback(p_serial,
                                     serial_ula_transmit_ready_callback,
                                     p_serial_ula);

  return p_serial_ula;
}

void
serial_ula_destroy(struct serial_ula_struct* p_serial_ula) {
  struct tape_struct* p_tape = p_serial_ula->p_tape;

  tape_set_serial_ula(p_tape, NULL);

  if (tape_is_playing(p_tape)) {
    tape_stop(p_tape);
  }

  util_free(p_serial_ula);
}

void
serial_ula_set_fast_mode_callback(
    struct serial_ula_struct* p_serial_ula,
    void (*set_fast_mode_callback)(void* p, int fast),
    void* p_set_fast_mode_object) {
  p_serial_ula->set_fast_mode_callback = set_fast_mode_callback;
  p_serial_ula->p_set_fast_mode_object = p_set_fast_mode_object;
}

void
serial_ula_set_io_handles(struct serial_ula_struct* p_serial_ula,
                          intptr_t handle_input,
                          intptr_t handle_output) {
  p_serial_ula->handle_input = handle_input;
  p_serial_ula->handle_output = handle_output;
}

static void
serial_ula_update_mc6850_logic_lines(struct serial_ula_struct* p_serial_ula) {
  int is_CTS;
  int is_DCD;
  struct mc6850_struct* p_serial = p_serial_ula->p_serial;

  /* CTS. When tape is selected, CTS is always low (meaning active). For RS423,
   * it is high (meaning inactive) unless we've connected a virtual device on
   * the other end.
   */
  if (p_serial_ula->is_rs423_selected) {
    if (p_serial_ula->handle_output != -1) {
      is_CTS = 0;
    } else {
      is_CTS = 1;
    }
  } else {
    is_CTS = 0;
  }

  /* DCD. In the tape case, it depends on a carrier tone on the tape.
   * For the RS423 case, AUG clearly states: "It will always be low when the
   * RS423 interface is selected".
   */
  if (p_serial_ula->is_rs423_selected) {
    is_DCD = 0;
  } else {
    is_DCD = p_serial_ula->is_tape_DCD;
  }

  mc6850_set_DCD(p_serial, is_DCD);
  mc6850_set_CTS(p_serial, is_CTS);
}

void
serial_ula_power_on_reset(struct serial_ula_struct* p_serial_ula) {
  if (p_serial_ula->is_motor_on) {
    tape_stop(p_serial_ula->p_tape);
  }

  p_serial_ula->tape_carrier_count = 0;
  p_serial_ula->is_tape_DCD = 0;

  p_serial_ula->is_motor_on = 0;
  p_serial_ula->is_rs423_selected = 0;

  serial_ula_update_mc6850_logic_lines(p_serial_ula);
}

uint8_t
serial_ula_read(struct serial_ula_struct* p_serial_ula) {
  /* EMU NOTE: returns 0 on a real beeb, but appears to have side effects.
   * The side effect is as if 0xFE had been written to this same register.
   * Rich Talbot-Watkins came up with a good theory as to why: the serial ULA
   * doesn't have a read/write bit, so selecting it will always write. And
   * on a 6502 read cycle, the bus value will be the high byte of the address,
   * 0xFE of 0xFE10 in this case.
   */
  serial_ula_write(p_serial_ula, 0xFE);

  return 0;
}

void
serial_ula_write(struct serial_ula_struct* p_serial_ula, uint8_t val) {
  static const char* p_rate_strs[] = {
    "1228.8k", "76.8k", "307.2k", "9.6k", "614.4k", "19.2k", "153.6k", "4.8k"
  };
  int is_rs423_selected = !!(val & k_serial_ula_rs423);
  int is_motor_on = !!(val & k_serial_ula_motor);

  if (p_serial_ula->log_state) {
    log_do_log(k_log_serial,
               k_log_info,
               "serial ULA control: [R %s] [T %s] [%s] [%s]",
               p_rate_strs[(val >> 3) & 7],
               p_rate_strs[val & 7],
               (is_rs423_selected ? "RS423" : "TAPE"),
               (is_motor_on ? "MOTOR" : ""));
  }

  if (is_motor_on && !p_serial_ula->is_motor_on) {
    tape_play(p_serial_ula->p_tape);
    if (p_serial_ula->is_fasttape) {
      if (p_serial_ula->set_fast_mode_callback != NULL) {
        p_serial_ula->set_fast_mode_callback(
            p_serial_ula->p_set_fast_mode_object, 1);
      }
    }
  } else if (!is_motor_on && p_serial_ula->is_motor_on) {
    tape_stop(p_serial_ula->p_tape);
    if (p_serial_ula->is_fasttape) {
      if (p_serial_ula->set_fast_mode_callback != NULL) {
        p_serial_ula->set_fast_mode_callback(
            p_serial_ula->p_set_fast_mode_object, 0);
      }
    }
  }

  p_serial_ula->is_motor_on = is_motor_on;
  p_serial_ula->is_rs423_selected = is_rs423_selected;

  /* Selecting the ACIA's connection between RS423 vs. tape will update the
   * physical line levels, as can switching off the tape motor if tape is
   * selected.
   */
  serial_ula_update_mc6850_logic_lines(p_serial_ula);
}

void
serial_ula_receive_tape_bit(struct serial_ula_struct* p_serial_ula,
                            int8_t bit) {
  p_serial_ula->is_tape_DCD = 0;

  if (bit == k_tape_bit_silence) {
    p_serial_ula->tape_carrier_count = 0;
  } else {
    p_serial_ula->tape_carrier_count++;
    /* The tape hardware doesn't raise DCD until the carrier tone has persisted      * for a while. The BBC service manual opines,
     * "The DCD flag in the 6850 should change 0.1 to 0.4 seconds after a
     * continuous tone appears".
     * We use ~0.17s, measured on an issue 3 model B.
     * Star Drifter doesn't load without this.
     * Testing on real hardware, DCD is blipped, it lowers about 210us after it      * raises, even though the carrier tone may be continuing.
     */
    if (p_serial_ula->tape_carrier_count == 20) {
      p_serial_ula->is_tape_DCD = 1;
    }
  }

  serial_ula_update_mc6850_logic_lines(p_serial_ula);

  if (!p_serial_ula->is_rs423_selected) {
    /* Silence will send a 0 bit. */
    int serial_bit = 0;
    if (bit == k_tape_bit_1) {
      serial_bit = 1;
    }
    mc6850_receive_bit(p_serial_ula->p_serial, serial_bit);
  }
}

void
serial_ula_tick(struct serial_ula_struct* p_serial_ula) {
  struct mc6850_struct* p_serial;
  intptr_t handle_input;
  intptr_t handle_output;

  if (!p_serial_ula->is_rs423_selected) {
    return;
  }

  p_serial = p_serial_ula->p_serial;
  handle_input = p_serial_ula->handle_input;
  handle_output = p_serial_ula->handle_output;

  /* Check for external serial input. */
  if ((handle_input != -1) && mc6850_get_RTS(p_serial)) {
    /* TODO: this doesn't seem correct. The serial connection may not be via
     * a host terminal?
     */
    int has_bytes = os_terminal_has_readable_bytes(handle_input);
    if (has_bytes) {
      uint8_t val;
      int ret = os_terminal_handle_read_byte(handle_input, &val);
      if (ret) {
        /* Rewrite \n to \r for BBC style input. */
        if (val == '\n') {
          val = '\r';
        }
        mc6850_receive(p_serial, val);
      }
    }
  }

  /* Check for external serial output. */
  if ((handle_output != -1) && mc6850_is_transmit_ready(p_serial)) {
    uint8_t val = mc6850_transmit(p_serial);
    /* NOTE: no suppression of \r in the BBC stream's newlines. */
    /* This may block; we rely on the host end to be faster than our BBC! */
    (void) os_terminal_handle_write_byte(handle_output, val);
  }
}
