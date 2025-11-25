#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "sim7080g.h"

using std::string;

Sim7080G::Sim7080G() {
    clear_buffer();
}

bool Sim7080G::start_modem() {
    printf("\n===== Starting Modem =====\n");

    if (boot_modem()) {
        config_modem();
        return true;
    } else {
        printf("ERROR: Modem boot failed\n");
        for (int i = 0; i < 6; i++) {
            gpio_put(LED_PIN, true);
            sleep_ms(100);
            gpio_put(LED_PIN, false);
            sleep_ms(100);
        }        
    }

    return false;
}

bool Sim7080G::boot_modem() {
    bool powered = false;
    uint32_t attempts = 0;
    uint32_t start_time = time_us_32();

    while (attempts < 20) {
        if (send_at("ATE1")) {
            // #ifdef DEBUG
            printf("Modem ready after %i ms\n", (time_us_32() - start_time) / 1000);
            // #endif

            return true;
        }
        if (!powered) {
            printf("Toggling power...\n");
            toggle_module_power();
            powered = true;
            // Wait 35s for modem to boot before first AT command
            printf("Waiting 35s for modem boot...\n");
            sleep_ms(35000);
        }
        
        sleep_ms(4000);
        attempts++;
    }

    return false;
}

void Sim7080G::config_modem() {
    // Set error reporting to 2, set modem to text mode, delete left-over SMS,
    // select LTE-only mode, select Cat-M only mode, set the APN

    printf("\n===== Configuring Modem =====\n");
    
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CMEE=2;+CMGF=1;+CMGD=,4;+CNMP=38;+CMNB=1;+CGDCONT=1,\"IP\",\"%s\"", NETWORK_APN);
    send_at(cmd);

    // Set SST version, set SSL no verify, set header config
    send_at("AT+CSSLCFG=\"sslversion\",1,3;+SHSSL=1,\"\";+SHCONF=\"BODYLEN\",1024;+SHCONF=\"HEADERLEN\",350");

    printf("Modem configured for Cat-M and the APN set to \"%s\"\n", NETWORK_APN);
} 

void Sim7080G::toggle_module_power() {
    gpio_put(PIN_MODEM_PWR, true);
    sleep_ms(1500);
    gpio_put(PIN_MODEM_PWR, false);
}

bool Sim7080G::check_sim() {
    printf("\n===== Checking SIM =====\n");

    string response = send_at_response("AT+CPIN?", 1000);

    if (response.find("READY") != string::npos) {
        printf("SIM: READY\n");
        return true;
    } else if (response.find("SIM PIN") != string::npos) {
        printf("SIM: PIN REQUIRED\n");
    } else if (response.find("SIM PUK") != string::npos) {
        printf("SIM: PUK REQUIRED\n");
    } else {
        printf("SIM: NOT DETECTED\n");
    }
    return false;
}

void Sim7080G::get_sim_info() {
    printf("\n===== SIM Info =====\n");

    printf("\nICCID:\n");
    send_at_response("AT+CCID", 1000);

    printf("\nIMSI:\n");
    send_at_response("AT+CIMI", 1000);

    printf("\nPhone Number:\n");
    send_at_response("AT+CNUM", 1000);

    printf("\nOperator:\n");
    send_at_response("AT+COPS?", 2000);

    printf("\nSignal:\n");
    send_at_response("AT+CSQ", 1000);

    printf("\nRegistration:\n");
    send_at_response("AT+CREG?", 1000);
}

void Sim7080G::get_modem_info() {
    printf("\n===== Modem Info =====\n");

    printf("\nManufacturer:\n");
    send_at_response("AT+CGMI", 1000);

    printf("\nModel:\n");
    send_at_response("AT+CGMM", 1000);

    printf("\nFirmware:\n");
    send_at_response("AT+CGMR", 1000);

    printf("\nIMEI:\n");
    send_at_response("AT+CGSN", 1000);
}

bool Sim7080G::send_at(string cmd, string expected, uint32_t timeout) {
    const string response = send_at_response(cmd, timeout);
    return (response.length() > 0 && response.find(expected) != string::npos);
}

string Sim7080G::send_at_response(string cmd, uint32_t timeout) {
    const string data_out = cmd + "\r\n";
    uart_puts(MODEM_UART, data_out.c_str());

    read_buffer(timeout);

    // Return response as string
    if (rx_ptr > &uart_buffer[0]) {
        return buffer_to_string();
    }
    return "ERROR";
}

void Sim7080G::read_buffer(uint32_t timeout) {
    clear_buffer();
    const uint8_t* buffer_start = &uart_buffer[0];
    rx_ptr = (uint8_t*)buffer_start;

    uint32_t start = time_us_32();
    while ((time_us_32() - start < timeout * 1000) &&
           (rx_ptr - buffer_start < UART_BUFFER_SIZE)) {
        if (uart_is_readable(MODEM_UART)) {
            uart_read_blocking(MODEM_UART, rx_ptr, 1);
            rx_ptr++;
        }
    }
    debug_output(buffer_to_string());
}

void Sim7080G::debug_output(string msg) {
    size_t start = 0;
    size_t end = 0;

    while ((end = msg.find('\n', start)) != string::npos) {
        string line = msg.substr(start, end - start);
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line = line.substr(0, line.length() - 1);
        }
        if (!line.empty()) {
            printf(">>> %s\n", line.c_str());
        }
        start = end + 1;
    }

    if (start < msg.length()) {
        string line = msg.substr(start);
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line = line.substr(0, line.length() - 1);
        }
        if (!line.empty()) {
            printf(">>> %s\n", line.c_str());
        }
    }
} 

void Sim7080G::clear_buffer() {
    for (uint32_t i = 0; i < UART_BUFFER_SIZE; i++) {
        uart_buffer[i] = 0;
    }
}

string Sim7080G::buffer_to_string() {
    string new_string(uart_buffer, rx_ptr);
    return new_string;
}
