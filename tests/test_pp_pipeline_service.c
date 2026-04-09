#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pp_pipeline_service.h"
#include "pp_protocol.h"
#include "pp_storage.h"

static uint16_t make_valid_graph(uint8_t *payload) {
    uint16_t body_len;
    uint16_t crc;
    uint8_t template_payload[] = {
        0x50, 0x50, 0x01, 0x02, 0x01, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x01, 0x04, 0x04, 0x00, 0x01, 0x02,
        0x01, 0x09, 0x08, 0x01, 0x06, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x80,
        0x02, 0x04, 0x00, 0x00, 0x01, 0x00
    };

    memcpy(payload, template_payload, sizeof(template_payload));
    body_len = (uint16_t)(sizeof(template_payload) - PP_PROTOCOL_HEADER_SIZE);
    payload[6] = (uint8_t)(body_len & 0xFF);
    payload[7] = (uint8_t)(body_len >> 8);
    crc = pp_protocol_crc16(payload + PP_PROTOCOL_HEADER_SIZE, body_len);
    payload[8] = (uint8_t)(crc & 0xFF);
    payload[9] = (uint8_t)(crc >> 8);
    return (uint16_t)sizeof(template_payload);
}

static void send_frame(uint8_t seq, uint8_t flags, const uint8_t *payload, uint16_t len) {
    uint8_t frame[80];
    assert(len + 2U <= sizeof(frame));
    frame[0] = seq;
    frame[1] = flags;
    memcpy(frame + 2, payload, len);
    pp_pipeline_service_on_write(frame, (uint16_t)(len + 2U));
}

static void test_chunked_upload_saves_pipeline(void) {
    uint8_t payload[80];
    uint16_t len = make_valid_graph(payload);

    pp_storage_clear();
    pp_pipeline_service_init();
    send_frame(0, PP_CHUNK_FLAG_FIRST, payload, 12);
    assert(pp_pipeline_service_status() == PP_SVC_STATUS_RECEIVING);
    send_frame(1, PP_CHUNK_FLAG_LAST, payload + 12, (uint16_t)(len - 12U));
    assert(pp_pipeline_service_status() == PP_SVC_STATUS_VALID_RESET);
    assert(pp_storage_has_valid_pipeline());
    printf("  PASS: test_chunked_upload_saves_pipeline\n");
}

static void test_corrupt_upload_sets_crc_error(void) {
    uint8_t payload[80];
    uint16_t len = make_valid_graph(payload);

    pp_storage_clear();
    pp_pipeline_service_init();
    payload[PP_PROTOCOL_HEADER_SIZE] ^= 0x55;
    send_frame(0, (uint8_t)(PP_CHUNK_FLAG_FIRST | PP_CHUNK_FLAG_LAST), payload, len);
    assert(pp_pipeline_service_status() == PP_SVC_STATUS_ERR_CRC);
    assert(!pp_storage_has_valid_pipeline());
    printf("  PASS: test_corrupt_upload_sets_crc_error\n");
}

int main(void) {
    printf("test_pp_pipeline_service:\n");
    test_chunked_upload_saves_pipeline();
    test_corrupt_upload_sets_crc_error();
    printf("All tests passed.\n");
    return 0;
}
