#include "bbc.h"
#include "config.h"
#include "cpu_driver.h"
#include "keyboard.h"
#include "log.h"
#include "os_channel.h"
#include "os_poller.h"
#include "os_sound.h"
#include "os_terminal.h"
#include "os_window.h"
#include "render.h"
#include "serial.h"
#include "sound.h"
#include "state.h"
#include "test.h"
#include "util.h"
#include "version.h"
#include "video.h"
#include "wd_fdc.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t k_sound_default_rate = 48000;
static const uint32_t k_sound_default_num_periods = 4;
enum {
  k_max_discs_per_drive = 4,
  k_max_tapes = 4,
};

static void
main_save_frame(const char* p_frames_dir,
                uint32_t save_frame_count,
                struct render_struct* p_render) {
  char file_name[256];
  struct util_file* p_file;
  uint32_t* p_buffer = render_get_buffer(p_render);
  uint32_t size = render_get_buffer_size(p_render);

  (void) snprintf(file_name,
                  sizeof(file_name),
                  "%s/beebjit_frame_%d.bgra",
                  p_frames_dir,
                  save_frame_count);
  p_file = util_file_open(&file_name[0], 1, 1);
  if (p_file == NULL) {
    util_bail("util_file_open failed");
  }

  util_file_write(p_file, p_buffer, size);

  util_file_close(p_file);
}

