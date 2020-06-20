#ifndef BEEBJIT_KEYBOARD_H
#define BEEBJIT_KEYBOARD_H

#include <stdint.h>

struct keyboard_struct;

struct bbc_options;
struct timing_struct;

enum {
  k_keyboard_key_escape = 128,
  k_keyboard_key_backspace = 129,
  k_keyboard_key_tab = 130,
  k_keyboard_key_enter = 131,
  k_keyboard_key_ctrl = 132,
  k_keyboard_key_shift_left = 133,
  k_keyboard_key_shift_right = 134,
  k_keyboard_key_caps_lock = 135,
  k_keyboard_key_f0 = 136,
  k_keyboard_key_f1 = 137,
  k_keyboard_key_f2 = 138,
  k_keyboard_key_f3 = 139,
  k_keyboard_key_f4 = 140,
  k_keyboard_key_f5 = 141,
  k_keyboard_key_f6 = 142,
  k_keyboard_key_f7 = 143,
  k_keyboard_key_f8 = 144,
  k_keyboard_key_f9 = 145,
  k_keyboard_key_arrow_up = 146,
  k_keyboard_key_arrow_down = 147,
  k_keyboard_key_arrow_left = 148,
  k_keyboard_key_arrow_right = 149,
  k_keyboard_key_alt_left = 150,
  k_keyboard_key_f11 = 151,
  k_keyboard_key_f12 = 152,
  k_keyboard_key_end = 153,
  k_keyboard_key_page_up = 154,
  k_keyboard_key_page_down = 155,
  k_keyboard_key_windows = 155,
  k_keyboard_key_delete = 156,
};

struct keyboard_struct* keyboard_create(struct timing_struct* p_timing,
                                        struct bbc_options* p_options);
void keyboard_destroy(struct keyboard_struct* p_keyboard);

void keyboard_set_virtual_updated_callback(struct keyboard_struct* p_keyboard,
                                           void (*p_callback)(void*),
                                           void* p_callback_object);
void keyboard_set_fast_mode_callback(struct keyboard_struct* p_keyboard,
                                     void (*p_set_fast_mode_callback)(void* p,
                                                                      int fast),
                                     void* p_set_fast_mode_callback_object);

void keyboard_power_on_reset(struct keyboard_struct* p_keyboard);

void keyboard_set_capture_file_name(struct keyboard_struct* p_keyboard,
                                    const char* p_name);
void keyboard_set_replay_file_name(struct keyboard_struct* p_keyboard,
                                   const char* p_name);
int keyboard_is_capturing(struct keyboard_struct* p_keyboard);
int keyboard_is_replaying(struct keyboard_struct* p_keyboard);
void keyboard_end_replay(struct keyboard_struct* p_keyboard);
int keyboard_can_rewind(struct keyboard_struct* p_keyboard);
void keyboard_rewind(struct keyboard_struct* p_keyboard, uint64_t stop_cycles);

void keyboard_read_queue(struct keyboard_struct* p_keyboard);

int keyboard_bbc_is_key_pressed(struct keyboard_struct* p_keyboard,
                                uint8_t row,
                                uint8_t col);
int keyboard_bbc_is_key_column_pressed(struct keyboard_struct* p_keyboard,
                                       uint8_t col);
int keyboard_bbc_is_any_key_pressed(struct keyboard_struct* p_keyboard);

int keyboard_consume_key_press(struct keyboard_struct* p_keyboard, uint8_t key);
int keyboard_consume_alt_key_press(struct keyboard_struct* p_keyboard,
                                   uint8_t key);

void keyboard_release_all_physical_keys(struct keyboard_struct* p_keyboard);

/* Callbacks from the system code. */
void keyboard_system_key_pressed(struct keyboard_struct* p_keyboard,
                                 uint8_t key);
void keyboard_system_key_released(struct keyboard_struct* p_keyboard,
                                  uint8_t key);

#endif /* BEEBJIT_KEYBOARD_H */
