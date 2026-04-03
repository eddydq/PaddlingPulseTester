/**
 ****************************************************************************************
 *
 * @file paddling_pulse_console.c
 *
 * @brief Minimal local AT console for PaddlingPulse.
 *
 ****************************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include "app_task.h"
#include "battery.h"
#include "ke_msg.h"
#include "paddling_pulse_board.h"
#include "paddling_pulse_console.h"
#include "paddling_pulse_imu.h"
#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_stroke_rate.h"
#include "uart.h"

#if defined(CFG_PADDLING_PULSE_AT_COMMANDS)

#define PADDLING_PULSE_CONSOLE_CMD_MAX_LEN    (64)
#define PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN  (128)
#define PADDLING_PULSE_CONSOLE_MSG_ID         (KE_FIRST_MSG(TASK_APP) + 0x80)

struct paddling_pulse_console_cmd
{
    uint16_t length;
    bool overflow;
    char text[__ARRAY_EMPTY];
};

static struct
{
    uint8_t rx_char;
    uint8_t length;
    bool overflow;
    char buffer[PADDLING_PULSE_CONSOLE_CMD_MAX_LEN];
} paddling_pulse_console_env __SECTION_ZERO("retention_mem_area0");

static void paddling_pulse_console_rx_cb(uint16_t status);

static void paddling_pulse_console_arm_rx(void)
{
    uart_register_rx_cb(UART1, paddling_pulse_console_rx_cb);
    uart_receive(UART1, &paddling_pulse_console_env.rx_char, 1, UART_OP_INTR);
}

static void paddling_pulse_console_send_reply(const char *reply)
{
#if defined(CFG_UART_ONE_WIRE_SUPPORT)
    uart_one_wire_tx_en(UART1);
#endif
    uart_send(UART1, (const uint8_t *)reply, (uint16_t)strlen(reply), UART_OP_BLOCKING);
    uart_wait_tx_finish(UART1);
#if defined(CFG_UART_ONE_WIRE_SUPPORT)
    uart_one_wire_rx_en(UART1);
#endif
}

static void paddling_pulse_console_reset_input(void)
{
    paddling_pulse_console_env.length = 0;
    paddling_pulse_console_env.overflow = false;
}

static void paddling_pulse_console_queue_command(void)
{
    uint16_t payload_len = paddling_pulse_console_env.overflow ? 1 : (uint16_t)paddling_pulse_console_env.length + 1;
    struct paddling_pulse_console_cmd *msg = KE_MSG_ALLOC_DYN(PADDLING_PULSE_CONSOLE_MSG_ID,
                                                              TASK_APP,
                                                              TASK_APP,
                                                              paddling_pulse_console_cmd,
                                                              payload_len);

    msg->length = paddling_pulse_console_env.overflow ? 0 : paddling_pulse_console_env.length;
    msg->overflow = paddling_pulse_console_env.overflow;

    if (paddling_pulse_console_env.overflow)
    {
        msg->text[0] = '\0';
    }
    else
    {
        memcpy(msg->text, paddling_pulse_console_env.buffer, paddling_pulse_console_env.length);
        msg->text[paddling_pulse_console_env.length] = '\0';
    }

    ke_msg_send(msg);
}

static void paddling_pulse_console_trim(char *text)
{
    size_t start = 0;
    size_t end = strlen(text);

    while ((text[start] == ' ') || (text[start] == '\t'))
    {
        start++;
    }

    while ((end > start) && ((text[end - 1] == ' ') || (text[end - 1] == '\t')))
    {
        end--;
    }

    if (start != 0)
    {
        memmove(text, text + start, end - start);
    }

    text[end - start] = '\0';
}

static char paddling_pulse_console_upper(char ch)
{
    if ((ch >= 'a') && (ch <= 'z'))
    {
        return (char)(ch - ('a' - 'A'));
    }

    return ch;
}

static bool paddling_pulse_console_match_query(const char *command, const char *token)
{
    while ((*command != '\0') && (*token != '\0'))
    {
        if (paddling_pulse_console_upper(*command) != paddling_pulse_console_upper(*token))
        {
            return false;
        }

        command++;
        token++;
    }

    if (*token != '\0')
    {
        return false;
    }

    return (*command == '\0') || ((*command == '?') && (command[1] == '\0'));
}

static void paddling_pulse_console_reply_error(void)
{
    paddling_pulse_console_send_reply("\r\nERROR\r\n");
}

static void paddling_pulse_console_reply_batt(void)
{
    char reply[PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN];
    uint8_t level = battery_get_lvl(BATT_CR2032);
    uint16_t voltage_mv = battery_get_voltage(BATT_CR2032);

    snprintf(reply, sizeof(reply), "\r\n+BATT:%u,%umV\r\nOK\r\n", level, voltage_mv);
    paddling_pulse_console_send_reply(reply);
}

static void paddling_pulse_console_reply_iocfg(void)
{
    char reply[PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN];

#if PRODUCTION_DEBUG_OUTPUT
    snprintf(reply,
             sizeof(reply),
             "\r\n+IOCFG:UART1_SW=P0_5,SPI_EN=P0_1,SPI_CLK=P0_4,SPI_DO=P0_0,SPI_DI=P0_3,PROD_DBG=P0_11\r\nOK\r\n");
#else
    snprintf(reply,
             sizeof(reply),
             "\r\n+IOCFG:UART1_SW=P0_5,SPI_EN=P0_1,SPI_CLK=P0_4,SPI_DO=P0_0,SPI_DI=P0_3\r\nOK\r\n");
#endif
    paddling_pulse_console_send_reply(reply);
}

extern uint8_t current_cadence_rpm;
extern bool csc_meas_ntf_enabled;
extern bool imu_active;

static bool paddling_pulse_console_match_set(const char *command,
                                             const char *token,
                                             const char **arg)
{
    while (*command && *token)
    {
        if (paddling_pulse_console_upper(*command) != paddling_pulse_console_upper(*token))
            return false;
        command++;
        token++;
    }
    if (*token) return false;
    if (*command == '=')
    {
        *arg = command + 1;
        return true;
    }
    return false;
}

static void paddling_pulse_console_reply_cad(void)
{
    char reply[PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN];
    uint8_t algo_rpm = pp_stroke_rate_get_rpm();
    snprintf(reply, sizeof(reply),
             "\r\n+CAD:%u,algo=%u\r\nOK\r\n",
             current_cadence_rpm, algo_rpm);
    paddling_pulse_console_send_reply(reply);
}

static void paddling_pulse_console_set_cad(const char *arg)
{
    int val = 0;
    while (*arg >= '0' && *arg <= '9')
    {
        val = val * 10 + (*arg - '0');
        arg++;
    }
    if (val > 255) val = 255;
    current_cadence_rpm = (uint8_t)val;

    char reply[PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN];
    snprintf(reply, sizeof(reply), "\r\nOK\r\n");
    paddling_pulse_console_send_reply(reply);
}

static void paddling_pulse_console_reply_imu(void)
{
    char reply[PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN];
    snprintf(reply, sizeof(reply),
             "\r\n+IMU:%s,running=%u,samples=%u,rate=%uHz\r\nOK\r\n",
             pp_imu_get_name(),
             imu_active ? pp_imu_is_running() : 0,
             pp_sample_store_get_count(),
             pp_sample_store_get_rate_hz());
    paddling_pulse_console_send_reply(reply);
}

static void paddling_pulse_console_process(char *command, bool overflow)
{
    paddling_pulse_console_trim(command);

    if (overflow || (command[0] == '\0'))
    {
        paddling_pulse_console_reply_error();
        return;
    }

    if (paddling_pulse_console_match_query(command, "AT+BATT"))
    {
        paddling_pulse_console_reply_batt();
        return;
    }

    if (paddling_pulse_console_match_query(command, "AT+IOCFG"))
    {
        paddling_pulse_console_reply_iocfg();
        return;
    }

    const char *arg = NULL;
    if (paddling_pulse_console_match_set(command, "AT+CAD", &arg))
    {
        paddling_pulse_console_set_cad(arg);
        return;
    }

    if (paddling_pulse_console_match_query(command, "AT+CAD"))
    {
        paddling_pulse_console_reply_cad();
        return;
    }

    if (paddling_pulse_console_match_query(command, "AT+IMU"))
    {
        paddling_pulse_console_reply_imu();
        return;
    }

    paddling_pulse_console_reply_error();
}

static void paddling_pulse_console_rx_cb(uint16_t status)
{
    char rx = (char)paddling_pulse_console_env.rx_char;

    (void)status;

    if ((rx == '\r') || (rx == '\n'))
    {
        if (paddling_pulse_console_env.overflow || (paddling_pulse_console_env.length != 0))
        {
            paddling_pulse_console_queue_command();
            paddling_pulse_console_reset_input();
        }
    }
    else if ((rx == '\b') || (rx == 0x7F))
    {
        if (paddling_pulse_console_env.length != 0)
        {
            paddling_pulse_console_env.length--;
        }
    }
    else if (!paddling_pulse_console_env.overflow)
    {
        if (paddling_pulse_console_env.length < (PADDLING_PULSE_CONSOLE_CMD_MAX_LEN - 1))
        {
            paddling_pulse_console_env.buffer[paddling_pulse_console_env.length++] = rx;
        }
        else
        {
            paddling_pulse_console_env.overflow = true;
        }
    }

    paddling_pulse_console_arm_rx();
}

void paddling_pulse_console_init(void)
{
    memset(&paddling_pulse_console_env, 0, sizeof(paddling_pulse_console_env));

#if defined(CFG_UART_ONE_WIRE_SUPPORT)
    uart_one_wire_rx_en(UART1);
#endif

    while (uart_data_ready_getf(UART1))
    {
        (void)uart_read_byte(UART1);
    }

    paddling_pulse_console_arm_rx();
}

bool paddling_pulse_console_handle_message(ke_msg_id_t const msgid, void const *param)
{
    if (msgid == PADDLING_PULSE_CONSOLE_MSG_ID)
    {
        struct paddling_pulse_console_cmd const *cmd = (struct paddling_pulse_console_cmd const *)param;
        char command[PADDLING_PULSE_CONSOLE_CMD_MAX_LEN];

        memcpy(command, cmd->text, cmd->length + 1);
        paddling_pulse_console_process(command, cmd->overflow);
        return true;
    }

    return false;
}

#else

void paddling_pulse_console_init(void)
{
}

bool paddling_pulse_console_handle_message(ke_msg_id_t const msgid, void const *param)
{
    (void)msgid;
    (void)param;
    return false;
}

#endif // CFG_PADDLING_PULSE_AT_COMMANDS
