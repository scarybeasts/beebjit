#ifndef BEEBJIT_WD_FDC_H
#define BEEBJIT_WD_FDC_H

#include <stdint.h>

struct wd_fdc_struct;

struct bbc_options;
struct disc_drive_struct;
struct state_6502;
struct timing_struct;

struct wd_fdc_struct* wd_fdc_create(struct state_6502* p_state_6502,
                                    int is_master,
                                    int is_1772,
                                    struct timing_struct* p_timing,
                                    struct bbc_options* p_options);
void wd_fdc_destroy(struct wd_fdc_struct* p_fdc);

/* Setup. */
void wd_fdc_set_drives(struct wd_fdc_struct* p_fdc,
                       struct disc_drive_struct* p_drive_0,
                       struct disc_drive_struct* p_drive_1);

void wd_fdc_power_on_reset(struct wd_fdc_struct* p_fdc);
void wd_fdc_break_reset(struct wd_fdc_struct* p_fdc);

/* Host hardware register I/O. */
uint8_t wd_fdc_read(struct wd_fdc_struct* p_fdc, uint16_t addr);
void wd_fdc_write(struct wd_fdc_struct* p_fdc, uint16_t addr, uint8_t val);

#endif /* BEEBJIT_WD_FDC_H */
