#ifndef AQUAGATE_CONFIG_H
#define AQUAGATE_CONFIG_H

#include "hardware/uart.h"

// ---- Debug ---- 0 = no debug, 1 = debug
#define DEBUG 0

// ---- Hardware ----
#define PIN_UART_TX             0
#define PIN_UART_RX             1
#define MODEM_UART              uart0
#define PIN_MODEM_PWR           2

// ---- Actuator GPIO Pins ----
#define VALVE1_PIN              20
#define VALVE2_PIN              25
#define VALVE3_PIN              24
#define ACSWITCH_PIN            23
#define GPIO_COUNT              4

// ---- Network ----
#define NETWORK_APN             "iot2.safaricom.com"

// ---- MQTT Broker ----
#define BROKER_HOSTNAME         "broker.emqx.io"
#define BROKER_PORT             1883

// ---- MQTT Topics ----
#define TOPIC_TO_PREFIX         "to/aquagate/"
#define TOPIC_FROM_PREFIX       "from/aquagate/"
#define MAX_TOPIC_LEN           64

// ---- Device ----
#define DEVICE_TYPE             "aquagate"

// ---- Timings ----
#define HEARTBEAT_INTERVAL_MS   30000   /* 30 seconds */
#define RECONNECT_DELAY_MS      5000    /* 5 seconds */

// ---- Buffer ----
#define UART_BUFFER_SIZE        1024
#define STORAGE_FLASH_OFFSET    (16 * 1024 * 1024)   /* 16  MB offset*/

#endif /* AQUAGATE_CONFIG_H */