/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* MODE_CONFIG bit7 SHDN: analog off, LEDs off, I2C still alive. */
#define MAX30102_MODE_SHDN  0x80u

typedef struct {
    bool  i2c_attached;
} max30102_bus_state_t;

/**
 * One deinit attempt. On ESP_OK, i2c_attached is false and the wrapper may be
 * freed. On error the bus is still attached so the caller can retry.
 */
static inline esp_err_t max30102_deinit_try(max30102_bus_state_t *st,
                                            esp_err_t shutdown_err,
                                            esp_err_t rm_err)
{
    if (st == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!st->i2c_attached) {
        return ESP_OK;
    }
    if (shutdown_err != ESP_OK) {
        return shutdown_err;
    }
    if (rm_err != ESP_OK) {
        return rm_err;
    }
    st->i2c_attached = false;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
