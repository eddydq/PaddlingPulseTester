#include "pp_pipeline_service.h"

#include <string.h>
#include "pp_graph.h"
#include "pp_protocol.h"
#include "pp_storage.h"

static uint8_t s_status;
static uint8_t s_expected_seq;
static uint16_t s_rx_len;

static void reset_transfer(uint8_t status)
{
    s_status = status;
    s_expected_seq = 0;
    s_rx_len = 0;
}

void pp_pipeline_service_init(void)
{
    reset_transfer(PP_SVC_STATUS_IDLE);
}

uint8_t pp_pipeline_service_status(void)
{
    return s_status;
}

static uint8_t validate_and_save(uint16_t len)
{
    pp_protocol_header_t header;
    pp_graph_t graph;
    uint16_t capacity = 0;
    uint8_t *data = pp_storage_pipeline_write_buffer(&capacity);

    if (!data || len > capacity || pp_protocol_validate(data, len, &header) != PP_PROTO_OK) {
        return PP_SVC_STATUS_ERR_CRC;
    }
    if (pp_graph_build_from_binary(data, len, &graph) != PP_OK) {
        return PP_SVC_STATUS_ERR_GRAPH;
    }
    if (pp_graph_validate_ports(&graph) != PP_OK) {
        return PP_SVC_STATUS_ERR_GRAPH;
    }
    if (pp_graph_topo_sort(&graph) != PP_OK) {
        return PP_SVC_STATUS_ERR_GRAPH;
    }
    if (!pp_storage_commit_pipeline(len)) {
        return PP_SVC_STATUS_ERR_CRC;
    }

    (void)header;
    return PP_SVC_STATUS_VALID_RESET;
}

void pp_pipeline_service_on_write(const uint8_t *data, uint16_t len)
{
    uint8_t seq;
    uint8_t flags;
    const uint8_t *payload;
    uint16_t payload_len;
    uint16_t capacity = 0;
    uint8_t *rx_buf;

    if (!data || len < 2U) {
        reset_transfer(PP_SVC_STATUS_ERR_GRAPH);
        return;
    }

    seq = data[0];
    flags = data[1];
    payload = &data[2];
    payload_len = (uint16_t)(len - 2U);

    if ((flags & PP_CHUNK_FLAG_ABORT) != 0U) {
        reset_transfer(PP_SVC_STATUS_IDLE);
        return;
    }

    if ((flags & PP_CHUNK_FLAG_FIRST) != 0U) {
        pp_storage_begin_pipeline_write();
        s_expected_seq = 0;
        s_rx_len = 0;
        s_status = PP_SVC_STATUS_RECEIVING;
    }

    if (s_status != PP_SVC_STATUS_RECEIVING || seq != s_expected_seq) {
        reset_transfer(PP_SVC_STATUS_ERR_GRAPH);
        return;
    }

    rx_buf = pp_storage_pipeline_write_buffer(&capacity);
    if (!rx_buf || (uint32_t)s_rx_len + payload_len > capacity) {
        reset_transfer(PP_SVC_STATUS_ERR_TOO_LARGE);
        return;
    }

    memcpy(&rx_buf[s_rx_len], payload, payload_len);
    s_rx_len = (uint16_t)(s_rx_len + payload_len);

    if ((flags & PP_CHUNK_FLAG_LAST) != 0U) {
        uint8_t result_status = validate_and_save(s_rx_len);
        reset_transfer(result_status);
        return;
    }

    s_expected_seq++;
    s_status = PP_SVC_STATUS_RECEIVING;
}
