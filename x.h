#ifndef BEEBJIT_X_H
#define BEEBJIT_X_H

#include <stddef.h>

struct bbc_struct;

struct x_struct;

struct x_struct* x_create(struct bbc_struct* p_bbc,
                          size_t chars_width,
                          size_t chars_height);

void x_destroy(struct x_struct* p_x);

void x_render(struct x_struct* p_x);

void x_event_check(struct x_struct* p_x);

#endif /* BEEBJIT_X_H */
