#include "os_window.h"

#include "keyboard.h"
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

struct os_window_struct {
  struct keyboard_struct* p_keyboard;
  struct video_struct* p_video;
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

struct os_window_struct*
os_window_create(struct keyboard_struct* p_keyboard,
                 struct video_struct* p_video,
                 size_t chars_width,
                 size_t chars_height) {
  struct os_window_struct* p_window;
  int s;
  Window root_window;
  unsigned long black_pixel;
  unsigned long white_pixel;
  unsigned long ul_ret;
  Bool bool_ret;
  int ret;
  Visual* p_visual;
  int depth;

  p_window = malloc(sizeof(struct os_window_struct));
  if (p_window == NULL) {
    errx(1, "couldn't allocate os_window_struct");
  }
  (void) memset(p_window, '\0', sizeof(struct os_window_struct));

  p_window->p_keyboard = p_keyboard;
  p_window->p_video = p_video;
  p_window->chars_width = chars_width;
  p_window->chars_height = chars_height;

  p_window->d = XOpenDisplay(NULL);
  if (p_window->d == NULL) {
    errx(1, "cannot open X display");
  }

  (void) XkbSetDetectableAutoRepeat(p_window->d, True, &bool_ret);
  if (bool_ret != True) {
    errx(1, "can't set detect auto repeat");
  }

  p_window->p_font = XLoadQueryFont(p_window->d, k_p_font_name);
  if (p_window->p_font == NULL) {
    errx(1, "cannot load font");
  }

  bool_ret = XGetFontProperty(p_window->p_font, XA_FONT, &ul_ret);
  if (bool_ret != True) {
    errx(1, "cannot get font property XA_FONT");
  }
  p_window->fx = p_window->p_font->per_char[0].width;
  p_window->fy = p_window->p_font->per_char[0].ascent;
  printf("loaded font: %s (%zux%zu)\n",
         XGetAtomName(p_window->d, (Atom) ul_ret),
         p_window->fx,
         p_window->fy);

  s = DefaultScreen(p_window->d);
  root_window = RootWindow(p_window->d, s);
  p_visual = DefaultVisual(p_window->d, s);
  black_pixel = BlackPixel(p_window->d, s);
  white_pixel = WhitePixel(p_window->d, s);
  depth = DefaultDepth(p_window->d, s);

  if (depth != 24) {
    errx(1, "default depth not 24");
  }

  p_window->shmid = shmget(IPC_PRIVATE, 640 * 512 * 4, IPC_CREAT|0600);
  if (p_window->shmid < 0) {
    errx(1, "shmget failed");
  }
  p_window->p_shm = shmat(p_window->shmid, NULL, 0);
  if (p_window->p_shm == NULL) {
    errx(1, "shmat failed");
  }
  ret = shmctl(p_window->shmid, IPC_RMID, NULL);
  if (ret != 0) {
    errx(1, "shmctl failed");
  }

  p_window->p_image = XShmCreateImage(p_window->d,
                                 p_visual,
                                 24,
                                 ZPixmap,
                                 NULL,
                                 &p_window->shm_info,
                                 640,
                                 512);
  if (p_window->p_image == NULL) {
    errx(1, "XShmCreateImage failed");
  }

  p_window->shm_info.shmid = p_window->shmid;
  p_window->shm_info.shmaddr = p_window->p_image->data = p_window->p_shm;
  p_window->shm_info.readOnly = False;

  bool_ret = XShmAttach(p_window->d, &p_window->shm_info);
  if (bool_ret != True) {
    errx(1, "XShmAttach failed");
  }

  p_window->w = XCreateSimpleWindow(p_window->d,
                               root_window,
                               10,
                               10,
                               640,
                               512,
                               1,
                               black_pixel,
                               black_pixel);
  if (p_window->w == 0) {
    errx(1, "XCreateSimpleWindow failed");
  }
  ret = XSelectInput(p_window->d, p_window->w, KeyPressMask | KeyReleaseMask);
  if (ret != 1) {
    errx(1, "XSelectInput failed");
  }

  ret = XMapWindow(p_window->d, p_window->w);
  if (ret != 1) {
    errx(1, "XMapWindow failed");
  }

  p_window->gc = XCreateGC(p_window->d, p_window->w, 0, 0);
  if (p_window->gc == 0) {
    errx(1, "XCreateGC failed");
  }
  ret = XSetBackground(p_window->d, p_window->gc, black_pixel);
  if (ret != 1) {
    errx(1, "XSetBackground failed");
  }
  ret = XSetForeground(p_window->d, p_window->gc, white_pixel);
  if (ret != 1) {
    errx(1, "XSetForeground failed");
  }
  ret = XSetFont(p_window->d, p_window->gc, p_window->p_font->fid);
  if (ret != 1) {
    errx(1, "XSetFont failed");
  }

  ret = XFlush(p_window->d);
  if (ret != 1) {
    errx(1, "XFlush failed");
  }

  return p_window;
}

void
os_window_destroy(struct os_window_struct* p_window) {
  int ret;
  Bool bool_ret;

  ret = XFreeGC(p_window->d, p_window->gc);
  if (ret != 1) {
    errx(1, "XFreeGC failed");
  }
  ret = XUnmapWindow(p_window->d, p_window->w);
  if (ret != 1) {
    errx(1, "XUnmapWindow failed");
  }
  ret = XDestroyWindow(p_window->d, p_window->w);
  if (ret != 1) {
    errx(1, "XDestroyWindow failed");
  }
  bool_ret = XShmDetach(p_window->d, &p_window->shm_info);
  if (bool_ret != True) {
    errx(1, "XShmDetach failed");
  }
  ret = XDestroyImage(p_window->p_image);
  if (ret != 1) {
    errx(1, "XDestroyImage failed");
  }
  ret = shmdt(p_window->p_shm);
  if (ret != 0) {
    errx(1, "shmdt failed");
  }
  ret = XFreeFont(p_window->d, p_window->p_font);
  if (ret != 1) {
    errx(1, "XFreeFont failed");
  }

  ret = XCloseDisplay(p_window->d);
  if (ret != 0) {
    errx(1, "XCloseDisplay failed");
  }

  free(p_window);
}

size_t
os_window_get_handle(struct os_window_struct* p_window) {
  Display* d = p_window->d;
  int fd = ConnectionNumber(d);

  return fd;
}

void
os_window_render(struct os_window_struct* p_window) {
  struct video_struct* p_video = p_window->p_video;
  int is_text = video_is_text(p_video);

  if (is_text) {
    size_t y;
    size_t offset = 0;
    size_t y_offset = 16;
    size_t chars_width = p_window->chars_width;
    size_t chars_height = p_window->chars_height;
    for (y = 0; y < chars_height; ++y) {
      uint8_t* p_video_mem = video_get_memory(p_video, offset, chars_width);
      /* Seems to return 0 on success -- status not checked. */
      XDrawImageString(p_window->d,
                  p_window->w,
                  p_window->gc,
                  0,
                  y_offset,
                  (char*) p_video_mem,
                  chars_width);
      offset += chars_width;
      y_offset += 16;
    }
  } else {
    video_render(p_video, p_window->p_shm, 640, 512, 4);
    Bool bool_ret = XShmPutImage(p_window->d,
                                 p_window->w,
                                 p_window->gc,
                                 p_window->p_image,
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

  /* We need to check for X events here, in case a key event comes
   * in during X rendering. In that case, the data could be read from the
   * socket and placed in the event queue during standard X queue processing.
   * This would lead to a delayed event because the poll() wouldn't see it
   * in the socket queue.
   */
  os_window_process_events(p_window);
}

static uint8_t
x_key_to_keyboard_key(int key) {
  uint8_t ret = 0;

  switch (key) {
  case 9:
    ret = k_keyboard_key_escape;
    break;
  case 10:
  case 11:
  case 12:
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
    ret = (key + '1' - 10);
    break;
  case 19:
    ret = '0';
    break;
  case 20:
    ret = '-';
    break;
  case 21:
    ret = '=';
    break;
  case 22:
    ret = k_keyboard_key_backspace;
    break;
  case 23:
    ret = k_keyboard_key_tab;
    break;
  case 24:
    ret = 'Q';
    break;
  case 25:
    ret = 'W';
    break;
  case 26:
    ret = 'E';
    break;
  case 27:
    ret = 'R';
    break;
  case 28:
    ret = 'T';
    break;
  case 29:
    ret = 'Y';
    break;
  case 30:
    ret = 'U';
    break;
  case 31:
    ret = 'I';
    break;
  case 32:
    ret = 'O';
    break;
  case 33:
    ret = 'P';
    break;
  case 34:
    ret = '[';
    break;
  case 35:
    ret = ']';
    break;
  case 36:
    ret = k_keyboard_key_enter;
    break;
  case 37:
    ret = k_keyboard_key_ctrl_left;
    break;
  case 38:
    ret = 'A';
    break;
  case 39:
    ret = 'S';
    break;
  case 40:
    ret = 'D';
    break;
  case 41:
    ret = 'F';
    break;
  case 42:
    ret = 'G';
    break;
  case 43:
    ret = 'H';
    break;
  case 44:
    ret = 'J';
    break;
  case 45:
    ret = 'K';
    break;
  case 46:
    ret = 'L';
    break;
  case 47:
    ret = ';';
    break;
  case 48:
    ret = '\'';
    break;
  case 50:
    ret = k_keyboard_key_shift_left;
    break;
  case 51:
    ret = '\\';
    break;
  case 52:
    ret = 'Z';
    break;
  case 53:
    ret = 'X';
    break;
  case 54:
    ret = 'C';
    break;
  case 55:
    ret = 'V';
    break;
  case 56:
    ret = 'B';
    break;
  case 57:
    ret = 'N';
    break;
  case 58:
    ret = 'M';
    break;
  case 59:
    ret = ',';
    break;
  case 60:
    ret = '.';
    break;
  case 61:
    ret = '/';
    break;
  case 62:
    ret = k_keyboard_key_shift_right;
    break;
  case 64:
    ret = k_keyboard_key_alt_left;
    break;
  case 65:
    ret = ' ';
    break;
  case 66:
    ret = k_keyboard_key_caps_lock;
    break;
  case 67:
    ret = k_keyboard_key_f1;
    break;
  case 68:
    ret = k_keyboard_key_f2;
    break;
  case 69:
    ret = k_keyboard_key_f3;
    break;
  case 70:
    ret = k_keyboard_key_f4;
    break;
  case 71:
    ret = k_keyboard_key_f5;
    break;
  case 72:
    ret = k_keyboard_key_f6;
    break;
  case 73:
    ret = k_keyboard_key_f7;
    break;
  case 74:
    ret = k_keyboard_key_f8;
    break;
  case 75:
    ret = k_keyboard_key_f9;
    break;
  case 76:
    ret = k_keyboard_key_f0;
    break;
  case 95:
    ret = k_keyboard_key_f11;
    break;
  case 96:
    ret = k_keyboard_key_f12;
    break;
  case 111:
    ret = k_keyboard_key_arrow_up;
    break;
  case 113:
    ret = k_keyboard_key_arrow_left;
    break;
  case 114:
    ret = k_keyboard_key_arrow_right;
    break;
  case 116:
    ret = k_keyboard_key_arrow_down;
    break;
  default:
    break;
  }

  return ret;
}

void
os_window_process_events(struct os_window_struct* p_window) {
  Display* d = p_window->d;
  struct keyboard_struct* p_keyboard = p_window->p_keyboard;

  while (XPending(d) > 0) {
    XEvent event;
    int keycode;
    int key;

    int ret = XNextEvent(d, &event);
    if (ret != 0) {
      errx(1, "XNextEvent failed");
    }

    switch (event.type) {
    case KeyPress:
      keycode = event.xkey.keycode;
      key = x_key_to_keyboard_key(keycode);
      if (key != 0) {
        keyboard_system_key_pressed(p_keyboard, key);
      } else {
        printf("warning: unmapped key press %d\n", keycode);
      }
      break;
    case KeyRelease:
      keycode = event.xkey.keycode;
      key = x_key_to_keyboard_key(keycode);
      if (key != 0) {
        keyboard_system_key_released(p_keyboard, key);
      } else {
        printf("warning: unmapped key release %d\n", keycode);
      }
      break;
    default:
      assert(0);
    }
  }
}
