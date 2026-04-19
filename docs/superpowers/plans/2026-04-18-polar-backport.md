# Polar Backport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Backport the Polar central IMU path, required stability fixes, and console support onto the pre-blocks stroke-rate branch without introducing graph/block/OTA pipeline code.

**Architecture:** Keep the existing `paddling_pulse_app.c` stroke-rate flow (`IMU -> sample store -> autocorrelation -> Kalman -> CSCP`) intact, but backport the later Polar connection/config fixes around it. Extract only the pieces that are easy to test in isolation into small helpers: one helper for Polar PMD decision logic and settings selection, and one helper for console command parsing. Route all debug output through a shared one-wire UART writer so app and Polar logs behave the same way.

**Tech Stack:** C99 firmware on the Renesas DA14531 SDK, MinGW `gcc` for host-side unit tests, GNU Make / `mingw32-make` for firmware builds.

---

## File Map

- Create: `firmware/app/include/paddling_pulse_imu_polar_logic.h`
  - Testable helper API for PMD settings selection plus disconnect/cancel ownership decisions.
- Create: `firmware/app/src/paddling_pulse_imu_polar_logic.c`
  - Pure logic implementation with no DA14531 SDK dependencies.
- Create: `tests/test_polar_logic.c`
  - Host-side regression tests for PMD settings parsing and disconnect/cancel decisions.
- Create: `firmware/app/include/paddling_pulse_console_commands.h`
  - Testable console command parser API.
- Create: `firmware/app/src/paddling_pulse_console_commands.c`
  - Parses `AT+BATT`, `AT+IOCFG`, `AT+CAD`, and `AT+IMU` into command structs.
- Create: `tests/test_console_commands.c`
  - Host-side regression tests for console parsing.
- Create: `firmware/app/include/paddling_pulse_console_io.h`
  - Shared single-wire UART output helper declarations.
- Create: `firmware/app/src/paddling_pulse_console_io.c`
  - Shared `write` / `printf` wrappers used by the app, console, and Polar driver.
- Modify: `firmware/app/src/paddling_pulse_imu_polar.c`
  - Use the logic helper, complete PMD discovery/start flow, and apply disconnect/cancel fixes.
- Modify: `firmware/app/include/paddling_pulse_imu_polar.h`
  - Export `pp_imu_polar_on_connect_failed()`.
- Modify: `firmware/app/src/paddling_pulse_console.c`
  - Use the parser helper and shared console I/O helper.
- Modify: `firmware/app/src/paddling_pulse_app.c`
  - Keep phone connection ownership in the app and route logs through shared console I/O.
- Modify: `firmware/config/user_config.h`
  - Enable dual-role and MTU changes only when `CFG_IMU_POLAR` is defined.
- Modify: `firmware/config/user_callback_config.h`
  - Register Polar scan/connect callbacks only for Polar builds.
- Modify: `firmware/config/da14531_config_advanced.h`
  - Apply only the RAM/packet-length settings needed for stable dual-link behavior on this branch.
- Modify: `Makefile`
  - Compile the new helper source files.

## Task 1: Add a Testable Polar Logic Helper

**Files:**
- Create: `firmware/app/include/paddling_pulse_imu_polar_logic.h`
- Create: `firmware/app/src/paddling_pulse_imu_polar_logic.c`
- Create: `tests/test_polar_logic.c`

- [ ] **Step 1: Write the failing test**

