#include "rocket.h"

#include "device.h"
#include "sync.h"
#include "../bbc.h"
#include "../bbc_options.h"
#include "../util.h"

#include <assert.h>
#include <string.h>

enum {
  k_rocket_default_vpr = 4,
  k_rocket_default_addr_audio_flag = 0x9D,
  k_rocket_default_addr_vsync_count = 0x9E,
  k_rocket_default_addr_track_values = 0x70
};

struct rocket_struct {
  /* Needed to poke memory into the BBC. */
  struct bbc_struct* p_bbc;

  /* BBC addresses for transfering Rocket data. */
  uint16_t addr_audio_flag;
  uint16_t addr_vsync_count;
  uint16_t addr_track_values;

  /* Communicate with Rocket sync library. */
  struct sync_device *device;
  struct sync_cb cb;

  /* Internals */
  int audio_is_playing;
  uint32_t vsyncs;
  uint32_t vpr;        /* vsyncs per row */

  int num_tracks;
  const struct sync_track* s_tracks[];
};

static int8_t val_f_as_bbc(float val_f)
{
    /* Archie returns fixed point s15:16 format.
    int val_sign = val_f < 0.0f ? -1 : 1;
    int val_fp = fabs(val_f) * (1 << 16);
    return val_fp * val_sign; */
    /* BBC returns int8. */
    int v = (int)val_f;
    return v % 256;
}

static void rocket_sync_pause(void* data, int flag)
{
  struct rocket_struct* p_rocket = (struct rocket_struct*)data;
  p_rocket->audio_is_playing = !flag;
  /* Mirror the audio play/pause flag in BBC RAM. */
  bbc_memory_write(p_rocket->p_bbc, p_rocket->addr_audio_flag, p_rocket->audio_is_playing);
}

static void rocket_sync_set_row(void* data, int row)
{
  struct rocket_struct* p_rocket = (struct rocket_struct*)data;
  p_rocket->vsyncs = row * p_rocket->vpr;
}

static int rocket_sync_is_playing(void* data)
{
  struct rocket_struct* p_rocket = (struct rocket_struct*)data;
  return p_rocket->audio_is_playing;
}

static void rocket_sync_write_key(void *data, FILE *fp, char type, int row, float value)
{
  struct rocket_struct* p_rocket = (struct rocket_struct*)data;
  /* TODO: Write binary data in BBC format. */
  uint16_t time = (row * p_rocket->vpr) & 0xffff;
  int8_t val_bbc = val_f_as_bbc(value);
  fwrite(&type, sizeof(char), 1, fp);
  fwrite(&time, sizeof(uint16_t), 1, fp);
  fwrite(&val_bbc, sizeof(int8_t), 1, fp);
}

static float row_from_vsyncs(int vsyncs, int vpr)
{
  return vsyncs / (float)vpr;
}

struct rocket_struct*
rocket_create(struct bbc_struct* p_bbc,
              const char* p_track_list_file_name,
              const char* p_prefix,
              const char* p_opt_flags) {
  /* Determine number of tracks from the track list.*/
  struct util_file* p_track_list = util_file_open(p_track_list_file_name, 0, 0);
  int num_tracks = 0;
  char track_name[256];
  /* Security much? scarybeasts will kill me :) */
  while(fscanf((FILE*)p_track_list, "%s", track_name) == 1) {
    num_tracks++;
  }
  if (num_tracks == 0) {
    util_bail("Rocket track list is empty.");
  }

  /* Allocate an array of sync_track pointers at the end of our rocket_struct. */
  struct rocket_struct* p_rocket = util_mallocz(sizeof(struct rocket_struct) + num_tracks * sizeof(struct sync_track *));

  p_rocket->p_bbc = p_bbc;

  /* Set all the Rocket options. */
  p_rocket->vpr = k_rocket_default_vpr;
  (void) util_get_u32_option(&p_rocket->vpr, p_opt_flags, "rocket:vpr=");

  p_rocket->addr_audio_flag = k_rocket_default_addr_audio_flag;
  (void) util_get_x16_option(&p_rocket->addr_audio_flag, p_opt_flags, "rocket:addr_audio_flag=");

  p_rocket->addr_vsync_count = k_rocket_default_addr_vsync_count;
  (void) util_get_x16_option(&p_rocket->addr_vsync_count, p_opt_flags, "rocket:addr_vsync_count=");

  p_rocket->addr_track_values = k_rocket_default_addr_track_values;
  (void) util_get_x16_option(&p_rocket->addr_track_values, p_opt_flags, "rocket:addr_track_values=");

  /* Create Rocket device. */
  p_rocket->device = sync_create_device(p_prefix);
  if (!p_rocket->device) {
    util_bail("Failed to create Rocket device.");
  }

  /* Set Rocket callbacks. */
  p_rocket->cb.is_playing = rocket_sync_is_playing;
  p_rocket->cb.pause = rocket_sync_pause;
  p_rocket->cb.set_row = rocket_sync_set_row;
  p_rocket->cb.write_key = rocket_sync_write_key;

  /* Connect to Rocket. */
  if (sync_tcp_connect(p_rocket->device, "localhost", SYNC_DEFAULT_PORT)) {
    util_bail("Rocket failed to connect.");
  }

  /* Register Rocket sync tracks. */
  util_file_seek(p_track_list, 0);
  int i = 0;
  while(fscanf((FILE*)p_track_list, "%s", track_name) == 1) {
    p_rocket->s_tracks[i++] = sync_get_track(p_rocket->device, track_name);
  }
  util_file_close(p_track_list);
  assert(i == num_tracks);
  p_rocket->num_tracks = num_tracks;

  return p_rocket;
}

void
rocket_destroy(struct rocket_struct* p_rocket) {
  sync_destroy_device(p_rocket->device);
  util_free(p_rocket);
}

void
rocket_run(struct rocket_struct* p_rocket) {
  float row = row_from_vsyncs(p_rocket->vsyncs, p_rocket->vpr);

  if (sync_update(p_rocket->device, row, &p_rocket->cb, p_rocket)) {
    sync_tcp_connect(p_rocket->device, "localhost", SYNC_DEFAULT_PORT);
  }

  uint8_t* p_mem_read = bbc_get_mem_read(p_rocket->p_bbc);

  if (p_rocket->audio_is_playing) {
    /* Vsync counter driven by BBC. */
    p_rocket->vsyncs = p_mem_read[p_rocket->addr_vsync_count+0] + (p_mem_read[p_rocket->addr_vsync_count+1]<<8);
  } else {
    /* Set vsync counter from sync row. */
    bbc_memory_write(p_rocket->p_bbc, p_rocket->addr_vsync_count+0, p_rocket->vsyncs % 256);
    bbc_memory_write(p_rocket->p_bbc, p_rocket->addr_vsync_count+1, p_rocket->vsyncs / 256);
  }

  /* Populate BBC RAM from track data. */
  for(int i = 0; i < p_rocket->num_tracks; ++i) {
    float row = row_from_vsyncs(p_rocket->vsyncs, p_rocket->vpr);
    uint8_t val_bbc = val_f_as_bbc(sync_get_val(p_rocket->s_tracks[i], row));
    bbc_memory_write(p_rocket->p_bbc, p_rocket->addr_track_values+i, val_bbc);
  }
}
