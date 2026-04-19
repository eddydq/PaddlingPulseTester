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

static void test_parses_imu_set_auto(void)
{
    pp_console_command_t command = {0};

    assert(pp_console_parse_command("AT+IMU=AUTO", &command));
    assert(command.kind == PP_CONSOLE_CMD_IMU_SET);
    assert(command.imu_target == PP_IMU_OVERRIDE_AUTO);
}

static void test_parses_imu_set_lis3dh(void)
{
    pp_console_command_t command = {0};

    assert(pp_console_parse_command("AT+IMU=LIS3DH", &command));
    assert(command.kind == PP_CONSOLE_CMD_IMU_SET);
    assert(command.imu_target == PP_IMU_OVERRIDE_LIS3DH);
}

static void test_parses_imu_set_polar(void)
{
    pp_console_command_t command = {0};

    assert(pp_console_parse_command("AT+IMU=POLAR", &command));
    assert(command.kind == PP_CONSOLE_CMD_IMU_SET);
    assert(command.imu_target == PP_IMU_OVERRIDE_POLAR);
}

static void test_parses_imu_set_case_insensitively(void)
{
    pp_console_command_t command = {0};

    assert(pp_console_parse_command("AT+IMU=auto", &command));
    assert(command.imu_target == PP_IMU_OVERRIDE_AUTO);

    assert(pp_console_parse_command("AT+IMU=Polar", &command));
    assert(command.imu_target == PP_IMU_OVERRIDE_POLAR);
}

static void test_rejects_invalid_imu_set_target(void)
{
    pp_console_command_t command = {0};

    assert(!pp_console_parse_command("AT+IMU=BOGUS", &command));
    assert(!pp_console_parse_command("AT+IMU=", &command));
    assert(!pp_console_parse_command("AT+IMU=123", &command));
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
    test_parses_imu_set_auto();
    test_parses_imu_set_lis3dh();
    test_parses_imu_set_polar();
    test_parses_imu_set_case_insensitively();
    test_rejects_invalid_imu_set_target();
    test_rejects_unknown_command();
    return 0;
}
