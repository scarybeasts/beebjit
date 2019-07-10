#ifndef BEEBJIT_OS_H
#define BEEBJIT_OS_H

#include <stddef.h>

struct keyboard_struct;
struct video_struct;

struct os_poller_struct;
struct os_window_struct;

struct os_window_struct* os_window_create(struct keyboard_struct* p_keyboard,
                                          struct video_struct* p_video,
                                          size_t chars_width,
                                          size_t chars_height);
void os_window_destroy(struct os_window_struct* p_window);

size_t os_window_get_handle(struct os_window_struct* p_window);
void os_window_render(struct os_window_struct* p_window);
void os_window_process_events(struct os_window_struct* p_window);


struct os_poller_struct* os_poller_create();
void os_poller_destroy(struct os_poller_struct* p_poller);

void os_poller_add_handle(struct os_poller_struct* p_poller, size_t handle);
void os_poller_poll(struct os_poller_struct* p_poller);
int os_poller_handle_triggered(struct os_poller_struct* p_poller, size_t index);

#endif /* BEEBJIT_OS_H */
