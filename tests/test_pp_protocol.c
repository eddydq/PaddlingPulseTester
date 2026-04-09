#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "pp_protocol.h"

/* Test: validate a well-formed header */
static void test_header_valid(void) {
    uint8_t buf[] = {
        0x50, 0x50,
        0x01,
        0x02,
        0x01,
        0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00
    };

    pp_protocol_header_t hdr;
    assert(pp_protocol_parse_header(buf, sizeof(buf), &hdr) == PP_PROTO_OK);
    assert(hdr.version == 1);
    assert(hdr.block_count == 2);
    assert(hdr.edge_count == 1);
    printf("  PASS: test_header_valid\n");
}

/* Test: reject bad magic */
static void test_header_bad_magic(void) {
    uint8_t buf[12] = {0xFF, 0xFF};
    pp_protocol_header_t hdr;
    assert(pp_protocol_parse_header(buf, 12, &hdr) == PP_PROTO_ERR_MAGIC);
    printf("  PASS: test_header_bad_magic\n");
}

/* Test: parse a TLV block record */
static void test_tlv_block_record(void) {
    uint8_t tlv[] = {0x01, 0x04, 0x04, 0x00, 0x01, 0x02};
    pp_tlv_record_t rec;
    uint16_t consumed = 0;
    assert(pp_protocol_parse_tlv(tlv, sizeof(tlv), &rec, &consumed) == PP_PROTO_OK);
    assert(rec.tag == 0x01);
    assert(rec.length == 4);
    assert(rec.value[0] == 0x04);
    assert(rec.value[1] == 0x00);
    assert(consumed == 6);
    printf("  PASS: test_tlv_block_record\n");
}

/* Test: parse a TLV edge record */
static void test_tlv_edge_record(void) {
    uint8_t tlv[] = {0x02, 0x04, 0x00, 0x00, 0x01, 0x00};
    pp_tlv_record_t rec;
    uint16_t consumed = 0;
    assert(pp_protocol_parse_tlv(tlv, sizeof(tlv), &rec, &consumed) == PP_PROTO_OK);
    assert(rec.tag == 0x02);
    assert(rec.length == 4);
    printf("  PASS: test_tlv_edge_record\n");
}

/* Test: CRC-16 round-trip */
static void test_crc16(void) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t crc = pp_protocol_crc16(data, sizeof(data));
    assert(crc != 0);
    assert(pp_protocol_crc16(data, sizeof(data)) == crc);
    data[0] = 0xFF;
    assert(pp_protocol_crc16(data, sizeof(data)) != crc);
    printf("  PASS: test_crc16\n");
}

int main(void) {
    printf("test_pp_protocol:\n");
    test_header_valid();
    test_header_bad_magic();
    test_tlv_block_record();
    test_tlv_edge_record();
    test_crc16();
    printf("All tests passed.\n");
    return 0;
}
