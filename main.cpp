#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

#include "config.h"
#include "log.h"
#include "driver/net/modem/modem.h"
#include "driver/net/mqtt/mqtt_client.h"
#include "driver/actuators/actuators.h"
#include "lib/command_parser.h"

static struct {
    bool actuators_initialized;
    bool modem_initialized;
    bool network_ready;
    bool mqtt_connected;
} app_state = {false, false, false, false};

static char device_imei[16] = {0};
static bool mqtt_reconnect = false;
static SimA7670E modem;
static MqttClient mqtt(modem);

static void uart_init_modem() {
    uart_init(MODEM_UART, 115200);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(MODEM_UART, false, false);
    LOG_INF("UART initialized");
}

static bool modem_stage_run() {
    static enum {
        MODEM_START,
        MODEM_WAIT_SIM,
        MODEM_WAIT_REG,
        MODEM_ACTIVATE_PDP,
        MODEM_GET_IMEI,
        MODEM_DONE,
        MODEM_FAILED,
    } state = MODEM_START;

    switch (state) {
    case MODEM_START:
        if (!modem.start_modem()) {
            LOG_ERR("Modem failed to start — check power, UART (TX=%d RX=%d), PWR=%d",
                    PIN_UART_TX, PIN_UART_RX, PIN_MODEM_PWR);
            state = MODEM_FAILED;
            return false;
        }
#if DEBUG
        modem.get_modem_info();
#endif
        state = MODEM_WAIT_SIM;
        return false;
    case MODEM_WAIT_SIM:
        if (!modem.check_sim()) {
            LOG_WRN("SIM not ready — check SIM is inserted and seated. Retrying in 2s...");
            sleep_ms(2000);
            return false;
        }
#if DEBUG
        modem.get_sim_info();
#endif
        state = MODEM_WAIT_REG;
        return false;
    case MODEM_WAIT_REG:
        modem.wait_for_registration();
        app_state.network_ready = true;
        state = MODEM_ACTIVATE_PDP;
        return false;
    case MODEM_ACTIVATE_PDP:
        if (!modem.activate_pdp()) {
            LOG_WRN("PDP activation failed — check APN (\"%s\"). Retrying in %dms...",
                    NETWORK_APN, RECONNECT_DELAY_MS);
            sleep_ms(RECONNECT_DELAY_MS);
            return false;
        }
        state = MODEM_GET_IMEI;
        return false;
    case MODEM_GET_IMEI: {
        std::string r = modem.send_at_response("AT+CGSN", 1000);
        LOG_DBG("CGSN response: %s", r.c_str());
        const char *p = r.c_str();
        while (*p && (*p < '0' || *p > '9'))
            p++;
        size_t i = 0;
        while (*p && *p >= '0' && *p <= '9' && i < sizeof(device_imei) - 1)
            device_imei[i++] = *p++;
        device_imei[i] = '\0';
        if (i == 0) {
            LOG_ERR("IMEI not retrieved — modem communication failed. Cannot continue.");
            state = MODEM_FAILED;
            return false;
        }
        LOG_INF("IMEI: %s", device_imei);
        app_state.modem_initialized = true;
        state = MODEM_DONE;
        return false;
    }
    case MODEM_DONE:
        return true;
    case MODEM_FAILED:
        LOG_WRN("Modem stage failed, restarting in 5s...");
        sleep_ms(5000);
        state = MODEM_START;
        return false;
    }
    return false;
}

static bool mqtt_stage_run(bool reconnect) {
    static enum {
        MQTT_REACTIVATE_PDP,
        MQTT_CONNECT,
        MQTT_SUBSCRIBE,
        MQTT_DONE,
    } state = MQTT_CONNECT;

    if (reconnect) {
        LOG_WRN("MQTT reconnect requested — resetting MQTT stage");
        state = MQTT_REACTIVATE_PDP;
    }

    switch (state) {
    case MQTT_REACTIVATE_PDP:
        LOG_INF("Reactivating PDP context...");
        if (!modem.activate_pdp()) {
            LOG_WRN("PDP reactivation failed, retrying in %dms...", RECONNECT_DELAY_MS);
            sleep_ms(RECONNECT_DELAY_MS);
            return false;
        }
        state = MQTT_CONNECT;
        return false;
    case MQTT_CONNECT: {
        char client_id[32];
        snprintf(client_id, sizeof(client_id), "aquagate_%s", device_imei);
        LOG_DBG("Connecting to %s:%d as %s", BROKER_HOSTNAME, BROKER_PORT, client_id);
        if (!mqtt.connect(BROKER_HOSTNAME, BROKER_PORT, client_id)) {
            LOG_WRN("Retrying in %dms...", RECONNECT_DELAY_MS);
            sleep_ms(RECONNECT_DELAY_MS);
            return false;
        }
        state = MQTT_SUBSCRIBE;
        return false;
    }
    case MQTT_SUBSCRIBE: {
        char sub_topic[MAX_TOPIC_LEN];
        snprintf(sub_topic, sizeof(sub_topic), "%s%s", TOPIC_TO_PREFIX, device_imei);
        LOG_DBG("Subscribing to %s", sub_topic);
        if (!mqtt.subscribe(sub_topic)) {
            LOG_WRN("MQTT subscribe failed on topic: %s — commands will not be received",
                    sub_topic);
        }
        app_state.mqtt_connected = true;
        LOG_INF("Subscribed to %s", sub_topic);
        state = MQTT_DONE;
        return false;
    }
    case MQTT_DONE:
        return true;
    }
    return false;
}

