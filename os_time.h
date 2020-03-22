#ifndef BEEBJIT_OS_TIME_H
#define BEEBJIT_OS_TIME_H

#include <stdint.h>

uint64_t os_time_get_us(void);
void os_time_sleep_us(uint64_t us);

#endif /* BEEBJIT_OS_TIME_H */
