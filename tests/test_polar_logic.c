#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "paddling_pulse_imu_polar_logic.h"

static void test_prefers_52hz_16bit_8g_xyz(void)
{
    const uint8_t input[] = {
        0x00, 0x03, 25, 0x00, 52, 0x00, 100, 0x00,
        0x01, 0x02, 8, 0x00, 16, 0x00,
        0x02, 0x02, 4, 0x00, 8, 0x00,
        0x04, 0x02, 0x01, 0x03,
    };
    pp_polar_acc_settings_t out;

    memset(&out, 0, sizeof(out));
    assert(pp_polar_parse_acc_settings(input, sizeof(input), &out));
    assert(out.sample_rate_hz == 52);
    assert(out.tlv_len == 15);
    assert(out.tlv_count == 4);

    assert(out.tlvs[0] == 0x00);
    assert(out.tlvs[1] == 0x01);
    assert(out.tlvs[2] == 52);
    assert(out.tlvs[3] == 0x00);

    assert(out.tlvs[4] == 0x01);
    assert(out.tlvs[5] == 0x01);
    assert(out.tlvs[6] == 16);
    assert(out.tlvs[7] == 0x00);

    assert(out.tlvs[8] == 0x02);
    assert(out.tlvs[9] == 0x01);
    assert(out.tlvs[10] == 8);
    assert(out.tlvs[11] == 0x00);

    assert(out.tlvs[12] == 0x04);
    assert(out.tlvs[13] == 0x01);
    assert(out.tlvs[14] == 3);
}

static void test_handles_mixed_width_acc_settings(void)
{
    const uint8_t input[] = {
        0x00, 0x03, 25, 0x00, 52, 0x00, 100, 0x00,
        0x01, 0x02, 8, 0x00, 16, 0x00,
        0x03, 0x01, 0x40, 0x1F, 0x00, 0x00,
        0x04, 0x02, 0x01, 0x03,
    };
    pp_polar_acc_settings_t out;

    memset(&out, 0, sizeof(out));
    assert(pp_polar_parse_acc_settings(input, sizeof(input), &out));
    assert(out.sample_rate_hz == 52);
    assert(out.tlv_len == 17);
    assert(out.tlv_count == 4);

    assert(out.tlvs[0] == 0x00);
    assert(out.tlvs[1] == 0x01);
    assert(out.tlvs[2] == 52);
    assert(out.tlvs[3] == 0x00);

    assert(out.tlvs[4] == 0x01);
    assert(out.tlvs[5] == 0x01);
    assert(out.tlvs[6] == 16);
    assert(out.tlvs[7] == 0x00);

    assert(out.tlvs[8] == 0x03);
    assert(out.tlvs[9] == 0x01);
    assert(out.tlvs[10] == 0x40);
    assert(out.tlvs[11] == 0x1F);
    assert(out.tlvs[12] == 0x00);
    assert(out.tlvs[13] == 0x00);

    assert(out.tlvs[14] == 0x04);
    assert(out.tlvs[15] == 0x01);
    assert(out.tlvs[16] == 0x03);
}

static void test_claims_only_the_matching_disconnect(void)
{
    assert(pp_polar_disconnect_is_owned(0x1234, 0x1234, false));
    assert(!pp_polar_disconnect_is_owned(0x1234, 0x1235, false));
    assert(!pp_polar_disconnect_is_owned(0x1234, 0x1234, true));
    assert(!pp_polar_disconnect_is_owned(0x0000, 0x0000, false));
}

static void test_rejects_malformed_acc_settings(void)
{
    const uint8_t partial_only[] = { 0x00 };
    const uint8_t truncated[] = { 0x00, 0x02, 0x34 };
    const uint8_t zero_count[] = { 0x00, 0x00 };
    const uint8_t trailing_byte[] = { 0x00, 0x01, 52, 0x00, 0xFF };
    const uint8_t overflow[] = {
        0x00, 0x01, 52, 0x00,
        0x01, 0x01, 16, 0x00,
        0x02, 0x01, 8, 0x00,
        0x04, 0x01, 3, 0x00,
        0x05, 0x01, 1, 0x00,
        0x06, 0x01, 2, 0x00,
        0x07, 0x01, 3, 0x00,
    };
    pp_polar_acc_settings_t out;

    memset(&out, 0xA5, sizeof(out));
    assert(!pp_polar_parse_acc_settings(partial_only, sizeof(partial_only), &out));
    assert(!pp_polar_parse_acc_settings(truncated, sizeof(truncated), &out));
    assert(!pp_polar_parse_acc_settings(zero_count, sizeof(zero_count), &out));
    assert(!pp_polar_parse_acc_settings(trailing_byte, sizeof(trailing_byte), &out));
    assert(!pp_polar_parse_acc_settings(overflow, sizeof(overflow), &out));
}

static void test_cancel_actions_distinguish_stop_vs_retry(void)
{
    assert(pp_polar_cancel_action(false, false) == PP_POLAR_CANCEL_NONE);
    assert(pp_polar_cancel_action(true, false) == PP_POLAR_CANCEL_RESET);
    assert(pp_polar_cancel_action(false, true) == PP_POLAR_CANCEL_RETRY);
    assert(pp_polar_cancel_action(true, true) == PP_POLAR_CANCEL_RESET);
}

static void test_scan_complete_actions_honor_stop_and_state(void)
{
    assert(pp_polar_scan_complete_action(true, false, true) ==
           PP_POLAR_SCAN_COMPLETE_NONE);
    assert(pp_polar_scan_complete_action(true, true, false) ==
           PP_POLAR_SCAN_COMPLETE_NONE);
    assert(pp_polar_scan_complete_action(false, true, false) ==
           PP_POLAR_SCAN_COMPLETE_DEFER_CONNECT);
    assert(pp_polar_scan_complete_action(false, false, true) ==
           PP_POLAR_SCAN_COMPLETE_RETRY);
    assert(pp_polar_scan_complete_action(false, false, false) ==
           PP_POLAR_SCAN_COMPLETE_NONE);
}

static void test_stop_completion_actions_distinguish_reset_and_retry(void)
{
    assert(pp_polar_stop_completion_action(true) ==
           PP_POLAR_STOP_COMPLETION_RESET);
    assert(pp_polar_stop_completion_action(false) ==
           PP_POLAR_STOP_COMPLETION_RETRY);
}

static void test_cancel_reset_is_blocked_once_connected(void)
{
    assert(pp_polar_cancel_should_reset(true, false));
    assert(!pp_polar_cancel_should_reset(true, true));
    assert(!pp_polar_cancel_should_reset(false, false));
}

int main(void)
{
    test_prefers_52hz_16bit_8g_xyz();
    test_handles_mixed_width_acc_settings();
    test_claims_only_the_matching_disconnect();
    test_rejects_malformed_acc_settings();
    test_cancel_actions_distinguish_stop_vs_retry();
    test_scan_complete_actions_honor_stop_and_state();
    test_stop_completion_actions_distinguish_reset_and_retry();
    test_cancel_reset_is_blocked_once_connected();
    return 0;
}
