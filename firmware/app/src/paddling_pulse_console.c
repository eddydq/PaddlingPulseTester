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
#include "paddling_pulse_console_commands.h"
#include "paddling_pulse_console_io.h"
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
    paddling_pulse_console_write(reply);
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
extern bool imu_active;

#ifdef CFG_IMU_DUAL
static const char *const paddling_pulse_console_imu_override_names[] = {
    "AUTO",
    "LIS3DH",
    "POLAR",
};

static const char *const paddling_pulse_console_imu_source_names[] = {
    "NONE",
    "LIS3DH",
    "POLAR",
};

static const char *const paddling_pulse_console_imu_state_names[] = {
    "IDLE",
    "SEEKING",
    "STREAMING",
    "ACTIVE",
};

static const char *paddling_pulse_console_imu_override_name(pp_imu_override_t override)
{
    if ((uint8_t)override > (uint8_t)PP_IMU_OVERRIDE_POLAR)
    {
        override = PP_IMU_OVERRIDE_AUTO;
    }

    return paddling_pulse_console_imu_override_names[override];
}

static const char *paddling_pulse_console_imu_source_name(pp_imu_source_t source)
{
    if ((uint8_t)source > (uint8_t)PP_IMU_POLAR)
    {
        source = PP_IMU_NONE;
    }

    return paddling_pulse_console_imu_source_names[source];
}

static const char *paddling_pulse_console_imu_state_name(pp_imu_state_t state)
{
    if ((uint8_t)state > (uint8_t)PP_IMU_STATE_LIS3DH_ACTIVE)
    {
        state = PP_IMU_STATE_IDLE;
    }

    return paddling_pulse_console_imu_state_names[state];
}
#endif

static void paddling_pulse_console_reply_cad(void)
{
    char reply[PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN];
    uint8_t algo_rpm = pp_stroke_rate_get_rpm();

    snprintf(reply, sizeof(reply),
             "\r\n+CAD:%u,algo=%u\r\nOK\r\n",
             current_cadence_rpm, algo_rpm);
    paddling_pulse_console_send_reply(reply);
}

static void paddling_pulse_console_reply_imu(void)
{
    char reply[PADDLING_PULSE_CONSOLE_REPLY_MAX_LEN];

#ifdef CFG_IMU_DUAL
    snprintf(reply, sizeof(reply),
             "\r\n+IMU: override=%s,source=%s,state=%s,rate=%u\r\nOK\r\n",
             paddling_pulse_console_imu_override_name(pp_imu_manager_get_override()),
             paddling_pulse_console_imu_source_name(pp_imu_manager_get_source()),
             paddling_pulse_console_imu_state_name(pp_imu_manager_get_state()),
             pp_imu_manager_get_rate_hz());
#else
    snprintf(reply, sizeof(reply),
             "\r\n+IMU:%s,running=%u,samples=%u,rate=%uHz\r\nOK\r\n",
             pp_imu_get_name(),
             imu_active ? pp_imu_is_running() : 0,
             pp_sample_store_get_count(),
             pp_sample_store_get_rate_hz());
#endif
    paddling_pulse_console_send_reply(reply);
}

static void paddling_pulse_console_set_imu(pp_imu_override_t target)
{
#ifdef CFG_IMU_DUAL
    if (pp_imu_manager_set_override(target))
    {
        paddling_pulse_console_send_reply("\r\nOK\r\n");
    }
    else
    {
        paddling_pulse_console_reply_error();
    }
#else
    (void)target;
    paddling_pulse_console_reply_error();
#endif
}

static void paddling_pulse_console_process(char *command, bool overflow)
{
    pp_console_command_t parsed = {0};

    paddling_pulse_console_trim(command);

    if (overflow || !pp_console_parse_command(command, &parsed))
    {
        paddling_pulse_console_reply_error();
        return;
    }

    switch (parsed.kind)
    {
    case PP_CONSOLE_CMD_BATT_GET:
        paddling_pulse_console_reply_batt();
        return;
    case PP_CONSOLE_CMD_IOCFG_GET:
        paddling_pulse_console_reply_iocfg();
        return;
    case PP_CONSOLE_CMD_CAD_GET:
        paddling_pulse_console_reply_cad();
        return;
    case PP_CONSOLE_CMD_CAD_SET:
        current_cadence_rpm = parsed.cad_value;
        paddling_pulse_console_send_reply("\r\nOK\r\n");
        return;
    case PP_CONSOLE_CMD_IMU_GET:
        paddling_pulse_console_reply_imu();
        return;
    case PP_CONSOLE_CMD_IMU_SET:
        paddling_pulse_console_set_imu(parsed.imu_target);
        return;
    default:
        paddling_pulse_console_reply_error();
        return;
    }
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
