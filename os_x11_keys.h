#ifndef OS_X11_KEYS_H
#define OS_X11_KEYS_H

/* Must return a 256 array mapping to keyboard.h values. */
uint8_t* os_x11_keys_get_mapping(void* p_display);

#endif /* OS_X11_KEYS_H */
