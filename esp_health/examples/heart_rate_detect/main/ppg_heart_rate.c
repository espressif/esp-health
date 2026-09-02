/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_board_manager_includes.h"
#include "esp_max30102.h"
#include "esp_health_hr.h"
#include "hr_ppg_msg.h"
#include "hr_example_win.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "PPG_HEART_RATE";

#define QUEUE_SEC     30
#define RING_SEC      15
#define ANALYSIS_SEC  HR_EXAMPLE_ANALYSIS_SEC
#define UPDATE_MS     (HR_EXAMPLE_UPDATE_SEC * 1000)
#define SAMPLE_RATE   100

#if (SAMPLE_RATE < 16) || (SAMPLE_RATE > 1000)
#error SAMPLE_RATE is outside esp_health_hr supported range
#endif  /* (SAMPLE_RATE < 16) || (SAMPLE_RATE > 1000) */

#define RING_SAMPLES      (RING_SEC * SAMPLE_RATE)
#define ANALYSIS_SAMPLES  (ANALYSIS_SEC * SAMPLE_RATE)
#define MIN_RING_SAMPLES  SAMPLE_RATE

#define HR_EMA_ALPHA_FAST  0.55f
#define HR_EMA_ALPHA_SLOW  0.30f

#define HR_HOLD_MAX_SEC      8
#define MIN_PUBLISH_SAMPLES  (SAMPLE_RATE * HR_EXAMPLE_MIN_WINDOW_SEC)

#define DATA_TASK_STACK  4096
#define ALGO_TASK_STACK  8192
#define DATA_TASK_PRIO   12
#define ALGO_TASK_PRIO   8

typedef struct {
    uint32_t  samples[SAMPLE_RATE];
    int       kind;  /* HR_STREAM_* */
    int       n;     /* real samples in this item; 0 if kind is not SAMPLES */
} hr_ppg_msg_t;

typedef struct {
    esp_health_hr_handle_t  hr_handle;
    QueueHandle_t           ppg_queue;
    atomic_int             *heart_rate;
} algorithm_thread_arg;

typedef struct {
    i2c_MAX30102_handle_t  MAX30102_handle;
    QueueHandle_t          ppg_queue;
} data_thread_arg;

static void HR_LOG_EX(int bpm, int win_sec)
{
    ESP_LOGI(TAG, "Heart Rate: %d BPM (win=%ds)", bpm, win_sec);
}

static void HR_LOG_HOLD(int bpm, int win_sec)
{
    ESP_LOGI(TAG, "Heart Rate: %d BPM (win=%ds, hold)", bpm, win_sec);
}

static void HR_LOG_MEASURING(int win_sec)
{
    ESP_LOGI(TAG, "Heart Rate: -- (win=%ds, measuring)", win_sec);
}

static void publish_hold(float hr_ema, atomic_int *heart_rate, int win_sec, int *miss_streak)
{
    (*miss_streak)++;
    if (hr_ema >= 40.0f && *miss_streak <= HR_HOLD_MAX_SEC) {
        atomic_store(heart_rate, (int)lrintf(hr_ema));
        HR_LOG_HOLD(atomic_load(heart_rate), win_sec);
        return;
    }
    HR_LOG_MEASURING(win_sec);
}

static float ema_alpha_for_window(int analyze_n)
{
    if (analyze_n < SAMPLE_RATE * 5) {
        return HR_EMA_ALPHA_FAST;
    }
    return HR_EMA_ALPHA_SLOW;
}

static void publish_hr(float *hr_ema, atomic_int *heart_rate, float bpm, int analyze_n, int *miss_streak)
{
    float alpha = ema_alpha_for_window(analyze_n);
    if (*hr_ema < 0.0f) {
        *hr_ema = bpm;
    } else {
        *hr_ema = alpha * bpm + (1.0f - alpha) * (*hr_ema);
    }
    *miss_streak = 0;
    atomic_store(heart_rate, (int)lrintf(*hr_ema));
    HR_LOG_EX(atomic_load(heart_rate), analyze_n / SAMPLE_RATE);
}

static void discard_queue(QueueHandle_t q, hr_ppg_msg_t *msg)
{
    while (xQueueReceive(q, msg, 0) == pdTRUE) {
    }
}

static int ring_push(float *ring, int *ring_count, const float *src, int n)
{
    if (n > RING_SAMPLES) {
        memcpy(ring, src + (n - RING_SAMPLES), RING_SAMPLES * sizeof(float));
        *ring_count = RING_SAMPLES;
        return RING_SAMPLES;
    }
    if (*ring_count + n > RING_SAMPLES) {
        int drop = *ring_count + n - RING_SAMPLES;
        memmove(ring, ring + drop, (*ring_count - drop) * sizeof(float));
        *ring_count -= drop;
    }
    memcpy(ring + *ring_count, src, n * sizeof(float));
    *ring_count += n;
    return *ring_count;
}

