#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "pp_graph.h"
#include "pp_block.h"
#include "pp_protocol.h"

/* Test: topological sort of a linear 3-node graph */
static void test_topo_sort_linear(void) {
    pp_graph_t g = {0};
    g.node_count = 3;
    g.nodes[0].block_id = PP_BLOCK_SELECT_AXIS;
    g.nodes[1].block_id = PP_BLOCK_HPF_GRAVITY;
    g.nodes[2].block_id = PP_BLOCK_AUTOCORRELATION;

    g.edge_count = 2;
    g.edges[0] = (pp_edge_t){0, 0, 1, 0};
    g.edges[1] = (pp_edge_t){1, 0, 2, 0};

    assert(pp_graph_topo_sort(&g) == PP_OK);
    assert(g.exec_order[0] == 0);
    assert(g.exec_order[1] == 1);
    assert(g.exec_order[2] == 2);
    printf("  PASS: test_topo_sort_linear\n");
}

/* Test: detect cycle */
static void test_topo_sort_cycle(void) {
    pp_graph_t g = {0};
    g.node_count = 2;
    g.nodes[0].block_id = PP_BLOCK_HPF_GRAVITY;
    g.nodes[1].block_id = PP_BLOCK_LOWPASS;

    g.edge_count = 2;
    g.edges[0] = (pp_edge_t){0, 0, 1, 0};
    g.edges[1] = (pp_edge_t){1, 0, 0, 0};

    assert(pp_graph_topo_sort(&g) == PP_ERR);
    printf("  PASS: test_topo_sort_cycle\n");
}

/* Test: build graph from protocol binary */
static void test_graph_from_binary(void) {
    uint8_t payload[] = {
        0x50, 0x50, 0x01, 0x02, 0x01, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x01, 0x04, 0x04, 0x00, 0x01, 0x02,
        0x01, 0x05, 0x0C, 0x01, 0x02, 0x1E, 0xC8,
        0x02, 0x04, 0x00, 0x00, 0x01, 0x00
    };
    uint16_t body_len = (uint16_t)(sizeof(payload) - PP_PROTOCOL_HEADER_SIZE);
    uint16_t crc;
    pp_graph_t g = {0};

    payload[6] = (uint8_t)(body_len & 0xFF);
    payload[7] = (uint8_t)(body_len >> 8);
    crc = pp_protocol_crc16(payload + PP_PROTOCOL_HEADER_SIZE, body_len);
    payload[8] = (uint8_t)(crc & 0xFF);
    payload[9] = (uint8_t)(crc >> 8);

    assert(pp_graph_build_from_binary(payload, sizeof(payload), &g) == PP_OK);
    assert(g.node_count == 2);
    assert(g.edge_count == 1);
    assert(g.nodes[0].block_id == PP_BLOCK_SELECT_AXIS);
    assert(g.nodes[0].params_len == 1);
    assert(g.nodes[0].params[0] == PP_AXIS_Z);
    assert(g.nodes[1].block_id == PP_BLOCK_SPM_RANGE_GATE);
    printf("  PASS: test_graph_from_binary\n");
}

/* Test: validate port kind matching */
static void test_port_validation(void) {
    pp_graph_t g = {0};
    g.node_count = 2;
    g.nodes[0].block_id = PP_BLOCK_SELECT_AXIS;
    g.nodes[1].block_id = PP_BLOCK_AUTOCORRELATION;

    g.edge_count = 1;
    g.edges[0] = (pp_edge_t){0, 0, 1, 0};

    assert(pp_graph_validate_ports(&g) == PP_OK);

    g.nodes[1].block_id = PP_BLOCK_SELECT_AXIS;
    assert(pp_graph_validate_ports(&g) == PP_ERR);
    printf("  PASS: test_port_validation\n");
}

int main(void) {
    printf("test_pp_graph:\n");
    test_topo_sort_linear();
    test_topo_sort_cycle();
    test_graph_from_binary();
    test_port_validation();
    printf("All tests passed.\n");
    return 0;
}
