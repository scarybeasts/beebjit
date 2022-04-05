#include "os_sound.h"

#include "log.h"
#include "util.h"

#include <AudioToolbox/AudioQueue.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>

enum {
  k_os_sound_max_period = 16,
};

struct os_sound_struct {
  uint32_t sample_rate;
  uint32_t buffer_size_in_frames;
  uint32_t num_periods;
  uint32_t frames_per_period;

  AudioQueueRef queue;
  AudioQueueBufferRef buffers[k_os_sound_max_period];
  int pipe_read;
  int pipe_write;
  int is_filling;
  uint32_t fill_buffer_index;
  uint32_t fill_frame_pos;
};

static void
audio_queue_buffer_ready(struct os_sound_struct* p_driver, uint32_t index) {
  OSStatus err;
  ssize_t ret;
  AudioQueueBufferRef buffer;
  int8_t val = ('0' + index);

  assert(index < p_driver->num_periods);

  buffer = p_driver->buffers[index];
  assert(buffer->mAudioDataBytesCapacity == (p_driver->frames_per_period * 2));

  (void) memset(buffer->mAudioData, '\0', buffer->mAudioDataBytesCapacity);

  ret = write(p_driver->pipe_write, &val, 1);
  if (ret != 1) {
    util_bail("write audio pipe");
  }

  buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
  err = AudioQueueEnqueueBuffer(p_driver->queue, buffer, 0, NULL);
  if (err != noErr) {
    util_bail("AudioQueueEnqueueBuffer");
  }
}

static void
audio_callback(void* p, AudioQueueRef queue, AudioQueueBufferRef buffer) {
  struct os_sound_struct* p_driver = (struct os_sound_struct*) p;
  uint32_t index = (uint32_t) (intptr_t) buffer->mUserData;

  (void) queue;
  /* To avoid any potential multithreading headaches, we simple re-queue the
   * buffer immediately (without filling it), and signal to the writer that it
   * should fill the buffer. This avoids any potential trouble with a different
   * thread calling into the macOS audio APIs while the callback hasn't fully
   * finished.
   */
  audio_queue_buffer_ready(p_driver, index);
}

uint32_t
os_sound_get_default_buffer_size(void) {
  /* Match the other platforms. */
  return 2048;
}

struct os_sound_struct*
os_sound_create(char* p_device_name,
                uint32_t sample_rate,
                uint32_t buffer_size_in_frames,
                uint32_t num_periods) {
  struct os_sound_struct* p_driver =
      util_mallocz(sizeof(struct os_sound_struct));

  (void) p_device_name;

  if (num_periods > k_os_sound_max_period) {
    util_bail("num_periods too high");
  }

  p_driver->sample_rate = sample_rate;
  p_driver->buffer_size_in_frames = buffer_size_in_frames;
  p_driver->num_periods = num_periods;
  p_driver->frames_per_period = (buffer_size_in_frames / num_periods);

  return p_driver;
}