```c
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "paddling_pulse_imu_polar_logic.h"

static void test_prefers_52hz_16bit_8g_xyz(void)
{
    static const uint8_t payload[] = {
        0x00, 0x02, 25, 0, 52, 0,
        0x01, 0x02, 8, 0, 16, 0,
        0x02, 0x02, 4, 0, 8, 0,
        0x04, 0x01, 3
    };
    static const uint8_t expected[] = {
        0x00, 0x01, 0x34, 0x00,
        0x01, 0x01, 0x10, 0x00,
        0x02, 0x01, 0x08, 0x00,
        0x04, 0x01, 0x03
    };
    pp_polar_acc_settings_t parsed = {0};

    assert(pp_polar_parse_acc_settings(payload, sizeof(payload), &parsed));
    assert(parsed.sample_rate_hz == 52);
    assert(parsed.tlv_len == sizeof(expected));
    assert(parsed.tlv_count == 4);
    assert(memcmp(parsed.tlvs, expected, sizeof(expected)) == 0);
}

static void test_claims_only_the_matching_disconnect(void)
{
    assert(pp_polar_disconnect_is_owned(0x1234, 0x1234, false));
    assert(!pp_polar_disconnect_is_owned(0x1234, 0x5678, false));
    assert(!pp_polar_disconnect_is_owned(0x1234, 0x1234, true));
}

static void test_cancel_actions_distinguish_stop_vs_retry(void)
{
    assert(pp_polar_cancel_action(true, false) == PP_POLAR_CANCEL_RESET);
    assert(pp_polar_cancel_action(false, true) == PP_POLAR_CANCEL_RETRY);
    assert(pp_polar_cancel_action(false, false) == PP_POLAR_CANCEL_NONE);
}

int main(void)
{
    test_prefers_52hz_16bit_8g_xyz();
    test_claims_only_the_matching_disconnect();
    test_cancel_actions_distinguish_stop_vs_retry();
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_polar_logic.c firmware/app/src/paddling_pulse_imu_polar_logic.c -o build/test_polar_logic.exe
```

Expected: FAIL with missing file errors for `paddling_pulse_imu_polar_logic.h` / `.c`.

- [ ] **Step 3: Write minimal implementation**

`firmware/app/include/paddling_pulse_imu_polar_logic.h`

```c
#ifndef _PADDLING_PULSE_IMU_POLAR_LOGIC_H_
#define _PADDLING_PULSE_IMU_POLAR_LOGIC_H_

#include <stdbool.h>
#include <stdint.h>

#define PP_POLAR_ACC_TLV_MAX_LEN 24

typedef struct
{
    uint16_t sample_rate_hz;
    uint8_t tlvs[PP_POLAR_ACC_TLV_MAX_LEN];
    uint8_t tlv_len;
    uint8_t tlv_count;
} pp_polar_acc_settings_t;

typedef enum
{
    PP_POLAR_CANCEL_NONE = 0,
    PP_POLAR_CANCEL_RESET,
    PP_POLAR_CANCEL_RETRY,
} pp_polar_cancel_action_t;

bool pp_polar_parse_acc_settings(const uint8_t *data, uint16_t len, pp_polar_acc_settings_t *out);
bool pp_polar_disconnect_is_owned(uint16_t tracked_conhdl, uint16_t event_conhdl, bool idle);
pp_polar_cancel_action_t pp_polar_cancel_action(bool stop_cancel_pending, bool still_connecting);

#endif
```

`firmware/app/src/paddling_pulse_imu_polar_logic.c`

