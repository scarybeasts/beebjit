#include "keyboard.h"

#include "bbc_options.h"
#include "log.h"
#include "os_lock.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"
#include "version.h"
#include "via.h"

#include <assert.h>
#include <inttypes.h>
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
  k_capture_version_offset = 16,
  k_capture_version_len = 8,
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
  void (*p_virtual_updated_callback)(void* p);
  void* p_virtual_updated_callback_object;
  void (*p_set_fast_mode_callback)(void* p, int fast);
  void* p_set_fast_mode_callback_object;

  /* The OS thread populates the queue of physical key events and the BBC thread
   * empties it from time to time.
   */
  struct os_lock_struct* p_lock;
  uint8_t queue_key[k_keyboard_queue_size];
  uint8_t queue_isdown[k_keyboard_queue_size];
  uint32_t queue_pos;

  struct util_file* p_capture_file;
  struct util_file* p_replay_file;
  char* p_capture_file_name;
  char* p_replay_file_name;
  uint32_t replay_timer_id;
  uint32_t rewind_timer_id;

  uint8_t replay_next_num_keys;
  uint8_t replay_next_keys[k_keyboard_queue_size];
  uint8_t replay_next_isdown[k_keyboard_queue_size];
  char replay_version[k_capture_version_len];

  struct keyboard_state* p_virtual_keyboard;
  struct keyboard_state* p_physical_keyboard;
  struct keyboard_state* p_active;
  uint8_t keyboard_links;
  uint8_t remap[256];

  int log_replay;
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
  case '\\':
    row = 7;
    col = 8;
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
  case '`': /* BBC @ */
    row = 4;
    col = 7;
    break;
  case '[': /* BBC [ */
    row = 3;
    col = 8;
    break;
  case k_keyboard_key_page_up: /* BBC _ / pound */
    row = 2;
    col = 8;
    break;
  case k_keyboard_key_caps_lock:
    row = 4;
    col = 0;
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
  case ']':
    row = 5;
    col = 8;
    break;
  case k_keyboard_key_enter: /* BBC RETURN */
    row = 4;
    col = 9;
    break;
  case k_keyboard_key_windows: /* BBC SHIFT LOCK */
    row = 5;
    col = 0;
    break;
  case k_keyboard_key_shift_left:
  case k_keyboard_key_shift_right:
    row = 0;
    col = 0;
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
  case k_keyboard_key_backspace: /* BBC DELETE */
    row = 5;
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

static int
keyboard_is_key_state_down(struct keyboard_state* p_state, uint8_t key) {
  return !!(p_state->key_state[key] & k_keyboard_state_flag_down);
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
  if ((row < 0) || (col < 0)) {
    return;
  }
  assert(row < 16);
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
  if ((row < 0) || (col < 0)) {
    return;
  }
  assert(row < 16);
  assert(col < 16);
  was_pressed = p_state->bbc_keys[row][col];
  p_state->bbc_keys[row][col] = 0;
  if (row == 0) {
    /* Row 0, notably including shift and ctrl, is not wired to interrupt. */
    return;
  }
  if (!was_pressed) {
    return;
  }

  assert(p_state->bbc_keys_count_col[col] > 0);
  assert(p_state->bbc_keys_count > 0);

  p_state->bbc_keys_count_col[col]--;
  p_state->bbc_keys_count--;
}

static void
keyboard_log_keys(struct keyboard_struct* p_keyboard,
                  const char* p_key_type,
                  uint8_t num_keys,
                  uint8_t* p_keys,
                  uint8_t* p_is_downs) {
  uint32_t i;

  uint64_t timer_ticks = timing_get_total_timer_ticks(p_keyboard->p_timing);

  for (i = 0; i < num_keys; ++i) {
    const char* p_updown = "up";
    if (p_is_downs[i]) {
      p_updown = "down";
    }
    log_do_log(k_log_keyboard,
               k_log_info,
               "%s key %"PRIu8" %s at %"PRIu64,
               p_key_type,
               p_keys[i],
               p_updown,
               timer_ticks);
  }
}