static void mark_finger_off(float *hr_ema, atomic_int *heart_rate, int *miss_streak, int *was_worn, int *ring_count)
{
    *ring_count = 0;
    *hr_ema = -1.0f;
    *miss_streak = 0;
    atomic_store(heart_rate, -1);
    if (*was_worn) {
        *was_worn = 0;
        ESP_LOGI(TAG, "Heart Rate: -- (finger off)");
    }
}

static void push_msg_samples(float *ring, int *ring_count, const uint32_t *samples, int n)
{
    float sec_f[SAMPLE_RATE];
    if (n <= 0) {
        return;
    }
    if (n > SAMPLE_RATE) {
        n = SAMPLE_RATE;
    }
    for (int i = 0; i < n; i++) {
        sec_f[i] = (float)samples[i];
    }
    ring_push(ring, ring_count, sec_f, n);
}

static void algorithm_task(void *arg)
{
    algorithm_thread_arg *args = (algorithm_thread_arg *)arg;
    if (args == NULL) {
        ESP_LOGE(TAG, "algorithm task error");
        vTaskDelete(NULL);
        return;
    }

    hr_ppg_msg_t *msg = (hr_ppg_msg_t *)malloc(sizeof(*msg));
    float *ring = (float *)malloc(RING_SAMPLES * sizeof(float));

    if (!msg || !ring) {
        ESP_LOGE(TAG, "algorithm task malloc failed");
        free(msg);
        free(ring);
        vTaskDelete(NULL);
        return;
    }

    int ring_count = 0;
    int was_worn = 0;
    int miss_streak = 0;
    float hr_ema = -1.0f;

    while (true) {
        int new_batches = 0;
        int saw_gap = 0;
        hr_stream_state_t st = {
            .worn = was_worn,
            .drop_ring = 0,
        };

        /* Wear / gap travel with each queue item (FreeRTOS copy = happens-before). */
        while (xQueueReceive(args->ppg_queue, msg, 0) == pdTRUE) {
            hr_stream_apply(&st, msg->kind);
            if (st.drop_ring) {
                ring_count = 0;
            }
            if (msg->kind == HR_STREAM_FIFO_GAP) {
                hr_ema = -1.0f;
                miss_streak = 0;
                atomic_store(args->heart_rate, -1);
                saw_gap = 1;
                continue;
            }
            if (msg->kind == HR_STREAM_FINGER_OFF) {
                mark_finger_off(&hr_ema, args->heart_rate, &miss_streak, &was_worn, &ring_count);
                continue;
            }
            if (!was_worn) {
                hr_ema = -1.0f;
                miss_streak = 0;
                was_worn = 1;
                ESP_LOGI(TAG, "Heart Rate: -- (measuring)");
            }
            push_msg_samples(ring, &ring_count, msg->samples, msg->n);
            new_batches++;
        }

        if (saw_gap) {
            ESP_LOGW(TAG, "FIFO overflow, drop window");
        }

        if (!was_worn) {
            vTaskDelay(pdMS_TO_TICKS(UPDATE_MS));
            continue;
        }

        if (new_batches == 0) {
            /* No new second this tick: hold last BPM, do not re-run the old window. */
            if (!saw_gap && ring_count >= MIN_RING_SAMPLES) {
                int win_sec = ring_count / SAMPLE_RATE;
                if (win_sec > ANALYSIS_SEC) {
                    win_sec = ANALYSIS_SEC;
                }
                if (win_sec < 1) {
                    win_sec = 1;
                }
                publish_hold(hr_ema, args->heart_rate, win_sec, &miss_streak);
            }
            vTaskDelay(pdMS_TO_TICKS(UPDATE_MS));
            continue;
        }

        if (ring_count < MIN_PUBLISH_SAMPLES) {
            HR_LOG_MEASURING(ring_count / SAMPLE_RATE);
            vTaskDelay(pdMS_TO_TICKS(UPDATE_MS));
            continue;
        }

        int analyze_n = ring_count;
        if (analyze_n > ANALYSIS_SAMPLES) {
            analyze_n = ANALYSIS_SAMPLES;
        }
        int win_sec = analyze_n / SAMPLE_RATE;
        const float *window = ring + (ring_count - analyze_n);

        esp_health_hr_result_t result = {0};
        float bpm = 0.0f;
        if (esp_health_hr_process(args->hr_handle, window, analyze_n, &result) == ESP_HEALTH_ERR_OK) {
            bpm = (float)result.avg_bpm;
        }
        if (bpm >= 40.0f && bpm <= 200.0f) {
            publish_hr(&hr_ema, args->heart_rate, bpm, analyze_n, &miss_streak);
        } else {
            publish_hold(hr_ema, args->heart_rate, win_sec, &miss_streak);
        }

        vTaskDelay(pdMS_TO_TICKS(UPDATE_MS));
    }
}

