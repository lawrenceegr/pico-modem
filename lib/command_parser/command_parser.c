#include "command_parser.h"
#include "actuators.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>

// ---- Logging replacement ----
#define LOG_ERR(fmt, ...) printf("[ERR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) printf("[WRN] " fmt "\n", ##__VA_ARGS__)

// ---- Helpers ----

static const char *find_field(const char *json, const char *field)
{
    char search[32];
    snprintf(search, sizeof(search), "\"%s\"", field);

    const char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos = strchr(pos, ':');
    if (!pos) return NULL;

    pos++;
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\"')) {
        pos++;
    }
    return pos;
}

static int get_int_value(const char *json, const char *field, uint8_t *value)
{
    const char *pos = find_field(json, field);
    if (!pos) return -1;

    *value = (uint8_t)atoi(pos);
    return 0;
}

static int get_level_value(const char *json, uint8_t *level)
{
    const char *pos = find_field(json, "level");
    if (!pos) return -1;

    if (strncasecmp(pos, "high", 4) == 0 || *pos == '1') {
        *level = 1;
        return 0;
    }

    if (strncasecmp(pos, "low", 3) == 0 || *pos == '0') {
        *level = 0;
        return 0;
    }

    return -1;
}

// ---- Parser ----

int parse_command(const char *json_str, parsed_command_t *cmd)
{
    if (!json_str || !cmd) return -1;

    memset(cmd, 0, sizeof(*cmd));

    if (strstr(json_str, "reset")) {
        cmd->action = CMD_ACTION_RESET;
    } else if (strstr(json_str, "status")) {
        cmd->action = CMD_ACTION_STATUS;
    } else if (strstr(json_str, "toggle")) {
        cmd->action = CMD_ACTION_TOGGLE;
    } else if (strstr(json_str, "set")) {
        cmd->action = CMD_ACTION_SET;
    } else if (strstr(json_str, "get")) {
        cmd->action = CMD_ACTION_GET;
    } else {
        cmd->action = CMD_ACTION_UNKNOWN;
        return -1;
    }

    if (cmd->action == CMD_ACTION_SET ||
        cmd->action == CMD_ACTION_GET ||
        cmd->action == CMD_ACTION_TOGGLE) {

        if (get_int_value(json_str, "pin", &cmd->pin) == 0) {
            cmd->has_pin = true;

            if (cmd->pin >= GPIO_COUNT) {
                LOG_ERR("Invalid pin: %d", cmd->pin);
                return -1;
            }
        } else {
            LOG_ERR("Pin required");
            return -1;
        }
    }

    if (cmd->action == CMD_ACTION_SET) {
        if (get_level_value(json_str, &cmd->level) == 0) {
            cmd->has_level = true;
        } else {
            LOG_ERR("Level required");
            return -1;
        }
    }

    return 0;
}

// ---- Executor ----

int execute_command(const parsed_command_t *cmd,
                    const char *device_imei,
                    char *response_buf,
                    size_t buf_size)
{
    if (!cmd || !device_imei || !response_buf) return -1;

    int ret = 0;
    int offset = 0;
    char gpio_json[256];

    offset += snprintf(response_buf + offset, buf_size - offset,
                       "{\"d_id\":\"%s\",\"d_type\":\"%s\",",
                       device_imei, DEVICE_TYPE);

    switch (cmd->action) {

    case CMD_ACTION_SET:
        ret = actuators_set(cmd->pin, cmd->level);
        offset += snprintf(response_buf + offset, buf_size - offset,
            "\"action\":\"set\",\"pin\":%d,\"level\":\"%s\",\"result\":\"%s\"}",
            cmd->pin,
            cmd->level ? "high" : "low",
            ret == 0 ? "ok" : "err");
        break;

    case CMD_ACTION_GET:
        ret = actuators_get(cmd->pin);
        if (ret >= 0) {
            offset += snprintf(response_buf + offset, buf_size - offset,
                "\"action\":\"get\",\"pin\":%d,\"level\":\"%s\",\"result\":\"ok\"}",
                cmd->pin,
                ret ? "high" : "low");
            ret = 0;
        } else {
            offset += snprintf(response_buf + offset, buf_size - offset,
                "\"action\":\"get\",\"pin\":%d,\"result\":\"err\"}",
                cmd->pin);
        }
        break;

    case CMD_ACTION_TOGGLE:
        ret = actuators_toggle(cmd->pin);
        if (ret >= 0) {
            offset += snprintf(response_buf + offset, buf_size - offset,
                "\"action\":\"toggle\",\"pin\":%d,\"level\":\"%s\",\"result\":\"ok\"}",
                cmd->pin,
                ret ? "high" : "low");
            ret = 0;
        } else {
            offset += snprintf(response_buf + offset, buf_size - offset,
                "\"action\":\"toggle\",\"pin\":%d,\"result\":\"err\"}",
                cmd->pin);
        }
        break;

    case CMD_ACTION_RESET:
        ret = actuators_reset_all();
        offset += snprintf(response_buf + offset, buf_size - offset,
            "\"action\":\"reset\",\"result\":\"%s\"}",
            ret == 0 ? "ok" : "err");
        break;

    case CMD_ACTION_STATUS:
        actuators_get_status_json(gpio_json, sizeof(gpio_json));
        offset += snprintf(response_buf + offset, buf_size - offset,
            "\"action\":\"status\",\"gpio\":%s}", gpio_json);
        break;

    default:
        offset += snprintf(response_buf + offset, buf_size - offset,
            "\"action\":\"unknown\",\"result\":\"err\"}");
        ret = -1;
        break;
    }

    return ret;
}