static void
keyboard_capture_keys(struct keyboard_struct* p_keyboard,
                      int is_replay,
                      uint8_t num_keys,
                      uint8_t* p_keys,
                      uint8_t* p_is_downs) {
  uint64_t time;
  struct util_file* p_capture_file = p_keyboard->p_capture_file;
  struct util_file* p_replay_file = p_keyboard->p_replay_file;

  assert(num_keys > 0);

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
  util_file_write(p_capture_file, p_keys, num_keys);
  util_file_write(p_capture_file, p_is_downs, num_keys);
  util_file_flush(p_capture_file);

  if (p_keyboard->log_replay) {
    keyboard_log_keys(p_keyboard, "capture", num_keys, p_keys, p_is_downs);
  }
}

static void
keyboard_read_replay_frame(struct keyboard_struct* p_keyboard) {
  uint64_t ret;
  uint64_t replay_next_time;
  uint64_t delta_time;
  uint8_t num_keys;

  struct util_file* p_file = p_keyboard->p_replay_file;
  struct timing_struct* p_timing = p_keyboard->p_timing;
  uint32_t replay_timer_id = p_keyboard->replay_timer_id;
  uint64_t time = timing_get_total_timer_ticks(p_timing);
  assert(p_file != NULL);

  ret = util_file_read(p_file, &replay_next_time, sizeof(replay_next_time));
  if (ret == 0) {
    keyboard_end_replay(p_keyboard);
    return;
  }

  ret += util_file_read(p_file, &num_keys, sizeof(num_keys));
  if (ret != (sizeof(replay_next_time) + sizeof(num_keys))) {
    util_bail("corrupt replay file, truncated frame header");
  }

  if (num_keys == 0) {
    util_bail("corrupt replay file, zero keys");
  }
  if ((int64_t) replay_next_time < 0) {
    util_bail("corrupt replay file, negative time");
  }
  if (replay_next_time < time) {
    util_bail("corrupt replay file, backwards time");
  }

  p_keyboard->replay_next_num_keys = num_keys;

  ret = util_file_read(p_file, &p_keyboard->replay_next_keys[0], num_keys);
  ret += util_file_read(p_file, &p_keyboard->replay_next_isdown[0], num_keys);
  if (ret != (num_keys * 2)) {
    util_bail("replay: file truncated reading keys");
  }

  assert(timing_get_timer_value(p_timing, replay_timer_id) == 0);
  delta_time = (replay_next_time - time);
  (void) timing_set_timer_value(p_timing, replay_timer_id, delta_time);
}

static void
keyboard_virtual_updated(struct keyboard_struct* p_keyboard) {
  if (p_keyboard->p_virtual_updated_callback != NULL) {
    p_keyboard->p_virtual_updated_callback(
        p_keyboard->p_virtual_updated_callback_object);
  }
}

static void
keyboard_replay_timer_tick(void* p) {
  uint8_t i;

  struct keyboard_struct* p_keyboard = (struct keyboard_struct*) p;
  struct keyboard_state* p_state = p_keyboard->p_virtual_keyboard;
  uint8_t num_keys = p_keyboard->replay_next_num_keys;

  assert(p_keyboard->p_replay_file != NULL);
  assert(p_keyboard->p_active == p_keyboard->p_virtual_keyboard);
  assert(num_keys > 0);

  if (num_keys > k_keyboard_queue_size) {
    util_bail("replay: too many keys");
  }

  for (i = 0; i < num_keys; ++i) {
    uint8_t key = p_keyboard->replay_next_keys[i];
    uint8_t isdown = p_keyboard->replay_next_isdown[i];
    if (isdown) {
      keyboard_key_pressed(p_state, key);
    } else {
      keyboard_key_released(p_state, key);
    }
  }

  if (p_keyboard->log_replay) {
    keyboard_log_keys(p_keyboard,
                      "replay",
                      num_keys,
                      &p_keyboard->replay_next_keys[0],
                      &p_keyboard->replay_next_isdown[0]);
  }

  keyboard_virtual_updated(p_keyboard);

  keyboard_capture_keys(p_keyboard,
                        1,
                        num_keys,
                        &p_keyboard->replay_next_keys[0],
                        &p_keyboard->replay_next_isdown[0]);

  /* This finishes with the replay handle if we're at the end. */
  keyboard_read_replay_frame(p_keyboard);
}

