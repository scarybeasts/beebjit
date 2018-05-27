#include "x.h"

#include <err.h>

#include <X11/Xlib.h>

void
x_init() {
  Display* d;
  int s;
  Window root_window;
  unsigned long black_pixel;
  Window w;
  XEvent e;

  d = XOpenDisplay(NULL);
  if (d == NULL) {
    errx(1, "cannot open X display");
  }

  s = DefaultScreen(d);
  root_window = RootWindow(d, s);
  black_pixel = BlackPixel(d, s);

  w = XCreateSimpleWindow(d,
                          root_window,
                          10,
                          10,
                          100,
                          100,
                          1,
                          black_pixel,
                          black_pixel);
  XMapWindow(d, w);
  XFlush(d);
}
