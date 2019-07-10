#ifndef BEEBJIT_OS_WINDOW_H
#define BEEBJIT_OS_WINDOW_H

#include <stddef.h>

struct keyboard_struct;
struct video_struct;

struct os_window_struct;

struct os_window_struct* os_window_create(struct keyboard_struct* p_keyboard,
                                          struct video_struct* p_video,
                                          size_t chars_width,
                                          size_t chars_height);
void os_window_destroy(struct os_window_struct* p_window);

size_t os_window_get_handle(struct os_window_struct* p_window);
void os_window_render(struct os_window_struct* p_window);
void os_window_process_events(struct os_window_struct* p_window);

#endif /* BEEBJIT_OS_WINDOW_H */