static void
keyboard_flip_virtual_to_physical(struct keyboard_struct* p_keyboard) {
  /* This little dance is because when a replay is ending, we can't just drop
   * to the physical keyboard as this can cause keyboard state discontinuities.
   * For example, if rewinding to a time where a key was pressed (virtually),
   * but not pressed physically, the emulated code would stop seeing a key
   * press but without a key press event being recording to any capture log
   * -- disaster.
   * The simplest way to avoid tricky bugs is to just adopt the virtual
   * keyboard state into the physical keyboard state. This leads to stuck-down
   * keys but you just press them again.
   */
  struct keyboard_state* p_temp = p_keyboard->p_virtual_keyboard;
  p_keyboard->p_virtual_keyboard = p_keyboard->p_physical_keyboard;
  p_keyboard->p_physical_keyboard = p_temp;

  p_keyboard->p_active = p_keyboard->p_physical_keyboard;

  (void) memset(p_keyboard->p_virtual_keyboard,
                '\0',
                sizeof(struct keyboard_state));
}

static void
keyboard_rewind_timer_fired(void* p) {
  struct keyboard_struct* p_keyboard = (struct keyboard_struct*) p;
  struct timing_struct* p_timing = p_keyboard->p_timing;

  if (keyboard_is_capturing(p_keyboard)) {
    if (timing_timer_is_running(p_timing, p_keyboard->replay_timer_id)) {
      keyboard_end_replay(p_keyboard);
    }
    keyboard_flip_virtual_to_physical(p_keyboard);
  }

  (void) timing_stop_timer(p_timing, p_keyboard->rewind_timer_id);

  if (p_keyboard->p_set_fast_mode_callback) {
    p_keyboard->p_set_fast_mode_callback(
        p_keyboard->p_set_fast_mode_callback_object, 0);
  }
}

struct keyboard_struct*
keyboard_create(struct timing_struct* p_timing, struct bbc_options* p_options) {
  uint32_t i;
  struct keyboard_struct* p_keyboard =
      util_mallocz(sizeof(struct keyboard_struct));

  p_keyboard->p_physical_keyboard = util_mallocz(sizeof(struct keyboard_state));
  p_keyboard->p_virtual_keyboard = util_mallocz(sizeof(struct keyboard_state));

  p_keyboard->p_timing = p_timing;
  p_keyboard->p_lock = os_lock_create();
  p_keyboard->queue_pos = 0;
  p_keyboard->p_capture_file = NULL;
  p_keyboard->p_replay_file = NULL;
  p_keyboard->p_active = p_keyboard->p_physical_keyboard;

  p_keyboard->replay_timer_id =
      timing_register_timer(p_timing,
                            "keyboard_replay",
                            keyboard_replay_timer_tick,
                            p_keyboard);
  p_keyboard->rewind_timer_id =
      timing_register_timer(p_timing,
                            "keyboard_rewind",
                            keyboard_rewind_timer_fired,
                            p_keyboard);

  for (i = 0; i < sizeof(p_keyboard->remap); ++i) {
    p_keyboard->remap[i] = i;
  }

  p_keyboard->log_replay = util_has_option(p_options->p_log_flags,
                                           "keyboard:replay");

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
  if (p_keyboard->p_capture_file_name != NULL) {
    util_free(p_keyboard->p_capture_file_name);
  }
  if (p_keyboard->p_replay_file_name != NULL) {
    util_free(p_keyboard->p_replay_file_name);
  }
  os_lock_destroy(p_keyboard->p_lock);
  util_free(p_keyboard->p_physical_keyboard);
  util_free(p_keyboard->p_virtual_keyboard);
  util_free(p_keyboard);
}

void
keyboard_set_links(struct keyboard_struct* p_keyboard, uint8_t bits) {
  p_keyboard->keyboard_links = bits;
}

