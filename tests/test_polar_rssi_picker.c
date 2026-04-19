#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "paddling_pulse_imu_polar_rssi.h"

static void test_no_candidates_returns_none(void)
{
    pp_polar_rssi_picker_t picker;

    pp_polar_rssi_picker_init(&picker);
    assert(pp_polar_rssi_picker_count(&picker) == 0);
    assert(!pp_polar_rssi_picker_best(&picker, NULL));
}

static void test_single_candidate_always_wins(void)
{
    pp_polar_rssi_picker_t picker;
    pp_polar_candidate_t best;
    uint8_t addr[PP_POLAR_ADDR_LEN] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

    pp_polar_rssi_picker_init(&picker);
    pp_polar_rssi_picker_add(&picker, addr, 0, -80);

    assert(pp_polar_rssi_picker_best(&picker, &best));
    assert(memcmp(best.addr, addr, PP_POLAR_ADDR_LEN) == 0);
    assert(best.rssi == -80);
}

static void test_highest_rssi_wins(void)
{
    pp_polar_rssi_picker_t picker;
    pp_polar_candidate_t best;
    uint8_t a[PP_POLAR_ADDR_LEN] = { 1, 1, 1, 1, 1, 1 };
    uint8_t b[PP_POLAR_ADDR_LEN] = { 2, 2, 2, 2, 2, 2 };
    uint8_t c[PP_POLAR_ADDR_LEN] = { 3, 3, 3, 3, 3, 3 };

    pp_polar_rssi_picker_init(&picker);
    pp_polar_rssi_picker_add(&picker, a, 0, -85);
    pp_polar_rssi_picker_add(&picker, b, 0, -60);
    pp_polar_rssi_picker_add(&picker, c, 0, -75);

    assert(pp_polar_rssi_picker_best(&picker, &best));
    assert(best.rssi == -60);
    assert(memcmp(best.addr, b, PP_POLAR_ADDR_LEN) == 0);
}

static void test_duplicate_address_updates_rssi(void)
{
    pp_polar_rssi_picker_t picker;
    pp_polar_candidate_t best;
    uint8_t addr[PP_POLAR_ADDR_LEN] = { 1, 1, 1, 1, 1, 1 };

    pp_polar_rssi_picker_init(&picker);
    pp_polar_rssi_picker_add(&picker, addr, 0, -90);
    pp_polar_rssi_picker_add(&picker, addr, 0, -70);

    assert(pp_polar_rssi_picker_count(&picker) == 1);
    assert(pp_polar_rssi_picker_best(&picker, &best));
    assert(best.rssi == -70);
}

static void test_overflow_drops_new(void)
{
    pp_polar_rssi_picker_t picker;
    pp_polar_candidate_t best;
    uint8_t overflow[PP_POLAR_ADDR_LEN] = { 0xFE, 0, 0, 0, 0, 0 };
    int i;

    pp_polar_rssi_picker_init(&picker);
    for (i = 0; i < PP_POLAR_MAX_CANDIDATES; ++i)
    {
        uint8_t addr[PP_POLAR_ADDR_LEN] = { (uint8_t)i, 0, 0, 0, 0, 0 };
        pp_polar_rssi_picker_add(&picker, addr, 0, (int8_t)(-90 + i));
    }

    assert(pp_polar_rssi_picker_count(&picker) == PP_POLAR_MAX_CANDIDATES);
    pp_polar_rssi_picker_add(&picker, overflow, 0, -30);
    assert(pp_polar_rssi_picker_count(&picker) == PP_POLAR_MAX_CANDIDATES);
    assert(pp_polar_rssi_picker_best(&picker, &best));
    assert(best.addr[0] != 0xFE);
}

int main(void)
{
    test_no_candidates_returns_none();
    test_single_candidate_always_wins();
    test_highest_rssi_wins();
    test_duplicate_address_updates_rssi();
    test_overflow_drops_new();
    printf("polar rssi picker tests passed\n");
    return 0;
}
