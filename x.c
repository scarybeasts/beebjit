#include "x.h"

#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

static const char* k_p_font_name = "-*-fixed-*-*-*-*-20-*-*-*-*-*-iso8859-*";

struct x_struct {
  char* p_screen_mem;
  size_t chars_width;
  size_t chars_height;
  Display* d;
  Window w;
  GC gc;
  size_t fx;
  size_t fy;
};

struct x_struct*
x_create(char* p_screen_mem, size_t chars_width, size_t chars_height) {
  struct x_struct* p_x;
  int s;
  Window root_window;
  unsigned long black_pixel;
  unsigned long white_pixel;
  XFontStruct* p_font;
  unsigned long ul_ret;
  XEvent e;

  p_x = malloc(sizeof(struct x_struct));
  if (p_x == NULL) {
    errx(1, "couldn't allocate x_struct");
  }
  p_x->p_screen_mem = p_screen_mem;
  p_x->chars_width = chars_width;
  p_x->chars_height = chars_height;

  p_x->d = XOpenDisplay(NULL);
  if (p_x->d == NULL) {
    errx(1, "cannot open X display");
  }

  p_font = XLoadQueryFont(p_x->d, k_p_font_name);
  if (p_font == NULL) {
    errx(1, "cannot load font");
  }

  if (!XGetFontProperty(p_font, XA_FONT, &ul_ret)) {
    errx(1, "cannot get font property XA_FONT");
  }
  p_x->fx = p_font->per_char[0].width;
  p_x->fy = p_font->per_char[0].ascent;
  printf("loaded font: %s (%zux%zu)\n",
         XGetAtomName(p_x->d, (Atom) ul_ret),
         p_x->fx,
         p_x->fy);

  s = DefaultScreen(p_x->d);
  root_window = RootWindow(p_x->d, s);
  black_pixel = BlackPixel(p_x->d, s);
  white_pixel = WhitePixel(p_x->d, s);

  p_x->w = XCreateSimpleWindow(p_x->d,
                               root_window,
                               10,
                               10,
                               p_x->chars_width * p_x->fx,
                               p_x->chars_height * p_x->fy,
                               1,
                               black_pixel,
                               black_pixel);
  if (p_x->w == 0) {
    errx(1, "XCreateSimpleWindow failed");
  }

  if (!XMapWindow(p_x->d, p_x->w)) {
    errx(1, "XMapWindow failed");
  }

  p_x->gc = XCreateGC(p_x->d, p_x->w, 0, 0);
  if (p_x->gc == 0) {
    errx(1, "XCreateGC failed");
  }
  if (!XSetBackground(p_x->d, p_x->gc, black_pixel)) {
    errx(1, "XSetBackground failed");
  }
  if (!XSetForeground(p_x->d, p_x->gc, white_pixel)) {
    errx(1, "XSetForeground failed");
  }
  if (!XSetFont(p_x->d, p_x->gc, p_font->fid)) {
    errx(1, "XSetFont failed");
  }

  if (!XFlush(p_x->d)) {
    errx(1, "XFlush failed");
  }

  return p_x;
}

void
x_destroy(struct x_struct* p_x) {
  /* Seems to return 0 -- status not checked. */
  XCloseDisplay(p_x->d);
  free(p_x); 
}

void
x_render(struct x_struct* p_x) {
  size_t y;
  size_t y_offset = p_x->fy;
  char* p_text = p_x->p_screen_mem;
  if (!XClearWindow(p_x->d, p_x->w)) {
    errx(1, "XClearWindow failed");
  }
  for (y = 0; y < p_x->chars_height; ++y) {
    /* Seems to return 0 on success -- status not checked. */
    XDrawString(p_x->d, p_x->w, p_x->gc, 0, y_offset, p_text, p_x->chars_width);
    p_text += p_x->chars_width;
    y_offset += p_x->fy;
  }
  if (!XFlush(p_x->d)) {
    errx(1, "XFlush failed");
  }
}
