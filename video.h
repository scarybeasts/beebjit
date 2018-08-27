#ifndef BEEBJIT_VIDEO_H
#define BEEBJIT_VIDEO_H

#include <stddef.h>

struct video_struct;

struct video_struct* video_create();
void video_destroy(struct video_struct* p_video);

void video_render(struct video_struct* p_video,
                  unsigned char* p_video_mem,
                  size_t x,
                  size_t y,
                  size_t bpp);

#endif /* BEEBJIT_VIDEO_H */
