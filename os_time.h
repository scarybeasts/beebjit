#ifndef BEEBJIT_OS_TIME_H
#define BEEBJIT_OS_TIME_H

#include <stdint.h>

struct os_time_sleeper;

uint64_t os_time_get_us(void);

struct os_time_sleeper* os_time_create_sleeper(void);
void os_time_free_sleeper(struct os_time_sleeper* p_sleeper);
void os_time_sleeper_sleep_us(struct os_time_sleeper* p_sleeper, uint64_t us);

#endif /* BEEBJIT_OS_TIME_H */
