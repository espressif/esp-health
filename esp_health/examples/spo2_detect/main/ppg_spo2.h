/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include <stdbool.h>
#include "esp_health_spo2.h"

/** Latest SpO2 measurement exposed by the example task. */
typedef struct {
    bool              valid;        /*!< true when spo2 is in range, or while holding the last valid SpO2 */
    float             spo2;         /*!< SpO2 (%) when valid; unused when spo2 is 0 */
    float             correlation;  /*!< IR/Red correlation from esp_health_spo2 */
    float             quality;      /*!< Autocorrelation quality (q) */
    bool              worn;         /*!< true when finger contact detected */
    esp_health_err_t  err;          /*!< Last esp_health_spo2_process return */
} ppg_spo2_measurement_t;

/** Thread-safe snapshot of the latest measurement (same mutex as the worker). */
ppg_spo2_measurement_t ppg_spo2_get_measurement(void);