```c
#include "paddling_pulse_imu_polar_logic.h"
#include <string.h>

enum
{
    POLAR_SET_SAMPLE_RATE = 0x00,
    POLAR_SET_RESOLUTION = 0x01,
    POLAR_SET_RANGE = 0x02,
    POLAR_SET_RANGE_MILLIUNIT = 0x03,
    POLAR_SET_CHANNELS = 0x04,
};

static uint8_t setting_field_size(uint8_t type)
{
    switch (type)
    {
    case POLAR_SET_SAMPLE_RATE:
    case POLAR_SET_RESOLUTION:
    case POLAR_SET_RANGE:
        return 2;
    case POLAR_SET_RANGE_MILLIUNIT:
        return 4;
    case POLAR_SET_CHANNELS:
        return 1;
    default:
        return 0;
    }
}

static int32_t parse_signed_le(const uint8_t *data, uint8_t size)
{
    int32_t value = 0;
    uint8_t i;
    for (i = 0; i < size; i++)
    {
        value |= (int32_t)data[i] << (i * 8);
    }
    if ((size < 4) && (value & (1 << ((size * 8) - 1))))
    {
        value |= ~((1 << (size * 8)) - 1);
    }
    return value;
}

bool pp_polar_parse_acc_settings(const uint8_t *data, uint16_t len, pp_polar_acc_settings_t *out)
{
    uint16_t pos = 0;

    if ((data == NULL) || (out == NULL))
    {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->sample_rate_hz = 52;

    while ((pos + 2) <= len)
    {
        uint8_t type = data[pos++];
        uint8_t count = data[pos++];
        uint8_t field_size = setting_field_size(type);

        if ((field_size == 0) || (pos + ((uint16_t)count * field_size) > len))
        {
            return false;
        }

        if ((type == POLAR_SET_SAMPLE_RATE) && (count > 0))
        {
            int32_t selected = parse_signed_le(&data[pos], field_size);
            uint8_t i;
            for (i = 0; i < count; i++)
            {
                int32_t candidate = parse_signed_le(&data[pos + (i * field_size)], field_size);
                if (candidate == 52)
                {
                    selected = candidate;
                    break;
                }
            }

            out->sample_rate_hz = (uint16_t)selected;
            out->tlvs[out->tlv_len++] = type;
            out->tlvs[out->tlv_len++] = 1;
            out->tlvs[out->tlv_len++] = (uint8_t)(selected & 0xFF);
            out->tlvs[out->tlv_len++] = (uint8_t)((selected >> 8) & 0xFF);
            out->tlv_count++;
        }

        pos += (uint16_t)count * field_size;
    }

    return true;
}

bool pp_polar_disconnect_is_owned(uint16_t tracked_conhdl, uint16_t event_conhdl, bool idle)
{
    if (idle)
    {
        return false;
    }
    return tracked_conhdl == event_conhdl;
}

pp_polar_cancel_action_t pp_polar_cancel_action(bool stop_cancel_pending, bool still_connecting)
{
    if (stop_cancel_pending)
    {
        return PP_POLAR_CANCEL_RESET;
    }
    if (still_connecting)
    {
        return PP_POLAR_CANCEL_RETRY;
    }
    return PP_POLAR_CANCEL_NONE;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_polar_logic.c firmware/app/src/paddling_pulse_imu_polar_logic.c -o build/test_polar_logic.exe
.\build\test_polar_logic.exe
```

Expected: PASS with exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add tests/test_polar_logic.c firmware/app/include/paddling_pulse_imu_polar_logic.h firmware/app/src/paddling_pulse_imu_polar_logic.c
git commit -m "test: add Polar logic helper regression coverage"
```

## Task 2: Backport Dual-Role Config and Stable Polar Driver Flow

**Files:**
- Modify: `firmware/app/src/paddling_pulse_imu_polar.c`
- Modify: `firmware/app/include/paddling_pulse_imu_polar.h`
- Modify: `firmware/config/user_config.h`
- Modify: `firmware/config/user_callback_config.h`
- Modify: `firmware/config/da14531_config_advanced.h`
- Modify: `Makefile`

- [ ] **Step 1: Reproduce the missing Polar integration through a failing firmware build**

First wire the new helper source into the build and add the later Polar callback declaration to the header:

`firmware/app/include/paddling_pulse_imu_polar.h`

```c
void pp_imu_polar_on_connect_failed(void);
```

`Makefile`

```make
	firmware/app/src/paddling_pulse_imu_polar_logic.c \
```

Run:

```powershell
mingw32-make build
```

Expected: FAIL with one or more of:
- missing `pp_imu_polar_on_connect_failed`
- missing `app_easy_gap_start_connection_to_set`
- missing shared console I/O symbols
- compile errors in `user_callback_config.h` after Polar callback wiring starts

- [ ] **Step 2: Apply the minimal config and callback wiring**

`firmware/config/user_config.h`

```c
#include "da14531_config_basic.h"

/* Replace only the role / mtu members inside user_gapm_conf with: */
#ifdef CFG_IMU_POLAR
    .role = GAP_ROLE_ALL,
    .max_mtu = 247,
#else
    .role = GAP_ROLE_PERIPHERAL,
    .max_mtu = 23,
#endif

/* Replace only the CE length members inside user_central_conf with: */
    .ce_len_min = 15,
    .ce_len_max = 15,
```

`firmware/config/user_callback_config.h`

```c
#include "da14531_config_basic.h"
#ifdef CFG_IMU_POLAR
#include "paddling_pulse_imu_polar.h"
#endif

