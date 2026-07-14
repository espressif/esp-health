/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define I2C_DEVICE_ADDR  0x57

#define ESP_BOARD_DEVICE_NAME_MAX30102_PPG  "max30102_ppg"

#define MAX30102_INT_GPIO  GPIO_NUM_21

/* FIFO overflow: samples were lost. Caller must drop the stream window. */
#define MAX30102_ERR_FIFO_OVERFLOW  ESP_ERR_INVALID_STATE

// register addresses
#define REG_INTR_STATUS_1    0x00
#define REG_INTR_STATUS_2    0x01
#define REG_INTR_ENABLE_1    0x02
#define REG_INTR_ENABLE_2    0x03
#define REG_FIFO_WR_PTR      0x04
#define REG_OVF_COUNTER      0x05
#define REG_FIFO_RD_PTR      0x06
#define REG_FIFO_DATA        0x07
#define REG_FIFO_CONFIG      0x08
#define REG_MODE_CONFIG      0x09
#define REG_SPO2_CONFIG      0x0A
#define REG_LED1_PA          0x0C
#define REG_LED2_PA          0x0D
#define REG_PILOT_PA         0x10
#define REG_MULTI_LED_CTRL1  0x11
#define REG_MULTI_LED_CTRL2  0x12
#define REG_TEMP_INTR        0x1F
#define REG_TEMP_FRAC        0x20
#define REG_TEMP_CONFIG      0x21
#define REG_PROX_INT_THRESH  0x30
#define REG_REV_ID           0xFE
#define REG_PART_ID          0xFF

typedef struct {
    i2c_device_config_t  MAX30102_device;
} i2c_MAX30102_config_t;

struct i2c_MAX30102_t {
    i2c_master_dev_handle_t  i2c_dev;
};

typedef struct i2c_MAX30102_t *i2c_MAX30102_handle_t;

typedef struct {
    i2c_MAX30102_handle_t  sensor;
} max30102_dev_handle_t;

esp_err_t i2c_MAX30102_init(i2c_master_bus_handle_t bus_handle, const i2c_MAX30102_config_t *MAX30102_config,
                            i2c_MAX30102_handle_t *MAX30102_handle);

esp_err_t esp_MAX30102_set(i2c_MAX30102_handle_t MAX30102_handle);

esp_err_t i2c_MAX30102_read_red(i2c_MAX30102_handle_t MAX30102_handle, gpio_num_t PIN_NUM_INT, int *detect_flag,
                                uint32_t *data, int data_size, int *got);

esp_err_t i2c_MAX30102_write_reg(i2c_MAX30102_handle_t MAX30102_handle, uint8_t address, const uint8_t data);

esp_err_t i2c_MAX30102_read_reg(i2c_MAX30102_handle_t MAX30102_handle, uint8_t address, uint8_t *ach_i2c_data,
                                int read_size);

esp_err_t maxim_max30102_reset(i2c_MAX30102_handle_t MAX30102_handle);

/** Put the sensor in SHDN (LEDs/analog off). I2C remains usable. */
esp_err_t maxim_max30102_shutdown(i2c_MAX30102_handle_t MAX30102_handle);
