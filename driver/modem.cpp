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
    mqtt_connected = false;
}

bool SimA7670E::start_modem()
{
    printf("\n===== Starting Modem =====\n");

    if (boot_modem())
    {
        config_modem();
        return true;
    }
    else
    {
        printf("ERROR: Modem boot failed\n");
        for (int i = 0; i < 6; i++)
        {
            gpio_put(LED_PIN, true);
            sleep_ms(100);
            gpio_put(LED_PIN, false);
            sleep_ms(100);
        }
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
            // #ifdef DEBUG
            printf("Modem ready after %i ms\n", (time_us_32() - start_time) / 1000);
            // #endif

            return true;
        }
        if (!powered)
        {
            printf("Toggling power...\n");
            toggle_module_power();
            powered = true;
            // Wait 35s for modem to boot before first AT command
            // printf("Waiting 35s for modem boot...\n");
            // sleep_ms(35000);
        }

        sleep_ms(4000);
        attempts++;
    }

    return false;
}

void SimA7670E::config_modem()
{
    // Set error reporting to 2, set modem to text mode, delete left-over SMS,
    // select LTE-only mode, select Cat-M only mode, set the APN

    printf("\n===== Configuring Modem =====\n");

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+CMEE=2;+CMGF=1;+CMGD=,4;+CNMP=2;+CGDCONT=1,\"IP\",\"%s\"",
             NETWORK_APN);
    send_at(cmd);

    // Enable data roaming on A7670E
    send_at("AT+CGREG=2", "OK", 1000);

    // Enable automatic operator selection and network registration
    send_at("AT+COPS=0;+CREG=1");

    // Set SST version, set SSL no verify, set header config
    send_at("AT+CSSLCFG=\"sslversion\",1,3;+SHSSL=1,\"\";+SHCONF=\"BODYLEN\",1024;+SHCONF=\"HEADERLEN\",350");

    printf("Modem configured for LTE/3G and the APN set to \"%s\"\n", NETWORK_APN);
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
    {
        printf("SIM: PIN REQUIRED\n");
    }
    else if (response.find("SIM PUK") != string::npos)
    {
        printf("SIM: PUK REQUIRED\n");
    }
    else
    {
        printf("SIM: NOT DETECTED\n");
    }
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

        // Check for registered on home network (+CREG: 1,1) or roaming (+CREG: 1,5)
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
        {
            printf("Status: Searching for network...\n");
        }
        else if (response.find("+CREG: 1,3") != string::npos)
        {
            printf("Status: Registration denied\n");
        }
        else if (response.find("+CREG: 1,0") != string::npos)
        {
            printf("Status: Not registered, not searching\n");
        }

        sleep_ms(5000);
    }

    printf("Network registration successful\n");
}

