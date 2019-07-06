#ifndef BEEBJIT_KEYBOARD_H
#define BEEBJIT_KEYBOARD_H

#include <stdint.h>

struct keyboard_struct;

struct keyboard_struct* keyboard_create();
void keyboard_destroy(struct keyboard_struct* p_keyboard);

int keyboard_bbc_is_key_pressed(struct keyboard_struct* p_keyboard,
                                uint8_t row,
                                uint8_t col);
int keyboard_bbc_is_key_column_pressed(struct keyboard_struct* p_keyboard,
                                       uint8_t col);
int keyboard_bbc_is_any_key_pressed(struct keyboard_struct* p_keyboard);

/* Callbacks from the system code. */
void keyboard_system_key_pressed(struct keyboard_struct* p_keyboard, int key);
void keyboard_system_key_released(struct keyboard_struct* p_keyboard, int key);

#endif /* BEEBJIT_KEYBOARD_H */