void
keyboard_set_virtual_updated_callback(struct keyboard_struct* p_keyboard,
                                      void (*p_callback)(void*),
                                      void* p_callback_object) {
  p_keyboard->p_virtual_updated_callback = p_callback;
  p_keyboard->p_virtual_updated_callback_object = p_callback_object;
}

void
keyboard_set_fast_mode_callback(struct keyboard_struct* p_keyboard,
                                void (*p_set_fast_mode_callback)(void* p,
                                                                int fast),
                                void* p_set_fast_mode_callback_object) {
  p_keyboard->p_set_fast_mode_callback = p_set_fast_mode_callback;
  p_keyboard->p_set_fast_mode_callback_object = p_set_fast_mode_callback_object;
}

void
keyboard_power_on_reset(struct keyboard_struct* p_keyboard) {
  uint32_t i;
  uint8_t keyboard_links = p_keyboard->keyboard_links;

  (void) memset(p_keyboard->p_virtual_keyboard,
                '\0',
                sizeof(struct keyboard_state));
  (void) memset(p_keyboard->p_physical_keyboard,
                '\0',
                sizeof(struct keyboard_state));

  for (i = 0; i < 8; ++i) {
    int bit = (keyboard_links & 1);
    keyboard_links >>= 1;
    if (bit) {
      p_keyboard->p_virtual_keyboard->bbc_keys[0][9 - i] = 1;
      p_keyboard->p_physical_keyboard->bbc_keys[0][9 - i] = 1;
    }
  }
}

void
keyboard_set_capture_file_name(struct keyboard_struct* p_keyboard,
                               const char* p_name) {
  char buf[k_capture_header_size];

  assert(p_keyboard->p_capture_file == NULL);
  assert(p_keyboard->p_capture_file_name == NULL);

  p_keyboard->p_capture_file = util_file_open(p_name, 1, 1);
  p_keyboard->p_capture_file_name = util_strdup(p_name);

  (void) memset(buf, '\0', sizeof(buf));
  (void) memcpy(buf, k_capture_header, strlen(k_capture_header));
  (void) memcpy((buf + k_capture_version_offset),
                BEEBJIT_VERSION,
                strlen(BEEBJIT_VERSION));
  util_file_write(p_keyboard->p_capture_file, buf, sizeof(buf));
}

static void
keyboard_start_file_replay(struct keyboard_struct* p_keyboard,
                           struct util_file* p_file) {
  char buf[k_capture_header_size];
  uint64_t ret;

  assert(p_keyboard->p_replay_file == NULL);
  p_keyboard->p_replay_file = p_file;
  p_keyboard->p_active = p_keyboard->p_virtual_keyboard;

  ret = util_file_read(p_file, buf, sizeof(buf));
  if (ret != sizeof(buf)) {
    util_bail("capture file too short");
  }
  if (memcmp(buf, k_capture_header, strlen(k_capture_header))) {
    util_bail("capture file has bad header");
  }
  (void) memcpy(&p_keyboard->replay_version[0],
                &buf[k_capture_version_offset],
                k_capture_version_len);
  p_keyboard->replay_version[k_capture_version_len - 1] = '\0';

  (void) timing_start_timer_with_value(p_keyboard->p_timing,
                                       p_keyboard->replay_timer_id,
                                       0);
  keyboard_read_replay_frame(p_keyboard);
}

const char*
keyboard_get_replay_version(struct keyboard_struct* p_keyboard) {
  return &p_keyboard->replay_version[0];
}

void
keyboard_set_replay_file_name(struct keyboard_struct* p_keyboard,
                              const char* p_name) {
  struct util_file* p_file = util_file_open(p_name, 0, 0);

  assert(p_keyboard->p_replay_file == NULL);
  assert(p_keyboard->p_replay_file_name == NULL);

  p_keyboard->p_replay_file_name = util_strdup(p_name);

  keyboard_start_file_replay(p_keyboard, p_file);
}

int
keyboard_is_capturing(struct keyboard_struct* p_keyboard) {
  return (p_keyboard->p_capture_file != NULL);
}

