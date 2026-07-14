/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_board_manager_includes.h"
#include "esp_max30102.h"
#include "max30102_wear.h"
#include "esp_health_spo2.h"
#include "ppg_spo2.h"
#include "spo2_freshness.h"
#include "spo2_meas_pub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "PPG_SPO2";

#define SAMPLE_RATE  100

#if (SAMPLE_RATE < 16) || (SAMPLE_RATE > 1000)
#error SAMPLE_RATE is outside esp_health_spo2 supported range
#endif  /* (SAMPLE_RATE < 16) || (SAMPLE_RATE > 1000) */

#define CHUNK_SAMPLES       32
#define WINDOW_SEC          5
#define WINDOW_SAMPLES      (WINDOW_SEC * SAMPLE_RATE)
#define RING_SAMPLES        WINDOW_SAMPLES
#define UPDATE_INTERVAL_MS  (WINDOW_SEC * 1000)
#define SPO2_MIN_CORR       0.55f
#define SPO2_MIN_AUT_RATIO  0.30f
#define HOLD_MIN_Q          0.12f
#define FAIL_STREAK_OFF     2
#define ACQ_TASK_STACK      4096
#define PROC_TASK_STACK     8192
#define ACQ_TASK_PRIO       12
#define PROC_TASK_PRIO      8

typedef struct {
    i2c_MAX30102_handle_t     sensor;
    esp_health_spo2_handle_t  spo2;
    SemaphoreHandle_t         mu;
    float                    *red_ring;
    float                    *ir_ring;
    int                       ring_count;
    int                       detect_flag;
    int                       detect_fail_streak;
    bool                      buffering_logged;
    bool                      finger_off_logged;
    bool                      need_reset;
    spo2_freshness_t          freshness;
} spo2_ctx_t;

static ppg_spo2_measurement_t s_meas;
static SemaphoreHandle_t s_meas_mu;
static float s_last_valid_spo2 = -1.0f;

ppg_spo2_measurement_t ppg_spo2_get_measurement(void)
{
    return spo2_meas_snapshot(s_meas_mu, &s_meas);
}

static void ring_append_pair(float *red_ring, float *ir_ring, int *count,
                             const float *red, const float *ir, int n, int cap)
{
    if (n <= 0) {
        return;
    }
    if (n > cap) {
        memcpy(red_ring, red + (n - cap), cap * sizeof(float));
        memcpy(ir_ring, ir + (n - cap), cap * sizeof(float));
        *count = cap;
        return;
    }
    if (*count + n > cap) {
        int drop = *count + n - cap;
        memmove(red_ring, red_ring + drop, (*count - drop) * sizeof(float));
        memmove(ir_ring, ir_ring + drop, (*count - drop) * sizeof(float));
        *count -= drop;
    }
    memcpy(red_ring + *count, red, n * sizeof(float));
    memcpy(ir_ring + *count, ir, n * sizeof(float));
    *count += n;
}

static void spo2_clear_session(spo2_ctx_t *ctx)
{
    ctx->ring_count = 0;
    ctx->buffering_logged = false;
    ctx->detect_fail_streak = 0;
    ctx->need_reset = true;
    spo2_freshness_reset(&ctx->freshness);
    s_last_valid_spo2 = -1.0f;
}

static void spo2_invalidate(spo2_ctx_t *ctx, bool log_now, const char *why)
{
    const ppg_spo2_measurement_t off = {
        .valid = false,
        .spo2 = 0.0f,
        .correlation = 0.0f,
        .quality = 0.0f,
        .worn = false,
        .err = ESP_HEALTH_ERR_FAIL,
    };

    spo2_clear_session(ctx);
    spo2_meas_store_locked(&s_meas, &off);

    if (log_now && !ctx->finger_off_logged) {
        ESP_LOGI(TAG, "SpO2: --  (%s)", why);
        ctx->finger_off_logged = true;
    }
}

static void spo2_mark_finger_off(spo2_ctx_t *ctx, bool log_now)
{
    spo2_invalidate(ctx, log_now, "finger off");
}

