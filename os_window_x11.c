#include "os_window.h"

#include "keyboard.h"
#include "os_x11_keys.h"
#include "log.h"
#include "util.h"
#include "video.h"

#include <X11/Xlib.h>

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <X11/extensions/XShm.h>

#include <assert.h>
#include <err.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>

struct os_window_struct {
  uint32_t width;
  uint32_t height;
  struct keyboard_struct* p_keyboard;
  Display* d;
  Window w;
  GC gc;
  int shmid;
  void* p_shm_map_start;
  void* p_shm_map_data_start;
  XImage* p_image;
  XShmSegmentInfo shm_info;
  uint8_t* p_key_map;
};

/* For use by rm_window_shmid_error_handler, due to inconvenient X11 API. */
struct rm_window_shmid_error_handler_context_struct {
  struct os_window_struct* p_window;
  XErrorHandler old_error_handler;
};

static struct rm_window_shmid_error_handler_context_struct*
    s_p_rm_window_shmid_error_handler_context;

static void
rm_shmid(struct os_window_struct *p_window) {
  int ret = shmctl(p_window->shmid, IPC_RMID, NULL);
  if (ret != 0) {
    errx(1, "shmctl failed");
  }

  p_window->shmid = -1;
}

static int
rm_window_shmid_error_handler(Display* display, XErrorEvent* event) {
  int ret;

  (void) display;
  (void) event;

  rm_shmid(s_p_rm_window_shmid_error_handler_context->p_window);

  ret = (s_p_rm_window_shmid_error_handler_context->old_error_handler)(display,
                                                                       event);
  return ret;
}

