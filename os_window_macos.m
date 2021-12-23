#include "os_window.h"

#include "os_channel.h"
#include "os_thread.h"
#include "util.h"

#import <Cocoa/Cocoa.h>

struct os_window_struct {
  uint32_t width;
  uint32_t height;
  uint32_t* p_buffer;
  NSWindow* p_nswindow;
  CGContextRef context;
  intptr_t read1_fd;
  intptr_t write1_fd;
  intptr_t read2_fd;
  intptr_t write2_fd;
};

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

void
os_window_main_thread_start(void (*p_beebjit_main)(void)) {
  cocoa_check_is_main_thread();

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSApp = [NSApplication sharedApplication];

  (void) os_thread_create(beebjit_main_thread_start, p_beebjit_main);

  [NSApp run];

  [NSApp release];
  [pool release];
}

struct os_window_struct*
os_window_create(uint32_t width, uint32_t height) {
  struct os_window_struct* p_window;

  cocoa_check_is_not_main_thread();

  p_window = util_mallocz(sizeof(struct os_window_struct));
  p_window->width = width;
  p_window->height = height;

  os_channel_get_handles(&p_window->read1_fd,
                         &p_window->write1_fd,
                         &p_window->read2_fd,
                         &p_window->write2_fd);

  dispatch_sync(dispatch_get_main_queue(), ^{
    cocoa_check_is_main_thread();
    NSRect rect = NSMakeRect(100, 100, width, height);
    NSWindow* window = [[NSWindow alloc]
                        initWithContentRect: rect
                        styleMask:NSWindowStyleMaskTitled
                                 |NSWindowStyleMaskClosable
                                 |NSWindowStyleMaskMiniaturizable
                        backing:NSBackingStoreBuffered
                        defer:NO
                       ];
    p_window->p_nswindow = window;

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

    NSView* view = [window contentView];
    [view setWantsLayer:YES];

    [window makeKeyAndOrderFront: nil];
  });

  return p_window;
}

void
os_window_destroy(struct os_window_struct* p_window) {
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
  (void) p_window;
  (void) p_keyboard;
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
  return p_window->read2_fd;
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
  (void) p_window;
  util_bail("shouldn't hit here!");
}

int
os_window_is_closed(struct os_window_struct* p_window) {
  (void) p_window;
  return 0;
}
