#ifndef PP_BLOCK_H
#define PP_BLOCK_H

#include <stddef.h>
#include <stdint.h>

/* Packet kinds must match protocol encoding. */
enum {
    PP_KIND_RAW_WINDOW = 0,
    PP_KIND_SERIES     = 1,
    PP_KIND_CANDIDATE  = 2,
    PP_KIND_ESTIMATE   = 3
};

/* Axis identifiers. */
enum {
    PP_AXIS_X   = 0,
    PP_AXIS_Y   = 1,
    PP_AXIS_Z   = 2,
    PP_AXIS_MAG = 3,
    PP_AXIS_ALL = 0xFF
};

/* Block IDs must match protocol encoding. */
enum {
    PP_BLOCK_LIS3DH_SOURCE      = 0x01,
    PP_BLOCK_MPU6050_SOURCE     = 0x02,
    PP_BLOCK_POLAR_SOURCE       = 0x03,
    PP_BLOCK_SELECT_AXIS        = 0x04,
    PP_BLOCK_VECTOR_MAG         = 0x05,
    PP_BLOCK_HPF_GRAVITY        = 0x06,
    PP_BLOCK_LOWPASS            = 0x07,
    PP_BLOCK_AUTOCORRELATION    = 0x08,
    PP_BLOCK_FFT_DOMINANT       = 0x09,
    PP_BLOCK_ADAPTIVE_PEAK      = 0x0A,
    PP_BLOCK_ZERO_CROSSING      = 0x0B,
    PP_BLOCK_SPM_RANGE_GATE     = 0x0C,
    PP_BLOCK_PEAK_SELECTOR      = 0x0D,
    PP_BLOCK_CONFIDENCE_GATE    = 0x0E,
    PP_BLOCK_KALMAN_2D          = 0x0F,
    PP_BLOCK_CONFIRMATION       = 0x10,
    PP_BLOCK_COUNT              = 16
};

/* Status codes. */
enum {
    PP_OK   = 0,
    PP_ERR  = 1,
    PP_SKIP = 2
};

typedef struct {
    int16_t  *data;
    uint16_t  length;
    uint8_t   kind;
    uint8_t   axis;
    uint16_t  sample_rate_hz;
} pp_packet_t;

typedef struct {
    uint8_t  block_id;
    uint8_t  group;
    uint8_t  num_inputs;
    uint8_t  num_outputs;
    uint8_t  input_kinds[3];
    uint8_t  output_kinds[3];
    uint16_t state_size;
} pp_block_manifest_t;

typedef struct {
    uint8_t status;
} pp_block_result_t;

typedef struct {
    uint16_t sample_rates[8];
    uint8_t  num_rates;
    uint8_t  resolutions[4];
    uint8_t  num_resolutions;
    uint8_t  axes_available[3];
    uint8_t  num_axes;
    uint8_t  gyro_available;
} pp_source_caps_t;

typedef pp_block_result_t (*pp_block_exec_fn)(
    const pp_packet_t *inputs,
    uint8_t            num_inputs,
    const uint8_t     *params,
    uint16_t           params_len,
    uint8_t           *state,
    pp_packet_t       *outputs,
    uint8_t            num_outputs
);

pp_block_result_t pp_block_exec(
    uint8_t            block_id,
    const pp_packet_t *inputs,
    uint8_t            num_inputs,
    const uint8_t     *params,
    uint16_t           params_len,
    uint8_t           *state,
    pp_packet_t       *outputs,
    uint8_t            num_outputs
);

const pp_block_manifest_t *pp_block_get_manifest(uint8_t block_id);
const pp_source_caps_t *pp_source_get_caps(uint8_t block_id);

#endif /* PP_BLOCK_H */
