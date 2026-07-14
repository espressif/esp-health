/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Error type definition of health algorithms
 */
typedef enum {
    ESP_HEALTH_ERR_OK                = 0,   /*!< Operation succeeded */
    ESP_HEALTH_ERR_FAIL              = -1,  /*!< Operation failed */
    ESP_HEALTH_ERR_MEM_LACK          = -2,  /*!< Memory allocation failure */
    ESP_HEALTH_ERR_INVALID_PARAMETER = -3,  /*!< Invalid input parameter */
    ESP_HEALTH_ERR_NOT_SUPPORT       = -4,  /*!< Unsupported type */
} esp_health_err_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
