#ifndef _PADDLING_PULSE_IMU_POLAR_LOGIC_H_
#define _PADDLING_PULSE_IMU_POLAR_LOGIC_H_

#include <stdbool.h>
#include <stdint.h>

#define PP_POLAR_ACC_TLV_MAX_LEN 24

typedef struct
{
    uint16_t sample_rate_hz;
    uint8_t tlvs[PP_POLAR_ACC_TLV_MAX_LEN];
    uint8_t tlv_len;
    uint8_t tlv_count;
} pp_polar_acc_settings_t;

typedef enum
{
    PP_POLAR_CANCEL_NONE = 0,
    PP_POLAR_CANCEL_RESET,
    PP_POLAR_CANCEL_RETRY,
} pp_polar_cancel_action_t;

bool pp_polar_parse_acc_settings(const uint8_t *data,
                                 uint16_t len,
                                 pp_polar_acc_settings_t *out);

bool pp_polar_disconnect_is_owned(uint16_t tracked_conhdl,
                                  uint16_t event_conhdl,
                                  bool idle);

pp_polar_cancel_action_t pp_polar_cancel_action(bool stop_requested,
                                                bool retry_requested);

#endif
