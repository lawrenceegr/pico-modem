#include <stdio.h>
#include <cstring>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "modem.h"

using std::string;

SimA7670E::SimA7670E()
{
    clear_buffer();
}

bool SimA7670E::start_modem()
{
    printf("\n===== Starting Modem =====\n");

    if (boot_modem())
    {
        config_modem();
        return true;
    }

    printf("ERROR: Modem boot failed\n");
    for (int i = 0; i < 6; i++)
    {
        gpio_put(LED_PIN, true);
        sleep_ms(100);
        gpio_put(LED_PIN, false);
        sleep_ms(100);
    }
    return false;
}

bool SimA7670E::boot_modem()
{
    bool powered = false;
    uint32_t attempts = 0;
    uint32_t start_time = time_us_32();

    while (attempts < 20)
    {
        if (send_at("ATE1"))
        {
            printf("Modem ready after %i ms\n", (time_us_32() - start_time) / 1000);
            return true;
        }
        if (!powered)
        {
            printf("Toggling power...\n");
            toggle_module_power();
            powered = true;
        }
        sleep_ms(4000);
        attempts++;
    }
    return false;
}

void SimA7670E::config_modem()
{
    printf("\n===== Configuring Modem =====\n");

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+CMEE=2;+CMGF=1;+CMGD=,4;+CNMP=2;+CGDCONT=1,\"IP\",\"%s\"",
             NETWORK_APN);
    send_at(cmd);

    send_at("AT+CGREG=2", "OK", 1000);
    send_at("AT+COPS=0;+CREG=1");
    send_at("AT+CSSLCFG=\"sslversion\",1,3;+SHSSL=1,\"\";+SHCONF=\"BODYLEN\",1024;+SHCONF=\"HEADERLEN\",350");

    printf("Modem configured, APN: \"%s\"\n", NETWORK_APN);
}

void SimA7670E::toggle_module_power()
{
    gpio_put(PIN_MODEM_PWR, true);
    sleep_ms(1500);
    gpio_put(PIN_MODEM_PWR, false);
}

bool SimA7670E::check_sim()
{
    printf("\n===== Checking SIM =====\n");

    string response = send_at_response("AT+CPIN?", 1000);

    if (response.find("READY") != string::npos)
    {
        printf("SIM: READY\n");
        return true;
    }
    else if (response.find("SIM PIN") != string::npos)
        printf("SIM: PIN REQUIRED\n");
    else if (response.find("SIM PUK") != string::npos)
        printf("SIM: PUK REQUIRED\n");
    else
        printf("SIM: NOT DETECTED\n");

    return false;
}

void SimA7670E::wait_for_registration()
{
    printf("\n===== Waiting for Network Registration =====\n");

    uint32_t attempt = 0;
    while (true)
    {
        attempt++;
        printf("Registration attempt %u...\n", attempt);

        string response = send_at_response("AT+CREG?", 2000);

        if (response.find("+CREG: 1,1") != string::npos)
        {
            printf("Registered on home network\n");
            break;
        }
        else if (response.find("+CREG: 1,5") != string::npos)
        {
            printf("Registered (roaming)\n");
            break;
        }
        else if (response.find("+CREG: 1,2") != string::npos)
            printf("Status: Searching...\n");
        else if (response.find("+CREG: 1,3") != string::npos)
            printf("Status: Registration denied\n");
        else if (response.find("+CREG: 1,0") != string::npos)
            printf("Status: Not registered, not searching\n");

        sleep_ms(5000);
    }

    printf("Network registration successful\n");
}

void SimA7670E::get_sim_info()
{
    printf("\n===== SIM Info =====\n");
    printf("\nICCID:\n");   send_at_response("AT+CCID", 1000);
    printf("\nIMSI:\n");    send_at_response("AT+CIMI", 1000);
    printf("\nNumber:\n");  send_at_response("AT+CNUM", 1000);
    printf("\nOperator:\n");send_at_response("AT+COPS?", 2000);
    printf("\nSignal:\n");  send_at_response("AT+CSQ", 1000);
    printf("\nReg:\n");     send_at_response("AT+CREG?", 1000);
}

void SimA7670E::get_modem_info()
{
    printf("\n===== Modem Info =====\n");
    printf("\nManufacturer:\n"); send_at_response("AT+CGMI", 1000);
    printf("\nModel:\n");        send_at_response("AT+CGMM", 1000);
    printf("\nFirmware:\n");     send_at_response("AT+CGMR", 1000);
    printf("\nIMEI:\n");         send_at_response("AT+CGSN", 1000);
}

bool SimA7670E::activate_pdp()
{
    printf("\n===== Activating PDP Context =====\n");

    send_at("AT+CGACT=0,1", "OK", 5000);
    sleep_ms(1000);

    if (!send_at("AT+CGACT=1,1", "OK", 10000))
    {
        printf("PDP activation failed\n");
        return false;
    }
    sleep_ms(2000);

    string response = send_at_response("AT+CGPADDR=1", 2000);
    printf("PDP address: %s\n", response.c_str());

    if (response.find("0.0.0.0") != string::npos ||
        response.find("ERROR") != string::npos)
    {
        printf("No IP address assigned\n");
        return false;
    }

    printf("PDP context active\n");
    return true;
}

bool SimA7670E::send_at(string cmd, string expected, uint32_t timeout)
{
    const string response = send_at_response(cmd, timeout);
    return (response.length() > 0 && response.find(expected) != string::npos);
}

string SimA7670E::send_at_response(string cmd, uint32_t timeout)
{
    const string data_out = cmd + "\r\n";
    uart_puts(MODEM_UART, data_out.c_str());
    read_buffer(timeout);

    if (rx_ptr > &uart_buffer[0])
        return buffer_to_string();
    return "ERROR";
}

void SimA7670E::send_raw(const char *data)
{
    uart_puts(MODEM_UART, data);
}

void SimA7670E::read_buffer(uint32_t timeout)
{
    clear_buffer();
    const uint8_t *buffer_start = &uart_buffer[0];
    rx_ptr = (uint8_t *)buffer_start;

    uint32_t start = time_us_32();
    while ((time_us_32() - start < timeout * 1000) &&
           (rx_ptr - buffer_start < UART_BUFFER_SIZE))
    {
        if (uart_is_readable(MODEM_UART))
        {
            uart_read_blocking(MODEM_UART, rx_ptr, 1);
            rx_ptr++;
        }
    }
    debug_output(buffer_to_string());
}

void SimA7670E::debug_output(string msg)
{
    size_t start = 0;
    size_t end   = 0;

    while ((end = msg.find('\n', start)) != string::npos)
    {
        string line = msg.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            printf(">>> %s\n", line.c_str());
        start = end + 1;
    }

    if (start < msg.length())
    {
        string line = msg.substr(start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            printf(">>> %s\n", line.c_str());
    }
}

void SimA7670E::clear_buffer()
{
    memset(uart_buffer, 0, sizeof(uart_buffer));
}

string SimA7670E::buffer_to_string()
{
    return string((char *)uart_buffer, rx_ptr - uart_buffer);
}