static const struct app_callbacks user_app_callbacks = {
    .app_on_connection = user_app_connection,
    .app_on_disconnect = user_app_disconnect,
#ifdef CFG_IMU_POLAR
    .app_on_scanning_completed = pp_imu_polar_on_scan_complete,
    .app_on_adv_report_ind = pp_imu_polar_on_adv_report,
    .app_on_connect_failed = pp_imu_polar_on_connect_failed,
#else
    .app_on_scanning_completed = NULL,
    .app_on_adv_report_ind = NULL,
    .app_on_connect_failed = NULL,
#endif
    .app_on_get_dev_appearance = user_app_on_get_dev_appearance,
};
```

`firmware/config/da14531_config_advanced.h`

```c
#undef CFG_BLE_METRICS
#define CFG_MAX_TX_PACKET_LENGTH        (69)
#define CFG_MAX_RX_PACKET_LENGTH        (69)
#define CFG_RET_DATA_SIZE               (1860)
#define CFG_RET_DATA_UNINIT_SIZE        (12)
```

- [ ] **Step 3: Port the later stable Polar connection and PMD flow**

`firmware/app/src/paddling_pulse_imu_polar.c`

```c
#include "app_easy_gap.h"
#include "app_task.h"
#include "paddling_pulse_console_io.h"
#include "paddling_pulse_imu_polar_logic.h"

static bool s_stop_cancel_pending __SECTION_ZERO("retention_mem_area0");

static void polar_start_connection(void)
{
    if (s_polar.state != POLAR_CONNECTING) return;

    app_easy_gap_start_connection_to_set(
        s_polar.target_addr_type,
        (uint8_t *)&s_polar.target_addr,
        POLAR_CONNECT_INTV);
    app_easy_gap_start_connection_to();
    ke_state_set(TASK_APP, APP_CONNECTABLE);

    s_connect_timer = app_easy_timer(POLAR_CONNECT_TIMEOUT, polar_connect_timeout_cb);
}

void pp_imu_polar_on_connect_failed(void)
{
    if (s_polar.state != POLAR_CONNECTING) return;

    if (s_connect_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(s_connect_timer);
        s_connect_timer = EASY_TIMER_INVALID_TIMER;
    }

    ke_state_set(TASK_APP, APP_CONNECTED);

    if (pp_polar_cancel_action(s_stop_cancel_pending, true) == PP_POLAR_CANCEL_RESET)
    {
        s_stop_cancel_pending = false;
        polar_reset();
    }
    else
    {
        polar_retry_if_needed();
    }
}

static void polar_handle_cp_event(const uint8_t *value, uint16_t length)
{
    pp_polar_acc_settings_t parsed = {0};
    uint8_t opcode = value[1];
    uint8_t status = value[3];
    if ((opcode == POLAR_PMD_OP_GET_SETTINGS) && (status == POLAR_PMD_STATUS_SUCCESS))
    {
        if ((length > 5) && pp_polar_parse_acc_settings(value + 5, length - 5, &parsed))
        {
            s_polar.acc_sample_rate_hz = parsed.sample_rate_hz;
            memcpy(s_polar.acc_selected_tlvs, parsed.tlvs, parsed.tlv_len);
            s_polar.acc_selected_tlvs_len = parsed.tlv_len;
            s_polar.acc_selected_tlv_count = parsed.tlv_count;
        }
        s_polar.state = POLAR_START_ACC;
        polar_send_cp_cmd(POLAR_PMD_OP_START_MEAS, POLAR_PMD_MEAS_ACC,
                          s_polar.acc_selected_tlvs,
                          s_polar.acc_selected_tlvs_len,
                          POLAR_SEQ_START_ACC);
    }
}

static void polar_on_gattc_cmp(const struct gattc_cmp_evt *evt)
{
    bool discovery_cmp = (evt->operation == GATTC_DISC_BY_UUID_SVC) ||
                         (evt->operation == GATTC_DISC_ALL_CHAR) ||
                         (evt->operation == GATTC_DISC_DESC_CHAR);
    bool discovery_complete = discovery_cmp &&
                              ((evt->status == ATT_ERR_NO_ERROR) ||
                               (evt->status == ATT_ERR_ATTRIBUTE_NOT_FOUND));

    if (!discovery_complete &&
        evt->status != ATT_ERR_NO_ERROR &&
        evt->status != GAP_ERR_NO_ERROR)
    {
        polar_disconnect_setup_error();
        return;
    }
}

