#include "config.h"

void
config_apply_master_128_mos320(const char** p_os_rom_name,
                               const char** p_rom_names,
                               int* p_sideways_ram,
                               int* p_wd_1770_type) {
  *p_os_rom_name = "roms/mos3.20/mos.rom";

  p_rom_names[0x9] = "roms/mos3.20/dfs.rom";
  p_rom_names[0xA] = "roms/mos3.20/viewsht.rom";
  p_rom_names[0xB] = "roms/mos3.20/edit.rom";
  p_rom_names[0xC] = "roms/mos3.20/basic4.rom";
  p_rom_names[0xD] = "roms/mos3.20/adfs.rom";
  p_rom_names[0xE] = "roms/mos3.20/view.rom";
  p_rom_names[0xF] = "roms/mos3.20/terminal.rom";

  p_sideways_ram[0x4] = 1;
  p_sideways_ram[0x5] = 1;
  p_sideways_ram[0x6] = 1;
  p_sideways_ram[0x7] = 1;

  /* Plain 1770. */
  *p_wd_1770_type = 1;
}

void
config_apply_master_128_mos350(const char** p_os_rom_name,
                               const char** p_rom_names,
                               int* p_sideways_ram,
                               int* p_wd_1770_type) {
  *p_os_rom_name = "roms/mos3.50/mos.rom";

  p_rom_names[0x9] = "roms/mos3.50/dfs.rom";
  p_rom_names[0xA] = "roms/mos3.50/viewsht.rom";
  p_rom_names[0xB] = "roms/mos3.50/edit.rom";
  p_rom_names[0xC] = "roms/mos3.50/basic4.rom";
  p_rom_names[0xD] = "roms/mos3.50/adfs.rom";
  p_rom_names[0xE] = "roms/mos3.50/view.rom";
  p_rom_names[0xF] = "roms/mos3.50/terminal.rom";

  p_sideways_ram[0x4] = 1;
  p_sideways_ram[0x5] = 1;
  p_sideways_ram[0x6] = 1;
  p_sideways_ram[0x7] = 1;

  /* Plain 1770. */
  *p_wd_1770_type = 1;
}

void
config_apply_master_compact(const char** p_os_rom_name,
                            const char** p_rom_names,
                            int* p_sideways_ram,
                            int* p_wd_1770_type) {
  *p_os_rom_name = "roms/compact/os51";

  p_rom_names[0xD] = "roms/compact/adfs210";
  p_rom_names[0xE] = "roms/compact/basic48";
  p_rom_names[0xF] = "roms/compact/utils";

  p_sideways_ram[0x4] = 1;
  p_sideways_ram[0x5] = 1;
  p_sideways_ram[0x6] = 1;
  p_sideways_ram[0x7] = 1;

  /* The BBC Master Compact has a WD1772.
   * It's mostly the same as a 1770 but it settles faster (15ms vs. 30ms) and
   * optionally can step a lot faster. ADFS does use this, at 3ms.
   */
  *p_wd_1770_type = 2;
}
