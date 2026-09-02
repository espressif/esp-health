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
 * @brief  Blood oxygen saturation (SpO2) estimation from dual-channel PPG
 *
 *         Terminology used in this header:
 *         - PPG: photoplethysmography, the optical pulse waveform from the sensor
 *         - IR: infrared LED channel (typically ~880 nm on MAX30102-class parts)
 *         - Red: red LED channel (typically ~660 nm)
 *         - DC: mean / slowly varying level of a PPG window; must be **positive**
 *         - AC: pulsatile component after DC and linear-trend removal (RMS here)
 *         - R: ratio-of-ratios `(AC_red / DC_red) / (AC_ir / DC_ir)`
 *         - SpO2: estimated arterial oxygen saturation in percent
 *         - Correlation: similarity of IR and Red AC in one window (about 1 = aligned)
 *         - Autocorrelation: self-similarity of IR AC vs lag; used to check pulse period
 *
 *         Pipeline per `esp_health_spo2_process` call:
 *         1. Remove DC and linear trend from synchronous IR and Red arrays
 *         2. Compute AC/DC ratio-of-ratios `R` and map through
 *         `SpO2 = a * R^2 + b * R + c`
 *         3. Gate on IR/Red AC correlation and IR autocorrelation periodicity
 *
 *         Default calibration matches MAX30102-class 660 nm / 880 nm sensors.
 *         Other wavelengths or optics **must** be re-calibrated; do not treat
 *         the default polynomial as medical-grade accuracy.
 *
 *         IR and Red samples must be raw PPG with **positive DC**. Recommended
 *         window is several seconds (example: 5 s at 100 Hz).
 *
 *         The handle keeps periodicity state across `process()` calls for
 *         streaming windows. For unrelated recordings, call
 *         `esp_health_spo2_reset()` or close/reopen. The handle is
 *         **not thread-safe**.
 *
 *         Typical usage:
 *         1. `esp_health_spo2_cfg_t cfg = ESP_HEALTH_SPO2_CFG_DEFAULT();`
 *         (override `sample_rate` or `calib` if needed)
 *         2. Call `esp_health_spo2_open` to create a handle
 *         3. Call `esp_health_spo2_process` for each analysis window
 *         4. Call `esp_health_spo2_close` when done
 */

/**
 * @brief  Handle for SpO2 processor
 */
typedef void *esp_health_spo2_handle_t;

/**
 * @brief  SpO2 calibration coefficients
 *
 *         SpO2 = a * R^2 + b * R + c
 *         where R = (AC_red / DC_red) / (AC_ir / DC_ir)
 *
 *         r_min / r_max define the valid range of R. Results outside this
 *         range are rejected (`spo2` is 0).
 */
typedef struct {
    float  a;      /*!< Quadratic coefficient */
    float  b;      /*!< Linear coefficient */
    float  c;      /*!< Constant term */
    float  r_min;  /*!< Minimum valid R ratio */
    float  r_max;  /*!< Maximum valid R ratio */
} esp_health_spo2_calib_t;

/**
 * @brief  Configuration structure for SpO2 processor
 */
typedef struct {
    uint32_t                 sample_rate;                /*!< PPG sample rate in Hz, range[16, 1000] */
    esp_health_spo2_calib_t  calib;                      /*!< Calibration coefficients */
    float                    min_correlation;            /*!< Gate: IR and Red AC must match well enough, else SpO2 is 0. range[0, 1]; 0 disables this gate */
    float                    min_autocorrelation_ratio;  /*!< Gate: IR must look periodic like a heartbeat, else SpO2 is 0. range[0, 1]; 0 disables this gate */
} esp_health_spo2_cfg_t;

/**
 * @brief  Default SpO2 config (100 Hz, MAX30102-class polynomial, default quality gates)
 *
 *         Not medical-grade. Recalibrate `calib` for other wavelengths or optics.
 */
#define ESP_HEALTH_SPO2_CFG_DEFAULT()  {  \
    .sample_rate = 100U,                  \
    .calib       = {                      \
        .a     = -45.060f,                \
        .b     = 30.354f,                 \
        .c     = 93.245f,                 \
        .r_min = 0.02f,                   \
        .r_max = 1.84f,                   \
    },                                    \
    .min_correlation           = 0.8f,    \
    .min_autocorrelation_ratio = 0.5f,    \
}

/**
 * @brief  SpO2 processing result
 *
 *         On `ESP_HEALTH_ERR_OK`, `spo2` is range[70, 100]
 *         when a reading is available, or 0 when this window has no reliable
 *         value. `correlation` and `quality_ratio` are always written on `ESP_HEALTH_ERR_OK`.
 */
typedef struct {
    float  spo2;           /*!< SpO2 (%), range[70, 100], or 0 if none */
    float  correlation;    /*!< IR/Red AC correlation (normalized inner product after DC/trend removal) */
    float  quality_ratio;  /*!< Autocorrelation peak ratio (higher = more periodic) */
} esp_health_spo2_result_t;

/**
 * @brief  Create a SpO2 processor handle based on the provided configuration
 *
 * @param[in]   cfg     SpO2 processor configuration (read-only)
 * @param[out]  handle  The SpO2 handle. Set to NULL on error
 *
 * @return
 *       - ESP_HEALTH_ERR_OK                 Operation succeeded
 *       - ESP_HEALTH_ERR_MEM_LACK           Failed to allocate memory
 *       - ESP_HEALTH_ERR_INVALID_PARAMETER  Invalid input parameter (NULL, sample_rate, calib range, or gates outside [0, 1])
 *
 * @note  Not thread-safe.
 */
esp_health_err_t esp_health_spo2_open(const esp_health_spo2_cfg_t *cfg, esp_health_spo2_handle_t *handle);

/**
 * @brief  Calculate SpO2 from a pair of synchronous IR and Red PPG sample arrays
 *
 * @param[in]   handle       The SpO2 handle
 * @param[in]   ir_samples   Infrared (IR) PPG samples, float, positive DC
 * @param[in]   red_samples  Red PPG samples, float, positive DC, same length as IR
 * @param[in]   num_samples  Number of samples in each array (must be >= 2)
 * @param[out]  result       Written on `ESP_HEALTH_ERR_OK`. `spo2` is 0 if no reliable reading
 *
 * @return
 *       - ESP_HEALTH_ERR_OK                 Operation succeeded (`spo2` is 0 if no valid reading)
 *       - ESP_HEALTH_ERR_INVALID_PARAMETER  Invalid input parameter
 *       - ESP_HEALTH_ERR_MEM_LACK           Failed to grow working buffers
 *
 * @note  Not thread-safe. Periodicity state is kept across calls.
 */
esp_health_err_t esp_health_spo2_process(esp_health_spo2_handle_t handle,
                                         const float *ir_samples,
                                         const float *red_samples,
                                         int num_samples,
                                         esp_health_spo2_result_t *result);

/**
 * @brief  Clear periodicity state so the next `process` starts a fresh search
 *
 * @param[in]  handle  The SpO2 handle. NULL is ignored
 *
 * @note  Call this before feeding an unrelated recording on the same handle.
 */
void esp_health_spo2_reset(esp_health_spo2_handle_t handle);

/**
 * @brief  Deinitialize SpO2 processor handle
 *
 * @param[in]  handle  The SpO2 handle. NULL is ignored
 *
 * @note  Not thread-safe.
 */
void esp_health_spo2_close(esp_health_spo2_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
