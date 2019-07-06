#ifndef BEEBJIT_X_H
#define BEEBJIT_X_H

#include <stddef.h>

struct keyboard_struct;
struct video_struct;

struct x_struct;

struct x_struct* x_create(struct keyboard_struct* p_keyboard,
                          struct video_struct* p_video,
                          size_t chars_width,
                          size_t chars_height);
void x_destroy(struct x_struct* p_x);

void x_render(struct x_struct* p_x);
void x_event_check(struct x_struct* p_x);

int x_get_fd(struct x_struct* p_x);

#endif /* BEEBJIT_X_H */