void
os_sound_destroy(struct os_sound_struct* p_driver) {
  OSStatus status;
  int ret;

  /* The simplest way to shut down cleanly (avoiding problems with the callback
   * firing during shutdown) is to pass "false", meaning the audio system will
   * wait until queued buffers have played.
   * Note that AudioQueueDispose is documented as also disposing of any attached
   * buffers.
   */
  status = AudioQueueDispose(p_driver->queue, false);
  if (status != noErr) {
    util_bail("AudioQueueDispose");
  }

  ret = close(p_driver->pipe_read);
  if (ret != 0) {
    util_bail("close pipe read");
  }
  ret = close(p_driver->pipe_write);
  if (ret != 0) {
    util_bail("close pipe write");
  }

  util_free(p_driver);
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  /* References for macOS audio examples. In the end I used the AudioQueue
   * API. Examples include:
   * https://chromium.googlesource.com/chromium/+/4cecfb2ac11410fc21f86d96802c9da6acd90bcd/media/audio/mac/audio_output_mac.cc
   * https://stackoverflow.com/questions/19624605/coreaudio-audioqueue-stop-issue
   * The wonderful thing about macOS is there are so many audio APIs to choose
   * from. My earlier attempts to a use a lower level CoreAudio API were not
   * very successful. e.g. setting up a 48kHz output was resulting in the
   * system audio output flipping to 96kHz?!
   * Examples:
   * https://github.com/SimonKagstrom/despotify/blob/master/src/clients/despotify/coreaudio.c
   * https://www.marcusficner.de/blog/getting-started-with-core-audio
   */
  uint32_t i;
  OSStatus err;
  int ret;
  int fildes[2];
  AudioQueueRef queue;
  AudioStreamBasicDescription fmt = {};
  uint32_t frames_per_period = p_driver->frames_per_period;

  fmt.mSampleRate = p_driver->sample_rate;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = (kAudioFormatFlagIsSignedInteger |
                      kLinearPCMFormatFlagIsPacked);
  fmt.mBitsPerChannel = 16;
  fmt.mChannelsPerFrame = 1;
  fmt.mBytesPerFrame = 2;
  fmt.mFramesPerPacket = 1;
  fmt.mBytesPerPacket = 2;

  err = AudioQueueNewOutput(&fmt,
                            audio_callback,
                            p_driver,
                            NULL,
                            kCFRunLoopCommonModes,
                            0,
                            &queue);
  if (err != noErr) {
    util_bail("AudioQueueNewOutput");
  }
  p_driver->queue = queue;

  ret = pipe(&fildes[0]);
  if (ret != 0) {
    util_bail("pipe");
  }
  p_driver->pipe_read = fildes[0];
  p_driver->pipe_write = fildes[1];

  for (i = 0; i < p_driver->num_periods; ++i) {
    AudioQueueBufferRef buffer;

    /* This function allocates a count of bytes, not freames. */
    err = AudioQueueAllocateBuffer(queue, (frames_per_period * 2), &buffer);
    if (err != noErr) {
      util_bail("AudioQueueAllocateBuffer");
    }
    buffer->mUserData = (void*) (intptr_t) i;
    p_driver->buffers[i] = buffer;

    audio_queue_buffer_ready(p_driver, i);
  }

  err = AudioQueueStart(queue, NULL);
  if (err != noErr) {
    log_do_log(k_log_audio, k_log_warning, "AudioQueueStart failed: %d", err);
    return -1;
  }

  return 0;
}

uint32_t
os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
  return p_driver->sample_rate;
}

uint32_t
os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
  return p_driver->buffer_size_in_frames;
}

uint32_t
os_sound_get_period_size(struct os_sound_struct* p_driver) {
  return p_driver->frames_per_period;
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  while (num_frames > 0) {
    uint32_t fill_buffer_index = p_driver->fill_buffer_index;
    AudioQueueBufferRef buffer = p_driver->buffers[fill_buffer_index];
    uint16_t* p_buffer = (uint16_t*) buffer->mAudioData;
    uint32_t copy_frames;
    uint32_t remaining_frames;
    uint32_t fill_frame_pos;

    /* Block for the next available sound period buffer, if we're not already
     * filling one.
     */
    if (!p_driver->is_filling) {
      /* Drain the entire pipe, to catch up again if audio got behind (e.g.
       * at the built-in debug prompt.
       * In normal operation, this read will return 1 byte.
       */
      int ret;
      uint8_t buf[65536];
      int8_t val;
      ret = read(p_driver->pipe_read, &buf[0], sizeof(buf));
      if (ret <= 0) {
        util_bail("read from pipe_read");
      }
      /* Look at the most recent write to the pipe and see which buffer index
       * did the write. This is usually just "the next one", but if we're
       * re-syncing after an audio dropout, we'll start again on the most
       * recent buffer in the queue.
       */
      val = buf[ret - 1];
      val -= '0';
      assert((val >= 0) && ((uint32_t) val < p_driver->num_periods));
      fill_buffer_index = val;
      p_driver->fill_buffer_index = fill_buffer_index;
      p_driver->is_filling = 0;
      p_driver->fill_frame_pos = 0;
    }

    copy_frames = num_frames;
    fill_frame_pos = p_driver->fill_frame_pos;
    assert(fill_frame_pos < p_driver->frames_per_period);
    remaining_frames = (p_driver->frames_per_period - fill_frame_pos);
    if (copy_frames > remaining_frames) {
      copy_frames = remaining_frames;
    }
    (void) memcpy(&p_buffer[fill_frame_pos],
                  p_frames,
                  (copy_frames * 2));

    fill_frame_pos += copy_frames;
    p_driver->fill_frame_pos = fill_frame_pos;
    p_frames += copy_frames;
    num_frames -= copy_frames;

    if (fill_frame_pos == p_driver->frames_per_period) {
      p_driver->is_filling = 0;
      fill_buffer_index++;
      if (fill_buffer_index == p_driver->num_periods) {
        fill_buffer_index = 0;
      }
      p_driver->fill_buffer_index = fill_buffer_index;
    }
  }
}
