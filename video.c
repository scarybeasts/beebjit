#include "video.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

struct video_struct {
  unsigned char video_ula_control;
  unsigned int palette[16];
};

struct video_struct*
video_create() {
  struct video_struct* p_video = malloc(sizeof(struct video_struct));
  if (p_video == NULL) {
    errx(1, "cannot allocate video_struct");
  }
  memset(p_video, '\0', sizeof(struct video_struct));

  return p_video;
}

void
video_destroy(struct video_struct* p_video) {
  free(p_video);
}

void
video_render(struct video_struct* p_video,
             unsigned char* p_video_mem,
             size_t x,
             size_t y,
             size_t bpp) {
  assert(x == 640);
  assert(y == 256);
  assert(bpp == 4);
}
