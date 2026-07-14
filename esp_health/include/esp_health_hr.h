/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include <stdint.h>
#include "esp_health_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Static heart rate detection from PPG (photoplethysmography) samples
 *
 *         Pipeline per `esp_health_hr_process` call:
 *         1. Bandpass (about 0.7–3.5 Hz) and short moving-average smooth
 *         2. Baseline correction and adaptive peak detection
 *         3. Heart rate from the **median** RR interval (not the mean)
 *         4. Autocorrelation harmonic correction (0.5× / 2/3× / 3/2× / 2×)
 *
 *         Recommended analysis window is 8–15 s (at least several beats).
 *         Shorter or poor-quality inputs return `ESP_HEALTH_ERR_OK` with BPM 0. Valid BPM is range[40, 200].
 *
 *         This module does not include motion artifact suppression and is
 *         intended for static (resting) measurement. For motion, add IMU
 *         gating or signal-quality checks in the application.
 *
 *         The handle is **not thread-safe**. Do not call open/process/close
 *         concurrently on the same handle.
 *
 *         Typical usage:
 *         1. Fill `esp_health_hr_cfg_t` with the PPG sample rate
 *         2. Call `esp_health_hr_open` to create a handle
 *         3. Call `esp_health_hr_process` for each analysis window
 *         4. Call `esp_health_hr_close` when done
 */

/**
 * @brief  Handle for heart rate processor
 */
typedef void *esp_health_hr_handle_t;

/**
 * @brief  Configuration structure for heart rate processor
 */
typedef struct {
    uint32_t  sample_rate;  /*!< PPG sample rate in Hz, range[16, 1000] */
} esp_health_hr_cfg_t;

/**
 * @brief  Heart-rate result over the analyzed PPG window
 *
 *         `avg_bpm` is from the median RR, after harmonic correction.
 *         `min_bpm` / `max_bpm` are instantaneous RR extrema, then scaled by
 *         the same harmonic correction as `avg_bpm`. On `ESP_HEALTH_ERR_OK`, BPM fields
 *         are 0 when this window has no reliable reading.
 */
typedef struct {
    uint16_t  min_bpm;  /*!< Minimum BPM after harmonic scaling, or 0 if none */
    uint16_t  max_bpm;  /*!< Maximum BPM after harmonic scaling, or 0 if none */
    uint16_t  avg_bpm;  /*!< Median-RR BPM after harmonic correction, or 0 if none */
} esp_health_hr_result_t;

/** @deprecated  Use `esp_health_hr_result_t`. Kept for source compatibility. */
typedef esp_health_hr_result_t hr_result_t;

/**
 * @brief  Create a heart rate processor handle based on the provided configuration
 *
 * @param[in]   cfg     Heart rate processor configuration (read-only)
 * @param[out]  handle  The heart rate handle. Set to NULL on error
 *
 * @return
 *       - ESP_HEALTH_ERR_OK                 Operation succeeded
 *       - ESP_HEALTH_ERR_MEM_LACK           Failed to allocate memory
 *       - ESP_HEALTH_ERR_INVALID_PARAMETER  Invalid input parameter (NULL, or sample_rate out of range)
 */
esp_health_err_t esp_health_hr_open(const esp_health_hr_cfg_t *cfg, esp_health_hr_handle_t *handle);

/**
 * @brief  Calculate heart rate from a block of PPG samples
 *
 * @param[in]   handle       The heart rate handle
 * @param[in]   samples      Array of PPG sample values (float)
 * @param[in]   num_samples  Number of samples in the array (must be > 0)
 * @param[out]  result       Written on `ESP_HEALTH_ERR_OK` (BPM 0 if no reliable reading)
 *
 * @return
 *       - ESP_HEALTH_ERR_OK                 Operation succeeded (BPM 0 if no valid reading)
 *       - ESP_HEALTH_ERR_INVALID_PARAMETER  Invalid input parameter
 *       - ESP_HEALTH_ERR_MEM_LACK           Failed to grow working buffers
 */
esp_health_err_t esp_health_hr_process(esp_health_hr_handle_t handle, const float *samples,
                                       int num_samples, esp_health_hr_result_t *result);

/**
 * @brief  Deinitialize heart rate processor handle
 *
 * @param[in]  handle  The heart rate handle. NULL is ignored
 */
void esp_health_hr_close(esp_health_hr_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
