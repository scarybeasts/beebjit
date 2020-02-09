#ifndef BEEBJIT_RENDER_H
#define BEEBJIT_RENDER_H

#include <stdint.h>

struct render_struct;

struct bbc_options;
struct teletext_struct;

enum {
  k_render_mode0 = 0,
  k_render_mode1 = 1,
  k_render_mode2 = 2,
  k_render_mode4 = 3,
  k_render_mode5 = 4,
  k_render_mode7 = 5,
  k_render_num_modes = 6,
};

struct render_character_2MHz {
  uint32_t host_pixels[8];
};

struct render_character_1MHz {
  uint32_t host_pixels[16];
};

struct render_table_2MHz {
  struct render_character_2MHz values[256];
};

struct render_table_1MHz {
  struct render_character_1MHz values[256];
};

struct render_struct* render_create(struct teletext_struct* p_teletext,
                                    struct bbc_options* p_options);
void render_destroy(struct render_struct* p_render);

uint32_t render_get_width(struct render_struct* p_render);
uint32_t render_get_height(struct render_struct* p_render);

uint32_t* render_get_buffer(struct render_struct* p_render);
void render_set_buffer(struct render_struct* p_render, uint32_t* p_buffer);

void render_set_mode(struct render_struct* p_render, int mode);

void render_set_palette(struct render_struct* p_render,
                        uint8_t index,
                        uint32_t rgba);
void render_set_cursor_segments(struct render_struct* p_render,
                                int s0,
                                int s1,
                                int s2,
                                int s3);

struct render_table_2MHz* render_get_2MHz_render_table(
    struct render_struct* p_render, int mode);
struct render_table_1MHz* render_get_1MHz_render_table(
    struct render_struct* p_render, int mode);

void (*render_get_render_data_function(struct render_struct* p_render))
    (struct render_struct*, uint8_t);
void (*render_get_render_blank_function(struct render_struct* p_render))
    (struct render_struct*, uint8_t);

void render_clear_buffer(struct render_struct* p_render);
void render_double_up_lines(struct render_struct* p_render);
void render_hsync(struct render_struct* p_render);
void render_vsync(struct render_struct* p_render);
void render_frame_boundary(struct render_struct* p_render);
void render_cursor(struct render_struct* p_render);

#endif /* BEEBJIT_RENDER_H */
