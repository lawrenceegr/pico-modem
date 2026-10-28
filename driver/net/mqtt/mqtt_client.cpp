#include <stdio.h>
#include <cstring>
#include "mqtt_client.h"

using std::string;

MqttClient::MqttClient(SimA7670E &modem)
    : modem(modem), connected(false), port(0)
{
    broker[0]    = '\0';
    client_id[0] = '\0';
}

bool MqttClient::connect(const char *broker, uint16_t port, const char *client_id)
{
    printf("\n===== MQTT Connect =====\n");

    strncpy(this->broker,    broker,    sizeof(this->broker)    - 1);
    strncpy(this->client_id, client_id, sizeof(this->client_id) - 1);
    this->port = port;

    modem.send_at("AT+CMQTTDISC=0,60", "OK", 2000);
    modem.send_at("AT+CMQTTREL=0",     "OK", 1000);
    modem.send_at("AT+CMQTTSTOP",      "OK", 2000);

    if (!modem.send_at("AT+CMQTTSTART", "OK", 3000))
    {
        string r = modem.send_at_response("AT+CMQTTSTART", 3000);
        if (r.find("ERROR") != string::npos &&
            r.find("+CMQTTSTART: 0") == string::npos)
        {
            printf("MQTT service start failed\n");
            return false;
        }
    }
    sleep_ms(500);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\"", client_id);
    if (!modem.send_at(cmd, "OK", 2000))
    {
        printf("Failed to acquire MQTT client\n");
        return false;
    }
    sleep_ms(500);

    snprintf(cmd, sizeof(cmd),
             "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1", broker, port);
    string response = modem.send_at_response(cmd, 10000);

    if (response.find("+CMQTTCONNECT: 0,0") != string::npos)
    {
        printf("Connected to MQTT broker: %s:%d\n", broker, port);
        connected = true;
        return true;
    }

    printf("Failed to connect to MQTT broker\n");
    connected = false;
    return false;
}

bool MqttClient::disconnect()
{
    printf("\n===== MQTT Disconnect =====\n");

    modem.send_at("AT+CMQTTDISC=0,60", "OK", 5000);
    modem.send_at("AT+CMQTTREL=0",     "OK", 2000);
    modem.send_at("AT+CMQTTSTOP",      "OK", 2000);

    connected = false;
    printf("Disconnected from MQTT broker\n");
    return true;
}

bool MqttClient::publish(const char *topic, const char *message)
{
    for (int attempt = 0; attempt < 2; attempt++)
    {
        if (!connected)
        {
            printf("MQTT not connected, reconnecting...\n");
            if (!connect(broker, port, client_id))
            {
                printf("Reconnect failed\n");
                return false;
            }
        }

        char cmd[128];
        uint16_t topic_len = strlen(topic);
        uint16_t msg_len   = strlen(message);

        // Set topic
        snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", topic_len);
        string response = modem.send_at_response(cmd, 2000);
        if (response.find(">") != string::npos)
        {
            modem.send_raw(topic);
            sleep_ms(500);
            modem.read_buffer(2000);
        }
        else
        {
            printf("Failed to set topic, marking disconnected\n");
            connected = false;
            continue;
        }

        // Set payload
        snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", msg_len);
        response = modem.send_at_response(cmd, 2000);
        if (response.find(">") != string::npos)
        {
            modem.send_raw(message);
            sleep_ms(500);
            modem.read_buffer(2000);
        }
        else
        {
            printf("Failed to set payload, marking disconnected\n");
            connected = false;
            continue;
        }

        // Publish QoS 1
        response = modem.send_at_response("AT+CMQTTPUB=0,1,60", 5000);
        if (response.find("+CMQTTPUB: 0,0") != string::npos)
        {
            printf("Published to %s: %s\n", topic, message);
            return true;
        }

        printf("Publish failed (attempt %d), marking disconnected\n", attempt + 1);
        connected = false;
    }

    return false;
}

bool MqttClient::subscribe(const char *topic)
{
    if (!connected)
    {
        printf("MQTT not connected\n");
        return false;
    }

    char cmd[128];
    uint16_t topic_len = strlen(topic);

    snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=0,%d,1", topic_len);
    string response = modem.send_at_response(cmd, 2000);
    if (response.find(">") != string::npos)
    {
        modem.send_raw(topic);
        sleep_ms(500);
        modem.read_buffer(2000);
    }
    else
    {
        printf("Failed to set subscription topic\n");
        return false;
    }

    response = modem.send_at_response("AT+CMQTTSUB=0", 5000);
    if (response.find("+CMQTTSUB: 0,0") != string::npos)
    {
        printf("Subscribed to %s\n", topic);
        return true;
    }

    printf("Subscribe failed\n");
    return false;
}

bool MqttClient::is_connected()
{
    return connected;
}