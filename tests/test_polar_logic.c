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
        0x04, 0x02, 1, 0x00, 3, 0x00,
    };
    pp_polar_acc_settings_t out;

    memset(&out, 0, sizeof(out));
    assert(pp_polar_parse_acc_settings(input, sizeof(input), &out));
    assert(out.sample_rate_hz == 52);
    assert(out.tlv_len == 16);

    assert(out.tlv[0] == 0x00);
    assert(out.tlv[1] == 0x01);
    assert(out.tlv[2] == 52);
    assert(out.tlv[3] == 0x00);

    assert(out.tlv[4] == 0x01);
    assert(out.tlv[5] == 0x01);
    assert(out.tlv[6] == 16);
    assert(out.tlv[7] == 0x00);

    assert(out.tlv[8] == 0x02);
    assert(out.tlv[9] == 0x01);
    assert(out.tlv[10] == 8);
    assert(out.tlv[11] == 0x00);

    assert(out.tlv[12] == 0x04);
    assert(out.tlv[13] == 0x01);
    assert(out.tlv[14] == 3);
    assert(out.tlv[15] == 0x00);
}

static void test_claims_only_the_matching_disconnect(void)
{
    assert(pp_polar_disconnect_is_owned(0x1234, 0x1234));
    assert(!pp_polar_disconnect_is_owned(0x1234, 0x1235));
    assert(!pp_polar_disconnect_is_owned(0x0000, 0x0000));
}

static void test_cancel_actions_distinguish_stop_vs_retry(void)
{
    assert(pp_polar_cancel_action(false, false) == PP_POLAR_CANCEL_ACTION_NONE);
    assert(pp_polar_cancel_action(true, false) == PP_POLAR_CANCEL_ACTION_RESET);
    assert(pp_polar_cancel_action(false, true) == PP_POLAR_CANCEL_ACTION_RETRY);
}

int main(void)
{
    test_prefers_52hz_16bit_8g_xyz();
    test_claims_only_the_matching_disconnect();
    test_cancel_actions_distinguish_stop_vs_retry();
    return 0;
}
