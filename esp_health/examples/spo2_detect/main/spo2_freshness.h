/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* Consecutive FIFO/I2C failures before the session is dropped.
 * Acquisition retries every ~20 ms, so 50 ~= 1 s of NACKs (sensor unplugged). */
#define SPO2_ACQ_FAIL_INVALIDATE  50

typedef struct {
    uint32_t  sample_gen;     /*!< Bumped on each successful FIFO chunk */
    uint32_t  processed_gen;  /*!< sample_gen last fed to esp_health_spo2_process */
    int       fail_streak;    /*!< Consecutive failed reads */
} spo2_freshness_t;

static inline void spo2_freshness_on_samples(spo2_freshness_t *f)
{
    f->fail_streak = 0;
    f->sample_gen++;
}

/* Returns true once failures have reached SPO2_ACQ_FAIL_INVALIDATE. */
static inline bool spo2_freshness_on_fail(spo2_freshness_t *f)
{
    if (f->fail_streak < SPO2_ACQ_FAIL_INVALIDATE) {
        f->fail_streak++;
    }
    return f->fail_streak >= SPO2_ACQ_FAIL_INVALIDATE;
}

static inline bool spo2_freshness_should_process(const spo2_freshness_t *f,
                                                 int ring_count, int window)
{
    return ring_count >= window && f->sample_gen != f->processed_gen;
}

static inline void spo2_freshness_mark_processed(spo2_freshness_t *f)
{
    f->processed_gen = f->sample_gen;
}

static inline void spo2_freshness_reset(spo2_freshness_t *f)
{
    f->fail_streak = 0;
    f->sample_gen = 0;
    f->processed_gen = 0;
}

/* After process(): do not publish if wear was lost or the session was reset. */
static inline bool spo2_should_commit(bool worn, bool need_reset)
{
    return worn && !need_reset;
}

/* FIFO overflow: drop the ring so post-gap samples are not spliced onto the old window. */
static inline void spo2_on_fifo_overflow(int *ring_count, bool *need_reset, spo2_freshness_t *f)
{
    *ring_count = 0;
    *need_reset = true;
    spo2_freshness_reset(f);
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
