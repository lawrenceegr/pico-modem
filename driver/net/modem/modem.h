#ifndef _MODEM_H_
#define _MODEM_H_

#include <string>
#include <stdint.h>
#include "config.h"

class SimA7670E
{
public:
    SimA7670E();

    bool start_modem();
    bool check_sim();
    void wait_for_registration();
    void get_sim_info();
    void get_modem_info();
    bool activate_pdp();

    // AT command interface (used by MqttClient too)
    bool        send_at(std::string cmd, std::string expected = "OK", uint32_t timeout = 3000);
    std::string send_at_response(std::string cmd, uint32_t timeout = 3000);
    void        read_buffer(uint32_t timeout);
    void        send_raw(const char *data);  // uart_puts without \r\n

private:
    bool boot_modem();
    void config_modem();
    void toggle_module_power();
    void debug_output(std::string msg);
    void clear_buffer();
    std::string buffer_to_string();

    uint8_t  uart_buffer[UART_BUFFER_SIZE];
    uint8_t *rx_ptr;
};

#endif // _MODEM_H_