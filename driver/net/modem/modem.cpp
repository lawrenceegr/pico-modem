#include <stdio.h>
#include <cstring>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "modem.h"
#include "log.h"

using std::string;

SimA7670E::SimA7670E() : urc_stash_len(0)
{
    clear_buffer();
    memset(urc_stash, 0, sizeof(urc_stash));
}

bool SimA7670E::start_modem()
{
    LOG_INF("Starting modem");

    if (boot_modem())
    {
        config_modem();
        return true;
    }

    LOG_ERR("Modem boot failed");
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
            LOG_INF("Modem ready after %i ms", (time_us_32() - start_time) / 1000);
            return true;
        }
        if (!powered)
        {
            LOG_INF("Toggling power...");
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
    LOG_DBG("Configuring modem");

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+CMEE=2;+CMGF=1;+CMGD=,4;+CNMP=2;+CGDCONT=1,\"IP\",\"%s\"",
             NETWORK_APN);
    send_at(cmd);

    send_at("AT+CGREG=2", "OK", 1000);
    send_at("AT+COPS=0;+CREG=1");
    send_at("AT+CSSLCFG=\"sslversion\",1,3;+SHSSL=1,\"\";+SHCONF=\"BODYLEN\",1024;+SHCONF=\"HEADERLEN\",350");

    LOG_INF("Modem configured, APN: \"%s\"", NETWORK_APN);
}

void SimA7670E::toggle_module_power()
{
    gpio_put(PIN_MODEM_PWR, true);
    sleep_ms(1500);
    gpio_put(PIN_MODEM_PWR, false);
}

bool SimA7670E::check_sim()
{
    LOG_DBG("Checking SIM");

    string response = send_at_response("AT+CPIN?", 1000);

    if (response.find("READY") != string::npos)
    {
        LOG_INF("SIM: READY");
        return true;
    }
    else if (response.find("SIM PIN") != string::npos)
        LOG_WRN("SIM: PIN REQUIRED");
    else if (response.find("SIM PUK") != string::npos)
        LOG_WRN("SIM: PUK REQUIRED");
    else
        LOG_ERR("SIM: NOT DETECTED");

    return false;
}

void SimA7670E::wait_for_registration()
{
    LOG_INF("Waiting for network registration");

    uint32_t attempt = 0;
    while (true)
    {
        attempt++;
        LOG_DBG("Registration attempt %u", attempt);

        string response = send_at_response("AT+CREG?", 2000);

        if (response.find("+CREG: 1,1") != string::npos)
        {
            LOG_INF("Registered on home network");
            break;
        }
        else if (response.find("+CREG: 1,5") != string::npos)
        {
            LOG_INF("Registered (roaming)");
            break;
        }
        else if (response.find("+CREG: 1,2") != string::npos)
            LOG_DBG("Status: Searching...");
        else if (response.find("+CREG: 1,3") != string::npos)
            LOG_WRN("Status: Registration denied");
        else if (response.find("+CREG: 1,0") != string::npos)
            LOG_WRN("Status: Not registered, not searching");

        sleep_ms(5000);
    }
}

void SimA7670E::get_sim_info()
{
    LOG_DBG("SIM info:");
    LOG_DBG("IMSI:");     send_at_response("AT+CIMI", 1000);
    LOG_DBG("Operator:"); send_at_response("AT+COPS?", 2000);
    LOG_DBG("Signal:");   send_at_response("AT+CSQ", 1000);
    LOG_DBG("Reg:");      send_at_response("AT+CREG?", 1000);
}

void SimA7670E::get_modem_info()
{
    LOG_DBG("Modem info:");
    LOG_DBG("Manufacturer:"); send_at_response("AT+CGMI", 1000);
    LOG_DBG("Model:");        send_at_response("AT+CGMM", 1000);
    LOG_DBG("Firmware:");     send_at_response("AT+CGMR", 1000);
}

bool SimA7670E::activate_pdp()
{
    LOG_INF("Activating PDP context");

    send_at("AT+CGACT=0,1", "OK", 5000);
    sleep_ms(1000);

    if (!send_at("AT+CGACT=1,1", "OK", 30000))
    {
        LOG_ERR("PDP activation failed");
        return false;
    }

    string response = send_at_response("AT+CGPADDR=1", 2000);

    if (response.find("0.0.0.0") != string::npos ||
        response.find("ERROR") != string::npos)
    {
        LOG_ERR("No IP address assigned");
        return false;
    }

    LOG_INF("PDP context active");
    return true;
}

bool SimA7670E::send_at(const string &cmd, const string &expected, uint32_t timeout)
{
    const string response = send_at_response(cmd, timeout);
    return (response.length() > 0 && response.find(expected) != string::npos);
}

