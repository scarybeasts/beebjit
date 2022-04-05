#ifndef BEEBJIT_JOYSTICK_H
#define BEEBJIT_JOYSTICK_H

struct adc_struct;
struct keyboard_struct;
struct via_struct;

struct joystick_struct;

struct joystick_struct* joystick_create(struct via_struct* p_system_via,
                                        struct adc_struct* p_adc,
                                        struct keyboard_struct* p_keyboard);
void joystick_destroy(struct joystick_struct* p_joystick);

void joystick_set_use_keyboard(struct joystick_struct* p_joystick,
                               int is_use_keyboard);

void joystick_tick(struct joystick_struct* p_joystick);

#endif /* BEEBJIT_JOYSTICK_H */