static void spo2_update_measurement(spo2_ctx_t *ctx, esp_health_err_t err,
                                    const esp_health_spo2_result_t *result, bool log_now)
{
    ppg_spo2_measurement_t next = {
        .err = err,
        .worn = true,
        .correlation = result ? result->correlation : 0.0f,
        .quality = result ? result->quality_ratio : 0.0f,
        .valid = false,
        .spo2 = 0.0f,
    };

    ctx->finger_off_logged = false;

    if (err == ESP_HEALTH_ERR_OK && result != NULL &&
        result->spo2 >= 70.0f &&
        result->spo2 <= 100.0f) {
        ctx->detect_fail_streak = 0;
        s_last_valid_spo2 = result->spo2;
        next.spo2 = result->spo2;
        next.valid = true;
        spo2_meas_store_locked(&s_meas, &next);
        if (log_now) {
            ESP_LOGI(TAG, "SpO2: spo2=%.1f%% corr=%.2f q=%.2f err=%d",
                     next.spo2, next.correlation, next.quality, (int)next.err);
        }
        return;
    }

    if (s_last_valid_spo2 >= 0.0f && result && result->quality_ratio >= HOLD_MIN_Q) {
        ctx->detect_fail_streak = 0;
        next.spo2 = s_last_valid_spo2;
        next.valid = true;
        spo2_meas_store_locked(&s_meas, &next);
        return;
    }

    ctx->detect_fail_streak++;
    if (ctx->detect_fail_streak >= FAIL_STREAK_OFF) {
        spo2_mark_finger_off(ctx, log_now);
        return;
    }

    spo2_meas_store_locked(&s_meas, &next);
    if (log_now) {
        ESP_LOGI(TAG, "SpO2: spo2=-- corr=%.2f q=%.2f err=%d",
                 next.correlation, next.quality, (int)next.err);
    }
}

