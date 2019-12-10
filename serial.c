#include "serial.h"

#include <err.h>
#include <stdlib.h>
#include <string.h>

struct serial_struct {
  struct state_6502* p_state_6502;
};

struct serial_struct*
serial_create(struct state_6502* p_state_6502) {
  struct serial_struct* p_serial = malloc(sizeof(struct serial_struct));
  if (p_serial == NULL) {
    errx(1, "cannot allocate serial_struct");
  }

  (void) memset(p_serial, '\0', sizeof(struct serial_struct));

  p_serial->p_state_6502 = p_state_6502;

  return p_serial;
}

void
serial_destroy(struct serial_struct* p_serial) {
  free(p_serial);
}
