#ifndef _ACTUATORS_H_
#define _ACTUATORS_H_

#include <stdint.h>
#include <stddef.h>

typedef enum {
    ACTUATOR_VALVE1   = 0,
    ACTUATOR_VALVE2   = 1,
    ACTUATOR_VALVE3   = 2,
    ACTUATOR_ACSWITCH = 3,
} actuator_id_t;

typedef enum {
    ACTUATOR_OFF = 0,
    ACTUATOR_ON  = 1,
} actuator_state_t;

int  actuators_init(void);
int  actuators_set(actuator_id_t actuator, actuator_state_t state);
int  actuators_get(actuator_id_t actuator);
int  actuators_toggle(actuator_id_t actuator);
int  actuators_reset_all(void);
int  actuators_get_status_json(char *buf, size_t buf_size);

#endif // _ACTUATORS_H_