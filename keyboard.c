#include "keyboard.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct keyboard_struct {
  uint8_t keys[16][16];
  uint8_t keys_count;
  uint8_t keys_count_col[16];
};

struct keyboard_struct*
keyboard_create() {
  struct keyboard_struct* p_keyboard = malloc(sizeof(struct keyboard_struct));
  if (p_keyboard == NULL) {
    errx(1, "cannot allocate keyboard_struct");
  }

  (void) memset(p_keyboard, '\0', sizeof(struct keyboard_struct));

  return p_keyboard;
}

void
keyboard_destroy(struct keyboard_struct* p_keyboard) {
  free(p_keyboard);
}

static void
keyboard_bbc_key_to_rowcol(int key, int32_t* p_row, int32_t* p_col) {
  int32_t row = -1;
  int32_t col = -1;
  switch (key) {
  case 9: /* Escape */
    row = 7;
    col = 0;
    break;
  case 10: /* 1 */
    row = 3;
    col = 0;
    break;
  case 11: /* 2 */
    row = 3;
    col = 1;
    break;
  case 12: /* 3 */
    row = 1;
    col = 1;
    break;
  case 13: /* 4 */
    row = 1;
    col = 2;
    break;
  case 14: /* 5 */
    row = 1;
    col = 3;
    break;
  case 15: /* 6 */
    row = 3;
    col = 4;
    break;
  case 16: /* 7 */
    row = 2;
    col = 4;
    break;
  case 17: /* 8 */
    row = 1;
    col = 5;
    break;
  case 18: /* 9 */
    row = 2;
    col = 6;
    break;
  case 19: /* 0 */
    row = 2;
    col = 7;
    break;
  case 20: /* - */
    row = 1;
    col = 7;
    break;
  case 21: /* = (BBC ^) */
    row = 1;
    col = 8;
    break;
  case 22: /* Backspace / (BBC DELETE) */
    row = 5;
    col = 9;
    break;
  case 23: /* Tab */
    row = 6;
    col = 0;
    break;
  case 24: /* Q */
    row = 1;
    col = 0;
    break;
  case 25: /* W */
    row = 2;
    col = 1;
    break;
  case 26: /* E */
    row = 2;
    col = 2;
    break;
  case 27: /* R */
    row = 3;
    col = 3;
    break;
  case 28: /* T */
    row = 2;
    col = 3;
    break;
  case 29: /* Y */
    row = 4;
    col = 4;
    break;
  case 30: /* U */
    row = 3;
    col = 5;
    break;
  case 31: /* I */
    row = 2;
    col = 5;
    break;
  case 32: /* O */
    row = 3;
    col = 6;
    break;
  case 33: /* P */
    row = 3;
    col = 7;
    break;
  case 34: /* [ (BBC @) */
    row = 4;
    col = 7;
    break;
  case 35: /* ] (BBC [) */
    row = 3;
    col = 8;
    break;
  case 36: /* Enter (BBC RETURN) */
    row = 4;
    col = 9;
    break;
  case 37: /* Ctrl */
    row = 0;
    col = 1;
    break;
  case 38: /* A */
    row = 4;
    col = 1;
    break;
  case 39: /* S */
    row = 5;
    col = 1;
    break;
  case 40: /* D */
    row = 3;
    col = 2;
    break;
  case 41: /* F */
    row = 4;
    col = 3;
    break;
  case 42: /* G */
    row = 5;
    col = 3;
    break;
  case 43: /* H */
    row = 5;
    col = 4;
    break;
  case 44: /* J */
    row = 4;
    col = 5;
    break;
  case 45: /* K */
    row = 4;
    col = 6;
    break;
  case 46: /* L */
    row = 5;
    col = 6;
    break;
  case 47: /* ; */
    row = 5;
    col = 7;
    break;
  case 48: /* ' (BBC colon) */
    row = 4;
    col = 8;
    break;
  case 50: /* Left shift */
    row = 0;
    col = 0;
    break;
  case 51: /* \ (BBC ]) */
    row = 5;
    col = 8;
    break;
  case 52: /* Z */
    row = 6;
    col = 1;
    break;
  case 53: /* X */
    row = 4;
    col = 2;
    break;
  case 54: /* C */
    row = 5;
    col = 2;
    break;
  case 55: /* V */
    row = 6;
    col = 3;
    break;
  case 56: /* B */
    row = 6;
    col = 4;
    break;
  case 57: /* N */
    row = 5;
    col = 5;
    break;
  case 58: /* M */
    row = 6;
    col = 5;
    break;
  case 59: /* , */
    row = 6;
    col = 6;
    break;
  case 60: /* . */
    row = 6;
    col = 7;
    break;
  case 61: /* / */
    row = 6;
    col = 8;
    break;
  case 62: /* Right shift */
    row = 0;
    col = 0;
    break;
  case 65: /* Space */
    row = 6;
    col = 2;
    break;
  case 66: /* Caps Lock */
    row = 4;
    col = 0;
    break;
  case 67: /* F1 */
    row = 7;
    col = 1;
    break;
  case 68: /* F2 */
    row = 7;
    col = 2;
    break;
  case 69: /* F3 */
    row = 7;
    col = 3;
    break;
  case 70: /* F4 */
    row = 1;
    col = 4;
    break;
  case 71: /* F5 */
    row = 7;
    col = 4;
    break;
  case 72: /* F6 */
    row = 7;
    col = 5;
    break;
  case 73: /* F7 */
    row = 1;
    col = 6;
    break;
  case 74: /* F8 */
    row = 7;
    col = 6;
    break;
  case 75: /* F9 */
    row = 7;
    col = 7;
    break;
  case 76: /* F0 */
    row = 2;
    col = 0;
    break;
  case 111: /* Up arrow */
    row = 3;
    col = 9;
    break;
  case 113: /* Left arrow */
    row = 1;
    col = 9;
    break;
  case 114: /* Right arrow */
    row = 7;
    col = 9;
    break;
  case 116: /* Down arrow */
    row = 2;
    col = 9;
    break;
  default:
    printf("warning: unhandled key %d\n", key);
    break;
  }

  *p_row = row;
  *p_col = col;
}

