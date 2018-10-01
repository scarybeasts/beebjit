#include "x.h"

#include "bbc.h"
#include "video.h"

#include <X11/Xlib.h>

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <X11/extensions/XShm.h>

#include <assert.h>
#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>

static const char* k_p_font_name = "-*-fixed-*-*-*-*-20-*-*-*-*-*-iso8859-*";

struct x_struct {
  struct bbc_struct* p_bbc;
  size_t chars_width;
  size_t chars_height;
  Display* d;
  XFontStruct* p_font;
  Window w;
  GC gc;
  size_t fx;
  size_t fy;
  int shmid;
  void* p_shm;
  XImage* p_image;
  XShmSegmentInfo shm_info;
};

struct x_struct*
x_create(struct bbc_struct* p_bbc, size_t chars_width, size_t chars_height) {
  struct x_struct* p_x;
  int s;
  Window root_window;
  unsigned long black_pixel;
  unsigned long white_pixel;
  unsigned long ul_ret;
  Bool bool_ret;
  int ret;
  Visual* p_visual;
  int depth;

  p_x = malloc(sizeof(struct x_struct));
  if (p_x == NULL) {
    errx(1, "couldn't allocate x_struct");
  }
  memset(p_x, '\0', sizeof(struct x_struct));

  p_x->p_bbc = p_bbc;
  p_x->chars_width = chars_width;
  p_x->chars_height = chars_height;

  p_x->d = XOpenDisplay(NULL);
  if (p_x->d == NULL) {
    errx(1, "cannot open X display");
  }

  (void) XkbSetDetectableAutoRepeat(p_x->d, True, &bool_ret);
  if (bool_ret != True) {
    errx(1, "can't set detect auto repeat");
  }

  p_x->p_font = XLoadQueryFont(p_x->d, k_p_font_name);
  if (p_x->p_font == NULL) {
    errx(1, "cannot load font");
  }

  bool_ret = XGetFontProperty(p_x->p_font, XA_FONT, &ul_ret);
  if (bool_ret != True) {
    errx(1, "cannot get font property XA_FONT");
  }
  p_x->fx = p_x->p_font->per_char[0].width;
  p_x->fy = p_x->p_font->per_char[0].ascent;
  printf("loaded font: %s (%zux%zu)\n",
         XGetAtomName(p_x->d, (Atom) ul_ret),
         p_x->fx,
         p_x->fy);

  s = DefaultScreen(p_x->d);
  root_window = RootWindow(p_x->d, s);
  p_visual = DefaultVisual(p_x->d, s);
  black_pixel = BlackPixel(p_x->d, s);
  white_pixel = WhitePixel(p_x->d, s);
  depth = DefaultDepth(p_x->d, s);

  if (depth != 24) {
    errx(1, "default depth not 24");
  }

  p_x->shmid = shmget(IPC_PRIVATE, 640 * 512 * 4, IPC_CREAT|0600);
  if (p_x->shmid < 0) {
    errx(1, "shmget failed");
  }
  p_x->p_shm = shmat(p_x->shmid, NULL, 0);
  if (p_x->p_shm == NULL) {
    errx(1, "shmat failed");
  }
  ret = shmctl(p_x->shmid, IPC_RMID, NULL);
  if (ret != 0) {
    errx(1, "shmctl failed");
  }

  p_x->p_image = XShmCreateImage(p_x->d,
                                 p_visual,
                                 24,
                                 ZPixmap,
                                 NULL,
                                 &p_x->shm_info,
                                 640,
                                 512);
  if (p_x->p_image == NULL) {
    errx(1, "XShmCreateImage failed");
  }

  p_x->shm_info.shmid = p_x->shmid;
  p_x->shm_info.shmaddr = p_x->p_image->data = p_x->p_shm;
  p_x->shm_info.readOnly = False;

  bool_ret = XShmAttach(p_x->d, &p_x->shm_info);
  if (bool_ret != True) {
    errx(1, "XShmAttach failed");
  }

  p_x->w = XCreateSimpleWindow(p_x->d,
                               root_window,
                               10,
                               10,
                               640,
                               512,
                               1,
                               black_pixel,
                               black_pixel);
  if (p_x->w == 0) {
    errx(1, "XCreateSimpleWindow failed");
  }
  ret = XSelectInput(p_x->d, p_x->w, KeyPressMask | KeyReleaseMask);
  if (ret != 1) {
    errx(1, "XSelectInput failed");
  }

  ret = XMapWindow(p_x->d, p_x->w);
  if (ret != 1) {
    errx(1, "XMapWindow failed");
  }

  p_x->gc = XCreateGC(p_x->d, p_x->w, 0, 0);
  if (p_x->gc == 0) {
    errx(1, "XCreateGC failed");
  }
  ret = XSetBackground(p_x->d, p_x->gc, black_pixel);
  if (ret != 1) {
    errx(1, "XSetBackground failed");
  }
  ret = XSetForeground(p_x->d, p_x->gc, white_pixel);
  if (ret != 1) {
    errx(1, "XSetForeground failed");
  }
  ret = XSetFont(p_x->d, p_x->gc, p_x->p_font->fid);
  if (ret != 1) {
    errx(1, "XSetFont failed");
  }

  ret = XFlush(p_x->d);
  if (ret != 1) {
    errx(1, "XFlush failed");
  }

  return p_x;
}

