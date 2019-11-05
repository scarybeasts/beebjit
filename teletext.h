#ifndef BEEBJIT_TELETEXT_H
#define BEEBJIT_TELETEXT_H

struct teletext_struct;

struct video_struct;

struct teletext_struct* teletext_create();
void teletext_destroy(struct teletext_struct* p_teletext);

/* TODO: p_buffer shouldn't be passed in. */
void teletext_render_full(struct teletext_struct* p_teletext,
                          struct video_struct* p_video,
                          unsigned int* p_buffer);

#endif /* BEEBJIT_TELETEXT_H */
