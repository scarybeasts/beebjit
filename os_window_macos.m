#include "os_window.h"

#include "keyboard.h"
#include "os_thread.h"
#include "util.h"

#import <Cocoa/Cocoa.h>

#include <unistd.h>

struct os_window_struct {
  uint32_t width;
  uint32_t height;
  struct keyboard_struct* p_keyboard;
  uint32_t* p_buffer;
  NSWindow* p_nswindow;
  NSView* p_nsview;
  CGContextRef context;
  int pipe_read;
  int pipe_write;
};

static uint8_t s_cocoa_keycode_lookup[256];

static void
beebjit_main_thread_start(void* p) {
  void (*p_beebjit_main)(void) = p;
  p_beebjit_main();
}

static void
cocoa_check_is_main_thread(void) {
  if (![NSThread isMainThread]) {
    util_bail("not Cocoa main thread!");
  }
}

static void
cocoa_check_is_not_main_thread(void) {
  if ([NSThread isMainThread]) {
    util_bail("unexpected Cocoa main thread!");
  }
}

static void
cocoa_build_keycode_lookup(void) {
  /* https://stackoverflow.com/questions/36900825/where-are-all-the-cocoa-keycodes */
  s_cocoa_keycode_lookup[0] = 'A';
  s_cocoa_keycode_lookup[11] = 'B';
  s_cocoa_keycode_lookup[8] = 'C';
  s_cocoa_keycode_lookup[2] = 'D';
  s_cocoa_keycode_lookup[14] = 'E';
  s_cocoa_keycode_lookup[3] = 'F';
  s_cocoa_keycode_lookup[5] = 'G';
  s_cocoa_keycode_lookup[4] = 'H';
  s_cocoa_keycode_lookup[34] = 'I';
  s_cocoa_keycode_lookup[38] = 'J';
  s_cocoa_keycode_lookup[40] = 'K';
  s_cocoa_keycode_lookup[37] = 'L';
  s_cocoa_keycode_lookup[46] = 'M';
  s_cocoa_keycode_lookup[45] = 'N';
  s_cocoa_keycode_lookup[31] = 'O';
  s_cocoa_keycode_lookup[35] = 'P';
  s_cocoa_keycode_lookup[12] = 'Q';
  s_cocoa_keycode_lookup[15] = 'R';
  s_cocoa_keycode_lookup[1] = 'S';
  s_cocoa_keycode_lookup[17] = 'T';
  s_cocoa_keycode_lookup[32] = 'U';
  s_cocoa_keycode_lookup[9] = 'V';
  s_cocoa_keycode_lookup[13] = 'W';
  s_cocoa_keycode_lookup[7] = 'X';
  s_cocoa_keycode_lookup[16] = 'Y';
  s_cocoa_keycode_lookup[6] = 'Z';
  s_cocoa_keycode_lookup[29] = '0';
  s_cocoa_keycode_lookup[18] = '1';
  s_cocoa_keycode_lookup[19] = '2';
  s_cocoa_keycode_lookup[20] = '3';
  s_cocoa_keycode_lookup[21] = '4';
  s_cocoa_keycode_lookup[23] = '5';
  s_cocoa_keycode_lookup[22] = '6';
  s_cocoa_keycode_lookup[26] = '7';
  s_cocoa_keycode_lookup[28] = '8';
  s_cocoa_keycode_lookup[25] = '9';
  s_cocoa_keycode_lookup[122] = k_keyboard_key_f1;
  s_cocoa_keycode_lookup[120] = k_keyboard_key_f2;
  s_cocoa_keycode_lookup[99] = k_keyboard_key_f3;
  s_cocoa_keycode_lookup[118] = k_keyboard_key_f4;
  s_cocoa_keycode_lookup[96] = k_keyboard_key_f5;
  s_cocoa_keycode_lookup[97] = k_keyboard_key_f6;
  s_cocoa_keycode_lookup[98] = k_keyboard_key_f7;
  s_cocoa_keycode_lookup[100] = k_keyboard_key_f8;
  s_cocoa_keycode_lookup[101] = k_keyboard_key_f9;
  /* F10 */
  s_cocoa_keycode_lookup[109] = k_keyboard_key_f0;
  s_cocoa_keycode_lookup[111] = k_keyboard_key_f12;
  s_cocoa_keycode_lookup[49] = ' ';
  s_cocoa_keycode_lookup[36] = k_keyboard_key_enter;
  s_cocoa_keycode_lookup[51] = k_keyboard_key_backspace;
  s_cocoa_keycode_lookup[53] = k_keyboard_key_escape;
  s_cocoa_keycode_lookup[48] = k_keyboard_key_tab;
  s_cocoa_keycode_lookup[123] = k_keyboard_key_arrow_left;
  s_cocoa_keycode_lookup[124] = k_keyboard_key_arrow_right;
  s_cocoa_keycode_lookup[126] = k_keyboard_key_arrow_up;
  s_cocoa_keycode_lookup[125] = k_keyboard_key_arrow_down;
  s_cocoa_keycode_lookup[24] = '=';
  s_cocoa_keycode_lookup[27] = '-';
  s_cocoa_keycode_lookup[41] = ';';
  s_cocoa_keycode_lookup[44] = '/';
  s_cocoa_keycode_lookup[50] = '`';
  s_cocoa_keycode_lookup[33] = '[';
  s_cocoa_keycode_lookup[30] = ']';
  s_cocoa_keycode_lookup[42] = '\\';
  s_cocoa_keycode_lookup[39] = '\'';
  s_cocoa_keycode_lookup[43] = ',';
  s_cocoa_keycode_lookup[47] = '.';
  /* These don't directly exist on a MacBook Air keyboard; they're fn plus
   * up / down / right.
   */
  s_cocoa_keycode_lookup[116] = k_keyboard_key_page_up;
  s_cocoa_keycode_lookup[119] = k_keyboard_key_end;
}

