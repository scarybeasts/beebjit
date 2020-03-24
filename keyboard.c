#include "keyboard.h"

#include "log.h"
#include "os_thread.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"
#include "via.h"

#include <assert.h>
#include <string.h>

static const char* k_capture_header = "beebjit-capture";

enum {
  k_keyboard_state_flag_down = 1,
  k_keyboard_state_flag_pressed_not_released = 2,
  k_keyboard_state_flag_unconsumed_press = 4,
};

enum {
  k_keyboard_queue_size = 16,
};

enum {
  k_capture_header_size = 32,
};

struct keyboard_state {
  uint8_t bbc_keys[16][16];
  uint8_t bbc_keys_count;
  uint8_t bbc_keys_count_col[16];
  uint8_t key_state[256];
  uint8_t alt_key_state[256];
};

struct keyboard_struct {
  struct timing_struct* p_timing;
  struct via_struct* p_system_via;
  struct state_6502* p_state_6502;

  /* The OS thread populates the queue of key events and the BBC thread
   * empties it from time to time.
   */
  struct os_lock_struct* p_lock;
  uint8_t queue_key[k_keyboard_queue_size];
  uint8_t queue_isdown[k_keyboard_queue_size];
  uint8_t queue_pos;

  struct util_file* p_capture_file;
  struct util_file* p_replay_file;
  uint8_t replay_next_keys;
  int had_replay_eof;
  uint32_t replay_timer_id;

  struct keyboard_state virtual_keyboard;
  struct keyboard_state physical_keyboard;
  struct keyboard_state* p_active;
};

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
  case k_keyboard_key_ctrl:
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
  case k_keyboard_key_shift_right:
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
  case k_keyboard_key_end: /* BBC COPY */
    row = 6;
    col = 9;
    break;
  default:
    break;
  }

  *p_row = row;
  *p_col = col;
}

