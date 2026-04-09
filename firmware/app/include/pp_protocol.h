#ifndef PP_PROTOCOL_H
#define PP_PROTOCOL_H

#include <stdint.h>

#define PP_PROTOCOL_MAGIC       0x5050U
#define PP_PROTOCOL_VERSION     1U
#define PP_PROTOCOL_HEADER_SIZE 12U

#define PP_TLV_BLOCK 0x01U
#define PP_TLV_EDGE  0x02U
#define PP_TLV_DATA  0x03U

enum {
    PP_PROTO_OK = 0,
    PP_PROTO_ERR_MAGIC = 1,
    PP_PROTO_ERR_CRC = 2,
    PP_PROTO_ERR_TRUNCATED = 3,
    PP_PROTO_ERR_VERSION = 4
};

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t block_count;
    uint8_t edge_count;
    uint8_t flags;
    uint16_t body_length;
    uint16_t params_crc;
    uint16_t reserved;
} pp_protocol_header_t;

typedef struct {
    uint8_t tag;
    uint8_t length;
    const uint8_t *value;
} pp_tlv_record_t;

uint8_t pp_protocol_parse_header(
    const uint8_t *data,
    uint16_t len,
    pp_protocol_header_t *out_header
);

uint8_t pp_protocol_parse_tlv(
    const uint8_t *data,
    uint16_t len,
    pp_tlv_record_t *out_record,
    uint16_t *out_consumed
);

uint16_t pp_protocol_crc16(const uint8_t *data, uint16_t len);

uint8_t pp_protocol_validate(
    const uint8_t *data,
    uint16_t len,
    pp_protocol_header_t *out_header
);

#endif /* PP_PROTOCOL_H */
