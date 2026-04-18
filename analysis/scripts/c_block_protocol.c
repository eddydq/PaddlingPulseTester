#include "c_block_protocol.h"

#include <stdio.h>

int analysis_protocol_read_line(char *buffer, int buffer_size)
{
    return fgets(buffer, buffer_size, stdin) != NULL;
}

int analysis_protocol_write_result(const char *json_result)
{
    if (fprintf(stdout, "%s\n", json_result) < 0)
    {
        return 0;
    }
    fflush(stdout);
    return 1;
}
