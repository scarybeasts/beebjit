#ifndef BEEBJIT_OS_THREAD_H
#define BEEBJIT_OS_THREAD_H

struct os_lock_struct;
struct os_thread_struct;

struct os_thread_struct* os_thread_create(void* p_func, void* p_arg);
intptr_t os_thread_destroy(struct os_thread_struct* p_thread_struct);

struct os_lock_struct* os_lock_create();
void os_lock_destroy(struct os_lock_struct* p_lock);

void os_lock_lock(struct os_lock_struct* p_lock);
void os_lock_unlock(struct os_lock_struct* p_lock);

#endif /* BEEBJIT_OS_THREAD_H */
