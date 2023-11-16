#ifndef BEEBJIT_RENDER_H
#define BEEBJIT_RENDER_H

#include <stdint.h>

struct render_struct;

struct bbc_options;
struct teletext_struct;

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

void render_set_flyback_callback(struct render_struct* p_render,
                                 void (*p_flyback_callback)(void* p),
                                 void* p_callback_object);

uint32_t render_get_width(struct render_struct* p_render);
uint32_t render_get_height(struct render_struct* p_render);
uint32_t render_get_buffer_size(struct render_struct* p_render);
uint32_t render_get_horiz_pos(struct render_struct* p_render);
uint32_t render_get_vert_pos(struct render_struct* p_render);

uint32_t render_get_buffer_crc32(struct render_struct* p_render);

uint32_t* render_get_buffer(struct render_struct* p_render);
void render_set_buffer(struct render_struct* p_render, uint32_t* p_buffer);
int render_has_buffer(struct render_struct* p_render);
void render_create_internal_buffer(struct render_struct* p_render);

void render_set_mode(struct render_struct* p_render,
                     int clock_speed,
                     int chars_per_line,
                     int is_teletext);

void render_set_palette(struct render_struct* p_render,
                        uint8_t index,
                        uint32_t rgba);
void render_set_cursor_segments(struct render_struct* p_render,
                                int s0,
                                int s1,
                                int s2,
                                int s3);
void render_set_DISPEN(struct render_struct* p_render, int is_enabled);
void render_set_RA(struct render_struct* p_render, uint32_t row_address);

/* Call render_prepare() before a sequence of render_render() to ensure that
 * all pending pixel table rebuilds are taken care of.
 */
void render_prepare(struct render_struct* p_render);
void render_render(struct render_struct* p_render,
                   uint8_t data,
                   uint16_t addr,
                   uint64_t ticks);

void render_clear_buffer(struct render_struct* p_render);
void render_process_full_buffer(struct render_struct* p_render);
void render_hsync(struct render_struct* p_render, uint32_t hsync_pulse_ticks);
void render_vsync(struct render_struct* p_render);
void render_horiz_line(struct render_struct* p_render, uint32_t argb);
void render_cursor(struct render_struct* p_render);
void render_set_horiz_beam_pos(struct render_struct* p_render, uint32_t pos);

#endif /* BEEBJIT_RENDER_H */
