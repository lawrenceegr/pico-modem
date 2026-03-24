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
        void        wait_for_registration();
        void        get_sim_info();
        void        get_modem_info();
        bool        send_at(string cmd, string expected = "OK", uint32_t timeout = 500);
        string      send_at_response(string cmd, uint32_t timeout = 500);

        // MQTT functions
        bool        mqtt_connect(const char* broker, uint16_t port, const char* client_id);
        bool        mqtt_disconnect();
        bool        mqtt_publish(const char* topic, const char* message);
        bool        mqtt_subscribe(const char* topic);
        bool        mqtt_is_connected();
        bool        activate_pdp();

    private:
        bool        boot_modem();
        void        config_modem();
        void        toggle_module_power();
        void        debug_output(string msg);
        void        read_buffer(uint32_t timeout = 5000);
        void        clear_buffer();
        string      buffer_to_string();
        uint8_t     uart_buffer[UART_BUFFER_SIZE];
        uint8_t     *rx_ptr;
        bool        mqtt_connected;
};

#endif // _SIM7080G_DRIVER_H_