void
os_window_main_thread_start(void (*p_beebjit_main)(void)) {
  cocoa_check_is_main_thread();

  cocoa_build_keycode_lookup();

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSApp = [NSApplication sharedApplication];
  /* Application cannot gain focus (e.g. to get key presses) without this. */
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

  (void) os_thread_create(beebjit_main_thread_start, p_beebjit_main);

  [NSApp run];

  [NSApp release];
  [pool release];
}

@interface BeebjitView : NSView

@property struct os_window_struct* osWindow;

@end

@implementation BeebjitView
/* Key presses are rejected with the system "bip!" sound without this. */
- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)keyDown:(NSEvent*)event {
  uint32_t keyCode = event.keyCode;
  uint8_t beebjitKey = 0;
  struct keyboard_struct* p_keyboard = _osWindow->p_keyboard;
  if (p_keyboard == NULL) {
    return;
  }
  if (keyCode < 256) {
    beebjitKey = s_cocoa_keycode_lookup[(uint8_t) keyCode];
  }
  if (beebjitKey != 0) {
    keyboard_system_key_pressed(p_keyboard, beebjitKey);
  }
}

- (void)keyUp:(NSEvent*)event {
  uint32_t keyCode = event.keyCode;
  uint8_t beebjitKey = 0;
  struct keyboard_struct* p_keyboard = _osWindow->p_keyboard;
  if (p_keyboard == NULL) {
    return;
  }
  if (keyCode < 256) {
    beebjitKey = s_cocoa_keycode_lookup[(uint8_t) keyCode];
  }
  if (beebjitKey != 0) {
    keyboard_system_key_released(p_keyboard, beebjitKey);
  }
}

- (void)flagsChanged:(NSEvent*)event {
  uint32_t modifierFlags = event.modifierFlags;
  struct keyboard_struct* p_keyboard = _osWindow->p_keyboard;
  if (p_keyboard == NULL) {
    return;
  }
  /* Undocumented in the main constants, but 2 and 4 appear to denote left
   * shift and right shift.
   * We use left shift in place of caps lock, because caps lock doesn't offer
   * us the key up events that we need.
   */
  if (modifierFlags & 2) {
    keyboard_system_key_pressed(p_keyboard, k_keyboard_key_caps_lock);
  } else {
    keyboard_system_key_released(p_keyboard, k_keyboard_key_caps_lock);
  }
  if (modifierFlags & 4) {
    keyboard_system_key_pressed(p_keyboard, k_keyboard_key_shift_left);
  } else {
    keyboard_system_key_released(p_keyboard, k_keyboard_key_shift_left);
  }
  if (modifierFlags & NSEventModifierFlagControl) {
    keyboard_system_key_pressed(p_keyboard, k_keyboard_key_ctrl);
  } else {
    keyboard_system_key_released(p_keyboard, k_keyboard_key_ctrl);
  }
  if (modifierFlags & NSEventModifierFlagOption) {
    keyboard_system_key_pressed(p_keyboard, k_keyboard_key_alt_left);
  } else {
    keyboard_system_key_released(p_keyboard, k_keyboard_key_alt_left);
  }
}

@end

@interface BeebjitWindowDelegate : NSObject<NSWindowDelegate>

@property struct os_window_struct* osWindow;

@end

@implementation BeebjitWindowDelegate

- (void)windowWillClose:(NSNotification*)notification
{
  ssize_t ret;
  uint8_t val = 'C';
  /* Signal the main beebjit event loop. */
  ret = write(_osWindow->pipe_write, &val, 1);
  if (ret != 1) {
    util_bail("write");
  }
}

@end