void SimA7670E::get_sim_info()
{
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

void SimA7670E::get_modem_info()
{
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

    // Return response as string
    if (rx_ptr > &uart_buffer[0])
    {
        return buffer_to_string();
    }
    return "ERROR";
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
    size_t end = 0;

    while ((end = msg.find('\n', start)) != string::npos)
    {
        string line = msg.substr(start, end - start);
        if (!line.empty() && line[line.length() - 1] == '\r')
        {
            line = line.substr(0, line.length() - 1);
        }
        if (!line.empty())
        {
            printf(">>> %s\n", line.c_str());
        }
        start = end + 1;
    }

    if (start < msg.length())
    {
        string line = msg.substr(start);
        if (!line.empty() && line[line.length() - 1] == '\r')
        {
            line = line.substr(0, line.length() - 1);
        }
        if (!line.empty())
        {
            printf(">>> %s\n", line.c_str());
        }
    }
}

void SimA7670E::clear_buffer()
{
    for (uint32_t i = 0; i < UART_BUFFER_SIZE; i++)
    {
        uart_buffer[i] = 0;
    }
}

string SimA7670E::buffer_to_string()
{
    string new_string(uart_buffer, rx_ptr);
    return new_string;
}

bool SimA7670E::activate_pdp()
{
    printf("\n===== Activating PDP Context =====\n");

    // Set network mode to automatic (already done but be explicit)
    // First deactivate any existing context
    send_at("AT+CGACT=0,1", "OK", 5000);
    sleep_ms(1000);

    // Activate PDP context 1
    if (!send_at("AT+CGACT=1,1", "OK", 10000))
    {
        printf("PDP activation failed\n");
        return false;
    }
    sleep_ms(2000);

    // Verify we got an IP address
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

// MQTT Functions for A7670E
bool SimA7670E::mqtt_connect(const char *broker, uint16_t port, const char *client_id)
{
    printf("\n===== MQTT Connect =====\n");

    // Stop any existing MQTT connection
    send_at("AT+CMQTTDISC=0,60", "OK", 2000);
    send_at("AT+CMQTTREL=0", "OK", 1000);
    send_at("AT+CMQTTSTOP", "OK", 2000);

    // Start MQTT service
    if (!send_at("AT+CMQTTSTART", "OK", 3000))
    {
        string response = send_at_response("AT+CMQTTSTART", 3000);
        if (response.find("ERROR") != string::npos && response.find("+CMQTTSTART: 0") == string::npos)
        {
            printf("MQTT service already started or error\n");
        }
    }
    sleep_ms(500);

    // Acquire a client
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\"", client_id);
    if (!send_at(cmd, "OK", 2000))
    {
        printf("Failed to acquire MQTT client\n");
        return false;
    }
    sleep_ms(500);

    // Connect to broker
    snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1", broker, port);
    string response = send_at_response(cmd, 10000);

    if (response.find("+CMQTTCONNECT: 0,0") != string::npos)
    {
        printf("Connected to MQTT broker: %s:%d\n", broker, port);
        mqtt_connected = true;
        return true;
    }

    printf("Failed to connect to MQTT broker\n");
    mqtt_connected = false;
    return false;
}

bool SimA7670E::mqtt_disconnect()
{
    printf("\n===== MQTT Disconnect =====\n");

    send_at("AT+CMQTTDISC=0,60", "OK", 5000);
    send_at("AT+CMQTTREL=0", "OK", 2000);
    send_at("AT+CMQTTSTOP", "OK", 2000);

    mqtt_connected = false;
    printf("Disconnected from MQTT broker\n");
    return true;
}

bool SimA7670E::mqtt_publish(const char *topic, const char *message)
{
    if (!mqtt_connected)
    {
        printf("MQTT not connected\n");
        return false;
    }

    char cmd[128];
    uint16_t topic_len = strlen(topic);
    uint16_t msg_len = strlen(message);

    // Set topic
    snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", topic_len);
    string response = send_at_response(cmd, 2000);
    if (response.find(">") != string::npos)
    {
        uart_puts(MODEM_UART, topic);
        sleep_ms(500);
        read_buffer(2000);
    }
    else
    {
        printf("Failed to set topic\n");
        return false;
    }

    // Set payload
    snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", msg_len);
    response = send_at_response(cmd, 2000);
    if (response.find(">") != string::npos)
    {
        uart_puts(MODEM_UART, message);
        sleep_ms(500);
        read_buffer(2000);
    }
    else
    {
        printf("Failed to set payload\n");
        return false;
    }

    // Publish (QoS 1)
    response = send_at_response("AT+CMQTTPUB=0,1,60", 5000);
    if (response.find("+CMQTTPUB: 0,0") != string::npos)
    {
        printf("Published to %s: %s\n", topic, message);
        return true;
    }

    printf("Publish failed\n");
    return false;
}

bool SimA7670E::mqtt_subscribe(const char *topic)
{
    if (!mqtt_connected)
    {
        printf("MQTT not connected\n");
        return false;
    }

    char cmd[128];
    uint16_t topic_len = strlen(topic);

    // Set subscription topic
    snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=0,%d,1", topic_len);
    string response = send_at_response(cmd, 2000);
    if (response.find(">") != string::npos)
    {
        uart_puts(MODEM_UART, topic);
        sleep_ms(500);
        read_buffer(2000);
    }
    else
    {
        printf("Failed to set subscription topic\n");
        return false;
    }

    // Subscribe
    response = send_at_response("AT+CMQTTSUB=0", 5000);
    if (response.find("+CMQTTSUB: 0,0") != string::npos)
    {
        printf("Subscribed to %s\n", topic);
        return true;
    }

    printf("Subscribe failed\n");
    return false;
}

bool SimA7670E::mqtt_is_connected()
{
    return mqtt_connected;
}
