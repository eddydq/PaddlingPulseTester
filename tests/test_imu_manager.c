#include <assert.h>
#include <stdio.h>

#include "paddling_pulse_imu_manager.h"
#include "paddling_pulse_imu_manager_logic.h"

static void test_boot_auto_finds_polar(void)
{
    pp_imu_state_t next;

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING,
        PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_POLAR_STREAMING);
    assert(next == PP_IMU_STATE_POLAR_ACTIVE);
}

static void test_boot_auto_times_out_to_lis3dh(void)
{
    pp_imu_state_t next;

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING,
        PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_BOOT_SCAN_TIMEOUT);
    assert(next == PP_IMU_STATE_LIS3DH_ACTIVE);
}

static void test_polar_override_never_falls_back(void)
{
    pp_imu_state_t next;

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING,
        PP_IMU_OVERRIDE_POLAR,
        PP_IMU_EV_BOOT_SCAN_TIMEOUT);
    assert(next == PP_IMU_STATE_POLAR_SEEKING);

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING,
        PP_IMU_OVERRIDE_POLAR,
        PP_IMU_EV_RECONNECT_TIMEOUT);
    assert(next == PP_IMU_STATE_POLAR_SEEKING);
}

static void test_polar_active_reconnect_within_window(void)
{
    pp_imu_state_t next;

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_ACTIVE,
        PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_POLAR_DISCONNECT);
    assert(next == PP_IMU_STATE_POLAR_SEEKING);

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING,
        PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_POLAR_STREAMING);
    assert(next == PP_IMU_STATE_POLAR_ACTIVE);
}

static void test_polar_active_reconnect_timeout_auto(void)
{
    pp_imu_state_t next;

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING,
        PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_RECONNECT_TIMEOUT);
    assert(next == PP_IMU_STATE_LIS3DH_ACTIVE);
}

static void test_switch_needed_on_source_change(void)
{
    assert(pp_imu_manager_logic_switch_needed(
        PP_IMU_LIS3DH, 100, PP_IMU_POLAR, 52));
}

static void test_switch_not_needed_on_same_source_and_rate(void)
{
    assert(!pp_imu_manager_logic_switch_needed(
        PP_IMU_POLAR, 52, PP_IMU_POLAR, 52));
}

static void test_switch_needed_on_rate_change_same_source(void)
{
    assert(pp_imu_manager_logic_switch_needed(
        PP_IMU_POLAR, 52, PP_IMU_POLAR, 104));
}

static void test_rate_clamped_to_bounds(void)
{
    assert(pp_imu_manager_logic_clamp_rate(0) == 1);
    assert(pp_imu_manager_logic_clamp_rate(52) == 52);
    assert(pp_imu_manager_logic_clamp_rate(5000) == 1000);
}

static void test_switch_sequence_stop_before_store_init(void)
{
    pp_imu_lifecycle_step_t sequence[PP_IMU_LIFECYCLE_STEPS];

    pp_imu_manager_logic_fill_switch_sequence(sequence);

    assert(sequence[0] == PP_IMU_LIFECYCLE_STOP_OLD);
    assert(sequence[1] == PP_IMU_LIFECYCLE_SAMPLE_STORE_INIT);
    assert(sequence[2] == PP_IMU_LIFECYCLE_STROKE_RATE_INIT);
    assert(sequence[3] == PP_IMU_LIFECYCLE_START_NEW);
}

int main(void)
{
    test_boot_auto_finds_polar();
    test_boot_auto_times_out_to_lis3dh();
    test_polar_override_never_falls_back();
    test_polar_active_reconnect_within_window();
    test_polar_active_reconnect_timeout_auto();
    test_switch_needed_on_source_change();
    test_switch_not_needed_on_same_source_and_rate();
    test_switch_needed_on_rate_change_same_source();
    test_rate_clamped_to_bounds();
    test_switch_sequence_stop_before_store_init();
    printf("imu manager logic tests passed\n");
    return 0;
}
