#include "os_sound.h"

#include "log.h"
#include "util.h"

#include <assert.h>
#include <windows.h>

/* NOTE: we're using a simple but ancient Windows API for sound, which doesn't
 * have the best latency. On a Win10 laptop, a 2048 sized buffer was needed to
 * play sound without stuttering.
 */
enum {
  k_os_sound_max_period = 16,
  k_os_sound_default_buffer_size = 2048,
};

uint32_t
os_sound_get_default_buffer_size(void) {
  return k_os_sound_default_buffer_size;
}

struct os_sound_struct {
  uint32_t sample_rate;
  uint32_t buffer_size;
  uint32_t num_periods;
  uint32_t frames_per_period;

  HWAVEOUT handle_wav;
  WAVEHDR wav_headers[k_os_sound_max_period];
  HANDLE wait_objects[k_os_sound_max_period];
  uint16_t* buffers[k_os_sound_max_period];
  uint32_t wav_fill_index;
  int is_filling;
  uint32_t fill_frames_pos;
  long num_buffers_free;
};

struct os_sound_struct*
os_sound_create(char* p_device_name,
                uint32_t sample_rate,
                uint32_t buffer_size,
                uint32_t num_periods) {
  struct os_sound_struct* p_driver =
      util_mallocz(sizeof(struct os_sound_struct));

  (void) p_device_name;

  if (num_periods > k_os_sound_max_period) {
    util_bail("num_periods too high");
  }

  p_driver->sample_rate = sample_rate;
  p_driver->buffer_size = buffer_size;
  p_driver->num_periods = num_periods;
  p_driver->frames_per_period = (buffer_size / num_periods);

  return p_driver;
}

void
os_sound_destroy(struct os_sound_struct* p_driver) {
  MMRESULT ret;
  HWAVEOUT handle_wav = p_driver->handle_wav;
  uint32_t i;

  ret = waveOutPause(handle_wav);
  if (ret != MMSYSERR_NOERROR) {
    util_bail("waveOutPause failed");
  }

  for (i = 0; i < p_driver->num_periods; ++i) {
    WAVEHDR* p_wav_header = &p_driver->wav_headers[i];
    ret = waveOutUnprepareHeader(handle_wav, p_wav_header, sizeof(WAVEHDR));
    if (ret != MMSYSERR_NOERROR) {
      util_bail("waveOutPrepareHeader failed");
    }
    util_free(p_driver->buffers[i]);

    ret = CloseHandle(p_driver->wait_objects[i]);
    if (ret == 0) {
      util_bail("CloseHandle failed");
    }
  }

  ret = waveOutClose(handle_wav);
  if (ret != MMSYSERR_NOERROR) {
    util_bail("waveOutClose failed");
  }

  util_free(p_driver);
}

static void
waveOutProc(HWAVEOUT handle_wav,
            UINT uMsg,
            DWORD_PTR dwInstance,
            DWORD_PTR dwParam1,
            DWORD_PTR dwParam2) {
  /* CAUTION: different thread to the thread calling os_sound_write. */
  struct os_sound_struct* p_driver;
  WAVEHDR* p_wav_header;
  BOOL ret;
  uint32_t index;

  (void) handle_wav;
  (void) dwParam2;

  if (uMsg != WOM_DONE) {
    return;
  }

  p_driver = (struct os_sound_struct*) dwInstance;
  p_wav_header = (WAVEHDR*) dwParam1;

  assert(p_wav_header->dwFlags & WHDR_PREPARED);
  assert(p_wav_header->dwFlags & WHDR_DONE);
  assert(!(p_wav_header->dwFlags & WHDR_INQUEUE));

  index = (uint32_t) (uintptr_t) p_wav_header->dwUser;
  ret = SetEvent(p_driver->wait_objects[index]);
  if (ret == 0) {
    util_bail("SetEvent failed");
  }

  InterlockedIncrement(&p_driver->num_buffers_free);
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  HWAVEOUT handle_wav;
  WAVEFORMATEX wav_format;
  MMRESULT ret;
  uint32_t i;

  uint32_t num_periods = p_driver->num_periods;
  uint32_t bytes_per_period = (p_driver->frames_per_period * 2);

  (void) memset(&wav_format, '\0', sizeof(wav_format));
  wav_format.wFormatTag = WAVE_FORMAT_PCM;
  wav_format.nChannels = 1;
  wav_format.nSamplesPerSec = p_driver->sample_rate;
  wav_format.wBitsPerSample = 16;
  wav_format.nBlockAlign = 2;
  wav_format.nAvgBytesPerSec = (wav_format.nSamplesPerSec *
                                wav_format.nBlockAlign);

  ret = waveOutOpen(&handle_wav,
                    WAVE_MAPPER,
                    &wav_format,
                    (DWORD_PTR) waveOutProc,
                    (DWORD_PTR) p_driver,
                    CALLBACK_FUNCTION);
  if (ret != MMSYSERR_NOERROR) {
    log_do_log(k_log_audio,
               k_log_info,
               "failed to open wave output device: %d",
               (int) ret);
    return -1;
  }
  p_driver->handle_wav = handle_wav;

  for (i = 0; i < num_periods; ++i) {
    WAVEHDR* p_wav_header = &p_driver->wav_headers[i];
    uint16_t* p_buffer = util_mallocz(bytes_per_period);
    p_driver->buffers[i] = p_buffer;
    p_wav_header->lpData = (LPSTR) p_buffer;
    p_wav_header->dwBufferLength = bytes_per_period;
    p_wav_header->dwUser = (DWORD_PTR) i;

    ret = waveOutPrepareHeader(handle_wav, p_wav_header, sizeof(WAVEHDR));
    if (ret != MMSYSERR_NOERROR) {
      util_bail("waveOutPrepareHeader failed");
    }

    p_driver->wait_objects[i] = CreateEvent(NULL, FALSE, TRUE, NULL);
    if (p_driver->wait_objects[i] == NULL) {
      util_bail("CreateEvent failed");
    }
  }

  p_driver->is_filling = 0;
  p_driver->wav_fill_index = 0;
  p_driver->fill_frames_pos = 0;

  p_driver->num_buffers_free = num_periods;

  return 0;
}