struct os_window_struct*
os_window_create(uint32_t width, uint32_t height) {
  struct os_window_struct* p_window;
  int ret;
  int filedes[2];

  cocoa_check_is_not_main_thread();

  p_window = util_mallocz(sizeof(struct os_window_struct));
  p_window->width = width;
  p_window->height = height;

  ret = pipe(&filedes[0]);
  if (ret != 0) {
    util_bail("pipe");
  }
  p_window->pipe_read = filedes[0];
  p_window->pipe_write = filedes[1];

  dispatch_sync(dispatch_get_main_queue(), ^{
    cocoa_check_is_main_thread();
    NSRect rect = NSMakeRect(0, 0, width, height);
    NSWindow* window = [[NSWindow alloc]
                        initWithContentRect:rect
                        styleMask:NSWindowStyleMaskTitled
                                 |NSWindowStyleMaskClosable
                                 |NSWindowStyleMaskMiniaturizable
                        backing:NSBackingStoreBuffered
                        defer:NO
                       ];
    [window setReleasedWhenClosed:NO];
    p_window->p_nswindow = window;

    BeebjitView* view = [[BeebjitView alloc] initWithFrame:rect];
    p_window->p_nsview = view;
    view.osWindow = p_window;
    [view setWantsLayer:YES];

    [window setContentView:view];

    /* Set a delegate to listen for the window events, such as pending close. */
    BeebjitWindowDelegate* delegate = [BeebjitWindowDelegate alloc];
    delegate.osWindow = p_window;

    [window setDelegate:delegate];

    [window makeKeyAndOrderFront:nil];

    /* We use CG bitmaps and not NSBitmapImageRep, because the latter seems to
     * cache a point-in-time view of the pixel buffer, leading to the screen not
     * updating.
     */
    CGColorSpaceRef colorSpace =
        CGColorSpaceCreateWithName(kCGColorSpaceLinearSRGB);
    uint32_t bitmapInfo = (kCGImageByteOrder32Little |
                           kCGImageAlphaPremultipliedFirst);
    CGContextRef context = CGBitmapContextCreate(NULL,
                                                 width,
                                                 height,
                                                 8,
                                                 0,
                                                 colorSpace,
                                                 bitmapInfo);
    p_window->context = context;
    CGColorSpaceRelease(colorSpace);
    assert(CGBitmapContextGetBytesPerRow(context) == (width * 4));

    uint8_t* p_buffer = CGBitmapContextGetData(context);
    p_window->p_buffer = (uint32_t*) p_buffer;
  });

  return p_window;
}

void
os_window_destroy(struct os_window_struct* p_window) {
  int ret;

  NSWindow* window = p_window->p_nswindow;
  [window release];

  /* TODO: also release the other NS objects we created? */

  CGContextRef context = p_window->context;
  CGContextRelease(context);

  ret = close(p_window->pipe_read);
  if (ret != 0) {
    util_bail("close pipe_read");
  }

  ret = close(p_window->pipe_write);
  if (ret != 0) {
    util_bail("close pipe_write");
  }

  util_free(p_window);
}

void
os_window_set_name(struct os_window_struct* p_window, const char* p_name) {
  cocoa_check_is_not_main_thread();

  dispatch_sync(dispatch_get_main_queue(), ^{
    cocoa_check_is_main_thread();

    NSString* string = [[NSString alloc] initWithUTF8String:p_name];
    NSWindow* window = p_window->p_nswindow;
    [window setTitle:string];
    [string release];
  });
}

void
os_window_set_keyboard_callback(struct os_window_struct* p_window,
                                struct keyboard_struct* p_keyboard) {
  cocoa_check_is_not_main_thread();

  dispatch_sync(dispatch_get_main_queue(), ^{
    cocoa_check_is_main_thread();

    p_window->p_keyboard = p_keyboard;
  });
}

void
os_window_set_focus_lost_callback(struct os_window_struct* p_window,
                                  void (*p_focus_lost_callback)(void* p),
                                  void* p_focus_lost_callback_object) {
  (void) p_window;
  (void) p_focus_lost_callback;
  (void) p_focus_lost_callback_object;
}

uint32_t*
os_window_get_buffer(struct os_window_struct* p_window) {
  return p_window->p_buffer;
}

intptr_t
os_window_get_handle(struct os_window_struct* p_window) {
  return p_window->pipe_read;
}

void
os_window_sync_buffer_to_screen(struct os_window_struct* p_window) {
  cocoa_check_is_not_main_thread();

  dispatch_sync(dispatch_get_main_queue(), ^{
    cocoa_check_is_main_thread();

    NSWindow* window = p_window->p_nswindow;
    NSView* view = [window contentView];
    CALayer* layer = [view layer];

    /* Based on:
     * https://stackoverflow.com/questions/48293376/render-pixel-buffer-with-cocoa
     */
    CGImageRef image = CGBitmapContextCreateImage(p_window->context);
    [layer setContents:(__bridge id) image];
    CGImageRelease(image);

    /* Just updating the layer's contents above seems to be enough to cause a
     * repaint. However, we ideally want to block until the repaint is done, so
     * that beebjit doesn't start drawing in the paint buffer until it has been
     * committed to screen. This is an attempt to paint right away.
     */
    [view setNeedsLayout:YES];
    [view displayIfNeeded];
  });
}

void
os_window_process_events(struct os_window_struct* p_window) {
  /* Deliberately empty.
   * We only get here if the window is closing so there's nothing to do.
   */
  (void) p_window;
}

int
os_window_is_closed(struct os_window_struct* p_window) {
  /* This is only called after we signal the close event, which is the only
   * event we send to the beebjit main loop.
   */
  (void) p_window;
  return 1;
}
