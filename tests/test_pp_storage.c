#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pp_protocol.h"
#include "pp_storage.h"

static void make_valid_payload(uint8_t *payload, uint16_t *out_len) {
    uint16_t body_len;
    uint16_t crc;

    uint8_t template_payload[] = {
        0x50, 0x50, 0x01, 0x01, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x01, 0x04, 0x04, 0x00, 0x01, 0x02
    };

    memcpy(payload, template_payload, sizeof(template_payload));
    body_len = (uint16_t)(sizeof(template_payload) - PP_PROTOCOL_HEADER_SIZE);
    payload[6] = (uint8_t)(body_len & 0xFF);
    payload[7] = (uint8_t)(body_len >> 8);
    crc = pp_protocol_crc16(payload + PP_PROTOCOL_HEADER_SIZE, body_len);
    payload[8] = (uint8_t)(crc & 0xFF);
    payload[9] = (uint8_t)(crc >> 8);
    *out_len = (uint16_t)sizeof(template_payload);
}

static void test_save_load_valid_pipeline(void) {
    uint8_t payload[64];
    uint8_t loaded[64];
    uint16_t len = 0;
    uint16_t loaded_len = 0;

    pp_storage_clear();
    make_valid_payload(payload, &len);

    assert(pp_storage_save_pipeline(payload, len));
    assert(pp_storage_has_valid_pipeline());
    assert(pp_storage_load_pipeline(loaded, sizeof(loaded), &loaded_len));
    assert(loaded_len == len);
    assert(memcmp(payload, loaded, len) == 0);
    printf("  PASS: test_save_load_valid_pipeline\n");
}

static void test_reject_corrupt_pipeline(void) {
    uint8_t payload[64];
    uint16_t len = 0;

    pp_storage_clear();
    make_valid_payload(payload, &len);
    payload[PP_PROTOCOL_HEADER_SIZE] ^= 0x7F;

    assert(!pp_storage_save_pipeline(payload, len));
    assert(!pp_storage_has_valid_pipeline());
    printf("  PASS: test_reject_corrupt_pipeline\n");
}

static void test_clear_removes_pipeline(void) {
    uint8_t payload[64];
    uint16_t len = 0;

    pp_storage_clear();
    make_valid_payload(payload, &len);
    assert(pp_storage_save_pipeline(payload, len));
    pp_storage_clear();
    assert(!pp_storage_has_valid_pipeline());
    printf("  PASS: test_clear_removes_pipeline\n");
}

int main(void) {
    printf("test_pp_storage:\n");
    test_save_load_valid_pipeline();
    test_reject_corrupt_pipeline();
    test_clear_removes_pipeline();
    printf("All tests passed.\n");
    return 0;
}
