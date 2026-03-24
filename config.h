#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "hardware/uart.h"

// Hardware Pin Configuration
#define PIN_UART_TX             0
#define PIN_UART_RX             1
#define MODEM_UART              uart0
#define PIN_MODEM_PWR           2
#define LED_PIN                 23

#define NETWORK_APN   "iot2.safaricom.com"

// MQTT Configuration
#define MQTT_BROKER   "broker.emqx.io"
#define MQTT_PORT     1883
#define MQTT_CLIENT_ID "pico_modem_001"
#define MQTT_TOPIC    "aquagate_v2"

// Buffer Configuration
#define UART_BUFFER_SIZE        1024

#endif // _CONFIG_H_
