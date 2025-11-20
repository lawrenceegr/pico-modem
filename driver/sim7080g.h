#ifndef _SIM7080G_DRIVER_H_
#define _SIM7080G_DRIVER_H_

#include <string>
#include "../config.h"

using std::string;

class Sim7080G {
    public:
        Sim7080G();
        bool        start_modem();
        bool        check_sim();
        void        get_sim_info();
        void        get_modem_info();
        bool        send_at(string cmd, string expected = "OK", uint32_t timeout = 500);
        string      send_at_response(string cmd, uint32_t timeout = 500);

    private:
        bool        boot_modem();
        void        toggle_module_power();
        void        read_buffer(uint32_t timeout = 5000);
        void        clear_buffer();
        string      buffer_to_string();
        void        print_response(string msg);
        uint8_t     uart_buffer[UART_BUFFER_SIZE];
        uint8_t     *rx_ptr;
};

#endif // _SIM7080G_DRIVER_H_
