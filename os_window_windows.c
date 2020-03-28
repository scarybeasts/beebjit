#include "os_window.h"

#include "keyboard.h"
#include "util.h"

#include <windows.h>

static const char* s_p_beejit_class_name = "beebjit Window Class";

static struct os_window_struct* s_p_window;

struct os_window_struct {
  HWND handle;
  HDC handle_draw;
  uint32_t width;
  uint32_t height;
  HBITMAP handle_bitmap;
  HDC handle_draw_bitmap;
  uint32_t* p_buffer;
  int is_destroyed;
  struct keyboard_struct* p_keyboard;
};

static uint8_t
convert_windows_key_code(uint32_t vkey) {
  if ((vkey >= '0') && (vkey <= '9')) {
    return (uint8_t) vkey;
  }
  if ((vkey >= 'A') && (vkey <= 'Z')) {
    return (uint8_t) vkey;
  }
  if ((vkey >= VK_F1) && (vkey <= VK_F9)) {
    return (uint8_t) ((vkey - VK_F1) + k_keyboard_key_f1);
  }
  switch (vkey) {
  case VK_F10:
    return k_keyboard_key_f0;
  case VK_F12:
    return k_keyboard_key_f12;
  case VK_SPACE:
    return ' ';
  case VK_RETURN:
    return k_keyboard_key_enter;
  case VK_BACK:
    return k_keyboard_key_backspace;
  case VK_ESCAPE:
    return k_keyboard_key_escape;
  case VK_TAB:
    return k_keyboard_key_tab;
  case VK_SHIFT:
    /* TODO: distinguish between left and right shift. */
    return k_keyboard_key_shift_left;
  case VK_CAPITAL:
    return k_keyboard_key_caps_lock;
  case VK_CONTROL:
    return k_keyboard_key_ctrl;
  case VK_LEFT:
    return k_keyboard_key_arrow_left;
  case VK_RIGHT:
    return k_keyboard_key_arrow_right;
  case VK_UP:
    return k_keyboard_key_arrow_up;
  case VK_DOWN:
    return k_keyboard_key_arrow_down;
  case VK_END:
    return k_keyboard_key_end;
  case VK_OEM_PLUS:
    return '=';
  case VK_OEM_MINUS:
    return '-';
  case VK_OEM_4:
    return '[';
  case VK_OEM_6:
    return ']';
  case VK_OEM_5:
    return '\\';
  case VK_OEM_1:
    return ';';
  case VK_OEM_7:
    return '\'';
  case VK_OEM_COMMA:
    return ',';
  case VK_OEM_PERIOD:
    return '.';
  case VK_OEM_2:
    return '/';
  case VK_MENU:
    return k_keyboard_key_alt_left;
  default:
    break;
  }

  return 0;
}

LRESULT CALLBACK
WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  struct keyboard_struct* p_keyboard = s_p_window->p_keyboard;
  uint8_t key;

  /* F10 and Alt are special and come in via WM_SYSKEYDOWN. */

  switch (uMsg) {
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT) {
      SetCursor(NULL);
      return TRUE;
    }
    break;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    key = convert_windows_key_code((uint32_t) wParam);
    if (key != 0) {
      keyboard_system_key_pressed(p_keyboard, key);
    }
    break;
  case WM_KEYUP:
  case WM_SYSKEYUP:
    key = convert_windows_key_code((uint32_t) wParam);
    if (key != 0) {
      keyboard_system_key_released(p_keyboard, key);
    }
    break;
  case WM_DESTROY:
    s_p_window->is_destroyed = 1;
    break;
  default:
    break;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

struct os_window_struct*
os_window_create(uint32_t width, uint32_t height) {
  ATOM class_ret;
  HWND handle;
  HDC handle_draw;
  HBITMAP handle_bitmap;
  HDC handle_draw_bitmap;
  HGDIOBJ select_object_ret;

  WNDCLASS wc = {};
  BITMAPINFO bmi = {};
  VOID* pvBits = NULL;

  struct os_window_struct* p_window =
      util_mallocz(sizeof(struct os_window_struct));

  assert(s_p_window == NULL);
  s_p_window = p_window;

  p_window->width = width;
  p_window->height = height;

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

  handle_draw = GetDC(handle);
  if (handle_draw == NULL) {
    util_bail("GetDC failed");
  }
  p_window->handle_draw = handle_draw;

  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  /* NOTE: height has to be negative to get "normal" top-to-bottom pixel
   * ordering.
   */
  bmi.bmiHeader.biHeight = -height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  handle_bitmap = CreateDIBSection(NULL,
                                   &bmi,
                                   DIB_RGB_COLORS,
                                   &pvBits,
                                   NULL,
                                   0);
  if (handle_bitmap == NULL) {
    util_bail("CreateDIBSection failed");
  }
  if (pvBits == NULL) {
    util_bail("CreateDIBSection didn't return a buffer");
  }
  p_window->handle_bitmap = handle_bitmap;
  p_window->p_buffer = pvBits;

  handle_draw_bitmap = CreateCompatibleDC(NULL);
  if (handle_draw_bitmap == NULL) {
    util_bail("CreateCompatibleDC failed");
  }
  p_window->handle_draw_bitmap = handle_draw_bitmap;

  select_object_ret = SelectObject(handle_draw_bitmap, handle_bitmap);
  if (select_object_ret == NULL) {
    util_bail("SelectObject failed");
  }

  (void) ShowWindow(handle, SW_SHOWDEFAULT);

  return p_window;
}

void
os_window_destroy(struct os_window_struct* p_window) {
  (void) p_window;
  BOOL ret;

  ret = DeleteDC(p_window->handle_draw_bitmap);
  if (ret == 0) {
    util_bail("DeleteDC for bitmap failed");
  }

  ret = DeleteObject(p_window->handle_bitmap);
  if (ret == 0) {
    util_bail("DeleteObject for bitmap failed");
  }

  if (!p_window->is_destroyed) {
    ret = DeleteDC(p_window->handle_draw);
    if (ret == 0) {
      util_bail("DeleteDC for window failed");
    }
    ret = DestroyWindow(p_window->handle);
    if (ret == 0) {
      util_bail("DestroyWindow failed");
    }
  }

  ret = UnregisterClass(s_p_beejit_class_name, NULL);
  if (ret == 0) {
    util_bail("UnregisterClass failed");
  }

  util_free(p_window);
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
  p_window->p_keyboard = p_keyboard;
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
  BOOL ret;

  HDC handle_draw = p_window->handle_draw;

  ret = BitBlt(handle_draw,
               0,
               0,
               p_window->width,
               p_window->height,
               p_window->handle_draw_bitmap,
               0,
               0,
               SRCCOPY);
  if (ret == 0) {
    util_bail("BitBlt failed");
  }
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

int
os_window_is_closed(struct os_window_struct* p_window) {
  return p_window->is_destroyed;
}
