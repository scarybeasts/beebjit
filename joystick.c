#include "joystick.h"

#include "adc.h"
#include "keyboard.h"
#include "util.h"
#include "via.h"

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
  int is_fire;
  int is_left;
  int is_right;
  int is_up;
  int is_down;
  uint8_t via_val;
  uint16_t adc_val;
  struct keyboard_struct* p_keyboard;
  struct adc_struct* p_adc;

  if (!p_joystick->is_use_keyboard) {
    return;
  }

  p_keyboard = p_joystick->p_keyboard;
  p_adc = p_joystick->p_adc;

  is_fire = keyboard_is_key_down(p_keyboard, k_keyboard_key_page_down);
  is_left = keyboard_is_key_down(p_keyboard, k_keyboard_key_arrow_left);
  is_right = keyboard_is_key_down(p_keyboard, k_keyboard_key_arrow_right);
  is_up = keyboard_is_key_down(p_keyboard, k_keyboard_key_arrow_up);
  is_down = keyboard_is_key_down(p_keyboard, k_keyboard_key_arrow_down);
  via_val = 0xFF;
  if (is_fire) {
    via_val &= ~0x10;
  }
  via_set_peripheral_b(p_joystick->p_system_via, via_val);

  if (is_left && !is_right) {
    adc_val = 0xFFFF;
  } else if (is_right && !is_left) {
    adc_val = 0;
  } else {
    adc_val = 0x8000;
  }
  adc_set_channel_value(p_adc, 0, adc_val);

  if (is_up && !is_down) {
    adc_val = 0xFFFF;
  } else if (is_down && !is_up) {
    adc_val = 0;
  } else {
    adc_val = 0x8000;
  }
  adc_set_channel_value(p_adc, 1, adc_val);
}
