#ifndef BEEBJIT_OS_CHANNEL_H
#define BEEBJIT_OS_CHANNEL_H

#include <stdint.h>

void os_channel_get_handles(intptr_t* p_read1,
                            intptr_t* p_write1,
                            intptr_t* p_read2,
                            intptr_t* p_write2);
void os_channel_free_handles(intptr_t read1,
                             intptr_t write1,
                             intptr_t read2,
                             intptr_t write2);

void os_channel_read(intptr_t handle, void* p_message, uint32_t length);
void os_channel_write(intptr_t handle, const void* p_message, uint32_t length);

#endif /* BEEBJIT_OS_CHANNEL_H */
