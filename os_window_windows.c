#include "os_window.h"

#include "util.h"

#include <windows.h>

static const char* s_p_beejit_class_name = "beebjit Window Class";

struct os_window_struct {
  HWND handle;
  HBITMAP bitmap;
  uint32_t* p_buffer;
};

LRESULT CALLBACK
WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

struct os_window_struct*
os_window_create(uint32_t width, uint32_t height) {
  ATOM class_ret;
  HWND handle;
  HBITMAP bitmap;

  WNDCLASS wc = {};
  BITMAPINFO bmi = {};
  VOID* pvBits = NULL;

  struct os_window_struct* p_window =
      util_mallocz(sizeof(struct os_window_struct));

  wc.lpfnWndProc = WindowProc;
  wc.hInstance = NULL;
  wc.lpszClassName = s_p_beejit_class_name;

  class_ret = RegisterClass(&wc);
  if (class_ret == 0) {
    util_bail("RegisterClass failed");
  }

  handle = CreateWindowEx(0,
                          s_p_beejit_class_name,
                          "",
                          WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          width,
                          height,
                          NULL,
                          NULL,
                          NULL,
                          NULL);
  if (handle == NULL) {
    util_bail("CreateWindowEx failed");
  }
  p_window->handle = handle;

  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
  if (bitmap == NULL) {
    util_bail("CreateDIBSection failed");
  }
  if (pvBits == NULL) {
    util_bail("CreateDIBSection didn't return a buffer");
  }
  p_window->bitmap = bitmap;
  p_window->p_buffer = pvBits;

  (void) ShowWindow(handle, SW_SHOWDEFAULT);

  return p_window;
}

void
os_window_destroy(struct os_window_struct* p_window) {
  (void) p_window;
  BOOL ret = UnregisterClass(s_p_beejit_class_name, NULL);
  if (ret == 0) {
    util_bail("UnregisterClass failed");
  }
  util_bail("blah");
}

void
os_window_set_name(struct os_window_struct* p_window, const char* p_name) {
  BOOL ret = SetWindowText(p_window->handle, p_name);
  if (ret == 0) {
    util_bail("SetWindowText failed");
  }
}

void
os_window_set_keyboard_callback(struct os_window_struct* p_window,
                                struct keyboard_struct* p_keyboard) {
  (void) p_window;
  (void) p_keyboard;
}

uint32_t*
os_window_get_buffer(struct os_window_struct* p_window) {
  return p_window->p_buffer;
}

intptr_t
os_window_get_handle(struct os_window_struct* p_window) {
  /* Return a NULL handle. The Windows poller doesn't need a handle for the
   * window because MsgWaitForMultipleObjects includes the current thread's
   * message queue.
   */
  (void) p_window;
  return (intptr_t) NULL;
}

void
os_window_sync_buffer_to_screen(struct os_window_struct* p_window) {
  (void) p_window;
}

void
os_window_process_events(struct os_window_struct* p_window) {
  MSG msg;

  (void) p_window;

  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}
