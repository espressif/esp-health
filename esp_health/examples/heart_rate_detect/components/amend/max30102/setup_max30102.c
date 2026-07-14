/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "dev_custom.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "esp_board_entry.h"
#include "gen_board_device_custom.h"
#include "esp_max30102.h"
#include "max30102_deinit.h"

static const char *TAG = "MAX30102_PPG";

/* Soft-reset settle time from MAX30102 datasheet / practical boards. */
#define MAX30102_RESET_DELAY_MS  100

static int max30102_ppg_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    max30102_dev_handle_t *handle = (max30102_dev_handle_t *)device_handle;
    if (handle->sensor == NULL) {
        free(handle);
        return ESP_OK;
    }

    max30102_bus_state_t st = {
        .i2c_attached = (handle->sensor->i2c_dev != NULL),
    };
    if (!st.i2c_attached) {
        free(handle->sensor);
        free(handle);
        return ESP_OK;
    }

    /* SHDN first so LEDs/analog stop before the I2C client is torn down. */
    esp_err_t shutdown_err = maxim_max30102_shutdown(handle->sensor);
    if (shutdown_err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 SHDN failed: %s", esp_err_to_name(shutdown_err));
    }

    esp_err_t rm_err = ESP_OK;
    if (shutdown_err == ESP_OK) {
        rm_err = i2c_master_bus_rm_device(handle->sensor->i2c_dev);
        if (rm_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove MAX30102 I2C device: %s",
                     esp_err_to_name(rm_err));
        }
    }

    esp_err_t err = max30102_deinit_try(&st, shutdown_err, rm_err);
    if (err != ESP_OK) {
        return err;
    }

    handle->sensor->i2c_dev = NULL;
    free(handle->sensor);
    handle->sensor = NULL;
    free(handle);
    return ESP_OK;
}

static void max30102_ppg_abort_init(max30102_dev_handle_t *handle)
{
    int derr = max30102_ppg_deinit(handle);
    if (derr != ESP_OK) {
        ESP_LOGE(TAG, "cleanup deinit failed: %s (I2C device still attached)",
                 esp_err_to_name(derr));
    }
}

int max30102_ppg_init(void *config, int cfg_size, void **device_handle)
{
    ESP_LOGI(TAG, "Initializing MAX30102 PPG device");

    if (config == NULL || device_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg_size != (int)sizeof(dev_custom_max30102_ppg_config_t)) {
        ESP_LOGE(TAG, "Invalid config size: %d, expected: %u",
                 cfg_size, (unsigned)sizeof(dev_custom_max30102_ppg_config_t));
        return ESP_ERR_INVALID_ARG;
    }

    dev_custom_max30102_ppg_config_t *cfg = (dev_custom_max30102_ppg_config_t *)config;
    if (cfg->peripheral_count < 1) {
        ESP_LOGE(TAG, "Missing I2C peripheral in device config");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_err_t err = esp_board_periph_get_handle(cfg->peripheral_names[0], (void **)&i2c_bus);
    if (err != ESP_OK || i2c_bus == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C peripheral '%s': %s",
                 cfg->peripheral_names[0], esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    }

    max30102_dev_handle_t *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_MAX30102_config_t sensor_cfg = {
        .MAX30102_device = {
            .scl_speed_hz = cfg->scl_speed_hz,
            .device_address = (uint8_t)cfg->i2c_addr,
        },
    };

    ESP_LOGI(TAG, "I2C addr=0x%02X, scl=%" PRId32 " Hz (SDA=19 SCL=20 INT=%d)",
             (unsigned)sensor_cfg.MAX30102_device.device_address,
             sensor_cfg.MAX30102_device.scl_speed_hz,
             (int)MAX30102_INT_GPIO);

    err = i2c_MAX30102_init(i2c_bus, &sensor_cfg, &handle->sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_MAX30102_init failed: %s", esp_err_to_name(err));
        free(handle);
        return err;
    }

    uint8_t status = 0;
    err = maxim_max30102_reset(handle->sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed (check wiring SDA=GPIO19 SCL=GPIO20, 3V3, ADDR=0x57): %s",
                 esp_err_to_name(err));
        max30102_ppg_abort_init(handle);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(MAX30102_RESET_DELAY_MS));

    err = i2c_MAX30102_read_reg(handle->sensor, REG_INTR_STATUS_1, &status, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read status after reset failed: %s", esp_err_to_name(err));
        max30102_ppg_abort_init(handle);
        return err;
    }

    err = esp_MAX30102_set(handle->sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 configure failed: %s", esp_err_to_name(err));
        max30102_ppg_abort_init(handle);
        return err;
    }

    ESP_LOGI(TAG, "MAX30102 ready (INT=GPIO%d)", (int)MAX30102_INT_GPIO);
    *device_handle = handle;
    return ESP_OK;
}

CUSTOM_DEVICE_IMPLEMENT(max30102_ppg, max30102_ppg_init, max30102_ppg_deinit);
