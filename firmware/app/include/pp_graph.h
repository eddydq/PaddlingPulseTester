#ifndef PP_GRAPH_H
#define PP_GRAPH_H

#include <stdint.h>
#include "pp_block.h"

#ifndef PP_MAX_NODES
#define PP_MAX_NODES 16
#endif

#ifndef PP_MAX_EDGES
#define PP_MAX_EDGES 20
#endif

#ifndef PP_GRAPH_PACKET_CAPACITY
#define PP_GRAPH_PACKET_CAPACITY 512
#endif

typedef struct {
    uint8_t block_id;
    const uint8_t *params;
    uint16_t params_len;
    uint8_t *state;
    pp_packet_t output;
    pp_packet_t outputs[3];
} pp_node_t;

typedef struct {
    uint8_t src_node;
    uint8_t src_port;
    uint8_t dst_node;
    uint8_t dst_port;
} pp_edge_t;

typedef struct {
    pp_node_t nodes[PP_MAX_NODES];
    pp_edge_t edges[PP_MAX_EDGES];
    uint8_t node_count;
    uint8_t edge_count;
    uint8_t exec_order[PP_MAX_NODES];
} pp_graph_t;

uint8_t pp_graph_topo_sort(pp_graph_t *graph);
uint8_t pp_graph_build_from_binary(const uint8_t *data, uint16_t len, pp_graph_t *graph);
uint8_t pp_graph_validate_ports(const pp_graph_t *graph);
uint8_t pp_graph_execute(pp_graph_t *graph);

#endif /* PP_GRAPH_H */
