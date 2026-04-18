#include "pp_protocol.h"

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

uint8_t pp_protocol_parse_header(
    const uint8_t *data,
    uint16_t len,
    pp_protocol_header_t *out_header)
{
    pp_protocol_header_t header;

    if (!data || !out_header || len < PP_PROTOCOL_HEADER_SIZE) {
        return PP_PROTO_ERR_TRUNCATED;
    }

    header.magic = read_u16_le(&data[0]);
    if (header.magic != PP_PROTOCOL_MAGIC) {
        return PP_PROTO_ERR_MAGIC;
    }

    header.version = data[2];
    if (header.version != PP_PROTOCOL_VERSION) {
        return PP_PROTO_ERR_VERSION;
    }

    header.block_count = data[3];
    header.edge_count = data[4];
    header.flags = data[5];
    header.body_length = read_u16_le(&data[6]);
    header.params_crc = read_u16_le(&data[8]);
    header.reserved = read_u16_le(&data[10]);

    if ((uint32_t)PP_PROTOCOL_HEADER_SIZE + header.body_length > len) {
        return PP_PROTO_ERR_TRUNCATED;
    }

    *out_header = header;
    return PP_PROTO_OK;
}

uint8_t pp_protocol_parse_tlv(
    const uint8_t *data,
    uint16_t len,
    pp_tlv_record_t *out_record,
    uint16_t *out_consumed)
{
    uint8_t length;

    if (!data || !out_record || !out_consumed || len < 2U) {
        return PP_PROTO_ERR_TRUNCATED;
    }

    length = data[1];
    if ((uint16_t)(2U + length) > len) {
        return PP_PROTO_ERR_TRUNCATED;
    }

    out_record->tag = data[0];
    out_record->length = length;
    out_record->value = &data[2];
    *out_consumed = (uint16_t)(2U + length);

    return PP_PROTO_OK;
}

uint16_t pp_protocol_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;

    if (!data && len > 0U) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        uint8_t bit;
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

uint8_t pp_protocol_validate(
    const uint8_t *data,
    uint16_t len,
    pp_protocol_header_t *out_header)
{
    pp_protocol_header_t header;
    uint8_t status;
    const uint8_t *body;
    uint16_t crc;

    status = pp_protocol_parse_header(data, len, &header);
    if (status != PP_PROTO_OK) {
        return status;
    }

    body = &data[PP_PROTOCOL_HEADER_SIZE];
    crc = pp_protocol_crc16(body, header.body_length);
    if (crc != header.params_crc) {
        return PP_PROTO_ERR_CRC;
    }

    if (out_header) {
        *out_header = header;
    }
    return PP_PROTO_OK;
}
