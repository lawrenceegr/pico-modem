#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "hardware/uart.h"

// Hardware Pin Configuration
#define PIN_UART_TX             0
#define PIN_UART_RX             1
#define MODEM_UART              uart0
#define PIN_MODEM_PWR           2
#define LED_PIN                 23

// Buffer Configuration
#define UART_BUFFER_SIZE        1024

#endif // _CONFIG_H_
