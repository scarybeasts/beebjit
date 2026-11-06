#ifndef BEEBJIT_OS_THREAD_H
#define BEEBJIT_OS_THREAD_H

struct os_thread_struct;

struct os_thread_struct* os_thread_create(void* p_func, void* p_arg);
intptr_t os_thread_destroy(struct os_thread_struct* p_thread_struct);

/* Exit the entire process, i.e. all threads. */
void os_exit(int exit_code) __attribute__((noreturn));

#endif /* BEEBJIT_OS_THREAD_H */
