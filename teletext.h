#ifndef BEEBJIT_TELETEXT_H
#define BEEBJIT_TELETEXT_H

#include <stdint.h>

struct teletext_struct;

struct render_character_1MHz;
struct video_struct;

struct teletext_struct* teletext_create();
void teletext_destroy(struct teletext_struct* p_teletext);

void teletext_render_data(struct teletext_struct* p_teletext,
                          struct render_character_1MHz* p_out,
                          struct render_character_1MHz* p_next_out,
                          uint8_t data);
void teletext_DISPMTG_changed(struct teletext_struct* p_teletext, int value);
void teletext_VSYNC_changed(struct teletext_struct* p_teletext, int value);

#endif /* BEEBJIT_TELETEXT_H */
