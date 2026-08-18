#ifndef BEEBJIT_TELETEXT_H
#define BEEBJIT_TELETEXT_H

#include <stdint.h>

struct teletext_struct;

struct render_character_2MHz;
struct video_struct;

struct teletext_struct* teletext_create(void);
void teletext_destroy(struct teletext_struct* p_teletext);

void teletext_set_black_rgb(struct teletext_struct* p_teletext, uint32_t rgb);

void teletext_data(struct teletext_struct* p_teletext,
                   uint8_t data,
                   int is_dispen);
void teletext_RA_ISV_changed(struct teletext_struct* p_teletext,
                             uint8_t ra,
                             int is_isv);
void teletext_VSYNC_changed(struct teletext_struct* p_teletext, int value);

void teletext_render(struct teletext_struct* p_teletext,
                     int is_odd_tick,
                     struct render_character_2MHz* p_out,
                     struct render_character_2MHz* p_next_out);

#endif /* BEEBJIT_TELETEXT_H */
