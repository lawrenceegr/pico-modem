#include "actuators.h"
#include "storage.h"
#include "config.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

static const uint8_t actuator_pins[GPIO_COUNT] = {
    VALVE1_PIN,
    VALVE2_PIN,
    VALVE3_PIN,
    ACSWITCH_PIN
};

static actuator_state_t actuator_states[GPIO_COUNT];

static const actuator_state_t default_states[GPIO_COUNT] = {
    ACTUATOR_OFF,
    ACTUATOR_OFF,
    ACTUATOR_OFF,
    ACTUATOR_OFF,
};

int actuators_init(void)
{
    int ret;
    ret = storage_init();
    if (ret < 0) {
        LOG_ERR("Storage init failed: %d", ret);
        return ret;
    }
    ret = storage_load_actuator_states((uint8_t *)actuator_states, GPIO_COUNT);
    if (ret == 0) {
        LOG_INF("Loaded saved actuator states");
    } else {
        LOG_WRN("Using default actuator states");
        memcpy(actuator_states, default_states, sizeof(actuator_states));
    }
    for (int i = 0; i < GPIO_COUNT; i++) {
        uint pin = actuator_pins[i];
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, actuator_states[i]);
        LOG_DBG("Actuator %d (GPIO %d) = %s", i, pin,
                actuator_states[i] ? "ON" : "OFF");
    }
    LOG_INF("Actuators initialized");
    return 0;
}

int actuators_set(actuator_id_t actuator, actuator_state_t state)
{
    if (actuator >= GPIO_COUNT) return -1;
    uint pin = actuator_pins[actuator];
    gpio_put(pin, state);
    actuator_states[actuator] = state;
    int ret = storage_save_actuator_states((uint8_t *)actuator_states, GPIO_COUNT);
    if (ret < 0) {
        LOG_WRN("Failed to save states: %d", ret);
    }
    LOG_INF("Actuator %d -> %s", actuator, state ? "ON" : "OFF");
    return 0;
}

int actuators_get(actuator_id_t actuator)
{
    if (actuator >= GPIO_COUNT) return -1;
    return actuator_states[actuator];
}

int actuators_toggle(actuator_id_t actuator)
{
    if (actuator >= GPIO_COUNT) return -1;
    actuator_state_t new_state = !actuator_states[actuator];
    return actuators_set(actuator, new_state);
}

int actuators_reset_all(void)
{
    LOG_INF("Resetting all actuators");
    for (int i = 0; i < GPIO_COUNT; i++) {
        uint pin = actuator_pins[i];
        gpio_put(pin, default_states[i]);
        actuator_states[i] = default_states[i];
    }
    int ret = storage_save_actuator_states((uint8_t *)actuator_states, GPIO_COUNT);
    if (ret < 0) {
        LOG_WRN("Failed to save reset states: %d", ret);
    }
    LOG_INF("All actuators reset");
    return 0;
}

int actuators_get_status_json(char *buf, size_t buf_size)
{
    int offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "[");
    for (int i = 0; i < GPIO_COUNT; i++) {
        offset += snprintf(buf + offset, buf_size - offset,
            "{\"pin\":%d,\"level\":\"%s\"}%s",
            i,
            actuator_states[i] ? "high" : "low",
            (i < GPIO_COUNT - 1) ? "," : "");
    }
    offset += snprintf(buf + offset, buf_size - offset, "]");
    return 0;
}
