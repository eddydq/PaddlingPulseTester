#include "paddling_pulse_console_commands.h"

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

bool pp_console_parse_command(const char *text, pp_console_command_t *out)
{
    if ((text == NULL) || (out == NULL))
    {
        return false;
    }

    out->kind = PP_CONSOLE_CMD_NONE;
    out->cad_value = 0;

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

    return false;
}
