/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define MAX30102_IR_WEAR_MIN      5000U
#define MAX30102_IR_WEAR_STRONG   20000U
#define MAX30102_RED_AC_MIN_PP    20U
#define MAX30102_RED_AC_ON_PP     30U
#define MAX30102_IR_OFF_STREAK    3
#define MAX30102_IR_ON_STREAK     2
#define MAX30102_FLAT_STREAK_OFF  4

typedef struct {
    int  detect_flag;  /* 1 = finger off, 0 = worn */
    int  ir_off_streak;
    int  ir_on_streak;
    int  flat_streak;
} max30102_wear_t;

static inline uint32_t max30102_ir_avg(uint64_t ir_sum, int actual_count)
{
    if (actual_count <= 0) {
        return 0;
    }
    return (uint32_t)(ir_sum / (uint64_t)actual_count);
}

/* Wear and the caller use the real FIFO count. Never pad up to requested. */
static inline int max30102_batch_publish_count(int actual_count, int requested)
{
    (void)requested;
    return actual_count;
}

static inline void max30102_wear_update(max30102_wear_t *w,
                                        const uint32_t *red_data,
                                        int actual_count,
                                        uint64_t ir_sum)
{
    if (w == NULL || red_data == NULL || actual_count <= 0) {
        return;
    }

    uint32_t ir_avg = max30102_ir_avg(ir_sum, actual_count);
    uint32_t red_min = red_data[0];
    uint32_t red_max = red_data[0];

    for (int i = 1; i < actual_count; i++) {
        if (red_data[i] < red_min) {
            red_min = red_data[i];
        }
        if (red_data[i] > red_max) {
            red_max = red_data[i];
        }
    }
    uint32_t red_pp = red_max - red_min;

    if (ir_avg < MAX30102_IR_WEAR_MIN) {
        w->flat_streak = 0;
        w->ir_on_streak = 0;
        w->ir_off_streak++;
        if (w->ir_off_streak >= MAX30102_IR_OFF_STREAK && w->detect_flag == 0) {
            w->detect_flag = 1;
            w->ir_off_streak = 0;
        }
        return;
    }

    w->ir_off_streak = 0;

    /* Off ? on: IR DC is enough. A short FIFO chunk rarely covers one pulse,
     * so do not require red peak-to-peak to start a session. */
    if (w->detect_flag == 1) {
        if (ir_avg >= MAX30102_IR_WEAR_STRONG ||
            red_pp >= MAX30102_RED_AC_ON_PP) {
            w->ir_on_streak = 0;
            w->flat_streak = 0;
            w->detect_flag = 0;
            return;
        }
        w->ir_on_streak++;
        if (w->ir_on_streak >= MAX30102_IR_ON_STREAK) {
            w->ir_on_streak = 0;
            w->flat_streak = 0;
            w->detect_flag = 0;
        }
        return;
    }

    if (ir_avg >= MAX30102_IR_WEAR_STRONG) {
        w->flat_streak = 0;
        return;
    }

    if (red_pp < MAX30102_RED_AC_MIN_PP) {
        w->flat_streak++;
        if (w->flat_streak >= MAX30102_FLAT_STREAK_OFF) {
            w->detect_flag = 1;
            w->flat_streak = 0;
        }
        return;
    }

    w->flat_streak = 0;
}