int
main(int argc, const char* argv[]) {
  int i_args;
  size_t read_ret;
  uint8_t os_rom[k_bbc_rom_size];
  uint8_t load_rom[k_bbc_rom_size];
  uint32_t i;
  uint32_t j;
  struct os_poller_struct* p_poller;
  struct bbc_struct* p_bbc;
  struct keyboard_struct* p_keyboard;
  struct video_struct* p_video;
  struct render_struct* p_render;
  uint32_t run_result;
  uint32_t* p_render_buffer;
  intptr_t handle_channel_read_ui;
  intptr_t handle_channel_write_bbc;
  intptr_t handle_channel_read_bbc;
  intptr_t handle_channel_write_ui;
  char* p_opt_flags;
  char* p_log_flags;

  const char* rom_names[k_bbc_num_roms] = {};
  int sideways_ram[k_bbc_num_roms] = {};
  const char* disc_names[2][k_max_discs_per_drive] = {};
  const char* p_tape_file_names[k_max_tapes] = {};

  struct os_window_struct* p_window = NULL;
  struct os_sound_struct* p_sound_driver = NULL;
  intptr_t window_handle = -1;
  const char* os_rom_name = "roms/os12.rom";
  const char* load_name = NULL;
  const char* capture_name = NULL;
  const char* replay_name = NULL;
  const char* p_create_hfe_file = NULL;
  const char* p_create_hfe_spec = NULL;
  const char* p_frames_dir = ".";
  const char* p_commands = NULL;
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
  int fasttape_flag = 0;
  int convert_hfe_flag = 0;
  int convert_ssd_flag = 0;
  int convert_adl_flag = 0;
  int no_dfs_flag = 0;
  int wd_1770_type = 0;
  int watford_flag = 0;
  int opus_flag = 0;
  int is_master_flag = 0;
  int autoboot_flag = 0;
  int extended_roms_flag = 0;
  int32_t debug_stop_addr = -1;
  int32_t pc = -1;
  int mode = k_cpu_mode_jit;
  uint64_t cycles = 0;
  uint32_t expect = 0;
  int window_open = 0;
  uint32_t num_discs_0 = 0;
  uint32_t num_discs_1 = 0;
  uint32_t num_tapes = 0;
  int keyboard_links = -1;
  uint32_t save_frame_count = 0;
  uint64_t frame_cycles = 0;
  uint32_t max_frames = 1;
  int is_exit_on_max_frames_flag = 0;

  p_opt_flags = util_mallocz(1);
  p_log_flags = util_mallocz(1);

  for (i_args = 1; i_args < argc; ++i_args) {
    const char* arg = argv[i_args];
    int has_1 = 0;
    int has_2 = 0;
    const char* val1 = NULL;
    const char* val2 = NULL;

    if ((i_args + 1) < argc) {
      has_1 = 1;
      val1 = argv[i_args + 1];
    }
    if ((i_args + 2) < argc) {
      has_2 = 1;
      val2 = argv[i_args + 2];
    }

    if (has_2 && !strcmp(arg, "-rom")) {
      int32_t bank = -1;
      (void) sscanf(val1, "%"PRIx32, &bank);
      if (bank < 0 || bank >= k_bbc_num_roms) {
        util_bail("ROM bank number out of range");
      }
      rom_names[bank] = val2;
      i_args += 2;
    } else if (has_2 && (!strcmp(arg, "-create-hfe"))) {
      p_create_hfe_file = val1;
      p_create_hfe_spec = val2;
      i_args += 2;
    } else if (has_1 && !strcmp(arg, "-os")) {
      os_rom_name = val1;
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-load")) {
      load_name = val1;
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-capture")) {
      capture_name = val1;
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-replay")) {
      replay_name = val1;
      ++i_args;
    } else if (has_1 && (!strcmp(arg, "-disc") ||
                         !strcmp(arg, "-disc0") ||
                         !strcmp(arg, "-0"))) {
      if (num_discs_0 == k_max_discs_per_drive) {
        util_bail("too many discs for drive 0");
      }
      disc_names[0][num_discs_0] = val1;
      ++num_discs_0;
      ++i_args;
    } else if (has_1 && (!strcmp(arg, "-disc1") ||
                         !strcmp(arg, "-1"))) {
      if (num_discs_1 == k_max_discs_per_drive) {
        util_bail("too many discs for drive 1");
      }
      disc_names[1][num_discs_1] = val1;
      ++num_discs_1;
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-tape")) {
      if (num_tapes == k_max_tapes) {
        util_bail("too many tapes");
      }
      p_tape_file_names[num_tapes] = val1;
      ++num_tapes;
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-opt")) {
      char* p_old_opt_flags = p_opt_flags;
      p_opt_flags = util_strdup2(p_opt_flags, ",");
      util_free(p_old_opt_flags);
      p_old_opt_flags = p_opt_flags;
      p_opt_flags = util_strdup2(p_opt_flags, val1);
      util_free(p_old_opt_flags);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-log")) {
      char* p_old_log_flags = p_log_flags;
      p_log_flags = util_strdup2(p_log_flags, ",");
      util_free(p_old_log_flags);
      p_old_log_flags = p_log_flags;
      p_log_flags = util_strdup2(p_log_flags, val1);
      util_free(p_old_log_flags);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-stopat")) {
      (void) sscanf(val1, "%"PRIx32, &debug_stop_addr);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-pc")) {
      (void) sscanf(val1, "%"PRIx32, &pc);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-mode")) {
      if (!strcmp(val1, "jit")) {
        mode = k_cpu_mode_jit;
      } else if (!strcmp(val1, "interp")) {
        mode = k_cpu_mode_interp;
      } else if (!strcmp(val1, "inturbo")) {
        mode = k_cpu_mode_inturbo;
      } else {
        util_bail("unknown mode");
      }
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-swram")) {
      int32_t bank = -1;
      (void) sscanf(val1, "%"PRIx32, &bank);
      if ((bank < 0) || (bank >= k_bbc_num_roms)) {
        util_bail("RAM bank number out of range");
      }
      sideways_ram[bank] = 1;
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-cycles")) {
      (void) sscanf(val1, "%"PRIu64, &cycles);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-frame-cycles")) {
      (void) sscanf(val1, "%"PRIu64, &frame_cycles);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-max-frames")) {
      (void) sscanf(val1, "%"PRIu32, &max_frames);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-frames-dir")) {
      p_frames_dir = val1;
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-expect")) {
      (void) sscanf(val1, "%"PRIx32, &expect);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-log-file")) {
      log_set_log_filename(val1);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-keyboard-links")) {
      (void) sscanf(val1, "%"PRIx32, &keyboard_links);
      ++i_args;
    } else if (has_1 && !strcmp(arg, "-commands")) {
      p_commands = val1;
      ++i_args;
    } else if (!strcmp(arg, "-debug")) {
      debug_flag = 1;
    } else if (!strcmp(arg, "-run")) {
      run_flag = 1;
    } else if (!strcmp(arg, "-print")) {
      print_flag = 1;
    } else if (!strcmp(arg, "-fast")) {
      fast_flag = 1;
    } else if (!strcmp(arg, "-test")) {
      test_flag = 1;
      test_map_flag = 1;
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
    } else if (!strcmp(arg, "-fasttape")) {
      fasttape_flag = 1;
    } else if (!strcmp(arg, "-convert-hfe")) {
      convert_hfe_flag = 1;
    } else if (!strcmp(arg, "-convert-ssd")) {
      convert_ssd_flag = 1;
    } else if (!strcmp(arg, "-convert-adl")) {
      convert_adl_flag = 1;
    } else if (!strcmp(arg, "-no-dfs")) {
      no_dfs_flag = 1;
    } else if (!strcmp(arg, "-1770")) {
      wd_1770_type = 1;
    } else if (!strcmp(arg, "-watford")) {
      watford_flag = 1;
    } else if (!strcmp(arg, "-opus")) {
      opus_flag = 1;
    } else if (!strcmp(arg, "-autoboot")) {
      autoboot_flag = 1;
    } else if (!strcmp(arg, "-extended-roms")) {
      extended_roms_flag = 1;
    } else if (!strcmp(arg, "-exit-on-max-frames")) {
      is_exit_on_max_frames_flag = 1;
    } else if (!strcmp(arg, "-master")) {
      is_master_flag = 1;
      config_apply_master_128_mos320(&os_rom_name,
                                     &rom_names[0],
                                     &sideways_ram[0],
                                     &wd_1770_type);
    } else if (!strcmp(arg, "-compact")) {
      is_master_flag = 1;
      config_apply_master_compact(&os_rom_name,
                                  &rom_names[0],
                                  &sideways_ram[0],
                                  &wd_1770_type);
    } else if (!strcmp(arg, "-test-map")) {
      test_map_flag = 1;
    } else if (!strcmp(arg, "-no-log-stdout")) {
      log_set_do_log_to_stdout(0);
    } else if (!strcmp(arg, "-version") ||
               !strcmp(arg, "-v")) {
      (void) printf("beebjit "BEEBJIT_VERSION"\n");
      exit(0);
    } else if (!strcmp(arg, "-help") ||
               !strcmp(arg, "--help") ||
               !strcmp(arg, "-h")) {
      (void) printf(
"The most common command line flags follow. See EXAMPLES or -more for more.\n"
"-0 -disc -disc0 <f>: load disc image file <f> into drive 0/2.\n"
"-1 -disc1       <f>: load disc image file <f> into drive 1/3.\n"
"-autoboot          : do a shift+break boot at startup.\n"
"-writeable         : discs are not write protected (by default they are).\n"
"-mutable           : disc image changes are written back to host image file.\n"
"-tape           <f>: load tape image file <f>.\n"
"-fasttape          : emulate fast when the tape motor is on.\n"
"-swram        <hex>: specified ROM bank is sideways RAM.\n"
"-rom      <hex> <f>: load ROM file <f> into specified ROM bank.\n"
"-debug             : enable 6502 debugger and start in debugger.\n"
"-run               : if -debug, run instead of starting in debugger.\n"
"-print             : if -debug, print every instruction run.\n"
"-mode              : CPU emulation driver: jit,interp,inturbo (default jit).\n"
"-fast              : run CPU as fast as host can; lowers accuracy.\n"
"-log-file       <f>: log to file <f> as well as stdout.\n"
"-1770              : emulate a 1770 instead of an 8271 floppy controller.\n"
"-master            : set up a Master 128 with MOS 3.20.\n"
"-compact           : set up a BBC Master Compact.\n"
"-help              : show this help text.\n"
"-more              : show more, less common options.\n"
"");
      exit(0);
    } else if (!strcmp(arg, "-more")) {
      (void) printf(
"-frame-cycles   <c>: start saving frame images after <c> cycles.\n"
"-max-frames     <m>: max frame images to save, default 1.\n"
"-exit-on-max-frames: exit the process once max-frames is hit.\n"
"-frames-dir     <d>: directory for frame files, default '.'.\n"
"-watford           : for a model B with a 1770, load Watford DDFS ROM.\n"
"-opus              : for a model B with a 1770, load Opus DDOS ROM.\n"
"-extended-roms     : disable ROM slot aliasing.\n"
"");
      exit(0);
    } else {
      log_do_log(k_log_misc,
                 k_log_warning,
                 "unknown command line option or missing argument: %s",
                 arg);
    }
  }

  (void) memset(os_rom, '\0', k_bbc_rom_size);
  (void) memset(load_rom, '\0', k_bbc_rom_size);

  read_ret = util_file_read_fully(os_rom_name, os_rom, k_bbc_rom_size);
  if (read_ret != k_bbc_rom_size) {
    util_bail("can't load OS rom");
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
                     is_master_flag,
                     os_rom,
                     wd_1770_type,
                     debug_flag,
                     run_flag,
                     print_flag,
                     fast_flag,
                     accurate_flag,
                     fasttape_flag,
                     test_map_flag,
                     p_opt_flags,
                     p_log_flags,
                     debug_stop_addr);
  if (p_bbc == NULL) {
    util_bail("bbc_create failed");
  }

  if (test_flag) {
    test_do_tests(p_bbc);
    return 0;
  }

  if (cycles != 0) {
    bbc_set_stop_cycles(p_bbc, cycles);
  }
  if (p_commands != NULL) {
    bbc_set_commands(p_bbc, p_commands);
  }

  if (rom_names[k_bbc_default_basic_rom_slot] == NULL) {
    rom_names[k_bbc_default_basic_rom_slot] = "roms/basic.rom";
  }
  if ((wd_1770_type > 0) && opus_flag) {
    struct wd_fdc_struct* p_wd_fdc = bbc_get_wd_fdc(p_bbc);
    wd_fdc_set_is_opus(p_wd_fdc, 1);
  }
  if (!no_dfs_flag && (rom_names[k_bbc_default_dfs_rom_slot] == NULL)) {
    const char* p_dfs_rom_name;
    if (wd_1770_type > 0) {
      if (watford_flag) {
        p_dfs_rom_name = "roms/WDDFS154T";
      } else if (opus_flag) {
        p_dfs_rom_name = "roms/ODDOS346";
      } else {
        p_dfs_rom_name = "roms/DFS226";
      }
    } else {
      if (watford_flag) {
        p_dfs_rom_name = "roms/WDFS144";
      } else {
        p_dfs_rom_name = "roms/DFS-0.9.rom";
      }
    }
    rom_names[k_bbc_default_dfs_rom_slot] = p_dfs_rom_name;
  }

  if (extended_roms_flag) {
    bbc_enable_extended_rom_addressing(p_bbc);
  }
  for (i = 0; i < k_bbc_num_roms; ++i) {
    const char* p_rom_name = rom_names[i];
    if (p_rom_name != NULL) {
      (void) memset(load_rom, '\0', k_bbc_rom_size);
      (void) util_file_read_fully(p_rom_name, load_rom, k_bbc_rom_size);
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
    for (j = 0; j < k_max_discs_per_drive; ++j) {
      const char* p_filename = disc_names[i][j];

      if (p_filename == NULL) {
        continue;
      }
      bbc_add_disc(p_bbc,
                   p_filename,
                   i,
                   disc_writeable_flag,
                   disc_mutable_flag,
                   convert_hfe_flag,
                   convert_ssd_flag,
                   convert_adl_flag);
    }
  }
  if (p_create_hfe_file) {
    if (num_discs_0 == k_max_discs_per_drive) {
      util_bail("can't create hfe, too many discs");
    }
    bbc_add_raw_disc(p_bbc, p_create_hfe_file, p_create_hfe_spec);
  }

  if (convert_hfe_flag || convert_ssd_flag || convert_adl_flag) {
    exit(0);
  }

  /* Load the tapes! */
  for (i = 0; i < k_max_tapes; ++i) {
    const char* p_file_name = p_tape_file_names[i];
    if (p_file_name != NULL) {
      bbc_add_tape(p_bbc, p_file_name);
    }
  }

  /* Set up keyboard capture / replay / links. */
  p_keyboard = bbc_get_keyboard(p_bbc);
  if (capture_name) {
    keyboard_set_capture_file_name(p_keyboard, capture_name);
  }
  if (replay_name) {
    keyboard_set_replay_file_name(p_keyboard, replay_name);
  }
  if (keyboard_links != -1) {
    keyboard_set_links(p_keyboard, keyboard_links);
  }
  if (autoboot_flag) {
    bbc_set_autoboot(p_bbc, 1);
  }

  p_render = bbc_get_render(p_bbc);

  p_poller = os_poller_create();
  if (p_poller == NULL) {
    util_bail("os_poller_create failed");
  }

  if (!headless_flag) {
    p_window = os_window_create(render_get_width(p_render),
                                render_get_height(p_render));
    if (p_window == NULL) {
      util_bail("os_window_create failed");
    }
    window_open = 1;
    os_window_set_name(p_window, "beebjit technology preview");
    os_window_set_keyboard_callback(p_window, p_keyboard);
    os_window_set_focus_lost_callback(p_window, bbc_focus_lost_callback, p_bbc);
    p_render_buffer = os_window_get_buffer(p_window);
    render_set_buffer(p_render, p_render_buffer);

    window_handle = os_window_get_handle(p_window);
  } else if (frame_cycles > 0) {
    render_create_internal_buffer(p_render);
  }

  if (!headless_flag && !util_has_option(p_opt_flags, "sound:off")) {
    int ret;
    char* p_device_name = NULL;
    uint32_t sound_sample_rate = k_sound_default_rate;
    uint32_t sound_buffer_size = os_sound_get_default_buffer_size();
    uint32_t num_periods = k_sound_default_num_periods;
    (void) util_get_u32_option(&sound_sample_rate, p_opt_flags, "sound:rate=");
    (void) util_get_u32_option(&sound_buffer_size,
                               p_opt_flags,
                               "sound:buffer=");
    (void) util_get_u32_option(&num_periods, p_opt_flags, "sound:periods=");
    (void) util_get_str_option(&p_device_name, p_opt_flags, "sound:dev=");

    p_sound_driver = os_sound_create(p_device_name,
                                     sound_sample_rate,
                                     sound_buffer_size,
                                     num_periods);
    util_free(p_device_name);
    ret = os_sound_init(p_sound_driver);
    if (ret == 0) {
      sound_set_driver(bbc_get_sound(p_bbc), p_sound_driver);
    }
  }

  if (terminal_flag) {
    struct serial_struct* p_serial = bbc_get_serial(p_bbc);
    intptr_t stdin_handle = os_terminal_get_stdin_handle();
    intptr_t stdout_handle = os_terminal_get_stdout_handle();

    os_terminal_setup(stdin_handle);

    serial_set_io_handles(p_serial, stdin_handle, stdout_handle);
  }

  os_channel_get_handles(&handle_channel_read_ui,
                         &handle_channel_write_bbc,
                         &handle_channel_read_bbc,
                         &handle_channel_write_ui);
  bbc_set_channel_handles(p_bbc,
                          handle_channel_read_bbc,
                          handle_channel_write_bbc,
                          handle_channel_read_ui,
                          handle_channel_write_ui);

  bbc_power_on_reset(p_bbc);

  /* Can only set the PC after the bbc_power_on_reset reset call, otherwise the
   * 6502 reset will clobber it.
   */
  if (pc >= 0) {
    bbc_set_pc(p_bbc, pc);
  }

  bbc_run_async(p_bbc);

  os_poller_add_handle(p_poller, handle_channel_read_ui);
  if (window_handle != -1) {
    os_poller_add_handle(p_poller, window_handle);
  }

  p_video = bbc_get_video(p_bbc);

  while (1) {
    os_poller_poll(p_poller);

    if (os_poller_handle_triggered(p_poller, 0)) {
      struct bbc_message message;
      int do_full_render;
      int framing_changed;
      int save_frame;
      uint64_t cycles;

      bbc_client_receive_message(p_bbc, &message);
      if (message.data[0] == k_message_exited) {
        break;
      }

      assert(message.data[0] == k_message_vsync);
      do_full_render = message.data[1];
      framing_changed = message.data[2];
      cycles = message.data[3];
      save_frame = 0;
      if ((frame_cycles > 0) &&
          (cycles >= frame_cycles) &&
          (save_frame_count < max_frames)) {
        save_frame = 1;
      }
      if (window_open || save_frame) {
        if (do_full_render) {
          video_render_full_frame(p_video);
        }
        render_process_full_buffer(p_render);
        if (window_open) {
          os_window_sync_buffer_to_screen(p_window);
        }
        if (save_frame) {
          main_save_frame(p_frames_dir, save_frame_count, p_render);
          save_frame_count++;
          if (is_exit_on_max_frames_flag && (save_frame_count == max_frames)) {
            exit(0);
          }
        }
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

    if (window_open && os_poller_handle_triggered(p_poller, 1)) {
      os_window_process_events(p_window);
      if (os_window_is_closed(p_window)) {
        struct cpu_driver* p_cpu_driver = bbc_get_cpu_driver(p_bbc);
        window_open = 0;
        if (!(p_cpu_driver->p_funcs->get_flags(p_cpu_driver) &
              k_cpu_flag_exited)) {
          p_cpu_driver->p_funcs->apply_flags(p_cpu_driver,
                                             k_cpu_flag_exited,
                                             0);
          p_cpu_driver->p_funcs->set_exit_value(p_cpu_driver, 0xFFFFFFFF);
        }
      }
    }
  }

  run_result = bbc_get_run_result(p_bbc);
  if (expect) {
    if (run_result != expect) {
      util_bail("run result %X is not as expected (%X)", run_result, expect);
    }
  }

  os_poller_destroy(p_poller);
  if (p_window != NULL) {
    os_window_destroy(p_window);
  }
  bbc_destroy(p_bbc);

  os_channel_free_handles(handle_channel_read_ui,
                          handle_channel_write_bbc,
                          handle_channel_read_bbc,
                          handle_channel_write_ui);

  if (p_sound_driver != NULL) {
    os_sound_destroy(p_sound_driver);
  }

  util_free(p_opt_flags);
  util_free(p_log_flags);

  return 0;
}
