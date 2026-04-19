/**
 ****************************************************************************************
 *
 * @file paddling_pulse_console_io.c
 *
 * @brief Shared single-wire UART console output helpers.
 *
 ****************************************************************************************
 */

#include "da14531_config_basic.h"
#include "paddling_pulse_console_io.h"

#if defined(CFG_PADDLING_PULSE_CONSOLE_MODE)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "uart.h"

#define PADDLING_PULSE_CONSOLE_PRINTF_MAX_LEN  (128)

void paddling_pulse_console_write(const char *text)
{
    if ((text == NULL) || (text[0] == '\0'))
    {
        return;
    }

#if defined(CFG_UART_ONE_WIRE_SUPPORT)
    uart_one_wire_tx_en(UART1);
#endif
    uart_send(UART1, (const uint8_t *)text, (uint16_t)strlen(text), UART_OP_BLOCKING);
    uart_wait_tx_finish(UART1);
#if defined(CFG_UART_ONE_WIRE_SUPPORT)
    uart_one_wire_rx_en(UART1);
#endif
}

void paddling_pulse_console_printf(const char *fmt, ...)
{
    char buffer[PADDLING_PULSE_CONSOLE_PRINTF_MAX_LEN];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written <= 0)
    {
        return;
    }

    buffer[sizeof(buffer) - 1] = '\0';
    paddling_pulse_console_write(buffer);
}

#else

void paddling_pulse_console_write(const char *text)
{
    (void)text;
}

void paddling_pulse_console_printf(const char *fmt, ...)
{
    (void)fmt;
}

#endif
