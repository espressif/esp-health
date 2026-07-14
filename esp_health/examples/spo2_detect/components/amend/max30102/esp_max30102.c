/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#include <stdlib.h>
#include "esp_max30102.h"
#include "max30102_wear.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX30102";

#define FIFO_DEPTH        32
#define BATCH_TIMEOUT_MS  1500
#define MIN_BATCH_RATIO   8
#define OVF_LOG_MIN_MS    1000

static max30102_wear_t s_wear;
static TickType_t s_last_ovf_log;

static int fifo_available(uint8_t wr_ptr, uint8_t rd_ptr)
{
    return (wr_ptr - rd_ptr) & (FIFO_DEPTH - 1);
}

/**
 * Datasheet: clear FIFO by writing WR, OVF, and RD all to 0.
 * Setting RD=WR is a no-op when the FIFO is already full (pointers equal).
 * OVF is sticky until a sample is written into a non-full FIFO — do not
 * treat a leftover counter as overflow right after reset/init.
 */
static void fifo_reset(i2c_MAX30102_handle_t handle)
{
    (void)i2c_MAX30102_write_reg(handle, REG_FIFO_WR_PTR, 0);
    (void)i2c_MAX30102_write_reg(handle, REG_OVF_COUNTER, 0);
    (void)i2c_MAX30102_write_reg(handle, REG_FIFO_RD_PTR, 0);
}

static void log_fifo_overflow(uint8_t ovf)
{
    TickType_t now = xTaskGetTickCount();
    if (s_last_ovf_log == 0 ||
        (now - s_last_ovf_log) >= pdMS_TO_TICKS(OVF_LOG_MIN_MS)) {
        ESP_LOGW(TAG, "FIFO overflow (%u), discontinuity", ovf);
        s_last_ovf_log = now;
    }
}

static esp_err_t fifo_on_overflow(i2c_MAX30102_handle_t handle, uint8_t ovf)
{
    log_fifo_overflow(ovf);
    fifo_reset(handle);
    return MAX30102_ERR_FIFO_OVERFLOW;
}

static esp_err_t fifo_read_ovf(i2c_MAX30102_handle_t handle, uint8_t *ovf)
{
    return i2c_MAX30102_read_reg(handle, REG_OVF_COUNTER, ovf, 1);
}

/* wr==rd is empty or full. Full+lost samples only after we already read some. */
static int fifo_is_full_overflow(uint8_t wr_ptr, uint8_t rd_ptr, uint8_t ovf, int collected)
{
    return ovf != 0 && fifo_available(wr_ptr, rd_ptr) == 0 && collected > 0;
}

