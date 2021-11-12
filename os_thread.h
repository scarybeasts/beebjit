#ifndef BEEBJIT_OS_THREAD_H
#define BEEBJIT_OS_THREAD_H

struct os_thread_struct;

struct os_thread_struct* os_thread_create(void* p_func, void* p_arg);
intptr_t os_thread_destroy(struct os_thread_struct* p_thread_struct);

#endif /* BEEBJIT_OS_THREAD_H */
