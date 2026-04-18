#include "../../../../analysis/scripts/c_block_protocol.h"
#include "../../../../tests/algorithms/c_stroke_rate/filters.h"

int main(void)
{
    char line[8192];
    while (analysis_protocol_read_line(line, (int)sizeof(line)))
    {
        if (!analysis_protocol_write_result("{\"outputs\":{\"primary\":[]},\"state\":{},\"diagnostics\":{}}"))
        {
            return 1;
        }
    }
    return 0;
}
