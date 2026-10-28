#ifndef _MQTT_CLIENT_H_
#define _MQTT_CLIENT_H_

#include <stdint.h>
#include "driver/net/modem/modem.h"

class MqttClient
{
public:
    MqttClient(SimA7670E &modem);

    bool connect(const char *broker, uint16_t port, const char *client_id);
    bool disconnect();
    bool publish(const char *topic, const char *message);
    bool subscribe(const char *topic);
    bool is_connected();

private:
    SimA7670E &modem;

    char     broker[128];
    uint16_t port;
    char     client_id[64];
    bool     connected;
};

#endif // _MQTT_CLIENT_H_