int
keyboard_bbc_is_key_pressed(struct keyboard_struct* p_keyboard,
                            uint8_t row,
                            uint8_t col) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  volatile uint8_t* p_key = &p_keyboard->keys[row][col];
  return *p_key;
}

int
keyboard_bbc_is_key_column_pressed(struct keyboard_struct* p_keyboard,
                                   uint8_t col) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  volatile uint8_t* p_count = &p_keyboard->keys_count_col[col];
  return (*p_count > 0);
}

int
keyboard_bbc_is_any_key_pressed(struct keyboard_struct* p_keyboard) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  volatile uint8_t* p_count = &p_keyboard->keys_count;
  return (*p_count > 0);
}

void
keyboard_system_key_pressed(struct keyboard_struct* p_keyboard, int key) {
  /* Threading model: called from the system thread.
   * Allowed to read/write keyboard state.
   */
  int32_t row;
  int32_t col;
  keyboard_bbc_key_to_rowcol(key, &row, &col);
  if (row == -1 && col == -1) {
    return;
  }
  assert(row >= 0);
  assert(row < 16);
  assert(col >= 0);
  assert(col < 16);
  if (p_keyboard->keys[row][col]) {
    return;
  }
  p_keyboard->keys[row][col] = 1;
  if (row == 0) {
    /* Row 0, notably including shift and ctrl, is not wired to interrupt. */
    return;
  }
  p_keyboard->keys_count_col[col]++;
  p_keyboard->keys_count++;
}

void
keyboard_system_key_released(struct keyboard_struct* p_keyboard, int key) {
  /* Threading model: called from the system thread.
   * Allowed to read/write keyboard state.
   * There's no other writer thread, so updates (such as increment / decrement
   * of counters) don't need to be atomic. However, the reader should be aware
   * that keyboard state is changing asynchronously.
   * e.g. keyboard_bbc_is_any_key_pressed() could return 0 but an immediately
   * following specific keyboard_bbc_is_key_pressed() could return 1.
   */
  int32_t row;
  int32_t col;
  int was_pressed;
  keyboard_bbc_key_to_rowcol(key, &row, &col);
  if (row == -1 && col == -1) {
    return;
  }
  assert(row >= 0);
  assert(row < 16);
  assert(col >= 0);
  assert(col < 16);
  was_pressed = p_keyboard->keys[row][col];
  p_keyboard->keys[row][col] = 0;
  if (row == 0) {
    /* Row 0, notably including shift and ctrl, is not wired to interrupt. */
    return;
  }
  if (was_pressed) {
    assert(p_keyboard->keys_count_col[col] > 0);
    p_keyboard->keys_count_col[col]--;
    assert(p_keyboard->keys_count > 0);
    p_keyboard->keys_count--;
  }
}
