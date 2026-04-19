#include "paddling_pulse_imu_polar_logic.h"

#define PP_POLAR_MAX_SETTING_VALUES 8

enum
{
    PP_POLAR_SET_SAMPLE_RATE = 0x00,
    PP_POLAR_SET_RESOLUTION = 0x01,
    PP_POLAR_SET_RANGE = 0x02,
    PP_POLAR_SET_RANGE_MILLIUNIT = 0x03,
    PP_POLAR_SET_CHANNELS = 0x04,
};

typedef struct
{
    uint8_t count;
    int32_t values[PP_POLAR_MAX_SETTING_VALUES];
} pp_polar_setting_list_t;

static uint8_t pp_polar_setting_field_size(uint8_t type)
{
    switch (type)
    {
    case PP_POLAR_SET_SAMPLE_RATE:
    case PP_POLAR_SET_RESOLUTION:
    case PP_POLAR_SET_RANGE:
        return 2;

    case PP_POLAR_SET_RANGE_MILLIUNIT:
        return 4;

    case PP_POLAR_SET_CHANNELS:
        return 1;

    default:
        return 0;
    }
}

static int32_t pp_polar_parse_signed_le(const uint8_t *data, uint8_t size)
{
    int32_t value = 0;
    uint8_t i;

    for (i = 0; i < size; ++i)
    {
        value |= ((int32_t)data[i]) << (8u * i);
    }

    if (size < 4u && (data[size - 1u] & 0x80u) != 0u)
    {
        value |= (int32_t)(-1) << (8u * size);
    }

    return value;
}

static void pp_polar_setting_list_add(pp_polar_setting_list_t *list,
                                      int32_t value)
{
    if (list != NULL && list->count < PP_POLAR_MAX_SETTING_VALUES)
    {
        list->values[list->count++] = value;
    }
}

static int32_t pp_polar_select_setting(const pp_polar_setting_list_t *list,
                                       int32_t preferred)
{
    uint8_t i;

    if (list == NULL || list->count == 0u)
    {
        return -1;
    }

    for (i = 0; i < list->count; ++i)
    {
        if (list->values[i] == preferred)
        {
            return list->values[i];
        }
    }

    return list->values[0];
}

static bool pp_polar_append_selected_tlv(pp_polar_acc_settings_t *out,
                                         uint8_t type,
                                         int32_t value)
{
    uint8_t field_size = pp_polar_setting_field_size(type);
    uint8_t i;

    if (out == NULL || field_size == 0u)
    {
        return false;
    }

    if ((uint16_t)out->tlv_len + 2u + field_size > PP_POLAR_ACC_TLV_MAX_LEN)
    {
        return false;
    }

    out->tlvs[out->tlv_len++] = type;
    out->tlvs[out->tlv_len++] = 0x01;

    for (i = 0; i < field_size; ++i)
    {
        out->tlvs[out->tlv_len++] = (uint8_t)((uint32_t)value >> (8u * i));
    }

    out->tlv_count++;
    return true;
}

