#ifndef BEEBJIT_VIA_H
#define BEEBJIT_VIA_H

#include <stddef.h>

struct via_struct;

struct bbc_struct;

enum {
  k_via_system = 0,
  k_via_user = 1,
};
enum {
  k_via_ORB =   0x0,
  k_via_ORA =   0x1,
  k_via_DDRB =  0x2,
  k_via_DDRA =  0x3,
  k_via_T1CL =  0x4,
  k_via_T1CH =  0x5,
  k_via_T1LL =  0x6,
  k_via_T1LH =  0x7,
  k_via_SR =    0xa,
  k_via_ACR =   0xb,
  k_via_PCR =   0xc,
  k_via_IFR =   0xd,
  k_via_IER =   0xe,
  k_via_ORAnh = 0xf,
};
enum {
  k_int_CA2 =    0x01,
  k_int_CA1 =    0x02,
  k_int_TIMER1 = 0x40,
};

struct via_struct* via_create(int id, struct bbc_struct* p_bbc);
void via_destroy(struct via_struct* p_via);

unsigned char via_read(struct via_struct* p_via, size_t reg);
void via_write(struct via_struct* p_via, size_t reg, unsigned char val);

void via_raise_interrupt(struct via_struct* p_via, unsigned char val);
void via_clear_interrupt(struct via_struct* p_via, unsigned char val);

void via_check_interrupt(struct via_struct* p_via);

void via_time_advance(struct via_struct* p_via, size_t us);

void via_get_registers(struct via_struct* p_via,
                       unsigned char* ORA,
                       unsigned char* ORB,
                       unsigned char* DDRA,
                       unsigned char* DDRB,
                       unsigned char* SR,
                       unsigned char* ACR,
                       unsigned char* PCR,
                       unsigned char* IFR,
                       unsigned char* IER,
                       unsigned char* peripheral_a,
                       unsigned char* peripheral_b,
                       int* T1C,
                       int* T1L,
                       int* T2C,
                       int* T2L,
                       unsigned char* t1_oneshot_fired,
                       unsigned char* t2_oneshot_fired);

void via_set_registers(struct via_struct* p_via,
                       unsigned char ORA,
                       unsigned char ORB,
                       unsigned char DDRA,
                       unsigned char DDRB,
                       unsigned char SR,
                       unsigned char ACR,
                       unsigned char PCR,
                       unsigned char IFR,
                       unsigned char IER,
                       unsigned char peripheral_a,
                       unsigned char peripheral_b,
                       int T1C,
                       int T1L,
                       int T2C,
                       int T2L,
                       unsigned char t1_oneshot_fired,
                       unsigned char t2_oneshot_fired);

unsigned char* via_get_peripheral_b_ptr(struct via_struct* p_via);

#endif /* BEEBJIT_VIA_H */