bool pp_imu_polar_on_disconnect(uint16_t conhdl)
{
    if (!pp_polar_disconnect_is_owned(s_polar.conhdl, conhdl, s_polar.state == POLAR_IDLE))
    {
        return false;
    }

    polar_retry_if_needed();
    return true;
}

case GAPM_CMP_EVT:
{
    const struct gapm_cmp_evt *evt = (const struct gapm_cmp_evt *)param;
    if (evt->operation == GAPM_CANCEL)
    {
        switch (pp_polar_cancel_action(s_stop_cancel_pending, s_polar.state == POLAR_CONNECTING))
        {
        case PP_POLAR_CANCEL_RESET:
            s_stop_cancel_pending = false;
            polar_reset();
            return true;
        case PP_POLAR_CANCEL_RETRY:
            polar_retry_if_needed();
            return true;
        default:
            break;
        }
    }
}
```

- [ ] **Step 4: Run logic tests and firmware build to verify it passes**

Run:

```powershell
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_polar_logic.c firmware/app/src/paddling_pulse_imu_polar_logic.c -o build/test_polar_logic.exe
.\build\test_polar_logic.exe
mingw32-make build
```

Expected:
- `test_polar_logic.exe` exits `0`
- firmware build succeeds and produces `build/PaddlingPulse.hex`

- [ ] **Step 5: Commit**

```bash
git add Makefile firmware/app/include/paddling_pulse_imu_polar.h firmware/app/src/paddling_pulse_imu_polar.c firmware/config/user_config.h firmware/config/user_callback_config.h firmware/config/da14531_config_advanced.h
git commit -m "feat: backport stable Polar central IMU flow"
```

## Task 3: Add a Testable Console Command Parser

**Files:**
- Create: `firmware/app/include/paddling_pulse_console_commands.h`
- Create: `firmware/app/src/paddling_pulse_console_commands.c`
- Create: `tests/test_console_commands.c`

- [ ] **Step 1: Write the failing test**

```c
#include <assert.h>
#include <stdint.h>
#include "paddling_pulse_console_commands.h"

static void test_parses_cad_query(void)
{
    pp_console_command_t command = {0};
    assert(pp_console_parse_command("AT+CAD?", &command));
    assert(command.kind == PP_CONSOLE_CMD_CAD_GET);
}

static void test_parses_cad_set_and_clamps(void)
{
    pp_console_command_t command = {0};
    assert(pp_console_parse_command("AT+CAD=999", &command));
    assert(command.kind == PP_CONSOLE_CMD_CAD_SET);
    assert(command.cad_value == 255);
}

static void test_parses_imu_query_case_insensitively(void)
{
    pp_console_command_t command = {0};
    assert(pp_console_parse_command("at+imu", &command));
    assert(command.kind == PP_CONSOLE_CMD_IMU_GET);
}

static void test_rejects_unknown_command(void)
{
    pp_console_command_t command = {0};
    assert(!pp_console_parse_command("AT+NOPE", &command));
}