static void data_task(void *arg)
{
    data_thread_arg *args = (data_thread_arg *)arg;

    if (args == NULL) {
        ESP_LOGE(TAG, "Data task error");
        vTaskDelete(NULL);
        return;
    }

    hr_ppg_msg_t *msg = (hr_ppg_msg_t *)malloc(sizeof(*msg));
    hr_ppg_msg_t *throw_msg = (hr_ppg_msg_t *)malloc(sizeof(*throw_msg));
    if (!msg || !throw_msg) {
        ESP_LOGE(TAG, "data task malloc failed");
        free(msg);
        free(throw_msg);
        vTaskDelete(NULL);
        return;
    }

    /* Local to this task: driver hysteresis; published only via the queue. */
    int detect_flag = 1;
    int posted_gap = 0;

    while (true) {
        int got = 0;
        esp_err_t err = i2c_MAX30102_read_red(args->MAX30102_handle, MAX30102_INT_GPIO, &detect_flag,
                                              msg->samples, SAMPLE_RATE, &got);
        if (err == MAX30102_ERR_FIFO_OVERFLOW) {
            if (!posted_gap) {
                discard_queue(args->ppg_queue, throw_msg);
                msg->kind = HR_STREAM_FIFO_GAP;
                msg->n = 0;
                xQueueSend(args->ppg_queue, msg, portMAX_DELAY);
                posted_gap = 1;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        posted_gap = 0;
        if (err != ESP_OK || got <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        msg->kind = detect_flag ? HR_STREAM_FINGER_OFF : HR_STREAM_SAMPLES;
        msg->n = (msg->kind == HR_STREAM_SAMPLES) ? got : 0;
        if (uxQueueMessagesWaiting(args->ppg_queue) >= QUEUE_SEC) {
            xQueueReceive(args->ppg_queue, throw_msg, 0);
        }
        xQueueSend(args->ppg_queue, msg, portMAX_DELAY);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[ 1 ] Initialize Board Manager");
    ESP_ERROR_CHECK(esp_board_manager_init());

    ESP_LOGI(TAG, "[ 2 ] Initialize MAX30102 PPG device");
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_MAX30102_PPG));

    max30102_dev_handle_t *max30102_dev = NULL;
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_MAX30102_PPG,
                                                        (void **)&max30102_dev));
    if (max30102_dev == NULL || max30102_dev->sensor == NULL) {
        ESP_LOGE(TAG, "MAX30102 handle is NULL");
        return;
    }
    i2c_MAX30102_handle_t MAX30102_handle = max30102_dev->sensor;

    ESP_LOGI(TAG, "[ 3 ] Initialize PPG queue and algorithm");
    static atomic_int heart_rate = ATOMIC_VAR_INIT(-1);
    static algorithm_thread_arg algorithm_arg;
    static data_thread_arg data_arg;

    QueueHandle_t ppg_queue = xQueueCreate(QUEUE_SEC, sizeof(hr_ppg_msg_t));
    if (ppg_queue == NULL) {
        ESP_LOGE(TAG, "Queue create fail");
        return;
    }

    esp_health_hr_cfg_t hr_cfg = {
        .sample_rate = SAMPLE_RATE,
    };
    esp_health_hr_handle_t hr_handle = NULL;
    if (esp_health_hr_open(&hr_cfg, &hr_handle) != ESP_HEALTH_ERR_OK || !hr_handle) {
        ESP_LOGE(TAG, "esp_health_hr_open failed");
        vQueueDelete(ppg_queue);
        return;
    }

    ESP_LOGI(TAG, "[ 4 ] Start data + algorithm tasks");
    algorithm_arg = (algorithm_thread_arg) {
        .hr_handle = hr_handle,
        .heart_rate = &heart_rate,
        .ppg_queue = ppg_queue,
    };
    data_arg = (data_thread_arg) {
        .MAX30102_handle = MAX30102_handle,
        .ppg_queue = ppg_queue,
    };

    TaskHandle_t data_task_h = NULL;
    if (xTaskCreate(data_task, "hr_data", DATA_TASK_STACK, &data_arg, DATA_TASK_PRIO, &data_task_h) != pdPASS) {
        ESP_LOGE(TAG, "failed to create hr_data");
        esp_health_hr_close(hr_handle);
        vQueueDelete(ppg_queue);
        return;
    }
    if (xTaskCreate(algorithm_task, "hr_algo", ALGO_TASK_STACK, &algorithm_arg, ALGO_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create hr_algo");
        if (data_task_h != NULL) {
            vTaskDelete(data_task_h);
        }
        esp_health_hr_close(hr_handle);
        vQueueDelete(ppg_queue);
        return;
    }
}
