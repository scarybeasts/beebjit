#ifndef BEEBJIT_VIDEO_H
#define BEEBJIT_VIDEO_H

#include <stddef.h>
#include <stdint.h>

struct video_struct;

struct bbc_options;
struct render_struct;
struct teletext_struct;
struct timing_struct;
struct via_struct;

struct video_struct* video_create(uint8_t* p_mem,
                                  uint8_t* p_shadow_mem,
                                  int externally_clocked,
                                  struct timing_struct* p_timing,
                                  struct render_struct* p_render,
                                  struct teletext_struct* p_teletext,
                                  struct via_struct* p_system_via,
                                  void (*p_framebuffer_ready_callback)
                                      (void* p,
                                       int do_full_paint,
                                       int framing_changed),
                                  void* p_framebuffer_ready_object,
                                  int* p_fast_flag,
                                  struct bbc_options* p_options);
void video_destroy(struct video_struct* p_video);
void video_advance_for_memory_sync(void* p);
void video_advance_crtc_timing(struct video_struct* p_video);

void video_IC32_updated(struct video_struct* p_video, uint8_t IC32);
void video_shadow_mode_updated(struct video_struct* p_video,
                               int is_shadow_displayed);

void video_power_on_reset(struct video_struct* p_video);

uint64_t video_get_num_vsyncs(struct video_struct* p_video);
uint64_t video_get_num_crtc_advances(struct video_struct* p_video);
struct render_struct* video_get_render(struct video_struct* p_video);

void video_apply_wall_time_delta(struct video_struct* p_video, uint64_t delta);

void video_ula_write(struct video_struct* p_video, uint8_t addr, uint8_t val);
uint8_t video_crtc_read(struct video_struct* p_video, uint8_t addr);
void video_crtc_write(struct video_struct* p_video, uint8_t addr, uint8_t val);

void video_render_full_frame(struct video_struct* p_video);
void video_serial_ula_written_hack(struct video_struct* p_video, uint8_t val);

uint8_t video_get_ula_control(struct video_struct* p_video);
void video_set_ula_control(struct video_struct* p_video, uint8_t val);
void video_get_ula_full_palette(struct video_struct* p_video,
                                uint8_t* p_values);
void video_set_ula_palette(struct video_struct* p_video, uint8_t val);
void video_set_ula_full_palette(struct video_struct* p_video,
                                const uint8_t* p_values);

enum {
  k_video_crtc_num_registers = 18,
};
void video_get_crtc_registers(struct video_struct* p_video,
                              uint8_t* p_values);
void video_set_crtc_registers(struct video_struct* p_video,
                              const uint8_t* p_values);

void video_get_crtc_state(struct video_struct* p_video,
                          uint8_t* p_horiz_counter,
                          uint8_t* p_scanline_counter,
                          uint8_t* p_vert_counter,
                          uint16_t* p_address_counter);

#endif /* BEEBJIT_VIDEO_H */