void
x_destroy(struct x_struct* p_x) {
  int ret;
  Bool bool_ret;

  ret = XFreeGC(p_x->d, p_x->gc);
  if (ret != 1) {
    errx(1, "XFreeGC failed");
  }
  ret = XUnmapWindow(p_x->d, p_x->w);
  if (ret != 1) {
    errx(1, "XUnmapWindow failed");
  }
  ret = XDestroyWindow(p_x->d, p_x->w);
  if (ret != 1) {
    errx(1, "XDestroyWindow failed");
  }
  bool_ret = XShmDetach(p_x->d, &p_x->shm_info);
  if (bool_ret != True) {
    errx(1, "XShmDetach failed");
  }
  ret = XDestroyImage(p_x->p_image);
  if (ret != 1) {
    errx(1, "XDestroyImage failed");
  }
  ret = shmdt(p_x->p_shm);
  if (ret != 0) {
    errx(1, "shmdt failed");
  }
  ret = XFreeFont(p_x->d, p_x->p_font);
  if (ret != 1) {
    errx(1, "XFreeFont failed");
  }

  ret = XCloseDisplay(p_x->d);
  if (ret != 0) {
    errx(1, "XCloseDisplay failed");
  }

  free(p_x); 
}

void
x_render(struct x_struct* p_x) {
  struct bbc_struct* p_bbc = p_x->p_bbc;
  struct video_struct* p_video = bbc_get_video(p_bbc);
  int is_text = video_is_text(p_video);

  if (is_text) {
    size_t y;
    size_t offset = 0;
    size_t y_offset = 16;
    size_t chars_width = p_x->chars_width;
    size_t chars_height = p_x->chars_height;
    for (y = 0; y < chars_height; ++y) {
      unsigned char* p_video_mem = video_get_memory(p_video,
                                                    offset,
                                                    chars_width);
      /* Seems to return 0 on success -- status not checked. */
      XDrawImageString(p_x->d,
                  p_x->w,
                  p_x->gc,
                  0,
                  y_offset,
                  (char*) p_video_mem,
                  chars_width);
      offset += chars_width;
      y_offset += 16;
    }
  } else {
    video_render(p_video, p_x->p_shm, 640, 512, 4);
    Bool bool_ret = XShmPutImage(p_x->d,
                                 p_x->w,
                                 p_x->gc,
                                 p_x->p_image,
                                 0,
                                 0,
                                 0,
                                 0,
                                 640,
                                 512,
                                 False);
    if (bool_ret != True) {
      errx(1, "XShmPutImage failed");
    }
  }
}

void
x_event_check(struct x_struct* p_x) {
  Display* d = p_x->d;
  struct bbc_struct* p_bbc = p_x->p_bbc;

  while (XPending(d) > 0) {
    XEvent event;
    int key;

    int ret = XNextEvent(d, &event);
    if (ret != 0) {
      errx(1, "XNextEvent failed");
    }

    switch (event.type) {
    case KeyPress:
      key = event.xkey.keycode;
      /*printf("key press: %d\n", key);*/
      bbc_key_pressed(p_bbc, key);
      break;
    case KeyRelease:
      key = event.xkey.keycode;
      /*printf("key release: %d\n", key);*/
      bbc_key_released(p_bbc, key);
      break;
    default:
      assert(0);
    }
  }
}

int
x_get_fd(struct x_struct* p_x) {
  Display* d = p_x->d;
  int fd = ConnectionNumber(d);

  return fd;
}
