#include "paddling_pulse_imu_polar_logic.h"

static uint16_t pp_polar_read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint16_t pp_polar_preferred_value(uint8_t type,
                                         const uint8_t *values,
                                         uint8_t value_count)
{
    uint16_t preferred;
    uint8_t i;

    switch (type)
    {
    case 0x00:
        preferred = 52;
        break;
    case 0x01:
        preferred = 16;
        break;
    case 0x02:
        preferred = 8;
        break;
    case 0x04:
        preferred = 3;
        break;
    default:
        return pp_polar_read_u16_le(values);
    }

    for (i = 0; i < value_count; ++i)
    {
        uint16_t value = pp_polar_read_u16_le(&values[(uint16_t)i * 2]);
        if (value == preferred)
        {
            return value;
        }
    }

    return pp_polar_read_u16_le(values);
}

bool pp_polar_parse_acc_settings(const uint8_t *data,
                                 uint16_t len,
                                 pp_polar_acc_settings_t *out)
{
    uint16_t pos;

    if (out == NULL || data == NULL)
    {
        return false;
    }

    out->sample_rate_hz = 52;
    out->tlv_len = 0;

    pos = 0;
    while (pos + 2u <= len)
    {
        uint8_t type = data[pos];
        uint8_t value_count = data[pos + 1];
        uint16_t value_bytes = (uint16_t)value_count * 2u;
        uint16_t next_pos = (uint16_t)(pos + 2u + value_bytes);
        uint16_t selected;

        if (next_pos > len)
        {
            return false;
        }

        selected = pp_polar_preferred_value(type, &data[pos + 2], value_count);
        if (type == 0x00)
        {
            out->sample_rate_hz = selected;
        }

        if ((uint16_t)out->tlv_len + 4u > PP_POLAR_ACC_TLV_MAX_LEN)
        {
            return false;
        }

        out->tlv[out->tlv_len++] = type;
        out->tlv[out->tlv_len++] = 0x01;
        out->tlv[out->tlv_len++] = (uint8_t)(selected & 0xFFu);
        out->tlv[out->tlv_len++] = (uint8_t)(selected >> 8);

        pos = next_pos;
    }

    return true;
}

bool pp_polar_disconnect_is_owned(uint16_t owned_conhdl,
                                  uint16_t disconnect_conhdl)
{
    return (owned_conhdl != 0u) && (owned_conhdl == disconnect_conhdl);
}

pp_polar_cancel_action_t pp_polar_cancel_action(bool stop_requested,
                                                bool retry_requested)
{
    if (stop_requested)
    {
        return PP_POLAR_CANCEL_ACTION_RESET;
    }

    if (retry_requested)
    {
        return PP_POLAR_CANCEL_ACTION_RETRY;
    }

    return PP_POLAR_CANCEL_ACTION_NONE;
}