static void
keyboard_key_pressed(struct keyboard_state* p_state, uint8_t key) {
  int32_t row;
  int32_t col;

  p_state->key_state[key] |= k_keyboard_state_flag_down;
  if (!(p_state->key_state[key] & k_keyboard_state_flag_pressed_not_released)) {
    p_state->key_state[key] |= (k_keyboard_state_flag_pressed_not_released |
                                k_keyboard_state_flag_unconsumed_press);
  }

  if (p_state->key_state[k_keyboard_key_alt_left] &
      k_keyboard_state_flag_down) {
    /* Alt + key combos are for the emulator shell only, not the BBC. */
    p_state->alt_key_state[key] |= k_keyboard_state_flag_down;
    if (!(p_state->alt_key_state[key] &
          k_keyboard_state_flag_pressed_not_released)) {
      p_state->alt_key_state[key] |=
          (k_keyboard_state_flag_pressed_not_released |
           k_keyboard_state_flag_unconsumed_press);
    }
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
  if (p_state->bbc_keys[row][col]) {
    return;
  }
  p_state->bbc_keys[row][col] = 1;
  if (row == 0) {
    /* Row 0, notably including shift and ctrl, is not wired to interrupt. */
    return;
  }
  p_state->bbc_keys_count_col[col]++;
  p_state->bbc_keys_count++;
}

static void
keyboard_key_released(struct keyboard_state* p_state, uint8_t key) {
  int32_t row;
  int32_t col;
  int was_pressed;

  p_state->key_state[key] &= ~(k_keyboard_state_flag_down |
                               k_keyboard_state_flag_pressed_not_released);
  p_state->alt_key_state[key] &=
      ~(k_keyboard_state_flag_down |
        k_keyboard_state_flag_pressed_not_released);

  keyboard_bbc_key_to_rowcol(key, &row, &col);
  if (row == -1 && col == -1) {
    return;
  }
  assert(row >= 0);
  assert(row < 16);
  assert(col >= 0);
  assert(col < 16);
  was_pressed = p_state->bbc_keys[row][col];
  p_state->bbc_keys[row][col] = 0;
  if (row == 0) {
    /* Row 0, notably including shift and ctrl, is not wired to interrupt. */
    return;
  }
  if (was_pressed) {
    assert(p_state->bbc_keys_count_col[col] > 0);
    p_state->bbc_keys_count_col[col]--;
    assert(p_state->bbc_keys_count > 0);
    p_state->bbc_keys_count--;
  }
}

static void
keyboard_capture_keys(struct keyboard_struct* p_keyboard,
                      int is_replay,
                      uint8_t num_keys,
                      uint8_t* keys,
                      uint8_t* is_downs) {
  uint64_t time;
  struct util_file* p_capture_file = p_keyboard->p_capture_file;
  struct util_file* p_replay_file = p_keyboard->p_replay_file;

  if (p_capture_file == NULL) {
    return;
  }

  /* If we're replaying, don't capture physical keyboard. */
  if ((p_replay_file != NULL) && !is_replay) {
    return;
  }

  if (is_replay) {
    assert(p_replay_file != NULL);
  }

  time = timing_get_total_timer_ticks(p_keyboard->p_timing);
  util_file_write(p_capture_file, &time, sizeof(time));
  util_file_write(p_capture_file, &num_keys, sizeof(num_keys));
  util_file_write(p_capture_file, keys, num_keys);
  util_file_write(p_capture_file, is_downs, num_keys);
}

static void
keyboard_read_replay_frame(struct keyboard_struct* p_keyboard) {
  uint64_t ret;
  uint64_t replay_next_time;
  uint64_t delta_time;

  struct util_file* p_file = p_keyboard->p_replay_file;
  struct timing_struct* p_timing = p_keyboard->p_timing;
  uint32_t replay_timer_id = p_keyboard->replay_timer_id;
  uint64_t time = timing_get_total_timer_ticks(p_timing);
  assert(p_file != NULL);

  ret = util_file_read(p_file, &replay_next_time, sizeof(replay_next_time));
  if (ret == 0) {
    /* EOF. */
    keyboard_end_replay(p_keyboard);
    return;
  }
  ret += util_file_read(p_file,
                        &p_keyboard->replay_next_keys,
                        sizeof(p_keyboard->replay_next_keys));
  if (ret != (sizeof(replay_next_time) +
              sizeof(p_keyboard->replay_next_keys))) {
    util_bail("corrupt replay file, truncated frame header");
  }

  if (replay_next_time < time) {
    util_bail("corrupt replay file, backwards time");
  }

  assert(timing_get_timer_value(p_timing, replay_timer_id) == 0);
  delta_time = (replay_next_time - time);
  (void) timing_set_timer_value(p_timing, replay_timer_id, delta_time);
}

static void
keyboard_virtual_updated(struct keyboard_struct* p_keyboard) {
  /* Make sure interrupt state is synced with new keyboard state. */
  (void) via_update_port_a(p_keyboard->p_system_via);

  /* Check for BREAK key. */
  if (keyboard_consume_key_press(p_keyboard, k_keyboard_key_f12)) {
    /* The BBC break key is attached to the 6502 reset line. Other peripherals
     * continue along without reset.
     */
    state_6502_set_reset_pending(p_keyboard->p_state_6502);
  }
}

static void
keyboard_replay_timer_tick(struct keyboard_struct* p_keyboard) {
  uint64_t ret;
  uint8_t i;
  uint8_t replay_keys[k_keyboard_queue_size];
  uint8_t replay_isdown[k_keyboard_queue_size];

  struct util_file* p_replay_file = p_keyboard->p_replay_file;
  struct keyboard_state* p_state = &p_keyboard->virtual_keyboard;
  uint8_t num_keys = p_keyboard->replay_next_keys;

  assert(p_replay_file != NULL);
  assert(p_keyboard->p_active == &p_keyboard->virtual_keyboard);

  if (num_keys > k_keyboard_queue_size) {
    util_bail("replay: too many keys");
  }

  ret = util_file_read(p_replay_file, replay_keys, num_keys);
  ret += util_file_read(p_replay_file, replay_isdown, num_keys);
  if (ret != (num_keys * 2)) {
    util_bail("replay: file truncated reading keys");
  }

  for (i = 0; i < num_keys; ++i) {
    uint8_t key = replay_keys[i];
    uint8_t isdown = replay_isdown[i];
    if (isdown) {
      keyboard_key_pressed(p_state, key);
    } else {
      keyboard_key_released(p_state, key);
    }
  }

  keyboard_virtual_updated(p_keyboard);

  keyboard_capture_keys(p_keyboard, 1, num_keys, replay_keys, replay_isdown);

  /* This finishes with the replay handle if we're at the end. */
  keyboard_read_replay_frame(p_keyboard);
}

struct keyboard_struct*
keyboard_create(struct timing_struct* p_timing,
                struct via_struct* p_system_via,
                struct state_6502* p_state_6502) {
  struct keyboard_struct* p_keyboard =
      util_mallocz(sizeof(struct keyboard_struct));

  p_keyboard->p_timing = p_timing;
  p_keyboard->p_system_via = p_system_via;
  p_keyboard->p_state_6502 = p_state_6502;
  p_keyboard->p_lock = os_lock_create();
  p_keyboard->queue_pos = 0;
  p_keyboard->p_capture_file = NULL;
  p_keyboard->p_replay_file = NULL;
  p_keyboard->replay_next_keys = 0;
  p_keyboard->had_replay_eof = 0;
  p_keyboard->p_active = &p_keyboard->physical_keyboard;

  p_keyboard->replay_timer_id =
      timing_register_timer(p_timing, keyboard_replay_timer_tick, p_keyboard);

  return p_keyboard;
}

void
keyboard_destroy(struct keyboard_struct* p_keyboard) {
  if (p_keyboard->p_capture_file != NULL) {
    util_file_close(p_keyboard->p_capture_file);
  }
  if (p_keyboard->p_replay_file != NULL) {
    util_file_close(p_keyboard->p_replay_file);
  }
  os_lock_destroy(p_keyboard->p_lock);
  util_free(p_keyboard);
}

void
keyboard_set_capture_file_name(struct keyboard_struct* p_keyboard,
                               const char* p_name) {
  char buf[k_capture_header_size];

  p_keyboard->p_capture_file = util_file_open(p_name, 1, 1);

  (void) memset(buf, '\0', sizeof(buf));
  (void) memcpy(buf, k_capture_header, strlen(k_capture_header));
  util_file_write(p_keyboard->p_capture_file, buf, sizeof(buf));
}

void
keyboard_set_replay_file_name(struct keyboard_struct* p_keyboard,
                              const char* p_name) {
  char buf[k_capture_header_size];
  uint64_t ret;
  struct util_file* p_file = util_file_open(p_name, 0, 0);

  p_keyboard->p_replay_file = p_file;
  p_keyboard->p_active = &p_keyboard->virtual_keyboard;

  ret = util_file_read(p_file, buf, sizeof(buf));
  if (ret != sizeof(buf)) {
    util_bail("capture file too short");
  }
  if (memcmp(buf, k_capture_header, strlen(k_capture_header))) {
    util_bail("capture file has bad header");
  }

  (void) timing_start_timer_with_value(p_keyboard->p_timing,
                                       p_keyboard->replay_timer_id,
                                       0);
  keyboard_read_replay_frame(p_keyboard);
}

int
keyboard_is_replaying(struct keyboard_struct* p_keyboard) {
  return (p_keyboard->p_replay_file != NULL);
}

void
keyboard_end_replay(struct keyboard_struct* p_keyboard) {
  struct util_file* p_replay_file = p_keyboard->p_replay_file;

  assert(p_replay_file != NULL);
  assert(p_keyboard->p_active == &p_keyboard->virtual_keyboard);

  (void) timing_stop_timer(p_keyboard->p_timing, p_keyboard->replay_timer_id);
  util_file_close(p_replay_file);

  p_keyboard->p_replay_file = 0;
  p_keyboard->had_replay_eof = 1;
  p_keyboard->p_active = &p_keyboard->physical_keyboard;
}

int
keyboard_bbc_is_key_pressed(struct keyboard_struct* p_keyboard,
                            uint8_t row,
                            uint8_t col) {
  return p_keyboard->p_active->bbc_keys[row][col];
}

int
keyboard_bbc_is_key_column_pressed(struct keyboard_struct* p_keyboard,
                                   uint8_t col) {
  uint8_t count = p_keyboard->p_active->bbc_keys_count_col[col];
  return (count > 0);
}

int
keyboard_bbc_is_any_key_pressed(struct keyboard_struct* p_keyboard) {
  uint8_t count = p_keyboard->p_active->bbc_keys_count;
  return (count > 0);
}

int
keyboard_consume_key_press(struct keyboard_struct* p_keyboard, uint8_t key) {
  struct keyboard_state* p_state = p_keyboard->p_active;
  int ret = !!(p_state->key_state[key] &
               k_keyboard_state_flag_unconsumed_press);
  p_state->key_state[key] &= ~k_keyboard_state_flag_unconsumed_press;

  return ret;
}

int
keyboard_consume_alt_key_press(struct keyboard_struct* p_keyboard,
                               uint8_t key) {
  /* NOTE: alt key activity always checks the physical keyboard only. This is
   * so that emulator keys work without tangling with the replay.
   */
  struct keyboard_state* p_state = &p_keyboard->physical_keyboard;
  int ret = !!(p_state->alt_key_state[key] &
               k_keyboard_state_flag_unconsumed_press);
  p_state->alt_key_state[key] &= ~k_keyboard_state_flag_unconsumed_press;

  return ret;
}

static void
keyboard_put_key_in_queue(struct keyboard_struct* p_keyboard,
                          uint8_t key,
                          int is_down) {
  /* Called from the system thread.
   * Only the system thread puts keys in the queue and that's all it does.
   */
  os_lock_lock(p_keyboard->p_lock);

  if (p_keyboard->queue_pos == k_keyboard_queue_size) {
    log_do_log(k_log_keyboard, k_log_error, "keyboard queue full");
    os_lock_unlock(p_keyboard->p_lock);
    return;
  }
  p_keyboard->queue_key[p_keyboard->queue_pos] = key;
  p_keyboard->queue_isdown[p_keyboard->queue_pos] = is_down;
  p_keyboard->queue_pos++;

  os_lock_unlock(p_keyboard->p_lock);
}

int
keyboard_consume_had_replay_eof(struct keyboard_struct* p_keyboard) {
  if (!p_keyboard->had_replay_eof) {
    return 0;
  }

  p_keyboard->had_replay_eof = 0;
  return 1;
}

void
keyboard_system_key_pressed(struct keyboard_struct* p_keyboard, uint8_t key) {
  /* Called from the system thread. */
  keyboard_put_key_in_queue(p_keyboard, key, 1);
}

void
keyboard_system_key_released(struct keyboard_struct* p_keyboard, uint8_t key) {
  /* Called from the system thread. */
  keyboard_put_key_in_queue(p_keyboard, key, 0);
}

void
keyboard_read_queue(struct keyboard_struct* p_keyboard) {
  /* Called from the BBC thread. */
  uint8_t keys[k_keyboard_queue_size];
  uint8_t is_downs[k_keyboard_queue_size];
  uint8_t i;
  uint8_t num_keys;

  struct keyboard_state* p_state = &p_keyboard->physical_keyboard;

  /* Always check the physical keyboard. Even if we're replaying a replay, we
   * want to honor special emulator keys, i.e. Alt+combo.
   */
  if (!p_keyboard->queue_pos) {
    return;
  }

  /* Checking p_keyboard->queue_pos unlocked above should be safe as we'll
   * recheck any potential work with the lock.
   */
  os_lock_lock(p_keyboard->p_lock);

  num_keys = p_keyboard->queue_pos;
  assert(num_keys <= k_keyboard_queue_size);
  for (i = 0; i < num_keys; ++i) {
    uint8_t key = p_keyboard->queue_key[i];
    uint8_t is_down = p_keyboard->queue_isdown[i];
    keys[i] = key;
    is_downs[i] = is_down;
    if (is_down) {
      keyboard_key_pressed(p_state, key);
    } else {
      keyboard_key_released(p_state, key);
    }
  }

  p_keyboard->queue_pos = 0;

  os_lock_unlock(p_keyboard->p_lock);

  keyboard_capture_keys(p_keyboard, 0, num_keys, keys, is_downs);

  if (p_keyboard->p_active == &p_keyboard->physical_keyboard) {
    keyboard_virtual_updated(p_keyboard);
  }
}
