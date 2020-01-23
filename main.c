#include "bbc.h"
#include "cpu_driver.h"
#include "keyboard.h"
#include "os_poller.h"
#include "os_sound.h"
#include "os_window.h"
#include "render.h"
#include "serial.h"
#include "sound.h"
#include "state.h"
#include "test.h"
#include "util.h"
#include "video.h"

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, const char* argv[]) {
  size_t read_ret;
  uint8_t os_rom[k_bbc_rom_size];
  uint8_t load_rom[k_bbc_rom_size];
  int i;
  struct os_poller_struct* p_poller;
  struct bbc_struct* p_bbc;
  struct keyboard_struct* p_keyboard;
  struct video_struct* p_video;
  struct render_struct* p_render;
  intptr_t bbc_handle;
  uint32_t run_result;
  uint32_t* p_render_buffer;

  const char* rom_names[k_bbc_num_roms] = {};
  int sideways_ram[k_bbc_num_roms] = {};
  const char* disc_names[2] = {};
  struct util_file_map* p_disc_maps[2] = {};
  const char* p_tape_file_name = NULL;

  struct os_window_struct* p_window = NULL;
  struct os_sound_struct* p_sound_driver = NULL;
  intptr_t window_handle = -1;
  const char* os_rom_name = "roms/os12.rom";
  const char* load_name = NULL;
  const char* capture_name = NULL;
  const char* replay_name = NULL;
  const char* opt_flags = "";
  const char* log_flags = "";
  int debug_flag = 0;
  int run_flag = 0;
  int print_flag = 0;
  int fast_flag = 0;
  int test_flag = 0;
  int accurate_flag = 0;
  int test_map_flag = 0;
  int disc_writeable_flag = 0;
  int disc_mutable_flag = 0;
  int terminal_flag = 0;
  int headless_flag = 0;
  int debug_stop_addr = 0;
  int pc = 0;
  int mode = k_cpu_mode_jit;
  uint64_t cycles = 0;
  uint32_t expect = 0;

  rom_names[k_bbc_default_dfs_rom_slot] = "roms/DFS-0.9.rom";
  rom_names[k_bbc_default_basic_rom_slot] = "roms/basic.rom";

  for (i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (i + 2 < argc) {
      const char* val1 = argv[i + 1];
      const char* val2 = argv[i + 2];
      if (!strcmp(arg, "-rom")) {
        int bank;
        (void) sscanf(val1, "%x", &bank);
        if (bank < 0 || bank >= k_bbc_num_roms) {
          errx(1, "ROM bank number out of range");
        }
        rom_names[bank] = val2;
        i += 2;
      }
    }
    if (i + 1 < argc) {
      const char* val = argv[i + 1];
      if (!strcmp(arg, "-os")) {
        os_rom_name = val;
        ++i;
      } else if (!strcmp(arg, "-load")) {
        load_name = val;
        ++i;
      } else if (!strcmp(arg, "-capture")) {
        capture_name = val;
        ++i;
      } else if (!strcmp(arg, "-replay")) {
        replay_name = val;
        ++i;
      } else if (!strcmp(arg, "-disc") ||
                 !strcmp(arg, "-disc0") ||
                 !strcmp(arg, "-0")) {
        disc_names[0] = val;
        ++i;
      } else if (!strcmp(arg, "-disc1") ||
                 !strcmp(arg, "-1")) {
        disc_names[1] = val;
        ++i;
      } else if (!strcmp(arg, "-tape")) {
        p_tape_file_name = val;
        ++i;
      } else if (!strcmp(arg, "-opt")) {
        opt_flags = val;
        ++i;
      } else if (!strcmp(arg, "-log")) {
        log_flags = val;
        ++i;
      } else if (!strcmp(arg, "-stopat")) {
        (void) sscanf(val, "%x", &debug_stop_addr);
        ++i;
      } else if (!strcmp(arg, "-pc")) {
        (void) sscanf(val, "%x", &pc);
        ++i;
      } else if (!strcmp(arg, "-mode")) {
        if (!strcmp(val, "jit")) {
          mode = k_cpu_mode_jit;
        } else if (!strcmp(val, "interp")) {
          mode = k_cpu_mode_interp;
        } else if (!strcmp(val, "inturbo")) {
          mode = k_cpu_mode_inturbo;
        } else {
          errx(1, "unknown mode");
        }
        ++i;
      } else if (!strcmp(arg, "-swram")) {
        int bank;
        (void) sscanf(val, "%x", &bank);
        if (bank < 0 || bank >= k_bbc_num_roms) {
          errx(1, "RAM bank number out of range");
        }
        sideways_ram[bank] = 1;
        ++i;
      } else if (!strcmp(arg, "-cycles")) {
        (void) sscanf(val, "%ld", &cycles);
        ++i;
      } else if (!strcmp(arg, "-expect")) {
        (void) sscanf(val, "%x", &expect);
        ++i;
      }
    }
    if (!strcmp(arg, "-debug")) {
      debug_flag = 1;
    } else if (!strcmp(arg, "-run")) {
      run_flag = 1;
    } else if (!strcmp(arg, "-print")) {
      print_flag = 1;
    } else if (!strcmp(arg, "-fast")) {
      fast_flag = 1;
    } else if (!strcmp(arg, "-test")) {
      test_flag = 1;
    } else if (!strcmp(arg, "-accurate")) {
      accurate_flag = 1;
    } else if (!strcmp(arg, "-writeable")) {
      disc_writeable_flag = 1;
    } else if (!strcmp(arg, "-mutable")) {
      disc_mutable_flag = 1;
    } else if (!strcmp(arg, "-terminal")) {
      terminal_flag = 1;
    } else if (!strcmp(arg, "-headless")) {
      headless_flag = 1;
    } else if (!strcmp(arg, "-test-map")) {
      test_map_flag = 1;
    }
  }

  (void) memset(os_rom, '\0', k_bbc_rom_size);
  (void) memset(load_rom, '\0', k_bbc_rom_size);

  read_ret = util_file_read_fully(os_rom, k_bbc_rom_size, os_rom_name);
  if (read_ret != k_bbc_rom_size) {
    errx(1, "can't load OS rom");
  }

  if (terminal_flag) {
    /* If we're in terminal mode and it appears to be an OS v1.2 MOS ROM,
     * patch some default data values to enable serial I/O from boot.
     */
    if (memcmp(&os_rom[0x2825], "OS 1.2", 6) == 0) {
      /* This is *FX2,1, aka. RS423 for input. */
      os_rom[0xD981 - 0xC000] = 1;
      /* For our *FX2,1 hack to work, we also need to change the default ACIA
       * control register value to enable receive interrupts.
       * Enabling transmit interrupts crashes due to an unexpected early IRQ.
       */
      os_rom[0xD990 - 0xC000] = 0x96;
      /* This is *FX3,5, aka. screen and RS423 for output.
       * This works without needing to hack on the ACIA transmit interrupt. I
       * am unsure why.
       */
      os_rom[0xD9BC - 0xC000] = 5;
    }
  }

  if (test_flag) {
    mode = k_cpu_mode_jit;
  }

  p_bbc = bbc_create(mode,
                     os_rom,
                     debug_flag,
                     run_flag,
                     print_flag,
                     fast_flag,
                     accurate_flag,
                     test_map_flag,
                     opt_flags,
                     log_flags,
                     debug_stop_addr);
  if (p_bbc == NULL) {
    errx(1, "bbc_create failed");
  }

  if (test_flag) {
    test_do_tests(p_bbc);
    return 0;
  }

  if (pc != 0) {
    bbc_set_pc(p_bbc, pc);
  }
  if (cycles != 0) {
    bbc_set_stop_cycles(p_bbc, cycles);
  }

  for (i = 0; i < k_bbc_num_roms; ++i) {
    const char* p_rom_name = rom_names[i];
    if (p_rom_name != NULL) {
      (void) memset(load_rom, '\0', k_bbc_rom_size);
      (void) util_file_read_fully(load_rom, k_bbc_rom_size, p_rom_name);
      bbc_load_rom(p_bbc, i, load_rom);
    }
    if (sideways_ram[i]) {
      bbc_make_sideways_ram(p_bbc, i);
    }
  }

  if (load_name != NULL) {
    state_load(p_bbc, load_name);
  }

  /* Load the discs into the drive! */
  for (i = 0; i <= 1; ++i) {
    const char* p_filename = disc_names[i];

    if (p_filename == NULL) {
      continue;
    }
    bbc_load_disc(p_bbc, p_filename, i, disc_writeable_flag, disc_mutable_flag);
  }

  /* Load the tape! */
  if (p_tape_file_name != NULL) {
    bbc_load_tape(p_bbc, p_tape_file_name);
  }

  /* Set up keyboard capture / replay. */
  p_keyboard = bbc_get_keyboard(p_bbc);
  if (capture_name) {
    keyboard_set_capture_file_name(p_keyboard, capture_name);
  }
  if (replay_name) {
    keyboard_set_replay_file_name(p_keyboard, replay_name);
  }

  p_render = bbc_get_render(p_bbc);

  p_poller = os_poller_create();
  if (p_poller == NULL) {
    errx(1, "os_poller_create failed");
  }

  if (!headless_flag) {
    p_window = os_window_create(render_get_width(p_render),
                                render_get_height(p_render));
    if (p_window == NULL) {
      errx(1, "os_window_create failed");
    }
    os_window_set_name(p_window, "beebjit technology preview");
    os_window_set_keyboard_callback(p_window, p_keyboard);
    p_render_buffer = os_window_get_buffer(p_window);
    render_set_buffer(p_render, p_render_buffer);

    window_handle = os_window_get_handle(p_window);
  }

  if (!headless_flag && !util_has_option(opt_flags, "sound:off")) {
    int ret;
    char* p_device_name = NULL;
    uint32_t sound_sample_rate = 0;
    uint32_t sound_buffer_size = 0;
    (void) util_get_u32_option(&sound_sample_rate, opt_flags, "sound:rate=");
    (void) util_get_u32_option(&sound_buffer_size, opt_flags, "sound:buffer=");
    (void) util_get_str_option(&p_device_name, opt_flags, "sound:dev=");
    p_sound_driver = os_sound_create(p_device_name,
                                     sound_sample_rate,
                                     sound_buffer_size);
    ret = os_sound_init(p_sound_driver);
    if (ret == 0) {
      sound_set_driver(bbc_get_sound(p_bbc), p_sound_driver);
    }
  }

  if (terminal_flag) {
    struct serial_struct* p_serial = bbc_get_serial(p_bbc);
    intptr_t stdin_handle = util_get_stdin_handle();
    intptr_t stdout_handle = util_get_stdout_handle();

    util_make_handle_unbuffered(stdin_handle);

    serial_set_io_handles(p_serial, stdin_handle, stdout_handle);
  }

  bbc_run_async(p_bbc);

  bbc_handle = bbc_get_client_handle(p_bbc);
  os_poller_add_handle(p_poller, bbc_handle);
  if (window_handle != -1) {
    os_poller_add_handle(p_poller, window_handle);
  }

  p_video = bbc_get_video(p_bbc);

  while (1) {
    os_poller_poll(p_poller);

    if (os_poller_handle_triggered(p_poller, 0)) {
      struct bbc_message message;
      bbc_client_receive_message(p_bbc, &message);
      if (message.data[0] == k_message_exited) {
        break;
      } else {
        int do_full_render;
        int framing_changed;
        assert(message.data[0] == k_message_vsync);
        do_full_render = message.data[1];
        framing_changed = message.data[2];
        if (!headless_flag) {
          if (do_full_render) {
            video_render_full_frame(p_video);
          }
          render_double_up_lines(p_render);
          os_window_sync_buffer_to_screen(p_window);
          if (framing_changed) {
            /* NOTE: in accurate mode, it would be more correct to clear the
             * buffer from the framing change to the end of that frame, as well
             * as for the next frame.
             */
            render_clear_buffer(p_render);
          }
        }
        if (bbc_get_vsync_wait_for_render(p_bbc)) {
          message.data[0] = k_message_render_done;
          bbc_client_send_message(p_bbc, &message);
        }
      }
    }
    if ((window_handle != -1) && os_poller_handle_triggered(p_poller, 1)) {
      os_window_process_events(p_window);
    }
  }

  run_result = bbc_get_run_result(p_bbc);
  if (expect) {
    if (run_result != expect) {
      errx(1, "run result %x is not as expected", run_result);
    }
  }

  os_poller_destroy(p_poller);
  if (p_window != NULL) {
    os_window_destroy(p_window);
  }
  bbc_destroy(p_bbc);

  if (p_sound_driver != NULL) {
    os_sound_destroy(p_sound_driver);
  }

  for (i = 0; i <= 1; ++i) {
    struct util_file_map* p_disc_map = p_disc_maps[i];
    if (p_disc_map != NULL) {
      util_file_unmap(p_disc_map);
    }
  }

  return 0;
}
