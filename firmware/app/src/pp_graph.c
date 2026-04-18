#include "pp_graph.h"

#include <string.h>
#include "pp_protocol.h"

static int edge_leaves_node(const pp_graph_t *graph, uint8_t edge_index, uint8_t node_index)
{
    return graph->edges[edge_index].src_node == node_index;
}

static uint8_t topo_visit(
    pp_graph_t *graph,
    uint8_t node_index,
    uint8_t *visited,
    uint8_t *in_stack,
    uint8_t *out_count)
{
    uint8_t edge_index;

    if (in_stack[node_index]) {
        return PP_ERR;
    }
    if (visited[node_index]) {
        return PP_OK;
    }

    in_stack[node_index] = 1;
    for (edge_index = 0; edge_index < graph->edge_count; edge_index++) {
        if (edge_leaves_node(graph, edge_index, node_index)) {
            uint8_t dst = graph->edges[edge_index].dst_node;
            if (dst >= graph->node_count) {
                return PP_ERR;
            }
            if (topo_visit(graph, dst, visited, in_stack, out_count) != PP_OK) {
                return PP_ERR;
            }
        }
    }
    in_stack[node_index] = 0;
    visited[node_index] = 1;
    graph->exec_order[*out_count] = node_index;
    (*out_count)++;

    return PP_OK;
}

uint8_t pp_graph_topo_sort(pp_graph_t *graph)
{
    uint8_t visited[PP_MAX_NODES] = {0};
    uint8_t in_stack[PP_MAX_NODES] = {0};
    uint8_t post_count = 0;
    uint8_t i;

    if (!graph || graph->node_count > PP_MAX_NODES || graph->edge_count > PP_MAX_EDGES) {
        return PP_ERR;
    }

    for (i = 0; i < graph->node_count; i++) {
        if (!visited[i]) {
            if (topo_visit(graph, i, visited, in_stack, &post_count) != PP_OK) {
                return PP_ERR;
            }
        }
    }

    for (i = 0; i < graph->node_count / 2U; i++) {
        uint8_t tmp = graph->exec_order[i];
        graph->exec_order[i] = graph->exec_order[(uint8_t)(graph->node_count - 1U - i)];
        graph->exec_order[(uint8_t)(graph->node_count - 1U - i)] = tmp;
    }

    return PP_OK;
}

uint8_t pp_graph_build_from_binary(const uint8_t *data, uint16_t len, pp_graph_t *graph)
{
    pp_protocol_header_t header;
    uint8_t status;
    uint16_t offset;
    uint16_t body_end;
    uint8_t parsed_edges = 0;

    if (!data || !graph) {
        return PP_ERR;
    }

    status = pp_protocol_validate(data, len, &header);
    if (status != PP_PROTO_OK) {
        return PP_ERR;
    }
    if (header.block_count > PP_MAX_NODES || header.edge_count > PP_MAX_EDGES) {
        return PP_ERR;
    }

    memset(graph, 0, sizeof(*graph));
    graph->node_count = header.block_count;
    graph->edge_count = header.edge_count;

    offset = PP_PROTOCOL_HEADER_SIZE;
    body_end = (uint16_t)(PP_PROTOCOL_HEADER_SIZE + header.body_length);
    while (offset < body_end) {
        pp_tlv_record_t record;
        uint16_t consumed = 0;

        if (pp_protocol_parse_tlv(&data[offset], (uint16_t)(body_end - offset), &record, &consumed) != PP_PROTO_OK) {
            return PP_ERR;
        }

        if (record.tag == PP_TLV_BLOCK) {
            uint8_t node_index;
            uint8_t param_len;
            if (record.length < 3U) {
                return PP_ERR;
            }
            node_index = record.value[1];
            param_len = record.value[2];
            if (node_index >= header.block_count || (uint8_t)(3U + param_len) != record.length) {
                return PP_ERR;
            }
            graph->nodes[node_index].block_id = record.value[0];
            graph->nodes[node_index].params = &record.value[3];
            graph->nodes[node_index].params_len = param_len;
        } else if (record.tag == PP_TLV_EDGE) {
            if (record.length != 4U || parsed_edges >= header.edge_count) {
                return PP_ERR;
            }
            graph->edges[parsed_edges].src_node = record.value[0];
            graph->edges[parsed_edges].src_port = record.value[1];
            graph->edges[parsed_edges].dst_node = record.value[2];
            graph->edges[parsed_edges].dst_port = record.value[3];
            parsed_edges++;
        }

        offset = (uint16_t)(offset + consumed);
    }

    return parsed_edges == header.edge_count ? PP_OK : PP_ERR;
}

