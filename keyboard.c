#include "keyboard.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

struct keyboard_struct {
  uint8_t bbc_keys[16][16];
  uint8_t bbc_keys_count;
  uint8_t bbc_keys_count_col[16];
  uint8_t key_down[256];
  uint8_t alt_key_pressed[256];
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
keyboard_bbc_key_to_rowcol(uint8_t key, int32_t* p_row, int32_t* p_col) {
  int32_t row = -1;
  int32_t col = -1;
  switch (key) {
  case k_keyboard_key_escape:
    row = 7;
    col = 0;
    break;
  case '1':
    row = 3;
    col = 0;
    break;
  case '2':
    row = 3;
    col = 1;
    break;
  case '3':
    row = 1;
    col = 1;
    break;
  case '4':
    row = 1;
    col = 2;
    break;
  case '5':
    row = 1;
    col = 3;
    break;
  case '6':
    row = 3;
    col = 4;
    break;
  case '7':
    row = 2;
    col = 4;
    break;
  case '8':
    row = 1;
    col = 5;
    break;
  case '9':
    row = 2;
    col = 6;
    break;
  case '0':
    row = 2;
    col = 7;
    break;
  case '-':
    row = 1;
    col = 7;
    break;
  case '=': /* BBC ^ */
    row = 1;
    col = 8;
    break;
  case k_keyboard_key_backspace: /* BBC DELETE */
    row = 5;
    col = 9;
    break;
  case k_keyboard_key_tab:
    row = 6;
    col = 0;
    break;
  case 'Q':
    row = 1;
    col = 0;
    break;
  case 'W':
    row = 2;
    col = 1;
    break;
  case 'E':
    row = 2;
    col = 2;
    break;
  case 'R':
    row = 3;
    col = 3;
    break;
  case 'T':
    row = 2;
    col = 3;
    break;
  case 'Y':
    row = 4;
    col = 4;
    break;
  case 'U':
    row = 3;
    col = 5;
    break;
  case 'I':
    row = 2;
    col = 5;
    break;
  case 'O':
    row = 3;
    col = 6;
    break;
  case 'P':
    row = 3;
    col = 7;
    break;
  case '[': /* BBC @ */
    row = 4;
    col = 7;
    break;
  case ']': /* BBC [ */
    row = 3;
    col = 8;
    break;
  case k_keyboard_key_enter: /* BBC RETURN */
    row = 4;
    col = 9;
    break;
  case k_keyboard_key_ctrl_left:
    row = 0;
    col = 1;
    break;
  case 'A':
    row = 4;
    col = 1;
    break;
  case 'S':
    row = 5;
    col = 1;
    break;
  case 'D':
    row = 3;
    col = 2;
    break;
  case 'F':
    row = 4;
    col = 3;
    break;
  case 'G':
    row = 5;
    col = 3;
    break;
  case 'H':
    row = 5;
    col = 4;
    break;
  case 'J':
    row = 4;
    col = 5;
    break;
  case 'K':
    row = 4;
    col = 6;
    break;
  case 'L':
    row = 5;
    col = 6;
    break;
  case ';':
    row = 5;
    col = 7;
    break;
  case '\'': /* BBC : */
    row = 4;
    col = 8;
    break;
  case k_keyboard_key_shift_left:
    row = 0;
    col = 0;
    break;
  case '\\': /* BBC ] */
    row = 5;
    col = 8;
    break;
  case 'Z':
    row = 6;
    col = 1;
    break;
  case 'X':
    row = 4;
    col = 2;
    break;
  case 'C':
    row = 5;
    col = 2;
    break;
  case 'V':
    row = 6;
    col = 3;
    break;
  case 'B':
    row = 6;
    col = 4;
    break;
  case 'N':
    row = 5;
    col = 5;
    break;
  case 'M':
    row = 6;
    col = 5;
    break;
  case ',':
    row = 6;
    col = 6;
    break;
  case '.':
    row = 6;
    col = 7;
    break;
  case '/':
    row = 6;
    col = 8;
    break;
  case k_keyboard_key_shift_right:
    row = 0;
    col = 0;
    break;
  case ' ':
    row = 6;
    col = 2;
    break;
  case k_keyboard_key_caps_lock:
    row = 4;
    col = 0;
    break;
  case k_keyboard_key_f1:
    row = 7;
    col = 1;
    break;
  case k_keyboard_key_f2:
    row = 7;
    col = 2;
    break;
  case k_keyboard_key_f3:
    row = 7;
    col = 3;
    break;
  case k_keyboard_key_f4:
    row = 1;
    col = 4;
    break;
  case k_keyboard_key_f5:
    row = 7;
    col = 4;
    break;
  case k_keyboard_key_f6:
    row = 7;
    col = 5;
    break;
  case k_keyboard_key_f7:
    row = 1;
    col = 6;
    break;
  case k_keyboard_key_f8:
    row = 7;
    col = 6;
    break;
  case k_keyboard_key_f9:
    row = 7;
    col = 7;
    break;
  case k_keyboard_key_f0:
    row = 2;
    col = 0;
    break;
  case k_keyboard_key_arrow_up:
    row = 3;
    col = 9;
    break;
  case k_keyboard_key_arrow_left:
    row = 1;
    col = 9;
    break;
  case k_keyboard_key_arrow_right:
    row = 7;
    col = 9;
    break;
  case k_keyboard_key_arrow_down:
    row = 2;
    col = 9;
    break;
  default:
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
  volatile uint8_t* p_key = &p_keyboard->bbc_keys[row][col];
  return *p_key;
}

int
keyboard_bbc_is_key_column_pressed(struct keyboard_struct* p_keyboard,
                                   uint8_t col) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  volatile uint8_t* p_count = &p_keyboard->bbc_keys_count_col[col];
  return (*p_count > 0);
}

int
keyboard_bbc_is_any_key_pressed(struct keyboard_struct* p_keyboard) {
  /* Threading model: called from the BBC thread.
   * Only allowed to read keyboard state.
   */
  volatile uint8_t* p_count = &p_keyboard->bbc_keys_count;
  return (*p_count > 0);
}

int
keyboard_check_and_clear_alt_key(struct keyboard_struct* p_keyboard,
                                 uint8_t key) {
  int ret = p_keyboard->alt_key_pressed[key];
  p_keyboard->alt_key_pressed[key] = 0;

  return ret;
}

void
keyboard_system_key_pressed(struct keyboard_struct* p_keyboard, uint8_t key) {
  /* Threading model: called from the system thread.
   * Allowed to read/write keyboard state.
   */
  int32_t row;
  int32_t col;

  p_keyboard->key_down[key] = 1;

  if (p_keyboard->key_down[k_keyboard_key_alt_left]) {
    /* Alt + key combos are for the emulator shell only, not the BBC. */
    p_keyboard->alt_key_pressed[key] = 1;
    return;
  }

  keyboard_bbc_key_to_rowcol(key, &row, &col);
  if (row == -1 && col == -1) {
    return;
  }
  assert(row >= 0);
  assert(row < 16);
  assert(col >= 0);
  assert(col < 16);
  if (p_keyboard->bbc_keys[row][col]) {
    return;
  }
  p_keyboard->bbc_keys[row][col] = 1;
  if (row == 0) {
    /* Row 0, notably including shift and ctrl, is not wired to interrupt. */
    return;
  }
  p_keyboard->bbc_keys_count_col[col]++;
  p_keyboard->bbc_keys_count++;
}

void
keyboard_system_key_released(struct keyboard_struct* p_keyboard, uint8_t key) {
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

  p_keyboard->key_down[key] = 0;

  keyboard_bbc_key_to_rowcol(key, &row, &col);
  if (row == -1 && col == -1) {
    return;
  }
  assert(row >= 0);
  assert(row < 16);
  assert(col >= 0);
  assert(col < 16);
  was_pressed = p_keyboard->bbc_keys[row][col];
  p_keyboard->bbc_keys[row][col] = 0;
  if (row == 0) {
    /* Row 0, notably including shift and ctrl, is not wired to interrupt. */
    return;
  }
  if (was_pressed) {
    assert(p_keyboard->bbc_keys_count_col[col] > 0);
    p_keyboard->bbc_keys_count_col[col]--;
    assert(p_keyboard->bbc_keys_count > 0);
    p_keyboard->bbc_keys_count--;
  }
}
