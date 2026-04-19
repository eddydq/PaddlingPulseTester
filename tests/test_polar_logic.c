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
    assert(out.tlvs[15] == 0x00);
}

static void test_claims_only_the_matching_disconnect(void)
{
    assert(pp_polar_disconnect_is_owned(0x1234, 0x1234, false));
    assert(!pp_polar_disconnect_is_owned(0x1234, 0x1235, false));
    assert(!pp_polar_disconnect_is_owned(0x1234, 0x1234, true));
    assert(!pp_polar_disconnect_is_owned(0x0000, 0x0000, false));
}

static void test_cancel_actions_distinguish_stop_vs_retry(void)
{
    assert(pp_polar_cancel_action(false, false) == PP_POLAR_CANCEL_NONE);
    assert(pp_polar_cancel_action(true, false) == PP_POLAR_CANCEL_RESET);
    assert(pp_polar_cancel_action(false, true) == PP_POLAR_CANCEL_RETRY);
}

int main(void)
{
    test_prefers_52hz_16bit_8g_xyz();
    test_claims_only_the_matching_disconnect();
    test_cancel_actions_distinguish_stop_vs_retry();
    return 0;
}
