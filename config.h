#ifndef BEEBJIT_CONFIG_H
#define BEEBJIT_CONFIG_H

void
config_apply_master_128_mos320(const char** p_os_rom_name,
                               const char** p_rom_names,
                               int* p_sideways_ram,
                               int* p_wd_1770_type);
void
config_apply_master_128_mos350(const char** p_os_rom_name,
                               const char** p_rom_names,
                               int* p_sideways_ram,
                               int* p_wd_1770_type);

void
config_apply_master_compact(const char** p_os_rom_name,
                            const char** p_rom_names,
                            int* p_sideways_ram,
                            int* p_wd_1770_type);

#endif /* BEEBJIT_CONFIG_H */