int main(void)
{
    test_parses_cad_query();
    test_parses_cad_set_and_clamps();
    test_parses_imu_query_case_insensitively();
    test_rejects_unknown_command();
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_console_commands.c firmware/app/src/paddling_pulse_console_commands.c -o build/test_console_commands.exe
```

Expected: FAIL with missing file errors for `paddling_pulse_console_commands.h` / `.c`.

- [ ] **Step 3: Write minimal implementation**

`firmware/app/include/paddling_pulse_console_commands.h`

```c
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
```

`firmware/app/src/paddling_pulse_console_commands.c`

```c
#include "paddling_pulse_console_commands.h"

static char upper_char(char ch)
{
    if ((ch >= 'a') && (ch <= 'z'))
    {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static bool token_equals(const char *text, const char *token)
{
    while (*text && *token)
    {
        if (upper_char(*text) != upper_char(*token))
        {
            return false;
        }
        text++;
        token++;
    }
    return (*text == '\0' || (*text == '?' && text[1] == '\0')) && (*token == '\0');
}

bool pp_console_parse_command(const char *text, pp_console_command_t *out)
{
    int value = 0;

    if ((text == NULL) || (out == NULL))
    {
        return false;
    }

    out->kind = PP_CONSOLE_CMD_NONE;
    out->cad_value = 0;

    if (token_equals(text, "AT+BATT"))
    {
        out->kind = PP_CONSOLE_CMD_BATT_GET;
        return true;
    }
    if (token_equals(text, "AT+IOCFG"))
    {
        out->kind = PP_CONSOLE_CMD_IOCFG_GET;
        return true;
    }
    if (token_equals(text, "AT+CAD"))
    {
        out->kind = PP_CONSOLE_CMD_CAD_GET;
        return true;
    }
    if (token_equals(text, "AT+IMU"))
    {
        out->kind = PP_CONSOLE_CMD_IMU_GET;
        return true;
    }
    if (upper_char(text[0]) == 'A' && upper_char(text[1]) == 'T' && text[2] == '+' &&
        upper_char(text[3]) == 'C' && upper_char(text[4]) == 'A' && upper_char(text[5]) == 'D' && text[6] == '=')
    {
        const char *cursor = text + 7;
        while ((*cursor >= '0') && (*cursor <= '9'))
        {
            value = (value * 10) + (*cursor - '0');
            cursor++;
        }
        if (value > 255)
        {
            value = 255;
        }
        out->kind = PP_CONSOLE_CMD_CAD_SET;
        out->cad_value = (uint8_t)value;
        return true;
    }

    return false;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_console_commands.c firmware/app/src/paddling_pulse_console_commands.c -o build/test_console_commands.exe
.\build\test_console_commands.exe
```

Expected: PASS with exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add tests/test_console_commands.c firmware/app/include/paddling_pulse_console_commands.h firmware/app/src/paddling_pulse_console_commands.c
git commit -m "test: add console command parser coverage"
```

## Task 4: Wire Console Helpers and Backport `AT+CAD` / `AT+IMU`

**Files:**
- Create: `firmware/app/include/paddling_pulse_console_io.h`
- Create: `firmware/app/src/paddling_pulse_console_io.c`
- Modify: `firmware/app/src/paddling_pulse_console.c`
- Modify: `firmware/app/src/paddling_pulse_app.c`
- Modify: `firmware/app/src/paddling_pulse_imu_polar.c`
- Modify: `Makefile`

- [ ] **Step 1: Reproduce the missing shared console path through a failing firmware build**

Add the new source path to `Makefile` and replace one existing direct UART reply call with the shared symbol in `paddling_pulse_console.c`:

```make
	firmware/app/src/paddling_pulse_console_io.c \
	firmware/app/src/paddling_pulse_console_commands.c \
```

```c
static void paddling_pulse_console_send_reply(const char *reply)
{
    paddling_pulse_console_write(reply);
}
```

Run:

```powershell
mingw32-make build
```

Expected: FAIL with missing `paddling_pulse_console_write` / parser helper errors.

- [ ] **Step 2: Add the shared one-wire console I/O helper**

`firmware/app/include/paddling_pulse_console_io.h`

```c
#ifndef _PADDLING_PULSE_CONSOLE_IO_H_
#define _PADDLING_PULSE_CONSOLE_IO_H_

void paddling_pulse_console_write(const char *text);
void paddling_pulse_console_printf(const char *fmt, ...);

#endif
```

`firmware/app/src/paddling_pulse_console_io.c`

```c
#include "da14531_config_basic.h"
#include "paddling_pulse_console_io.h"

#if defined(CFG_PADDLING_PULSE_CONSOLE_MODE)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "uart.h"

#define PADDLING_PULSE_CONSOLE_PRINTF_MAX_LEN 128

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

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    paddling_pulse_console_write(buffer);
}

#endif
```

- [ ] **Step 3: Use the parser helper in the console and route app / Polar logs through shared I/O**

`firmware/app/src/paddling_pulse_console.c`

```c
#include "paddling_pulse_console_commands.h"
#include "paddling_pulse_console_io.h"
#include "paddling_pulse_imu.h"
#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_stroke_rate.h"

extern uint8_t current_cadence_rpm;
extern bool imu_active;

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
    default:
        paddling_pulse_console_reply_error();
        return;
    }
}
```

`firmware/app/src/paddling_pulse_app.c`

```c
#include "paddling_pulse_console_io.h"

#ifdef CFG_PADDLING_PULSE_CONSOLE_MODE
static void ble_log_cfg(const char *message, uint8_t conidx)
{
    paddling_pulse_console_printf("BLE: %s idx=%u\r\n", message, conidx);
}
#endif
```

Also replace the existing `arch_printf(...)` debug calls in `paddling_pulse_app.c` and `paddling_pulse_imu_polar.c` with `paddling_pulse_console_printf(...)`.

- [ ] **Step 4: Run parser tests and firmware build to verify it passes**

Run:

```powershell
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_console_commands.c firmware/app/src/paddling_pulse_console_commands.c -o build/test_console_commands.exe
.\build\test_console_commands.exe
mingw32-make build
```

Expected:
- `test_console_commands.exe` exits `0`
- firmware build succeeds and still produces `build/PaddlingPulse.hex`

- [ ] **Step 5: Commit**

```bash
git add Makefile firmware/app/include/paddling_pulse_console_io.h firmware/app/src/paddling_pulse_console_io.c firmware/app/src/paddling_pulse_console.c firmware/app/src/paddling_pulse_app.c firmware/app/src/paddling_pulse_imu_polar.c
git commit -m "feat: backport Polar console and shared UART logging"
```

## Task 5: Final Verification and Branch Cleanup

**Files:**
- Modify: none expected

- [ ] **Step 1: Run the two host-side test executables from scratch**

Run:

```powershell
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_polar_logic.c firmware/app/src/paddling_pulse_imu_polar_logic.c -o build/test_polar_logic.exe
.\build\test_polar_logic.exe
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_console_commands.c firmware/app/src/paddling_pulse_console_commands.c -o build/test_console_commands.exe
.\build\test_console_commands.exe
```

Expected: both executables exit `0`.

- [ ] **Step 2: Run the full firmware build**

Run:

```powershell
mingw32-make clean
mingw32-make build
```

Expected: clean rebuild succeeds and regenerates `build/PaddlingPulse.hex`.

- [ ] **Step 3: Verify the branch only contains the intended backport scope**

Run:

```powershell
git diff --stat 1625d9d..HEAD
git diff 1625d9d..HEAD -- firmware/app/src/paddling_pulse_app.c firmware/app/src/paddling_pulse_imu_polar.c firmware/app/src/paddling_pulse_console.c firmware/config/user_config.h firmware/config/user_callback_config.h firmware/config/da14531_config_advanced.h Makefile
```

Expected:
- only Polar/config/console/helper/test files changed
- no `pp_graph`, `pp_pipeline_service`, block-registry, or OTA pipeline files appear

- [ ] **Step 4: Commit the final implementation checkpoint**

```bash
git add Makefile firmware/app/include/paddling_pulse_imu_polar_logic.h firmware/app/src/paddling_pulse_imu_polar_logic.c firmware/app/include/paddling_pulse_console_commands.h firmware/app/src/paddling_pulse_console_commands.c firmware/app/include/paddling_pulse_console_io.h firmware/app/src/paddling_pulse_console_io.c firmware/app/include/paddling_pulse_imu_polar.h firmware/app/src/paddling_pulse_imu_polar.c firmware/app/src/paddling_pulse_console.c firmware/app/src/paddling_pulse_app.c firmware/config/user_config.h firmware/config/user_callback_config.h firmware/config/da14531_config_advanced.h tests/test_polar_logic.c tests/test_console_commands.c
git commit -m "feat: backport Polar support to stroke-rate app layer"
```

- [ ] **Step 5: Hand off for execution**

Use either:

```text
Subagent-Driven: one fresh worker per task with review after each commit.
```

or

```text
Inline Execution: execute Tasks 1-5 in this session with checkpoints after each commit.
```
