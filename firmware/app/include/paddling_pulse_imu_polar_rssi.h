#ifndef _PADDLING_PULSE_IMU_POLAR_RSSI_H_
#define _PADDLING_PULSE_IMU_POLAR_RSSI_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef PP_POLAR_MAX_CANDIDATES
#define PP_POLAR_MAX_CANDIDATES  4
#endif

#define PP_POLAR_ADDR_LEN  6

typedef struct
{
    uint8_t addr[PP_POLAR_ADDR_LEN];
    uint8_t addr_type;
    int8_t rssi;
} pp_polar_candidate_t;

typedef struct
{
    pp_polar_candidate_t items[PP_POLAR_MAX_CANDIDATES];
    uint8_t count;
} pp_polar_rssi_picker_t;

void pp_polar_rssi_picker_init(pp_polar_rssi_picker_t *picker);
bool pp_polar_rssi_picker_add(pp_polar_rssi_picker_t *picker,
                              const uint8_t addr[PP_POLAR_ADDR_LEN],
                              uint8_t addr_type,
                              int8_t rssi);
uint8_t pp_polar_rssi_picker_count(const pp_polar_rssi_picker_t *picker);
bool pp_polar_rssi_picker_best(const pp_polar_rssi_picker_t *picker,
                               pp_polar_candidate_t *out);

#endif /* _PADDLING_PULSE_IMU_POLAR_RSSI_H_ */
