#include "serial.h"

#include "state_6502.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  k_serial_acia_TCB_RTS_and_TIE = 0x20,
};

enum {
  k_serial_ula_rs423 = 0x40,
  k_serial_ula_motor = 0x80,
};

struct serial_struct {
  struct state_6502* p_state_6502;
  uint8_t acia_control;
  uint8_t acia_status;
  uint8_t acia_receive;
  uint8_t acia_transmit;
  int line_level_DCD;
  int line_level_CTS;
};

static void
serial_acia_update_irq(struct serial_struct* p_serial) {
  int send_int;
  int receive_int;
  int do_int;

  send_int = ((p_serial->acia_control & k_serial_acia_control_TCB_mask) ==
              k_serial_acia_TCB_RTS_and_TIE);
  send_int &= !!(p_serial->acia_status & k_serial_acia_status_TDRE);

  receive_int = !!(p_serial->acia_control & k_serial_acia_control_RIE);
  receive_int &= !!(p_serial->acia_status & k_serial_acia_status_RDRF);

  do_int = (send_int | receive_int);

  /* Bit 7 of the control register must be high if we're asserting IRQ. */
  p_serial->acia_status &= ~k_serial_acia_status_IRQ;
  if (do_int) {
    p_serial->acia_status |= k_serial_acia_status_IRQ;
  }

  state_6502_set_irq_level(p_serial->p_state_6502,
                           k_state_6502_irq_serial_acia,
                           do_int);
}

static void
serial_update_line_levels(struct serial_struct* p_serial,
                          int line_level_DCD,
                          int line_level_CTS) {
  /* TODO: nothing raises DCD yet because we don't support the cassette
   * interface. But when we do, we'll need to support DCD interrupting and
   * DCD staying high in the status register until data is read.
   */
  p_serial->acia_status &= ~k_serial_acia_status_DCD;
  if (line_level_DCD) {
    p_serial->acia_status |= k_serial_acia_status_DCD;
  }

  p_serial->acia_status &= ~k_serial_acia_status_CTS;
  if (line_level_CTS) {
    p_serial->acia_status |= k_serial_acia_status_CTS;
  }

  p_serial->line_level_DCD = line_level_DCD;
  p_serial->line_level_CTS = line_level_CTS;
}

static void
serial_acia_reset(struct serial_struct* p_serial) {
  p_serial->acia_receive = 0;
  p_serial->acia_transmit = 0;

  p_serial->acia_status = 0;

  /* Clear RDRF (receive data register full). */
  p_serial->acia_status &= ~k_serial_acia_status_RDRF;
  /* Set TDRE (transmit data register empty). */
  p_serial->acia_status |= k_serial_acia_status_TDRE;

  /* Reset of the ACIA cannot change external line levels. Make sure they are
   * kept.
   */
  serial_update_line_levels(p_serial,
                            p_serial->line_level_DCD,
                            p_serial->line_level_CTS);

  serial_acia_update_irq(p_serial);
}

struct serial_struct*
serial_create(struct state_6502* p_state_6502) {
  struct serial_struct* p_serial = malloc(sizeof(struct serial_struct));
  if (p_serial == NULL) {
    errx(1, "cannot allocate serial_struct");
  }

  (void) memset(p_serial, '\0', sizeof(struct serial_struct));

  p_serial->p_state_6502 = p_state_6502;

  p_serial->acia_control = 0;
  p_serial->acia_status = 0;
  p_serial->acia_receive = 0;
  p_serial->acia_transmit = 0;

  p_serial->line_level_DCD = 0;
  p_serial->line_level_CTS = 0;

  serial_acia_reset(p_serial);

  return p_serial;
}

void
serial_destroy(struct serial_struct* p_serial) {
  free(p_serial);
}

uint8_t
serial_acia_read(struct serial_struct* p_serial, uint8_t reg) {
  if (reg == 0) {
    /* Status register. */
    uint8_t ret = p_serial->acia_status;

    /* MC6850: "A low CTS indicates that there is a Clear-to-Send from the
     * modem. In the high state, the Transmit Data Register Empty bit is
     * inhibited".
     */
    if (p_serial->line_level_CTS) {
      ret &= ~k_serial_acia_status_TDRE;
    }

    return ret;
  } else {
    assert(0);
    return 0;
  }
}

void
serial_acia_write(struct serial_struct* p_serial, uint8_t reg, uint8_t val) {
  if (reg == 0) {
    /* Control register. */
    if ((val & 0x03) == 0x03) {
      /* TODO: the data sheet suggests this doesn't affect any control register
       * bits and only does a reset.
       * Testing on a real machine shows possible disagreement here.
       */
      serial_acia_reset(p_serial);
    } else {
      p_serial->acia_control = val;
    }
  } else {
assert(0);
    /* Data register, transmit byte. */
    assert(reg == 1);

    p_serial->acia_transmit = val;

    /* Clear TDRE (transmit data register empty). */
    p_serial->acia_status &= ~k_serial_acia_status_TDRE;
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
  int line_level_DCD;
  int line_level_CTS;

  int rs423_or_tape = !!(val & k_serial_ula_rs423);
  int motor_on = !!(val & k_serial_ula_motor);

  (void) motor_on;

  /* Selecting the ACIA's connection between RS423 vs. tape will update the
   * physical line levels.
   */
  /* DCD. We don't emulate tape yet so DCD is always low in the tape case.
   * For the RS423 case, AUG clearly states: "It will always be low when the
   * RS423 interface is selected".
   */
  line_level_DCD = 0;

  /* CTS. When tape is selected, CTS is always low (meaning active). For RS423,
   * it is high (meaning inactive) unless we've connected a virtual device on
   * the other end.
   */
  if (rs423_or_tape) {
    line_level_CTS = 1;
  } else {
    line_level_CTS = 0;
  }

  serial_update_line_levels(p_serial, line_level_DCD, line_level_CTS);
}