bool pp_polar_parse_acc_settings(const uint8_t *data,
                                 uint16_t len,
                                 pp_polar_acc_settings_t *out)
{
    pp_polar_setting_list_t sample_rates = {0};
    pp_polar_setting_list_t resolutions = {0};
    pp_polar_setting_list_t ranges = {0};
    pp_polar_setting_list_t range_milliunits = {0};
    pp_polar_setting_list_t channels = {0};
    uint16_t pos;

    if (out == NULL || data == NULL)
    {
        return false;
    }

    out->sample_rate_hz = 52;
    out->tlv_count = 0;
    out->tlv_len = 0;

    pos = 0;
    while (pos + 2u <= len)
    {
        uint8_t type = data[pos];
        uint8_t value_count = data[pos + 1];
        uint8_t field_size = pp_polar_setting_field_size(type);
        uint16_t value_bytes = (uint16_t)value_count * field_size;
        uint16_t next_pos = (uint16_t)(pos + 2u + value_bytes);
        uint8_t i;

        if (value_count == 0u || field_size == 0u)
        {
            return false;
        }

        if (next_pos > len)
        {
            return false;
        }

        pos = (uint16_t)(pos + 2u);
        for (i = 0; i < value_count; ++i)
        {
            int32_t value = pp_polar_parse_signed_le(&data[pos], field_size);

            switch (type)
            {
            case PP_POLAR_SET_SAMPLE_RATE:
                pp_polar_setting_list_add(&sample_rates, value);
                break;

            case PP_POLAR_SET_RESOLUTION:
                pp_polar_setting_list_add(&resolutions, value);
                break;

            case PP_POLAR_SET_RANGE:
                pp_polar_setting_list_add(&ranges, value);
                break;

            case PP_POLAR_SET_RANGE_MILLIUNIT:
                pp_polar_setting_list_add(&range_milliunits, value);
                break;

            case PP_POLAR_SET_CHANNELS:
                pp_polar_setting_list_add(&channels, value);
                break;

            default:
                return false;
            }

            pos = (uint16_t)(pos + field_size);
        }

        if (pos != next_pos)
        {
            return false;
        }
    }

    if (pos != len)
    {
        return false;
    }

    if (sample_rates.count > 0u)
    {
        int32_t selected = pp_polar_select_setting(&sample_rates, 52);
        out->sample_rate_hz = (uint16_t)selected;
        if (!pp_polar_append_selected_tlv(out, PP_POLAR_SET_SAMPLE_RATE,
                                          selected))
        {
            return false;
        }
    }

    if (resolutions.count > 0u)
    {
        int32_t selected = pp_polar_select_setting(&resolutions, 16);
        if (!pp_polar_append_selected_tlv(out, PP_POLAR_SET_RESOLUTION,
                                          selected))
        {
            return false;
        }
    }

    if (ranges.count > 0u)
    {
        int32_t selected = pp_polar_select_setting(&ranges, 8);
        if (!pp_polar_append_selected_tlv(out, PP_POLAR_SET_RANGE, selected))
        {
            return false;
        }
    }
    else if (range_milliunits.count > 0u)
    {
        int32_t selected = pp_polar_select_setting(&range_milliunits, -1);
        if (!pp_polar_append_selected_tlv(out, PP_POLAR_SET_RANGE_MILLIUNIT,
                                          selected))
        {
            return false;
        }
    }

    if (channels.count > 0u)
    {
        int32_t selected = pp_polar_select_setting(&channels, 3);
        if (!pp_polar_append_selected_tlv(out, PP_POLAR_SET_CHANNELS, selected))
        {
            return false;
        }
    }

    return (pos == len);
}

bool pp_polar_disconnect_is_owned(uint16_t tracked_conhdl,
                                  uint16_t event_conhdl,
                                  bool idle)
{
    return !idle && (tracked_conhdl != 0u) && (tracked_conhdl == event_conhdl);
}

pp_polar_cancel_action_t pp_polar_cancel_action(bool stop_requested,
                                                bool retry_requested)
{
    if (stop_requested)
    {
        return PP_POLAR_CANCEL_RESET;
    }

    if (retry_requested)
    {
        return PP_POLAR_CANCEL_RETRY;
    }

    return PP_POLAR_CANCEL_NONE;
}

pp_polar_scan_complete_action_t pp_polar_scan_complete_action(
    bool stop_requested,
    bool connecting,
    bool scanning)
{
    if (stop_requested)
    {
        return PP_POLAR_SCAN_COMPLETE_NONE;
    }

    if (connecting)
    {
        return PP_POLAR_SCAN_COMPLETE_DEFER_CONNECT;
    }

    if (scanning)
    {
        return PP_POLAR_SCAN_COMPLETE_RETRY;
    }

    return PP_POLAR_SCAN_COMPLETE_NONE;
}

pp_polar_stop_completion_action_t pp_polar_stop_completion_action(
    bool stop_requested)
{
    return stop_requested ? PP_POLAR_STOP_COMPLETION_RESET
                          : PP_POLAR_STOP_COMPLETION_RETRY;
}

bool pp_polar_cancel_should_reset(bool stop_requested, bool connected)
{
    return stop_requested && !connected;
}
