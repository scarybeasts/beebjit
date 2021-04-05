#include "joystick.h"

#include "util.h"

struct joystick_struct {
  struct via_struct* p_system_via;
  struct adc_struct* p_adc;
  struct keyboard_struct* p_keyboard;
  int is_use_keyboard;
};

struct joystick_struct*
joystick_create(struct via_struct* p_system_via,
                struct adc_struct* p_adc,
                struct keyboard_struct* p_keyboard) {
  struct joystick_struct* p_joystick =
      util_mallocz(sizeof(struct joystick_struct));

  p_joystick->p_system_via = p_system_via;
  p_joystick->p_adc = p_adc;
  p_joystick->p_keyboard = p_keyboard;

  return p_joystick;
}

void
joystick_destroy(struct joystick_struct* p_joystick) {
  util_free(p_joystick);
}

void
joystick_set_use_keyboard(struct joystick_struct* p_joystick,
                          int is_use_keyboard) {
  p_joystick->is_use_keyboard = is_use_keyboard;
}

void
joystick_tick(struct joystick_struct* p_joystick) {
  (void) p_joystick;
}
