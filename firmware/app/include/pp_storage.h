#ifndef PP_STORAGE_H
#define PP_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#define PP_PIPELINE_MAX_BYTES 512

typedef struct {
    bool (*read)(uint16_t addr, uint8_t *buf, uint16_t len);
    bool (*write)(uint16_t addr, const uint8_t *buf, uint16_t len);
    uint16_t capacity;
} pp_storage_t;

const pp_storage_t *pp_storage_get(void);

bool pp_storage_save_pipeline(const uint8_t *data, uint16_t len);
bool pp_storage_load_pipeline(uint8_t *buf, uint16_t buf_size, uint16_t *out_len);
bool pp_storage_has_valid_pipeline(void);
void pp_storage_clear(void);

#endif /* PP_STORAGE_H */
