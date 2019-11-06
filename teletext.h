#ifndef BEEBJIT_TELETEXT_H
#define BEEBJIT_TELETEXT_H

struct teletext_struct;

struct video_struct;

struct teletext_struct* teletext_create();
void teletext_destroy(struct teletext_struct* p_teletext);

void teletext_render_full(struct teletext_struct* p_teletext,
                          struct video_struct* p_video);

#endif /* BEEBJIT_TELETEXT_H */
