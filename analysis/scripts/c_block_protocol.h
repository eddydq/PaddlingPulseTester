#ifndef ANALYSIS_C_BLOCK_PROTOCOL_H
#define ANALYSIS_C_BLOCK_PROTOCOL_H

int analysis_protocol_read_line(char *buffer, int buffer_size);
int analysis_protocol_write_result(const char *json_result);

#endif
