/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include "ppg_spo2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* Caller must already hold mu. Publishes one consistent struct. */
static inline void spo2_meas_store_locked(ppg_spo2_measurement_t *dst,
                                          const ppg_spo2_measurement_t *src)
{
    *dst = *src;
}

/* Snapshot under the same mutex the worker uses to publish. */
static inline ppg_spo2_measurement_t spo2_meas_snapshot(SemaphoreHandle_t mu,
                                                        const ppg_spo2_measurement_t *src)
{
    ppg_spo2_measurement_t out = {0};

    if (mu == NULL || src == NULL) {
        return out;
    }
    xSemaphoreTake(mu, portMAX_DELAY);
    out = *src;
    xSemaphoreGive(mu);
    return out;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
