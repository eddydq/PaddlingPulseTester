#include "paddling_pulse_console_commands.h"

#include <stddef.h>

static char pp_console_upper(char ch)
{
    if ((ch >= 'a') && (ch <= 'z'))
    {
        return (char)(ch - ('a' - 'A'));
    }

    return ch;
}

static bool pp_console_token_equals(const char *text, const char *token)
{
    while ((*text != '\0') && (*token != '\0'))
    {
        if (pp_console_upper(*text) != pp_console_upper(*token))
        {
            return false;
        }

        ++text;
        ++token;
    }

    if (*token != '\0')
    {
        return false;
    }

    return (*text == '\0') || ((*text == '?') && (text[1] == '\0'));
}

static bool pp_console_parse_cad_set(const char *text, pp_console_command_t *out)
{
    const char *cursor = text + 7;
    uint16_t value = 0;

    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        value = (uint16_t)((value * 10) + (uint16_t)(*cursor - '0'));
        ++cursor;
    }

    if ((cursor == text + 7) || (*cursor != '\0'))
    {
        return false;
    }

    if (value > 255u)
    {
        value = 255u;
    }

    out->kind = PP_CONSOLE_CMD_CAD_SET;
    out->cad_value = (uint8_t)value;
    return true;
}

static bool pp_console_parse_imu_target(const char *arg,
                                        size_t len,
                                        pp_imu_override_t *out)
{
    if ((arg == NULL) || (out == NULL))
    {
        return false;
    }

    if ((len == 4u) &&
        (pp_console_upper(arg[0]) == 'A') &&
        (pp_console_upper(arg[1]) == 'U') &&
        (pp_console_upper(arg[2]) == 'T') &&
        (pp_console_upper(arg[3]) == 'O'))
    {
        *out = PP_IMU_OVERRIDE_AUTO;
        return true;
    }

    if ((len == 6u) &&
        (pp_console_upper(arg[0]) == 'L') &&
        (pp_console_upper(arg[1]) == 'I') &&
        (pp_console_upper(arg[2]) == 'S') &&
        (pp_console_upper(arg[3]) == '3') &&
        (pp_console_upper(arg[4]) == 'D') &&
        (pp_console_upper(arg[5]) == 'H'))
    {
        *out = PP_IMU_OVERRIDE_LIS3DH;
        return true;
    }

    if ((len == 5u) &&
        (pp_console_upper(arg[0]) == 'P') &&
        (pp_console_upper(arg[1]) == 'O') &&
        (pp_console_upper(arg[2]) == 'L') &&
        (pp_console_upper(arg[3]) == 'A') &&
        (pp_console_upper(arg[4]) == 'R'))
    {
        *out = PP_IMU_OVERRIDE_POLAR;
        return true;
    }

    return false;
}

static bool pp_console_parse_imu_set(const char *text, pp_console_command_t *out)
{
    const char *cursor = text + 7;
    size_t len = 0;
    pp_imu_override_t target;

    while (cursor[len] != '\0')
    {
        ++len;
    }

    if ((len == 0) || !pp_console_parse_imu_target(cursor, len, &target))
    {
        return false;
    }

    out->kind = PP_CONSOLE_CMD_IMU_SET;
    out->imu_target = target;
    return true;
}

bool pp_console_parse_command(const char *text, pp_console_command_t *out)
{
    if ((text == NULL) || (out == NULL))
    {
        return false;
    }

    out->kind = PP_CONSOLE_CMD_NONE;
    out->cad_value = 0;
    out->imu_target = PP_IMU_OVERRIDE_AUTO;

    if (pp_console_token_equals(text, "AT+BATT"))
    {
        out->kind = PP_CONSOLE_CMD_BATT_GET;
        return true;
    }

    if (pp_console_token_equals(text, "AT+IOCFG"))
    {
        out->kind = PP_CONSOLE_CMD_IOCFG_GET;
        return true;
    }

    if (pp_console_token_equals(text, "AT+CAD"))
    {
        out->kind = PP_CONSOLE_CMD_CAD_GET;
        return true;
    }

    if (pp_console_token_equals(text, "AT+IMU"))
    {
        out->kind = PP_CONSOLE_CMD_IMU_GET;
        return true;
    }

    if ((pp_console_upper(text[0]) == 'A') &&
        (pp_console_upper(text[1]) == 'T') &&
        (text[2] == '+') &&
        (pp_console_upper(text[3]) == 'C') &&
        (pp_console_upper(text[4]) == 'A') &&
        (pp_console_upper(text[5]) == 'D') &&
        (text[6] == '='))
    {
        return pp_console_parse_cad_set(text, out);
    }

    if ((pp_console_upper(text[0]) == 'A') &&
        (pp_console_upper(text[1]) == 'T') &&
        (text[2] == '+') &&
        (pp_console_upper(text[3]) == 'I') &&
        (pp_console_upper(text[4]) == 'M') &&
        (pp_console_upper(text[5]) == 'U') &&
        (text[6] == '='))
    {
        return pp_console_parse_imu_set(text, out);
    }

    return false;
}
