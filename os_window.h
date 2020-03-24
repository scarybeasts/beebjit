#ifndef BEEBJIT_OS_WINDOW_H
#define BEEBJIT_OS_WINDOW_H

#include <stddef.h>

struct keyboard_struct;
struct video_struct;

struct os_window_struct;

struct os_window_struct* os_window_create(uint32_t width, uint32_t height);
void os_window_destroy(struct os_window_struct* p_window);

void os_window_set_name(struct os_window_struct* p_window, const char* p_name);

void os_window_set_keyboard_callback(struct os_window_struct* p_window,
                                     struct keyboard_struct* p_keyboard);
uint32_t* os_window_get_buffer(struct os_window_struct* p_window);
intptr_t os_window_get_handle(struct os_window_struct* p_window);
void os_window_sync_buffer_to_screen(struct os_window_struct* p_window);
void os_window_process_events(struct os_window_struct* p_window);
int os_window_is_closed(struct os_window_struct* p_window);

#endif /* BEEBJIT_OS_WINDOW_H */