uint8_t pp_graph_validate_ports(const pp_graph_t *graph)
{
    uint8_t i;

    if (!graph || graph->node_count > PP_MAX_NODES || graph->edge_count > PP_MAX_EDGES) {
        return PP_ERR;
    }

    for (i = 0; i < graph->edge_count; i++) {
        const pp_edge_t *edge = &graph->edges[i];
        const pp_block_manifest_t *src_manifest;
        const pp_block_manifest_t *dst_manifest;
        uint8_t src_kind;
        uint8_t dst_kind;

        if (edge->src_node >= graph->node_count || edge->dst_node >= graph->node_count) {
            return PP_ERR;
        }
        src_manifest = pp_block_get_manifest(graph->nodes[edge->src_node].block_id);
        dst_manifest = pp_block_get_manifest(graph->nodes[edge->dst_node].block_id);
        if (!src_manifest || !dst_manifest) {
            return PP_ERR;
        }
        if (edge->src_port >= src_manifest->num_outputs || edge->dst_port >= dst_manifest->num_inputs) {
            return PP_ERR;
        }

        src_kind = src_manifest->output_kinds[edge->src_port];
        dst_kind = dst_manifest->input_kinds[edge->dst_port];
        if (src_kind != dst_kind) {
            return PP_ERR;
        }
    }

    return PP_OK;
}

uint8_t pp_graph_execute(pp_graph_t *graph)
{
    static int16_t scratch[2][PP_GRAPH_PACKET_CAPACITY];
    uint8_t order_index;

    if (!graph || graph->node_count > PP_MAX_NODES) {
        return PP_ERR;
    }

    for (order_index = 0; order_index < graph->node_count; order_index++) {
        uint8_t node_index = graph->exec_order[order_index];
        pp_node_t *node;
        const pp_block_manifest_t *manifest;
        pp_packet_t inputs[3];
        pp_packet_t outputs[3];
        uint8_t input_count = 0;
        uint8_t output_count;
        uint8_t edge_index;
        uint8_t port;
        pp_block_result_t result;

        if (node_index >= graph->node_count) {
            return PP_ERR;
        }
        node = &graph->nodes[node_index];
        manifest = pp_block_get_manifest(node->block_id);
        if (!manifest) {
            return PP_ERR;
        }

        memset(inputs, 0, sizeof(inputs));
        for (edge_index = 0; edge_index < graph->edge_count; edge_index++) {
            const pp_edge_t *edge = &graph->edges[edge_index];
            if (edge->dst_node == node_index && edge->dst_port < 3U && edge->src_node < graph->node_count) {
                inputs[edge->dst_port] = graph->nodes[edge->src_node].outputs[edge->src_port];
                if ((uint8_t)(edge->dst_port + 1U) > input_count) {
                    input_count = (uint8_t)(edge->dst_port + 1U);
                }
            }
        }

        output_count = manifest->num_outputs;
        if (output_count == 0 || output_count > 3U) {
            return PP_ERR;
        }
        for (port = 0; port < output_count; port++) {
            outputs[port].data = scratch[order_index & 1U];
            outputs[port].length = PP_GRAPH_PACKET_CAPACITY;
            outputs[port].kind = manifest->output_kinds[port];
            outputs[port].axis = PP_AXIS_ALL;
            outputs[port].sample_rate_hz = 0;
        }

        result = pp_block_exec(
            node->block_id,
            inputs,
            input_count,
            node->params,
            node->params_len,
            node->state,
            outputs,
            output_count);

        if (result.status == PP_ERR) {
            return PP_ERR;
        }

        for (port = 0; port < output_count; port++) {
            node->outputs[port] = outputs[port];
        }
        node->output = outputs[0];
    }

    return PP_OK;
}