struct os_window_struct*
os_window_create(uint32_t width, uint32_t height) {
  struct os_window_struct* p_window;
  int s;
  Window root_window;
  unsigned long black_pixel;
  unsigned long white_pixel;
  Bool bool_ret;
  int ret;
  Visual* p_visual;
  int depth;
  size_t map_size;
  struct rm_window_shmid_error_handler_context_struct error_handler_context;

  p_window = malloc(sizeof(struct os_window_struct));
  if (p_window == NULL) {
    errx(1, "couldn't allocate os_window_struct");
  }
  (void) memset(p_window, '\0', sizeof(struct os_window_struct));

  p_window->p_keyboard = NULL;
  p_window->width = width;
  p_window->height = height;

  if ((width > 1024) || (height > 1024)) {
    errx(1, "excessive dimension");
  }

  p_window->d = XOpenDisplay(NULL);
  if (p_window->d == NULL) {
    errx(1, "cannot open X display");
  }

  (void) XkbSetDetectableAutoRepeat(p_window->d, True, &bool_ret);
  if (bool_ret != True) {
    errx(1, "can't set detect auto repeat");
  }

  s = DefaultScreen(p_window->d);
  root_window = RootWindow(p_window->d, s);
  p_visual = DefaultVisual(p_window->d, s);
  black_pixel = BlackPixel(p_window->d, s);
  white_pixel = WhitePixel(p_window->d, s);
  depth = DefaultDepth(p_window->d, s);

  if (depth != 24) {
    errx(1, "default depth not 24");
  }

  map_size = (width * height * 4);
  /* Add in suitable extra size for guard pages, because I'm sure I'm going to
   * write off the beginning or end one day.
   */
  if ((map_size % 4096) != 0) {
    map_size += (4096 - (map_size % 4096));
  }
  map_size += (4096 * 2);

  p_window->shmid = shmget(IPC_PRIVATE, map_size, (IPC_CREAT | 0600));
  if (p_window->shmid < 0) {
    errx(1, "shmget failed");
  }
  p_window->p_shm_map_start = shmat(p_window->shmid, NULL, 0);
  if (p_window->p_shm_map_start == NULL) {
    rm_shmid(p_window);
    errx(1, "shmat failed");
  }

  util_make_mapping_none(p_window->p_shm_map_start, 4096);
  util_make_mapping_none(p_window->p_shm_map_start + map_size - 4096, 4096);
  p_window->p_shm_map_data_start = (p_window->p_shm_map_start + 4096);

  p_window->p_image = XShmCreateImage(p_window->d,
                                      p_visual,
                                      24,
                                      ZPixmap,
                                      NULL,
                                      &p_window->shm_info,
                                      width,
                                      height);
  if (p_window->p_image == NULL) {
    rm_shmid(p_window);
    errx(1, "XShmCreateImage failed");
  }

  p_window->shm_info.shmid = p_window->shmid;
  p_window->shm_info.shmaddr = p_window->p_shm_map_start;
  p_window->shm_info.readOnly = False;

  p_window->p_image->data = p_window->p_shm_map_data_start;

  bool_ret = XShmAttach(p_window->d, &p_window->shm_info);
  if (bool_ret != True) {
    rm_shmid(p_window);
    errx(1, "XShmAttach failed");
  }

  /* Don't let the shared memory leak if there's an error during XSync. */
  error_handler_context.p_window = p_window;
  error_handler_context.old_error_handler =
      XSetErrorHandler(rm_window_shmid_error_handler);
  s_p_rm_window_shmid_error_handler_context = &error_handler_context;
  XSync(p_window->d, False);
  (void) XSetErrorHandler(error_handler_context.old_error_handler);
  s_p_rm_window_shmid_error_handler_context = NULL;

  rm_shmid(p_window);

  p_window->w = XCreateSimpleWindow(p_window->d,
                                    root_window,
                                    10,
                                    10,
                                    width,
                                    height,
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

  ret = XFlush(p_window->d);
  if (ret != 1) {
    errx(1, "XFlush failed");
  }

  p_window->p_key_map = os_x11_keys_get_mapping();

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
  ret = shmdt(p_window->p_shm_map_start);
  if (ret != 0) {
    errx(1, "shmdt failed");
  }

  ret = XCloseDisplay(p_window->d);
  if (ret != 0) {
    errx(1, "XCloseDisplay failed");
  }

  free(p_window);
}

void
os_window_set_name(struct os_window_struct* p_window, const char* p_name) {
  int ret = XStoreName(p_window->d, p_window->w, p_name);
  if (ret != 1) {
    errx(1, "XStoreName failed");
  }
}

void
os_window_set_keyboard_callback(struct os_window_struct* p_window,
                                struct keyboard_struct* p_keyboard) {
  p_window->p_keyboard = p_keyboard;
}

uint32_t*
os_window_get_buffer(struct os_window_struct* p_window) {
  return (uint32_t*) p_window->p_shm_map_data_start;
}

uintptr_t
os_window_get_handle(struct os_window_struct* p_window) {
  Display* d = p_window->d;
  int fd = ConnectionNumber(d);

  return fd;
}

void
os_window_sync_buffer_to_screen(struct os_window_struct* p_window) {
  int ret;

  Bool bool_ret = XShmPutImage(p_window->d,
                               p_window->w,
                               p_window->gc,
                               p_window->p_image,
                               0,
                               0,
                               0,
                               0,
                               p_window->width,
                               p_window->height,
                               False);
  if (bool_ret != True) {
    errx(1, "XShmPutImage failed");
  }

  /* We need to sync here so that the server ack's it has finished the
   * XShmPutImage.
   * Clients of this function expect to be able to start writing to the buffer
   * again immediately without affecting display.
   */
  ret = XSync(p_window->d, False);
  if (ret != 1) {
    errx(1, "XSync failed");
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
os_window_convert_key_code(struct os_window_struct* p_window,
                           uint32_t keycode) {
  uint8_t* p_key_map = p_window->p_key_map;
  uint8_t key = 0;

  if (keycode < 256) {
    key = p_key_map[keycode];
  }

  return key;
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
      key = os_window_convert_key_code(p_window, keycode);
      if (key != 0) {
        keyboard_system_key_pressed(p_keyboard, key);
      } else {
        log_do_log(k_log_keyboard,
                   k_log_unimplemented,
                   "unmapped key press %d",
                   keycode);
      }
      break;
    case KeyRelease:
      keycode = event.xkey.keycode;
      key = os_window_convert_key_code(p_window, keycode);
      if (key != 0) {
        keyboard_system_key_released(p_keyboard, key);
      } else {
        log_do_log(k_log_keyboard,
                   k_log_unimplemented,
                   "unmapped key release %d",
                   keycode);
      }
      break;
    default:
      /* This fired once or twice on me, so try and log what it is in case it
       * happens again.
       */
      log_do_log(k_log_misc, k_log_error, "unexpected XEvent %d", event.type);
      assert(0);
    }
  }
}
