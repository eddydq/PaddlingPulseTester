#ifndef _PADDLING_PULSE_CONSOLE_COMMANDS_H_
#define _PADDLING_PULSE_CONSOLE_COMMANDS_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PP_CONSOLE_CMD_NONE = 0,
    PP_CONSOLE_CMD_BATT_GET,
    PP_CONSOLE_CMD_IOCFG_GET,
    PP_CONSOLE_CMD_CAD_GET,
    PP_CONSOLE_CMD_CAD_SET,
    PP_CONSOLE_CMD_IMU_GET,
} pp_console_command_kind_t;

typedef struct
{
    pp_console_command_kind_t kind;
    uint8_t cad_value;
} pp_console_command_t;

bool pp_console_parse_command(const char *text, pp_console_command_t *out);

#endif
