#include <string.h>

#include "paddling_pulse_imu_polar_rssi.h"

void pp_polar_rssi_picker_init(pp_polar_rssi_picker_t *picker)
{
    if (picker == NULL)
    {
        return;
    }

    memset(picker, 0, sizeof(*picker));
}

bool pp_polar_rssi_picker_add(pp_polar_rssi_picker_t *picker,
                              const uint8_t addr[PP_POLAR_ADDR_LEN],
                              uint8_t addr_type,
                              int8_t rssi)
{
    uint8_t i;

    if ((picker == NULL) || (addr == NULL))
    {
        return false;
    }

    for (i = 0; i < picker->count; ++i)
    {
        if (memcmp(picker->items[i].addr, addr, PP_POLAR_ADDR_LEN) == 0)
        {
            picker->items[i].rssi = rssi;
            picker->items[i].addr_type = addr_type;
            return true;
        }
    }

    if (picker->count >= PP_POLAR_MAX_CANDIDATES)
    {
        return false;
    }

    memcpy(picker->items[picker->count].addr, addr, PP_POLAR_ADDR_LEN);
    picker->items[picker->count].addr_type = addr_type;
    picker->items[picker->count].rssi = rssi;
    picker->count++;
    return true;
}

uint8_t pp_polar_rssi_picker_count(const pp_polar_rssi_picker_t *picker)
{
    if (picker == NULL)
    {
        return 0u;
    }

    return picker->count;
}

bool pp_polar_rssi_picker_best(const pp_polar_rssi_picker_t *picker,
                               pp_polar_candidate_t *out)
{
    uint8_t best_index = 0;
    uint8_t i;

    if ((picker == NULL) || (picker->count == 0u))
    {
        return false;
    }

    for (i = 1; i < picker->count; ++i)
    {
        if (picker->items[i].rssi > picker->items[best_index].rssi)
        {
            best_index = i;
        }
    }

    if (out != NULL)
    {
        *out = picker->items[best_index];
    }

    return true;
}
