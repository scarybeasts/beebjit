#ifndef BEEBJIT_X_H
#define BEEBJIT_X_H

#include <stddef.h>

struct bbc_struct;

struct x_struct;

struct x_struct* x_create(unsigned char* p_screen_mem,
                          size_t chars_width,
                          size_t chars_height,
                          struct bbc_struct* p_bbc);

void x_destroy(struct x_struct* p_x);

void x_render(struct x_struct* p_x);

void x_event_loop(struct x_struct* p_x);
void x_launch_event_loop_async(struct x_struct* p_x);

#endif /* BEEBJIT_X_H */