string SimA7670E::send_at_response(const string &cmd, uint32_t timeout)
{
    // Drain any stale bytes (e.g. URCs that arrived since the last command)
    // before sending so they don't poison this command's response.
    uint8_t discard;
    while (uart_is_readable(MODEM_UART))
        uart_read_blocking(MODEM_UART, &discard, 1);

    const string data_out = cmd + "\r\n";
    uart_puts(MODEM_UART, data_out.c_str());
    read_buffer(timeout, data_out.size());
    extract_urcs();

    if (rx_ptr > &uart_buffer[0])
        return buffer_to_string();
    return "ERROR";
}

void SimA7670E::send_raw(const char *data)
{
    uart_puts(MODEM_UART, data);
}

bool SimA7670E::has_terminator()
{
    size_t len = rx_ptr - uart_buffer;
    if (len < 2) return false;

    // Input prompt: "> "
    if (uart_buffer[len - 2] == '>' && uart_buffer[len - 1] == ' ')
        return true;

    // All final result codes end with \r\n
    if (uart_buffer[len - 2] != '\r' || uart_buffer[len - 1] != '\n')
        return false;

    // Find start of last line (scan backward past the \r\n we just matched)
    size_t pos = (len >= 3) ? len - 3 : 0;
    while (pos > 0 && uart_buffer[pos] != '\n')
        pos--;
    if (uart_buffer[pos] == '\n') pos++;

    size_t line_end = len - 2;

    // "OK" on its own line
    if (line_end - pos == 2 &&
        uart_buffer[pos] == 'O' && uart_buffer[pos + 1] == 'K')
        return true;

    // Any line containing "ERROR" (handles ERROR, +CME ERROR, +CMS ERROR)
    for (size_t i = pos; i + 5 <= line_end; i++) {
        if (memcmp(&uart_buffer[i], "ERROR", 5) == 0)
            return true;
    }

    return false;
}

void SimA7670E::read_buffer(uint32_t timeout, size_t skip_bytes)
{
    clear_buffer();
    rx_ptr = uart_buffer;

    uint32_t start      = time_us_32();
    uint32_t last_rx    = start;
    size_t   bytes_read = 0;

    while ((time_us_32() - start) < timeout * 1000 &&
           (rx_ptr - uart_buffer) < UART_BUFFER_SIZE)
    {
        if (uart_is_readable(MODEM_UART))
        {
            uart_read_blocking(MODEM_UART, rx_ptr, 1);
            rx_ptr++;
            bytes_read++;
            last_rx = time_us_32();

            if (has_terminator())
                break;
        }
        else if (bytes_read > skip_bytes &&
                 rx_ptr > uart_buffer &&
                 (time_us_32() - last_rx) > 50000)
        {
            // 50ms idle after receiving response data (beyond echo) — complete
            break;
        }
    }
#if DEBUG
    debug_output(buffer_to_string());
#endif
}

void SimA7670E::debug_output(const string &msg)
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

void SimA7670E::extract_urcs()
{
    size_t len = rx_ptr - uart_buffer;
    if (len == 0) return;

    // Scan for +CMQTTRXSTART ... +CMQTTRXEND block in the buffer
    const char *buf = (const char *)uart_buffer;
    const char *urc_start = nullptr;
    const char *urc_end   = nullptr;

    for (size_t i = 0; i + 13 <= len; i++) {
        if (memcmp(&buf[i], "+CMQTTRXSTART", 13) == 0) {
            // Walk back to include the leading \r\n if present
            urc_start = &buf[i];
            if (i >= 2 && buf[i - 2] == '\r' && buf[i - 1] == '\n')
                urc_start = &buf[i - 2];
            break;
        }
    }
    if (!urc_start) return;

    for (size_t i = (urc_start - buf); i + 11 <= len; i++) {
        if (memcmp(&buf[i], "+CMQTTRXEND", 11) == 0) {
            // Find end of this line
            urc_end = &buf[i + 11];
            while (urc_end < buf + len && *urc_end != '\n')
                urc_end++;
            if (urc_end < buf + len) urc_end++; // include \n
            break;
        }
    }
    if (!urc_end) return;

    // Copy URC to stash
    size_t urc_len = urc_end - urc_start;
    if (urc_stash_len + urc_len < sizeof(urc_stash)) {
        memcpy(&urc_stash[urc_stash_len], urc_start, urc_len);
        urc_stash_len += urc_len;
    }

    // Remove URC from main buffer by compacting
    size_t after = (buf + len) - urc_end;
    if (after > 0)
        memmove((void *)urc_start, urc_end, after);
    rx_ptr -= urc_len;
}

bool SimA7670E::has_pending_urc()
{
    return urc_stash_len > 0;
}

void SimA7670E::poll_urc(std::string &out, uint32_t timeout_ms)
{
    if (urc_stash_len > 0) {
        out.assign(urc_stash, urc_stash_len);
        urc_stash_len = 0;
        return;
    }
    read_buffer(timeout_ms);
    out = buffer_to_string();
}