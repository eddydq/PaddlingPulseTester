/**
 ****************************************************************************************
 * @file paddling_pulse_sample_store.h
 * @brief Circular sample buffer for IMU accelerometer data.
 ****************************************************************************************
 */

#ifndef _PADDLING_PULSE_SAMPLE_STORE_H_
#define _PADDLING_PULSE_SAMPLE_STORE_H_

#include <stdint.h>

#ifndef PP_SAMPLE_STORE_CAPACITY
#define PP_SAMPLE_STORE_CAPACITY  512
#endif
#ifndef PP_SAMPLE_STORE_MIN_RATE_HZ
#define PP_SAMPLE_STORE_MIN_RATE_HZ  1
#endif
#ifndef PP_SAMPLE_STORE_MAX_RATE_HZ
#define PP_SAMPLE_STORE_MAX_RATE_HZ  1000
#endif

/**
 * @brief Initialize the sample store and set the sampling rate.
 * @param sample_rate_hz  Sampling frequency (100 for I2C IMUs, 52 for Polar).
 */
void pp_sample_store_init(uint16_t sample_rate_hz);

/**
 * @brief Push a new accelerometer sample into the circular buffer.
 * @param sample  Signed 16-bit sample from the selected axis.
 */
void pp_sample_store_push(int16_t sample);

/**
 * @brief Get a sample by time order (0 = oldest in buffer).
 * @param ordered_index  Index from oldest (0) to newest (count-1).
 * @return Sample value, or 0 if out of range.
 */
int16_t pp_sample_store_get(uint16_t ordered_index);

/** @brief Number of valid samples currently in the buffer. */
uint16_t pp_sample_store_get_count(void);

/** @brief Configured sampling rate in Hz. */
uint16_t pp_sample_store_get_rate_hz(void);

/** @brief Clear all samples and reset write pointer. */
void pp_sample_store_reset(void);

#endif // _PADDLING_PULSE_SAMPLE_STORE_H_