static void handle_incoming_command() {
    char rx_topic[MAX_TOPIC_LEN];
    char rx_payload[256];

    if (!mqtt.poll_message(rx_topic, sizeof(rx_topic),
                           rx_payload, sizeof(rx_payload)))
        return;

    LOG_INF("CMD [%s]: %s", rx_topic, rx_payload);

    parsed_command_t cmd;
    if (parse_command(rx_payload, &cmd) != 0) {
        LOG_WRN("Failed to parse command: %s", rx_payload);
        return;
    }

    char response[256];
    execute_command(&cmd, device_imei, response, sizeof(response));

    char pub_topic[MAX_TOPIC_LEN];
    snprintf(pub_topic, sizeof(pub_topic), "%s%s", TOPIC_FROM_PREFIX, device_imei);
    mqtt.publish(pub_topic, response);
    LOG_INF("CMD response: %s", response);
}

int main() {
    stdio_init_all();
    uart_init_modem();

    gpio_init(PIN_MODEM_PWR);
    gpio_set_dir(PIN_MODEM_PWR, GPIO_OUT);
    gpio_put(PIN_MODEM_PWR, false);

    printf("\n===== AquaGate - Pico Firmware =====\n");
    printf("Build: %s %s\n\n", __DATE__, __TIME__);

    if (actuators_init() < 0) {
        LOG_ERR("Failed to initialize actuators — halting");
        while (true)
            sleep_ms(1000);
    }
    app_state.actuators_initialized = true;
    LOG_INF("Actuators: OK");

    uint32_t last_heartbeat_ms = 0;
    uint32_t last_status_log_ms = 0;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (!app_state.modem_initialized) {
            modem_stage_run();
            continue;
        }

        if (!app_state.mqtt_connected) {
            mqtt_stage_run(mqtt_reconnect);
            mqtt_reconnect = false;
            continue;
        }

        if (!mqtt.is_connected()) {
            LOG_WRN("MQTT connection lost — reactivating PDP and reconnecting");
            app_state.mqtt_connected = false;
            mqtt_reconnect = true;
            continue;
        }

        handle_incoming_command();

        now = to_ms_since_boot(get_absolute_time());

        if (now - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
            last_heartbeat_ms = now;

            char pub_topic[MAX_TOPIC_LEN];
            snprintf(pub_topic, sizeof(pub_topic), "%s%s",
                     TOPIC_FROM_PREFIX, device_imei);

            static uint32_t heartbeat_seq = 0;
            char msg[256];
            char gpio_status[128];

            actuators_get_status_json(gpio_status, sizeof(gpio_status));

            snprintf(msg, sizeof(msg),
                     "{\"d_id\":\"%s\",\"d_type\":\"%s\","
                     "\"type\":\"heartbeat\","
                     "\"seq\":%lu,"
                     "\"uptime\":%lu,"
                     "\"gpio\":%s}",
                     device_imei,
                     DEVICE_TYPE,
                     (unsigned long)heartbeat_seq++,
                     (unsigned long)(now / 1000),
                     gpio_status);

            mqtt.publish(pub_topic, msg);
            LOG_DBG("Heartbeat published, uptime %lus", (unsigned long)(now / 1000));
        }

#if DEBUG
        if (now - last_status_log_ms >= 60000) {
            last_status_log_ms = now;
            LOG_INF("Status: Actuators=%s  Modem=%s  Network=%s  MQTT=%s  Uptime=%lus",
                    app_state.actuators_initialized ? "OK" : "FAIL",
                    app_state.modem_initialized ? "OK" : "FAIL",
                    app_state.network_ready ? "UP" : "DOWN",
                    app_state.mqtt_connected ? "CONN" : "DISC",
                    (unsigned long)(now / 1000));
        }
#else
        (void)last_status_log_ms;
#endif
    }

    return 0;
}
