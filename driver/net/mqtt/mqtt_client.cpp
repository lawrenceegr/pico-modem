#include "mqtt_client.h"
#include "log.h"

#include <stdio.h>
#include <cstring>

#include "pico/stdlib.h"

using std::string;

MqttClient::MqttClient(SimA7670E &modem)
    : modem(modem), connected(false), port(0)
{
    broker[0] = '\0';
    client_id[0] = '\0';
}

bool MqttClient::connect(const char *broker, uint16_t port, const char *client_id)
{
    LOG_INF("Connecting to MQTT broker: %s:%d", broker, port);

    strncpy(this->broker, broker, sizeof(this->broker) - 1);
    strncpy(this->client_id, client_id, sizeof(this->client_id) - 1);
    this->port = port;

    modem.send_at("AT+CMQTTDISC=0,60", "OK", 2000);
    modem.send_at("AT+CMQTTREL=0", "OK", 1000);
    modem.send_at("AT+CMQTTSTOP", "OK", 2000);

    {
        string r = modem.send_at_response("AT+CMQTTSTART", 3000);
        if (r.find("ERROR") != string::npos)
        {
            LOG_ERR("MQTT service start failed: %s", r.c_str());
            return false;
        }
        // Wait for +CMQTTSTART: 0 — confirms the MQTT stack is ready.
        // Stale +CMQTTSTOP: 0 URCs from the cleanup may arrive first; skip them.
        bool started = false;
        for (int i = 0; i < 3 && !started; i++)
        {
            string urc;
            modem.poll_urc(urc, 3000);
            if (urc.find("+CMQTTSTART: 0") != string::npos)
                started = true;
            else if (urc.empty())
                break;
        }
        if (!started)
        {
            LOG_ERR("MQTT service start timeout");
            return false;
        }
    }
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\"", client_id);
    if (!modem.send_at(cmd, "OK", 2000))
    {
        LOG_ERR("Failed to acquire MQTT client");
        return false;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1", broker, port);
    // Modem sends OK first (command accepted), then +CMQTTCONNECT: 0,X async
    string r1 = modem.send_at_response(cmd, 10000);
    if (r1.find("OK") == string::npos)
    {
        LOG_ERR("MQTT connect command failed: %s", r1.c_str());
        connected = false;
        return false;
    }
    string r2;
    modem.poll_urc(r2, 10000);
    if (r2.find("+CMQTTCONNECT: 0,0") != string::npos)
    {
        LOG_INF("Connected to MQTT broker: %s:%d", broker, port);
        connected = true;
        return true;
    }

    LOG_ERR("MQTT connect URC failed: %s", r2.c_str());
    connected = false;
    return false;
}

bool MqttClient::disconnect()
{
    LOG_INF("Disconnecting from MQTT broker");

    modem.send_at("AT+CMQTTDISC=0,60", "OK", 5000);
    modem.send_at("AT+CMQTTREL=0", "OK", 2000);
    modem.send_at("AT+CMQTTSTOP", "OK", 2000);

    connected = false;
    LOG_INF("Disconnected from MQTT broker");
    return true;
}

bool MqttClient::publish(const char *topic, const char *message)
{
    if (!connected)
        return false;

    char cmd[128];
    uint16_t topic_len = strlen(topic);
    uint16_t msg_len   = strlen(message);

    snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", topic_len);
    string response = modem.send_at_response(cmd, 2000);
    if (response.find(">") == string::npos)
    {
        LOG_WRN("Publish failed: no topic prompt");
        connected = false;
        return false;
    }
    modem.send_raw(topic);
    modem.read_buffer(2000);

    snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", msg_len);
    response = modem.send_at_response(cmd, 2000);
    if (response.find(">") == string::npos)
    {
        LOG_WRN("Publish failed: no payload prompt");
        connected = false;
        return false;
    }
    modem.send_raw(message);
    modem.read_buffer(2000);

    // Modem sends OK first (command accepted), then +CMQTTPUB: 0,X async
    response = modem.send_at_response("AT+CMQTTPUB=0,1,60", 5000);
    if (response.find("OK") != string::npos)
    {
        string urc;
        modem.poll_urc(urc, 5000);
        if (urc.find("+CMQTTPUB: 0,0") != string::npos)
        {
            LOG_DBG("Published to %s: %s", topic, message);
            return true;
        }
    }

    LOG_WRN("Publish failed");
    connected = false;
    return false;
}

bool MqttClient::subscribe(const char *topic)
{
    if (!connected)
    {
        LOG_ERR("MQTT not connected, cannot subscribe");
        return false;
    }

    char cmd[128];
    uint16_t topic_len = strlen(topic);

    snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=0,%d,1", topic_len);
    string response = modem.send_at_response(cmd, 2000);
    if (response.find(">") != string::npos)
    {
        modem.send_raw(topic);
        modem.read_buffer(2000);
    }
    else
    {
        LOG_ERR("Failed to set subscription topic");
        return false;
    }

    // Modem sends OK first (command accepted), then +CMQTTSUB: 0,X async
    response = modem.send_at_response("AT+CMQTTSUB=0", 5000);
    if (response.find("OK") != string::npos)
    {
        string urc;
        modem.poll_urc(urc, 5000);
        if (urc.find("+CMQTTSUB: 0,0") != string::npos)
        {
            LOG_INF("Subscribed to %s", topic);
            return true;
        }
    }

    LOG_ERR("Subscribe failed");
    return false;
}

bool MqttClient::is_connected()
{
    return connected;
}

bool MqttClient::poll_message(char *topic_out, size_t topic_size,
                               char *payload_out, size_t payload_size)
{
    std::string urc;
    modem.poll_urc(urc, 300);

    if (urc.find("+CMQTTRXSTART") == std::string::npos)
        return false;

    LOG_DBG("URC: %s", urc.c_str());

    size_t tp = urc.find("+CMQTTRXTOPIC:");
    size_t pp = urc.find("+CMQTTRXPAYLOAD:");
    size_t ep = urc.find("+CMQTTRXEND");

    if (tp == std::string::npos || pp == std::string::npos)
    {
        LOG_WRN("Malformed MQTT URC, discarding");
        return false;
    }

    size_t tstart = urc.find('\n', tp) + 1;
    size_t tend   = urc.find('\n', tstart);
    std::string topic = urc.substr(tstart, tend - tstart);
    if (!topic.empty() && topic.back() == '\r') topic.pop_back();

    size_t pstart = urc.find('\n', pp) + 1;
    size_t pend   = (ep != std::string::npos)
                    ? urc.rfind('\n', ep)
                    : std::string::npos;
    std::string payload = urc.substr(pstart, pend - pstart);
    if (!payload.empty() && payload.back() == '\r') payload.pop_back();

    strncpy(topic_out,   topic.c_str(),   topic_size   - 1);
    topic_out[topic_size - 1]   = '\0';
    strncpy(payload_out, payload.c_str(), payload_size - 1);
    payload_out[payload_size - 1] = '\0';

    return true;
}