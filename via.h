#ifndef BEEBJIT_VIA_H
#define BEEBJIT_VIA_H

#include <stdint.h>

struct via_struct;

struct bbc_struct;
struct timing_struct;
struct video_struct;

enum {
  k_via_system = 0,
  k_via_user = 1,
};

enum {
  k_via_num_mapped_registers = 16,
};

struct via_struct* via_create(int id,
                              int externally_clocked,
                              struct timing_struct* p_timing,
                              struct bbc_struct* p_bbc);
void via_destroy(struct via_struct* p_via);

void via_power_on_reset(struct via_struct* p_via);

void via_set_CA2_changed_callback(struct via_struct* p_via,
                                  void (*p_CA2_changed_callback)
                                      (void* p, int level, int output),
                                  void* p_CA2_changed_object);
void via_set_CB2_changed_callback(struct via_struct* p_via,
                                  void (*p_CB2_changed_callback)
                                      (void* p, int level, int output),
                                  void* p_CB2_changed_object);
void via_set_timing_advancer(struct via_struct* p_via,
                             void (*p_timing_advancer)(void* p, uint64_t ticks),
                             void* p_timing_advancer_object);

void via_apply_wall_time_delta(struct via_struct* p_via, uint64_t delta);

uint8_t via_read(struct via_struct* p_via, uint8_t reg);
void via_write(struct via_struct* p_via, uint8_t reg, uint8_t val);

uint8_t via_calculate_port_a(struct via_struct* p_via);
uint8_t via_calculate_port_b(struct via_struct* p_via);
void via_update_port_a(struct via_struct* p_via);
void via_update_port_b(struct via_struct* p_via);

void via_get_all_CAB(struct via_struct* p_via,
                     int* p_CA1,
                     int* p_CA2,
                     int* p_CB1,
                     int* p_CB2);
void via_set_CA1(struct via_struct* p_via, int level);
void via_set_CA2(struct via_struct* p_via, int level);
void via_set_CB1(struct via_struct* p_via, int level);
void via_set_CB2(struct via_struct* p_via, int level);

void via_set_peripheral_b(struct via_struct* p_via, uint8_t val);

/* These are used by the debugger. In particular, for reads, side effects of
 * the read are minimized.
 */
uint8_t via_read_no_side_effects(struct via_struct* p_via, uint8_t reg);
void via_write_raw(struct via_struct* p_via, uint8_t reg, uint8_t val);

void via_get_registers(struct via_struct* p_via,
                       uint8_t* p_ORA,
                       uint8_t* p_ORB,
                       uint8_t* p_DDRA,
                       uint8_t* p_DDRB,
                       uint8_t* p_SR,
                       uint8_t* p_ACR,
                       uint8_t* p_PCR,
                       uint8_t* p_IFR,
                       uint8_t* p_IER,
                       uint8_t* p_peripheral_a,
                       uint8_t* p_peripheral_b,
                       int32_t* p_T1C_raw,
                       int32_t* p_T1L,
                       int32_t* p_T2C_raw,
                       int32_t* p_T2L,
                       uint8_t* p_t1_oneshot_fired,
                       uint8_t* p_t2_oneshot_fired,
                       uint8_t* p_t1_pb7);

void via_set_registers(struct via_struct* p_via,
                       uint8_t ORA,
                       uint8_t ORB,
                       uint8_t DDRA,
                       uint8_t DDRB,
                       uint8_t SR,
                       uint8_t ACR,
                       uint8_t PCR,
                       uint8_t IFR,
                       uint8_t IER,
                       uint8_t peripheral_a,
                       uint8_t peripheral_b,
                       int32_t T1C_raw,
                       int32_t T1L,
                       int32_t T2C_raw,
                       int32_t T2L,
                       uint8_t t1_oneshot_fired,
                       uint8_t t2_oneshot_fired,
                       uint8_t t1_pb7);

#endif /* BEEBJIT_VIA_H */
