#ifndef BEEBJIT_RENDER_H
#define BEEBJIT_RENDER_H

#include <stdint.h>

struct render_struct;

struct bbc_options;

struct render_struct* render_create(struct bbc_options* p_options);
void render_destroy(struct render_struct* p_render);

uint32_t* render_get_buffer(struct render_struct* p_render);
void render_set_buffer(struct render_struct* p_render, uint32_t* p_buffer);

uint32_t render_get_width(struct render_struct* p_render);
uint32_t render_get_height(struct render_struct* p_render);

void render_clear_buffer(struct render_struct* p_render);
void render_double_up_lines(struct render_struct* p_render);

#endif /* BEEBJIT_RENDER_H */