static esp_err_t fifo_read_ptrs(i2c_MAX30102_handle_t handle, uint8_t *wr_ptr, uint8_t *rd_ptr)
{
    if (i2c_MAX30102_read_reg(handle, REG_FIFO_WR_PTR, wr_ptr, 1) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_read_reg(handle, REG_FIFO_RD_PTR, rd_ptr, 1) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t fifo_read_sample(i2c_MAX30102_handle_t handle, uint32_t *red, uint32_t *ir)
{
    uint8_t i2c_data[6];

    if (i2c_MAX30102_read_reg(handle, REG_FIFO_DATA, i2c_data, 6) != ESP_OK) {
        return ESP_FAIL;
    }

    *red = (((uint32_t)i2c_data[0] << 16) | ((uint32_t)i2c_data[1] << 8) | (uint32_t)i2c_data[2]) & 0x03FFFFU;
    *ir = (((uint32_t)i2c_data[3] << 16) | ((uint32_t)i2c_data[4] << 8) | (uint32_t)i2c_data[5]) & 0x03FFFFU;
    return ESP_OK;
}

static void update_wear_state(int *detect_flag, const uint32_t *red_data, int actual_count, uint64_t ir_sum)
{
    if (detect_flag == NULL) {
        return;
    }

    int prev = *detect_flag;
    s_wear.detect_flag = *detect_flag;
    max30102_wear_update(&s_wear, red_data, actual_count, ir_sum);
    *detect_flag = s_wear.detect_flag;

    uint32_t ir_avg = max30102_ir_avg(ir_sum, actual_count);
    if (prev == 0 && *detect_flag == 1) {
        ESP_LOGW(TAG, "not worn (IR avg=%lu, actual=%d)",
                 (unsigned long)ir_avg, actual_count);
    } else if (prev == 1 && *detect_flag == 0) {
        ESP_LOGI(TAG, "finger detected (IR avg=%lu, actual=%d)",
                 (unsigned long)ir_avg, actual_count);
    }
}

esp_err_t i2c_MAX30102_init(i2c_master_bus_handle_t bus_handle, const i2c_MAX30102_config_t *MAX30102_config,
                            i2c_MAX30102_handle_t *MAX30102_handle)
{
    if (!bus_handle || !MAX30102_config || !MAX30102_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_MAX30102_handle_t out_handle = (i2c_MAX30102_handle_t)calloc(1, sizeof(*out_handle));
    if (!out_handle) {
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = MAX30102_config->MAX30102_device.scl_speed_hz,
        .device_address = MAX30102_config->MAX30102_device.device_address,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &i2c_dev_conf, &out_handle->i2c_dev);
    if (err != ESP_OK) {
        free(out_handle);
        return err;
    }

    *MAX30102_handle = out_handle;
    return ESP_OK;
}

esp_err_t esp_MAX30102_set(i2c_MAX30102_handle_t MAX30102_handle)
{
    /**
     * Soft contact / watch-like PPG at ~100 Hz effective (matches SAMPLE_RATE).
     * Hard-press + bright LED drives IR near full-scale (e.g. ~220k/262k) and
     * warped pulse shape; use low drive + wide ADC range instead.
     *
     * FIFO_CONFIG 0x5F: SMP_AVE=4, ROLLOVER_EN, A_FULL=15
     * MODE 0x03: SpO2 (Red+IR)
     * SPO2_CONFIG 0x6F: ADC_RGE=16384 nA, SR=400, PW=411 µs → ~100 sps w/ avg4
     * LED_PA 0x1F (~6 mA): typical reflective / loosely worn drive
     */
    const uint8_t fifo_cfg = 0x5F;
    const uint8_t mode_cfg = 0x03;
    const uint8_t spo2_cfg = 0x6F;
    const uint8_t led_red = 0x1F;
    const uint8_t led_ir = 0x1F;
    const uint8_t led_pilot = 0x0F;

    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_INTR_ENABLE_1, 0xC0) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_INTR_ENABLE_2, 0x00) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_FIFO_WR_PTR, 0x00) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_OVF_COUNTER, 0x00) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_FIFO_RD_PTR, 0x00) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_FIFO_CONFIG, fifo_cfg) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_MODE_CONFIG, mode_cfg) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_SPO2_CONFIG, spo2_cfg) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_LED1_PA, led_red) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_LED2_PA, led_ir) != ESP_OK) {
        return ESP_FAIL;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_PILOT_PA, led_pilot) != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sensor cfg: FIFO=0x%02X SPO2=0x%02X LED_R/IR=0x%02X/0x%02X (eff ~100 Hz)",
             fifo_cfg, spo2_cfg, led_red, led_ir);
    return ESP_OK;
}

