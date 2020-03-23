#ifndef BEEBJIT_OS_POLLER_H
#define BEEBJIT_OS_POLLER_H

#include <stdint.h>

struct os_poller_struct;

struct os_poller_struct* os_poller_create();
void os_poller_destroy(struct os_poller_struct* p_poller);

void os_poller_add_handle(struct os_poller_struct* p_poller, intptr_t handle);
void os_poller_poll(struct os_poller_struct* p_poller);
int os_poller_handle_triggered(struct os_poller_struct* p_poller,
                               uint32_t index);

#endif /* BEEBJIT_OS_POLLER_H */