uint32_t
os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
  return p_driver->sample_rate;
}

uint32_t
os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
  return p_driver->buffer_size;
}

uint32_t
os_sound_get_period_size(struct os_sound_struct* p_driver) {
  return p_driver->frames_per_period;
}

uint32_t
os_sound_get_frame_space(struct os_sound_struct* p_driver) {
  /* NOTE: for threading, I think this relies on os_sound_get_frame_space being
   * used on the same thread as os_sound_write.
   */
  return (p_driver->num_buffers_free * p_driver->frames_per_period);
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  uint32_t frames_per_period = p_driver->frames_per_period;

  while (num_frames > 0) {
    DWORD ret;
    WAVEHDR* p_wav_header;
    uint16_t* p_buffer;
    uint32_t fill_frames_left;

    uint32_t wav_fill_index = p_driver->wav_fill_index;
    uint32_t copy_frames = num_frames;

    /* Block if we need to. */
    if (!p_driver->is_filling) {
      HANDLE wait_object = p_driver->wait_objects[wav_fill_index];
      ret = WaitForSingleObject(wait_object, INFINITE);
      if (ret != WAIT_OBJECT_0) {
        util_bail("WaitForSingleObject failed");
      }
      p_driver->is_filling = 1;
      p_driver->fill_frames_pos = 0;

      InterlockedDecrement(&p_driver->num_buffers_free);
    }

    p_wav_header = &p_driver->wav_headers[wav_fill_index];
    assert(p_wav_header->dwFlags & WHDR_PREPARED);
    assert(!(p_wav_header->dwFlags & WHDR_INQUEUE));

    p_buffer = p_driver->buffers[wav_fill_index];
    fill_frames_left = (frames_per_period - p_driver->fill_frames_pos);
    assert(fill_frames_left > 0);

    if (copy_frames > fill_frames_left) {
      copy_frames = fill_frames_left;
    }
    (void) memcpy((p_buffer + p_driver->fill_frames_pos),
                  p_frames,
                  (copy_frames * 2));
    p_driver->fill_frames_pos += copy_frames;
    p_frames += copy_frames;
    num_frames -= copy_frames;

    /* Post buffer and move on if it is filled. */
    assert(p_driver->fill_frames_pos <= frames_per_period);
    if (p_driver->fill_frames_pos == frames_per_period) {
      ret = waveOutWrite(p_driver->handle_wav, p_wav_header, sizeof(WAVEHDR));
      if (ret != MMSYSERR_NOERROR) {
        util_bail("waveOutWrite failed");
      }
      p_driver->is_filling = 0;
      p_driver->wav_fill_index++;
      if (p_driver->wav_fill_index == p_driver->num_periods) {
        p_driver->wav_fill_index = 0;
      }
    }
  }
}