int
keyboard_is_replaying(struct keyboard_struct* p_keyboard) {
  return (p_keyboard->p_replay_file != NULL);
}

void
keyboard_end_replay(struct keyboard_struct* p_keyboard) {
  struct util_file* p_replay_file = p_keyboard->p_replay_file;
  char* p_replay_file_name = p_keyboard->p_replay_file_name;
  struct timing_struct* p_timing = p_keyboard->p_timing;

  assert(p_replay_file != NULL);
  assert(p_replay_file_name != NULL);
  assert(p_keyboard->p_active == p_keyboard->p_virtual_keyboard);

  (void) timing_stop_timer(p_timing, p_keyboard->replay_timer_id);

  util_file_close(p_replay_file);
  p_keyboard->p_replay_file = NULL;
  util_free(p_replay_file_name);
  p_keyboard->p_replay_file_name = NULL;

  if (timing_timer_is_running(p_timing, p_keyboard->rewind_timer_id)) {
    return;
  }

  keyboard_flip_virtual_to_physical(p_keyboard);

  if (p_keyboard->p_set_fast_mode_callback) {
    p_keyboard->p_set_fast_mode_callback(
        p_keyboard->p_set_fast_mode_callback_object, 0);
  }
}

int
keyboard_can_rewind(struct keyboard_struct* p_keyboard) {
  int is_capturing;
  int is_replaying;

  /* Can't rewind if we're already rewinding. */
  if (timing_timer_is_running(p_keyboard->p_timing,
                              p_keyboard->rewind_timer_id)) {
    return 0;
  }

  is_capturing = keyboard_is_capturing(p_keyboard);
  is_replaying = keyboard_is_replaying(p_keyboard);

  /* We don't yet have a behavior decided if both replaying and capturing at
   * the same time.
   */
  if (is_capturing && is_replaying) {
    return 0;
  }

  if (is_capturing || is_replaying) {
    return 1;
  }

  return 0;
}

