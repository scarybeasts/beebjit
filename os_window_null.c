#include "os_window.h"

#include "util.h"

struct os_window_struct*
os_window_create(uint32_t width, uint32_t height) {
  (void) width;
  (void) height;
  util_bail("headless");
  return NULL;
}

void
os_window_destroy(struct os_window_struct* p_window) {
  (void) p_window;
  util_bail("headless");
}

void
os_window_set_name(struct os_window_struct* p_window, const char* p_name) {
  (void) p_window;
  (void) p_name;
  util_bail("headless");
}

void
os_window_set_keyboard_callback(struct os_window_struct* p_window,
                                struct keyboard_struct* p_keyboard) {
  (void) p_window;
  (void) p_keyboard;
  util_bail("headless");
}

void
os_window_set_focus_lost_callback(struct os_window_struct* p_window,
                                  void (*p_focus_lost_callback)(void* p),
                                  void* p_focus_lost_callback_object) {
  (void) p_window;
  (void) p_focus_lost_callback;
  (void) p_focus_lost_callback_object;
  util_bail("headless");
}

uint32_t*
os_window_get_buffer(struct os_window_struct* p_window) {
  (void) p_window;
  util_bail("headless");
  return NULL;
}

intptr_t
os_window_get_handle(struct os_window_struct* p_window) {
  (void) p_window;
  util_bail("headless");
  return -1;
}

void
os_window_sync_buffer_to_screen(struct os_window_struct* p_window) {
  (void) p_window;
  util_bail("headless");
}

void
os_window_process_events(struct os_window_struct* p_window) {
  (void) p_window;
  util_bail("headless");
}

int
os_window_is_closed(struct os_window_struct* p_window) {
  (void) p_window;
  util_bail("headless");
  return -1;
}
