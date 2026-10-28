#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

int  storage_init(void);
int  storage_save_actuator_states(const uint8_t *states, size_t count);
int  storage_load_actuator_states(uint8_t *states, size_t count);
bool storage_has_saved_states(void);
int  storage_clear_actuator_states(void);

#endif // _STORAGE_H_