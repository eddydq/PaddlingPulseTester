#include "pp_storage.h"

#include <string.h>
#include "pp_protocol.h"

#if defined(PP_TARGET_TEST) || defined(PP_TARGET_WASM)
#define PP_RETENTION
#else
#include "arch.h"
#define PP_RETENTION __SECTION_ZERO("retention_mem_area0")
#endif

typedef struct {
    uint16_t len;
    uint16_t reserved;
    uint8_t data[PP_PIPELINE_MAX_BYTES];
} pp_retained_pipeline_t;

static pp_retained_pipeline_t s_pipeline PP_RETENTION;

static bool retained_read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    if (!buf || (uint32_t)addr + len > sizeof(s_pipeline.data)) {
        return false;
    }
    memcpy(buf, &s_pipeline.data[addr], len);
    return true;
}

static bool retained_write(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    if (!buf || (uint32_t)addr + len > sizeof(s_pipeline.data)) {
        return false;
    }
    memcpy(&s_pipeline.data[addr], buf, len);
    return true;
}

static const pp_storage_t s_storage = {
    .read = retained_read,
    .write = retained_write,
    .capacity = PP_PIPELINE_MAX_BYTES
};

const pp_storage_t *pp_storage_get(void)
{
    return &s_storage;
}

bool pp_storage_save_pipeline(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > PP_PIPELINE_MAX_BYTES) {
        return false;
    }
    if (pp_protocol_validate(data, len, 0) != PP_PROTO_OK) {
        return false;
    }

    memset(&s_pipeline, 0, sizeof(s_pipeline));
    memcpy(s_pipeline.data, data, len);
    s_pipeline.len = len;
    return true;
}

bool pp_storage_load_pipeline(uint8_t *buf, uint16_t buf_size, uint16_t *out_len)
{
    if (!buf || !out_len || s_pipeline.len == 0 || s_pipeline.len > PP_PIPELINE_MAX_BYTES) {
        return false;
    }
    if (buf_size < s_pipeline.len) {
        return false;
    }
    if (pp_protocol_validate(s_pipeline.data, s_pipeline.len, 0) != PP_PROTO_OK) {
        return false;
    }

    memcpy(buf, s_pipeline.data, s_pipeline.len);
    *out_len = s_pipeline.len;
    return true;
}

bool pp_storage_has_valid_pipeline(void)
{
    if (s_pipeline.len < PP_PROTOCOL_HEADER_SIZE || s_pipeline.len > PP_PIPELINE_MAX_BYTES) {
        return false;
    }
    if (s_pipeline.data[0] != 0x50U || s_pipeline.data[1] != 0x50U) {
        return false;
    }
    return pp_protocol_validate(s_pipeline.data, s_pipeline.len, 0) == PP_PROTO_OK;
}

void pp_storage_clear(void)
{
    memset(&s_pipeline, 0, sizeof(s_pipeline));
}
