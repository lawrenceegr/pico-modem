#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "config.h"
#include "driver/sim7080g.h"

void uart_init_modem() {
    printf("Initializing modem UART...\n");
    printf("  UART: uart%d\n", uart_get_index(MODEM_UART));
    printf("  Baud: 115200\n");
    printf("  TX Pin: GPIO %d\n", PIN_UART_TX);
    printf("  RX Pin: GPIO %d\n", PIN_UART_RX);

    uart_init(MODEM_UART, 115200);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    uart_set_format(MODEM_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MODEM_UART, true);

    printf("  UART initialized successfully\n\n");
}

void led_blink(int times, int delay_ms) {
    for (int i = 0; i < times; i++) {
        gpio_put(LED_PIN, true);
        sleep_ms(delay_ms);
        gpio_put(LED_PIN, false);
        sleep_ms(delay_ms);
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n=== Pi Modem - SIM7080G ===\n");
    printf("Build: %s %s\n\n", __DATE__, __TIME__);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, false);

    gpio_init(PIN_MODEM_PWR);
    gpio_set_dir(PIN_MODEM_PWR, GPIO_OUT);
    gpio_put(PIN_MODEM_PWR, false);

    uart_init_modem();

    led_blink(3, 150);

    Sim7080G modem;

    if (!modem.start_modem()) {
        printf("\nERROR: Modem failed to start\n");
        printf("Check: power, UART (TX=%d, RX=%d), PWR_EN=%d\n\n",
               PIN_UART_TX, PIN_UART_RX, PIN_MODEM_PWR);
        while (true) {
            led_blink(10, 100);
            sleep_ms(2000);
        }
    }

    led_blink(2, 500);
    modem.get_modem_info();

    bool sim_ready = modem.check_sim();

    if (sim_ready) {
        modem.get_sim_info();
        printf("\n=== SIM Ready ===\n\n");

        while (true) {
            led_blink(1, 100);
            sleep_ms(2000);
        }
    } else {
        printf("\n=== SIM Not Ready ===\n");
        printf("Check: SIM inserted, seated properly, not locked\n\n");

        while (true) {
            led_blink(5, 200);
            sleep_ms(2000);
        }
    }

    return 0;
}