static void spo2_acq_task(void *arg)
{
    spo2_ctx_t *ctx = (spo2_ctx_t *)arg;
    if (ctx == NULL) {
        ESP_LOGE(TAG, "acq_task error");
        vTaskDelete(NULL);
        return;
    }
    uint32_t *red_chunk = malloc(CHUNK_SAMPLES * sizeof(uint32_t));
    uint32_t *ir_chunk = malloc(CHUNK_SAMPLES * sizeof(uint32_t));
    int prev_detect_flag = ctx->detect_flag;

    TickType_t last_fail_log = 0;
    TickType_t last_progress_log = 0;
    TickType_t last_ovf_log = 0;
    int fail_count = 0;
    int overflow_latched = 0;

    if (!red_chunk || !ir_chunk) {
        ESP_LOGE(TAG, "acq_task malloc failed");
        free(red_chunk);
        free(ir_chunk);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "acq task running");

    while (true) {
        int detect_flag;
        int got = 0;

        xSemaphoreTake(ctx->mu, portMAX_DELAY);
        detect_flag = ctx->detect_flag;
        xSemaphoreGive(ctx->mu);

        esp_err_t rd = i2c_MAX30102_read_spo2_ppg(ctx->sensor, MAX30102_INT_GPIO,
                                                  &detect_flag, red_chunk, ir_chunk,
                                                  CHUNK_SAMPLES, &got);
        if (rd == MAX30102_ERR_FIFO_OVERFLOW) {
            xSemaphoreTake(ctx->mu, portMAX_DELAY);
            ctx->detect_flag = detect_flag;
            spo2_on_fifo_overflow(&ctx->ring_count, &ctx->need_reset, &ctx->freshness);
            ctx->buffering_logged = false;
            s_last_valid_spo2 = -1.0f;
            if (!overflow_latched) {
                const ppg_spo2_measurement_t drop = {
                    .valid = false,
                    .spo2 = 0.0f,
                    .correlation = 0.0f,
                    .quality = 0.0f,
                    .worn = (ctx->detect_flag == 0),
                    .err = ESP_HEALTH_ERR_FAIL,
                };
                spo2_meas_store_locked(&s_meas, &drop);
                overflow_latched = 1;
            }
            TickType_t now = xTaskGetTickCount();
            if (last_ovf_log == 0 || now - last_ovf_log >= pdMS_TO_TICKS(1000)) {
                ESP_LOGW(TAG, "SpO2: --  (fifo overflow)");
                last_ovf_log = now;
            }
            xSemaphoreGive(ctx->mu);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        overflow_latched = 0;
        if (rd != ESP_OK || got <= 0) {
            fail_count++;
            TickType_t now = xTaskGetTickCount();
            if (last_fail_log == 0 || now - last_fail_log >= pdMS_TO_TICKS(2000)) {
                ESP_LOGW(TAG, "FIFO read fail err=%d got=%d (x%d)", (int)rd, got, fail_count);
                last_fail_log = now;
            }
            xSemaphoreTake(ctx->mu, portMAX_DELAY);
            if (spo2_freshness_on_fail(&ctx->freshness)) {
                spo2_invalidate(ctx, true, "sensor lost");
            }
            xSemaphoreGive(ctx->mu);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        fail_count = 0;

        float red_f[CHUNK_SAMPLES];
        float ir_f[CHUNK_SAMPLES];
        for (int i = 0; i < got; i++) {
            red_f[i] = (float)red_chunk[i];
            ir_f[i] = (float)ir_chunk[i];
        }

        xSemaphoreTake(ctx->mu, portMAX_DELAY);
        ctx->detect_flag = detect_flag;
        if (ctx->detect_flag == 0 && prev_detect_flag != 0) {
            ctx->detect_fail_streak = 0;
            ctx->finger_off_logged = false;
        }
        prev_detect_flag = ctx->detect_flag;

        bool worn = (ctx->detect_flag == 0);
        if (!worn) {
            spo2_mark_finger_off(ctx, true);
            xSemaphoreGive(ctx->mu);
            TickType_t now = xTaskGetTickCount();
            if (last_progress_log == 0 ||
                now - last_progress_log >= pdMS_TO_TICKS(2000)) {
                uint64_t ir_sum = 0;
                for (int i = 0; i < got; i++) {
                    ir_sum += ir_chunk[i];
                }
                ESP_LOGI(TAG, "waiting for finger (IR avg=%lu)",
                         (unsigned long)max30102_ir_avg(ir_sum, got));
                last_progress_log = now;
            }
            continue;
        }

        ring_append_pair(ctx->red_ring, ctx->ir_ring, &ctx->ring_count,
                         red_f, ir_f, got, RING_SAMPLES);
        spo2_freshness_on_samples(&ctx->freshness);
        int n = ctx->ring_count;
        if (n < WINDOW_SAMPLES && !ctx->buffering_logged) {
            ctx->buffering_logged = true;
            ESP_LOGI(TAG, "Collecting first %ds window...", WINDOW_SEC);
        }
        xSemaphoreGive(ctx->mu);

        TickType_t now = xTaskGetTickCount();
        if (n < WINDOW_SAMPLES &&
            (last_progress_log == 0 || now - last_progress_log >= pdMS_TO_TICKS(1000))) {
            ESP_LOGI(TAG, "buffering %d/%d (keep finger on sensor)", n, WINDOW_SAMPLES);
            last_progress_log = now;
        }
    }
}

static void spo2_proc_task(void *arg)
{
    spo2_ctx_t *ctx = (spo2_ctx_t *)arg;
    if (ctx == NULL) {
        ESP_LOGE(TAG, "proc_task error");
        vTaskDelete(NULL);
        return;
    }
    float *red_snap = malloc(WINDOW_SAMPLES * sizeof(float));
    float *ir_snap = malloc(WINDOW_SAMPLES * sizeof(float));

    if (!red_snap || !ir_snap) {
        ESP_LOGE(TAG, "proc_task malloc failed");
        free(red_snap);
        free(ir_snap);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "proc task running");

    TickType_t last_process_tick = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));

        TickType_t now = xTaskGetTickCount();
        if (last_process_tick != 0 &&
            now - last_process_tick < pdMS_TO_TICKS(UPDATE_INTERVAL_MS)) {
            continue;
        }

        xSemaphoreTake(ctx->mu, portMAX_DELAY);
        bool worn = (ctx->detect_flag == 0);
        int ring_count = ctx->ring_count;
        bool ready = worn && spo2_freshness_should_process(&ctx->freshness, ring_count, WINDOW_SAMPLES);
        bool do_reset = false;
        if (ready) {
            const float *red_win = ctx->red_ring + (ring_count - WINDOW_SAMPLES);
            const float *ir_win = ctx->ir_ring + (ring_count - WINDOW_SAMPLES);
            memcpy(red_snap, red_win, WINDOW_SAMPLES * sizeof(float));
            memcpy(ir_snap, ir_win, WINDOW_SAMPLES * sizeof(float));
            do_reset = ctx->need_reset;
            /* Claim now so a later finger-off is not cleared by this in-flight process. */
            ctx->need_reset = false;
        }
        xSemaphoreGive(ctx->mu);

        if (!worn || !ready) {
            continue;
        }

        last_process_tick = now;

        if (do_reset) {
            /* Same task as process(): handle is not thread-safe. */
            esp_health_spo2_reset(ctx->spo2);
        }

        esp_health_spo2_result_t result = {0};
        esp_health_err_t err = esp_health_spo2_process(ctx->spo2, ir_snap, red_snap,
                                                       WINDOW_SAMPLES, &result);

        xSemaphoreTake(ctx->mu, portMAX_DELAY);
        /* Finger-off / overflow during process() must not publish the old window. */
        if (!spo2_should_commit(ctx->detect_flag == 0, ctx->need_reset)) {
            xSemaphoreGive(ctx->mu);
            continue;
        }
        spo2_freshness_mark_processed(&ctx->freshness);
        spo2_update_measurement(ctx, err, &result, true);
        xSemaphoreGive(ctx->mu);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Init board + MAX30102 + esp_health_spo2");

    ESP_ERROR_CHECK(esp_board_manager_init());
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_MAX30102_PPG));

    max30102_dev_handle_t *dev = NULL;
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_MAX30102_PPG,
                                                        (void **)&dev));
    if (dev == NULL || dev->sensor == NULL) {
        ESP_LOGE(TAG, "MAX30102 handle is NULL");
        return;
    }

    esp_health_spo2_cfg_t cfg = ESP_HEALTH_SPO2_CFG_DEFAULT();
    cfg.sample_rate = SAMPLE_RATE;
    cfg.min_correlation = SPO2_MIN_CORR;
    cfg.min_autocorrelation_ratio = SPO2_MIN_AUT_RATIO;
    esp_health_spo2_handle_t spo2 = NULL;
    if (esp_health_spo2_open(&cfg, &spo2) != ESP_HEALTH_ERR_OK || !spo2) {
        ESP_LOGE(TAG, "esp_health_spo2_open failed");
        return;
    }

    static spo2_ctx_t ctx = {
        .sensor = NULL,
        .spo2 = NULL,
        .mu = NULL,
        .red_ring = NULL,
        .ir_ring = NULL,
        .ring_count = 0,
        .detect_flag = 1,
        .detect_fail_streak = 0,
        .buffering_logged = false,
        .finger_off_logged = false,
        .need_reset = false,
        .freshness = {0},
    };
    ctx.sensor = dev->sensor;
    ctx.spo2 = spo2;
    ctx.mu = xSemaphoreCreateMutex();
    ctx.red_ring = calloc(RING_SAMPLES, sizeof(float));
    ctx.ir_ring = calloc(RING_SAMPLES, sizeof(float));
    if (!ctx.mu || !ctx.red_ring || !ctx.ir_ring) {
        ESP_LOGE(TAG, "spo2 ctx alloc failed");
        if (ctx.mu) {
            vSemaphoreDelete(ctx.mu);
        }
        free(ctx.red_ring);
        free(ctx.ir_ring);
        esp_health_spo2_close(spo2);
        return;
    }

    memset(&s_meas, 0, sizeof(s_meas));
    s_meas_mu = ctx.mu;

    ESP_LOGI(TAG, "heap free=%u, start acq/proc tasks", (unsigned)esp_get_free_heap_size());
    TaskHandle_t acq_h = NULL;
    if (xTaskCreate(spo2_acq_task, "spo2_acq", ACQ_TASK_STACK, &ctx, ACQ_TASK_PRIO, &acq_h) != pdPASS) {
        ESP_LOGE(TAG, "failed to create spo2_acq");
        s_meas_mu = NULL;
        vSemaphoreDelete(ctx.mu);
        free(ctx.red_ring);
        free(ctx.ir_ring);
        esp_health_spo2_close(spo2);
        return;
    }
    if (xTaskCreate(spo2_proc_task, "spo2_proc", PROC_TASK_STACK, &ctx, PROC_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create spo2_proc");
        if (acq_h != NULL) {
            vTaskDelete(acq_h);
        }
        s_meas_mu = NULL;
        vSemaphoreDelete(ctx.mu);
        free(ctx.red_ring);
        free(ctx.ir_ring);
        esp_health_spo2_close(spo2);
        return;
    }
}
