/**
 ****************************************************************************************
 * @file paddling_pulse_sample_store.c
 * @brief Circular sample buffer — 512 x int16_t in retention RAM.
 ****************************************************************************************
 */

#include "paddling_pulse_sample_store.h"
#include <string.h>
#include "arch.h"

static struct {
    int16_t  samples[PP_SAMPLE_STORE_CAPACITY];
    uint16_t wr_idx;
    uint16_t count;
    uint16_t sample_rate_hz;
} s_store __SECTION_ZERO("retention_mem_area0");

void pp_sample_store_init(uint16_t sample_rate_hz)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.sample_rate_hz = sample_rate_hz;
}

void pp_sample_store_push(int16_t sample)
{
    s_store.samples[s_store.wr_idx] = sample;
    s_store.wr_idx = (s_store.wr_idx + 1) % PP_SAMPLE_STORE_CAPACITY;
    if (s_store.count < PP_SAMPLE_STORE_CAPACITY)
    {
        s_store.count++;
    }
}

int16_t pp_sample_store_get(uint16_t ordered_index)
{
    if (ordered_index >= s_store.count)
    {
        return 0;
    }
    uint16_t oldest = (s_store.wr_idx + PP_SAMPLE_STORE_CAPACITY - s_store.count)
                      % PP_SAMPLE_STORE_CAPACITY;
    uint16_t idx = (oldest + ordered_index) % PP_SAMPLE_STORE_CAPACITY;
    return s_store.samples[idx];
}

uint16_t pp_sample_store_get_count(void)
{
    return s_store.count;
}

uint16_t pp_sample_store_get_rate_hz(void)
{
    return s_store.sample_rate_hz;
}

void pp_sample_store_reset(void)
{
    memset(&s_store, 0, sizeof(s_store));
}