void
keyboard_rewind(struct keyboard_struct* p_keyboard, uint64_t stop_cycles) {
  struct timing_struct* p_timing = p_keyboard->p_timing;

  int is_capturing = keyboard_is_capturing(p_keyboard);
  int is_replaying = keyboard_is_replaying(p_keyboard);

  /* Replay may have ended in the interim. */
  if (!is_capturing && !is_replaying) {
    return;
  }

  if (is_capturing) {
    char* p_capture_file_name = p_keyboard->p_capture_file_name;
    char* p_new_replay_file_name = util_strdup2(p_capture_file_name, ".replay");
    util_file_close(p_keyboard->p_capture_file);
    p_keyboard->p_capture_file = NULL;
    p_keyboard->p_capture_file_name = NULL;
    util_file_copy(p_capture_file_name, p_new_replay_file_name);

    keyboard_set_capture_file_name(p_keyboard, p_capture_file_name);
    util_free(p_capture_file_name);
    keyboard_set_replay_file_name(p_keyboard, p_new_replay_file_name);
    util_free(p_new_replay_file_name);
  } else {
    struct util_file* p_replay_file = p_keyboard->p_replay_file;

    (void) timing_stop_timer(p_timing, p_keyboard->replay_timer_id);

    p_keyboard->p_replay_file = NULL;
    util_file_seek(p_replay_file, 0);
    keyboard_start_file_replay(p_keyboard, p_replay_file);
  }

  (void) timing_start_timer_with_value(p_timing,
                                       p_keyboard->rewind_timer_id,
                                       stop_cycles);

  if (p_keyboard->log_replay) {
    log_do_log(k_log_keyboard,
               k_log_info,
               "rewind replay to %"PRIu64,
               stop_cycles);
  }

  if (p_keyboard->p_set_fast_mode_callback) {
    p_keyboard->p_set_fast_mode_callback(
        p_keyboard->p_set_fast_mode_callback_object, 1);
  }
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
keyboard_is_key_down(struct keyboard_struct* p_keyboard, uint8_t key) {
  return keyboard_is_key_state_down(p_keyboard->p_active, key);
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
  struct keyboard_state* p_state = p_keyboard->p_physical_keyboard;
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
    if (key == k_keyboard_key_SPECIAL_release_all) {
      p_keyboard->queue_pos--;
    } else {
      log_do_log(k_log_keyboard, k_log_error, "keyboard queue full");
      os_lock_unlock(p_keyboard->p_lock);
      return;
    }
  }
  p_keyboard->queue_key[p_keyboard->queue_pos] = key;
  p_keyboard->queue_isdown[p_keyboard->queue_pos] = is_down;
  p_keyboard->queue_pos++;

  os_lock_unlock(p_keyboard->p_lock);
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

static int
keyboard_is_emulator_internal_key(struct keyboard_state* p_state, uint8_t key) {
  (void) p_state;

  if (key == k_keyboard_key_home) {
    /* Enter debugger. */
    return 1;
  }
  /* TODO: should some of the Alt+key combos be considered "internal" and not
   * appear in capture files?
   */
  return 0;
}

static void
keyboard_apply_physical_keys(struct keyboard_struct* p_keyboard,
                             uint8_t* p_keys,
                             uint8_t* p_is_downs,
                             uint32_t num_keys) {
  uint8_t filtered_keys[256];
  uint8_t filtered_is_downs[256];
  uint32_t i;

  uint32_t num_keys_filtered = 0;
  struct keyboard_state* p_state = p_keyboard->p_physical_keyboard;

  for (i = 0; i < num_keys; ++i) {
    uint8_t key = p_keys[i];
    uint8_t is_down = p_is_downs[i];
    int curr_is_down = keyboard_is_key_state_down(p_state, key);
    int is_spurious = (is_down == curr_is_down);

    if (is_spurious) {
      continue;
    }

    if (is_down) {
      keyboard_key_pressed(p_state, key);
    } else {
      keyboard_key_released(p_state, key);
    }

    if (keyboard_is_emulator_internal_key(p_state, key)) {
      continue;
    }

    filtered_keys[num_keys_filtered] = key;
    filtered_is_downs[num_keys_filtered] = is_down;
    num_keys_filtered++;
  }

  if (num_keys_filtered == 0) {
    return;
  }

  keyboard_capture_keys(p_keyboard,
                        0,
                        num_keys_filtered,
                        filtered_keys,
                        filtered_is_downs);

  if (p_keyboard->p_active == p_keyboard->p_physical_keyboard) {
    keyboard_virtual_updated(p_keyboard);
  }
}

void
keyboard_read_queue(struct keyboard_struct* p_keyboard) {
  /* Called from the BBC thread. */
  uint8_t keys[k_keyboard_queue_size];
  uint8_t is_downs[k_keyboard_queue_size];
  uint32_t i;
  uint32_t num_keys;

  int has_release_all = 0;
  volatile uint32_t* p_queue_pos = &p_keyboard->queue_pos;

  /* Always check the physical keyboard. Even if we're replaying a replay, we
   * want to honor special emulator keys, i.e. Alt+combo.
   */
  if (*p_queue_pos == 0) {
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

    if (key == k_keyboard_key_SPECIAL_release_all) {
      has_release_all = 1;
    }

    /* Physical keyboard remapping, as per -key-remap command line option. */
    key = p_keyboard->remap[key];

    keys[i] = key;
    is_downs[i] = is_down;
  }

  p_keyboard->queue_pos = 0;

  os_lock_unlock(p_keyboard->p_lock);

  if (has_release_all) {
    uint8_t all_keys[256];
    uint8_t all_is_downs[256];

    for (i = 0; i < 256; ++i) {
      all_keys[i] = i;
      all_is_downs[i] = 0;
    }
    keyboard_apply_physical_keys(p_keyboard, all_keys, all_is_downs, 256);
  } else {
    keyboard_apply_physical_keys(p_keyboard, keys, is_downs, num_keys);
  }
}

void
keyboard_add_remap(struct keyboard_struct* p_keyboard,
                   uint8_t from,
                   uint8_t to) {
  p_keyboard->remap[from] = to;
}
