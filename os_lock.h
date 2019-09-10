#ifndef BEEBJIT_OS_LOCK_H
#define BEEBJIT_OS_LOCK_H

struct os_lock_struct;

struct os_lock_struct* os_lock_create();
void os_lock_destroy(struct os_lock_struct* p_lock);

void os_lock_lock(struct os_lock_struct* p_lock);
void os_lock_unlock(struct os_lock_struct* p_lock);

#endif /* BEEBJIT_OS_LOCK_H */
