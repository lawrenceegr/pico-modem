#ifndef _COMMAND_PARSER_H_
#define _COMMAND_PARSER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CMD_ACTION_UNKNOWN = 0,
    CMD_ACTION_SET,
    CMD_ACTION_GET,
    CMD_ACTION_TOGGLE,
    CMD_ACTION_RESET,
    CMD_ACTION_STATUS,
} cmd_action_t;

typedef struct {
    cmd_action_t action;
    uint8_t      pin;
    uint8_t      level;
    bool         has_pin;
    bool         has_level;
} parsed_command_t;

int parse_command(const char *json_str, parsed_command_t *cmd);
int execute_command(const parsed_command_t *cmd,
                    const char *device_imei,
                    char *response_buf,
                    size_t buf_size);

#endif // _COMMAND_PARSER_H_