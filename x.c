#include "x.h"

#include "bbc.h"

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

  ret = XInitThreads();
  if (ret != 1) {
    errx(1, "XInitThreads failed");
  }

  p_x = malloc(sizeof(struct x_struct));
  if (p_x == NULL) {
    errx(1, "couldn't allocate x_struct");
  }
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
  if (ret != 1) {
    errx(1, "XCloseDisplay failed");
  }

  free(p_x); 
}

void
x_render(struct x_struct* p_x) {
  struct bbc_struct* p_bbc = p_x->p_bbc;
  size_t y_offset = 16;
  unsigned char* p_screen_mem = bbc_get_screen_mem(p_bbc);
  int is_text = bbc_get_screen_is_text(p_bbc);
  size_t pixel_width = bbc_get_screen_pixel_width(p_bbc);
  size_t clock_speed = bbc_get_screen_clock_speed(p_bbc);
  size_t x;
  size_t y;
  int ret;
  unsigned int colors[16];
  memset(colors, '\0', sizeof(colors));
  colors[0] = 0;
  colors[1] = 0x00ff0000;
  colors[2] = 0x0000ff00;
  colors[3] = 0x00ffff00;
  colors[4] = 0x000000ff;
  colors[5] = 0x00ff00ff;
  colors[6] = 0x0000ffff;
  colors[7] = 0x00ffffff;
  colors[8] = 0;
  colors[9] = 0x00ff0000;
  colors[10] = 0x0000ff00;
  colors[11] = 0x00ffff00;
  colors[12] = 0x000000ff;
  colors[13] = 0x00ff00ff;
  colors[14] = 0x0000ffff;
  colors[15] = 0x00ffffff;

  ret = XClearWindow(p_x->d, p_x->w);
  if (ret != 1) {
    errx(1, "XClearWindow failed");
  }

  if (is_text) {
    for (y = 0; y < p_x->chars_height; ++y) {
      /* Seems to return 0 on success -- status not checked. */
      XDrawString(p_x->d,
                  p_x->w,
                  p_x->gc,
                  0,
                  y_offset,
                  (char*) p_screen_mem,
                  p_x->chars_width);
      p_screen_mem += p_x->chars_width;
      y_offset += 16;
    }
  } else if (pixel_width == 1)  {
    assert(clock_speed == 1);
    for (y = 0; y < 32; ++y) {
      for (x = 0; x < 80; ++x) {
        size_t y2;
        for (y2 = 0; y2 < 8; ++y2) {
          unsigned char packed_pixels = *p_screen_mem++;
          unsigned int* p_x_mem = (unsigned int*) p_x->p_shm;
          unsigned char p1 = !!(packed_pixels & 0x80);
          unsigned char p2 = !!(packed_pixels & 0x40);
          unsigned char p3 = !!(packed_pixels & 0x20);
          unsigned char p4 = !!(packed_pixels & 0x10);
          unsigned char p5 = !!(packed_pixels & 0x08);
          unsigned char p6 = !!(packed_pixels & 0x04);
          unsigned char p7 = !!(packed_pixels & 0x02);
          unsigned char p8 = !!(packed_pixels & 0x01);
          p_x_mem += ((y * 8) + y2) * 2 * 640;
          p_x_mem += x * 8;
          p_x_mem[0] = ~(p1 - 1);
          p_x_mem[1] = ~(p2 - 1);
          p_x_mem[2] = ~(p3 - 1);
          p_x_mem[3] = ~(p4 - 1);
          p_x_mem[4] = ~(p5 - 1);
          p_x_mem[5] = ~(p6 - 1);
          p_x_mem[6] = ~(p7 - 1);
          p_x_mem[7] = ~(p8 - 1);
          p_x_mem[640] = ~(p1 - 1);
          p_x_mem[641] = ~(p2 - 1);
          p_x_mem[642] = ~(p3 - 1);
          p_x_mem[643] = ~(p4 - 1);
          p_x_mem[644] = ~(p5 - 1);
          p_x_mem[645] = ~(p6 - 1);
          p_x_mem[646] = ~(p7 - 1);
          p_x_mem[647] = ~(p8 - 1);
        }
      }
    }
  } else if (pixel_width == 2 && clock_speed == 1) {
    for (y = 0; y < 32; ++y) {
      for (x = 0; x < 80; ++x) {
        size_t y2;
        for (y2 = 0; y2 < 8; ++y2) {
          unsigned char packed_pixels = *p_screen_mem++;
          /* TODO: lookup table to make this fast. */
          unsigned char v1 = ((packed_pixels & 0x80) >> 6) |
                             ((packed_pixels & 0x08) >> 3);
          unsigned char v2 = ((packed_pixels & 0x40) >> 5) |
                             ((packed_pixels & 0x04) >> 2);
          unsigned char v3 = ((packed_pixels & 0x20) >> 4) |
                             ((packed_pixels & 0x02) >> 1);
          unsigned char v4 = ((packed_pixels & 0x10) >> 3) |
                             ((packed_pixels & 0x01) >> 0);
          unsigned int p1 = colors[(1 << v1) - 1];
          unsigned int p2 = colors[(1 << v2) - 1];
          unsigned int p3 = colors[(1 << v3) - 1];
          unsigned int p4 = colors[(1 << v4) - 1];
          unsigned int* p_x_mem = (unsigned int*) p_x->p_shm;
          p_x_mem += ((y * 8) + y2) * 2 * 640;
          p_x_mem += x * 8;
          p_x_mem[0] = p1;
          p_x_mem[1] = p1;
          p_x_mem[2] = p2;
          p_x_mem[3] = p2;
          p_x_mem[4] = p3;
          p_x_mem[5] = p3;
          p_x_mem[6] = p4;
          p_x_mem[7] = p4;
          p_x_mem[640] = p1;
          p_x_mem[641] = p1;
          p_x_mem[642] = p2;
          p_x_mem[643] = p2;
          p_x_mem[644] = p3;
          p_x_mem[645] = p3;
          p_x_mem[646] = p4;
          p_x_mem[647] = p4;
        }
      }
    }
  } else if (pixel_width == 4 && clock_speed == 0) {
    for (y = 0; y < 32; ++y) {
      for (x = 0; x < 40; ++x) {
        size_t y2;
        for (y2 = 0; y2 < 8; ++y2) {
          unsigned char packed_pixels = *p_screen_mem++;
          /* TODO: lookup table to make this fast. */
          unsigned char v1 = ((packed_pixels & 0x80) >> 6) |
                             ((packed_pixels & 0x08) >> 3);
          unsigned char v2 = ((packed_pixels & 0x40) >> 5) |
                             ((packed_pixels & 0x04) >> 2);
          unsigned char v3 = ((packed_pixels & 0x20) >> 4) |
                             ((packed_pixels & 0x02) >> 1);
          unsigned char v4 = ((packed_pixels & 0x10) >> 3) |
                             ((packed_pixels & 0x01) >> 0);
          unsigned int p1 = colors[(1 << v1) - 1];
          unsigned int p2 = colors[(1 << v2) - 1];
          unsigned int p3 = colors[(1 << v3) - 1];
          unsigned int p4 = colors[(1 << v4) - 1];
          unsigned int* p_x_mem = (unsigned int*) p_x->p_shm;
          p_x_mem += ((y * 8) + y2) * 2 * 640;
          p_x_mem += x * 16;
          p_x_mem[0] = p1;
          p_x_mem[1] = p1;
          p_x_mem[2] = p1;
          p_x_mem[3] = p1;
          p_x_mem[4] = p2;
          p_x_mem[5] = p2;
          p_x_mem[6] = p2;
          p_x_mem[7] = p2;
          p_x_mem[8] = p3;
          p_x_mem[9] = p3;
          p_x_mem[10] = p3;
          p_x_mem[11] = p3;
          p_x_mem[12] = p4;
          p_x_mem[13] = p4;
          p_x_mem[14] = p4;
          p_x_mem[15] = p4;
          p_x_mem[640] = p1;
          p_x_mem[641] = p1;
          p_x_mem[642] = p1;
          p_x_mem[643] = p1;
          p_x_mem[644] = p2;
          p_x_mem[645] = p2;
          p_x_mem[646] = p2;
          p_x_mem[647] = p2;
          p_x_mem[648] = p3;
          p_x_mem[649] = p3;
          p_x_mem[650] = p3;
          p_x_mem[651] = p3;
          p_x_mem[652] = p4;
          p_x_mem[653] = p4;
          p_x_mem[654] = p4;
          p_x_mem[655] = p4;
        }
      }
    }
  } else if (pixel_width == 4 && clock_speed == 1) {
    for (y = 0; y < 32; ++y) {
      for (x = 0; x < 80; ++x) {
        size_t y2;
        for (y2 = 0; y2 < 8; ++y2) {
          unsigned char packed_pixels = *p_screen_mem++;
          /* TODO: lookup table to make this fast. */
          unsigned char v1 = ((packed_pixels & 0x80) >> 4) |
                             ((packed_pixels & 0x20) >> 3) |
                             ((packed_pixels & 0x08) >> 2) |
                             ((packed_pixels & 0x02) >> 1);
          unsigned char v2 = ((packed_pixels & 0x40) >> 3) |
                             ((packed_pixels & 0x10) >> 2) |
                             ((packed_pixels & 0x04) >> 1) |
                             ((packed_pixels & 0x01) >> 0);
          unsigned int p1 = colors[v1];
          unsigned int p2 = colors[v2];
          unsigned int* p_x_mem = (unsigned int*) p_x->p_shm;
          p_x_mem += ((y * 8) + y2) * 2 * 640;
          p_x_mem += x * 8;
          p_x_mem[0] = p1;
          p_x_mem[1] = p1;
          p_x_mem[2] = p1;
          p_x_mem[3] = p1;
          p_x_mem[4] = p2;
          p_x_mem[5] = p2;
          p_x_mem[6] = p2;
          p_x_mem[7] = p2;
          p_x_mem[640] = p1;
          p_x_mem[641] = p1;
          p_x_mem[642] = p1;
          p_x_mem[643] = p1;
          p_x_mem[644] = p2;
          p_x_mem[645] = p2;
          p_x_mem[646] = p2;
          p_x_mem[647] = p2;
        }
      }
    }
  }

  if (!is_text) {
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

  ret = XFlush(p_x->d);
  if (ret != 1) {
    errx(1, "XFlush failed");
  }
}

void
x_event_loop(struct x_struct* p_x) {
  Display* d = p_x->d;
  struct bbc_struct* p_bbc = p_x->p_bbc;
  XEvent event;
  int ret;
  int key;

  while (1) {
    ret = XNextEvent(d, &event);
    if (ret != 0) {
      errx(1, "XNextEvent failed");
    }
    switch (event.type) {
    case KeyPress:
      key = event.xkey.keycode;
      bbc_key_pressed(p_bbc, key);
      break;
    case KeyRelease:
      key = event.xkey.keycode;
      bbc_key_released(p_bbc, key);
      break;
    default:
      assert(0);
    }
  }
}

static void*
x_event_thread(void* p) {
  struct x_struct* p_x = (struct x_struct*) p;

  x_event_loop(p_x);

  assert(0);
}

void
x_launch_event_loop_async(struct x_struct* p_x) {
  pthread_t thread;
  int ret = pthread_create(&thread, NULL, x_event_thread, p_x);
  if (ret != 0) {
    errx(1, "couldn't create thread");
  }
}