esp_err_t i2c_MAX30102_read_red(i2c_MAX30102_handle_t MAX30102_handle, gpio_num_t PIN_NUM_INT,
                                int *detect_flag, uint32_t *data, int data_size, int *got)
{
    (void)PIN_NUM_INT;

    if (MAX30102_handle == NULL || detect_flag == NULL || data == NULL ||
        data_size <= 0 || got == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status;
    uint8_t wr_ptr = 0;
    uint8_t rd_ptr = 0;
    uint8_t ovf = 0;
    uint64_t ir_sum = 0;
    int collected = 0;
    int min_samples = (data_size * MIN_BATCH_RATIO) / 10;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BATCH_TIMEOUT_MS);

    i2c_MAX30102_read_reg(MAX30102_handle, REG_INTR_STATUS_1, &status, 1);
    i2c_MAX30102_read_reg(MAX30102_handle, REG_INTR_STATUS_2, &status, 1);

    while (collected < data_size) {
        if (xTaskGetTickCount() >= deadline) {
            break;
        }

        if (fifo_read_ptrs(MAX30102_handle, &wr_ptr, &rd_ptr) != ESP_OK) {
            fifo_reset(MAX30102_handle);
            *got = 0;
            return ESP_FAIL;
        }
        if (fifo_read_ovf(MAX30102_handle, &ovf) != ESP_OK) {
            fifo_reset(MAX30102_handle);
            *got = 0;
            return ESP_FAIL;
        }
        if (fifo_is_full_overflow(wr_ptr, rd_ptr, ovf, collected)) {
            *got = 0;
            return fifo_on_overflow(MAX30102_handle, ovf);
        }

        int avail = fifo_available(wr_ptr, rd_ptr);
        if (avail <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        while (avail-- > 0 && collected < data_size) {
            uint32_t red_led = 0;
            uint32_t ir_led = 0;

            if (fifo_read_sample(MAX30102_handle, &red_led, &ir_led) != ESP_OK) {
                fifo_reset(MAX30102_handle);
                *got = 0;
                return ESP_FAIL;
            }

            data[collected] = red_led;
            ir_sum += ir_led;
            collected++;
        }
    }

    if (collected < min_samples) {
        ESP_LOGW(TAG, "FIFO underrun: got %d/%d samples, flush FIFO", collected, data_size);
        fifo_reset(MAX30102_handle);
        if (collected == 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            esp_MAX30102_set(MAX30102_handle);
        }
        *got = 0;
        return ESP_FAIL;
    }

    /* Do not pad: wear IR avg and the caller must use the real FIFO count. */
    if (collected < data_size) {
        ESP_LOGW(TAG, "partial batch: %d/%d samples (no pad)", collected, data_size);
    }
    *got = max30102_batch_publish_count(collected, data_size);
    update_wear_state(detect_flag, data, collected, ir_sum);
    return ESP_OK;
}

esp_err_t i2c_MAX30102_read_spo2_ppg(i2c_MAX30102_handle_t MAX30102_handle, gpio_num_t PIN_NUM_INT,
                                     int *detect_flag, uint32_t *red, uint32_t *ir,
                                     int max_samples, int *got)
{
    (void)PIN_NUM_INT;

    if (MAX30102_handle == NULL || detect_flag == NULL || red == NULL || ir == NULL ||
        got == NULL || max_samples <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status;
    uint8_t wr_ptr = 0;
    uint8_t rd_ptr = 0;
    uint8_t ovf = 0;
    uint64_t ir_sum = 0;
    int collected = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BATCH_TIMEOUT_MS);

    *got = 0;

    i2c_MAX30102_read_reg(MAX30102_handle, REG_INTR_STATUS_1, &status, 1);
    i2c_MAX30102_read_reg(MAX30102_handle, REG_INTR_STATUS_2, &status, 1);

    while (collected < max_samples) {
        if (xTaskGetTickCount() >= deadline) {
            break;
        }

        if (fifo_read_ptrs(MAX30102_handle, &wr_ptr, &rd_ptr) != ESP_OK) {
            fifo_reset(MAX30102_handle);
            return ESP_FAIL;
        }
        if (fifo_read_ovf(MAX30102_handle, &ovf) != ESP_OK) {
            fifo_reset(MAX30102_handle);
            return ESP_FAIL;
        }
        if (fifo_is_full_overflow(wr_ptr, rd_ptr, ovf, collected)) {
            *got = 0;
            return fifo_on_overflow(MAX30102_handle, ovf);
        }

        int avail = fifo_available(wr_ptr, rd_ptr);
        if (avail <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        while (avail-- > 0 && collected < max_samples) {
            uint32_t red_led = 0;
            uint32_t ir_led = 0;

            if (fifo_read_sample(MAX30102_handle, &red_led, &ir_led) != ESP_OK) {
                fifo_reset(MAX30102_handle);
                return ESP_FAIL;
            }

            red[collected] = red_led;
            ir[collected] = ir_led;
            ir_sum += ir_led;
            collected++;
        }
    }

    *got = collected;
    if (collected <= 0) {
        ESP_LOGW(TAG, "FIFO empty after %d ms", BATCH_TIMEOUT_MS);
        return ESP_FAIL;
    }

    update_wear_state(detect_flag, red, collected, ir_sum);
    return ESP_OK;
}

esp_err_t i2c_MAX30102_write_reg(i2c_MAX30102_handle_t MAX30102_handle, uint8_t address, const uint8_t data)
{
    if (MAX30102_handle == NULL || MAX30102_handle->i2c_dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buffer[] = {address, data};
    if (i2c_master_transmit(MAX30102_handle->i2c_dev, buffer, sizeof(buffer), -1) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t i2c_MAX30102_read_reg(i2c_MAX30102_handle_t MAX30102_handle, uint8_t address,
                                uint8_t *ach_i2c_data, int read_size)
{
    if (MAX30102_handle == NULL || MAX30102_handle->i2c_dev == NULL ||
        ach_i2c_data == NULL || read_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buffer[] = {address};
    if (i2c_master_transmit_receive(MAX30102_handle->i2c_dev, buffer, sizeof(buffer),
                                    ach_i2c_data, read_size, -1) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t maxim_max30102_reset(i2c_MAX30102_handle_t MAX30102_handle)
{
    if (MAX30102_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (i2c_MAX30102_write_reg(MAX30102_handle, REG_MODE_CONFIG, 0x40) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t maxim_max30102_shutdown(i2c_MAX30102_handle_t MAX30102_handle)
{
    if (MAX30102_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* MODE_CONFIG bit7 SHDN: analog/LEDs off; I2C stays up for bus teardown. */
    return i2c_MAX30102_write_reg(MAX30102_handle, REG_MODE_CONFIG, 0x80);
}
