#include "os_window.h"

struct os_window_struct*
os_window_create(uint32_t width, uint32_t height) {
  (void) width;
  (void) height;
  return NULL;
}

void
os_window_destroy(struct os_window_struct* p_window) {
  (void) p_window;
}

void
os_window_set_name(struct os_window_struct* p_window, const char* p_name) {
  (void) p_window;
  (void) p_name;
}

void
os_window_set_keyboard_callback(struct os_window_struct* p_window,
                                struct keyboard_struct* p_keyboard) {
  (void) p_window;
  (void) p_keyboard;
}

uint32_t*
os_window_get_buffer(struct os_window_struct* p_window) {
  (void) p_window;
  return NULL;
}

uintptr_t
os_window_get_handle(struct os_window_struct* p_window) {
  (void) p_window;
  return 0;
}

void
os_window_sync_buffer_to_screen(struct os_window_struct* p_window) {
  (void) p_window;
}

void
os_window_process_events(struct os_window_struct* p_window) {
  (void) p_window;
}
