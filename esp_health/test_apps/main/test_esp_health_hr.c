/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_health_hr.h"
#include "hr_s1_data.h"
#include "hr_example_win.h"

#define TAG  "TEST_HR"

#define HR_TEST_SAMPLE_RATE       HR_S1_SAMPLE_RATE
#define HR_TEST_RING_SEC          15
#define HR_TEST_LABEL_SEC         HR_S1_LABEL_SEC
#define HR_TEST_RING_SAMPLES      (HR_TEST_SAMPLE_RATE * HR_TEST_RING_SEC)
#define HR_TEST_MIN_SAMPLES       (HR_TEST_SAMPLE_RATE * HR_EXAMPLE_MIN_WINDOW_SEC)
#define HR_TEST_ANALYSIS_SAMPLES  (HR_TEST_SAMPLE_RATE * HR_EXAMPLE_ANALYSIS_SEC)
#define HR_TEST_MIN_BPM           40.0f
#define HR_TEST_MAX_BPM           200.0f
#define S1_STEADY_MAE_MAX         15.0
#define S1_STEADY_VALID_RATE_MIN  0.55
#define BRANCH_LOG(tc, desc)      ESP_LOGI(TAG, "---- %s : %s ----", (tc), (desc))

typedef struct {
    double  mae;
    double  rmse;
    int     total;
    int     valid;
    double  abs_sum;
    double  sq_sum;
} hr_test_stats_t;

typedef struct {
    hr_test_stats_t  mae;  /* Labeled even seconds only */
    int              proc_total;
    int              proc_valid;
    int              first_process_sec;
} hr_phase_stats_t;

static void hr_test_stats_init(hr_test_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
}

static void hr_phase_init(hr_phase_stats_t *phase)
{
    memset(phase, 0, sizeof(*phase));
    hr_test_stats_init(&phase->mae);
}

static void hr_test_stats_add(hr_test_stats_t *stats, float predicted, float truth)
{
    stats->total++;
    if (predicted < HR_TEST_MIN_BPM || predicted > HR_TEST_MAX_BPM) {
        return;
    }
    stats->valid++;
    double err = (double)predicted - (double)truth;
    stats->abs_sum += fabs(err);
    stats->sq_sum += err * err;
}

static void hr_test_stats_finish(hr_test_stats_t *stats)
{
    if (stats->valid > 0) {
        stats->mae = stats->abs_sum / stats->valid;
        stats->rmse = sqrt(stats->sq_sum / stats->valid);
    }
}

static double hr_phase_valid_rate(const hr_phase_stats_t *phase)
{
    if (phase->proc_total <= 0) {
        return 0.0;
    }
    return (double)phase->proc_valid / (double)phase->proc_total;
}

static int hr_test_ring_push(float *ring, int *ring_count, const float *src, int n)
{
    if (n > HR_TEST_RING_SAMPLES) {
        memcpy(ring, src + (n - HR_TEST_RING_SAMPLES), HR_TEST_RING_SAMPLES * sizeof(float));
        *ring_count = HR_TEST_RING_SAMPLES;
        return HR_TEST_RING_SAMPLES;
    }
    if (*ring_count + n > HR_TEST_RING_SAMPLES) {
        int drop = *ring_count + n - HR_TEST_RING_SAMPLES;
        memmove(ring, ring + drop, (*ring_count - drop) * sizeof(float));
        *ring_count -= drop;
    }
    memcpy(ring + *ring_count, src, n * sizeof(float));
    *ring_count += n;
    return *ring_count;
}

/**
 * Replay the published example: first process at 4 s, then every 1 s,
 * analysis window capped at 8 s. MAE uses 2 s labels; efficiency uses every hop.
 */
static esp_health_err_t hr_test_run_example_replay(hr_phase_stats_t *warmup,
                                                   hr_phase_stats_t *steady)
{
    float *ring = (float *)malloc(HR_TEST_RING_SAMPLES * sizeof(float));
    if (!ring) {
        return ESP_HEALTH_ERR_MEM_LACK;
    }

    hr_phase_init(warmup);
    hr_phase_init(steady);

    esp_health_hr_cfg_t hr_cfg = {
        .sample_rate = HR_TEST_SAMPLE_RATE,
    };
    esp_health_hr_handle_t handle = NULL;
    esp_health_err_t err = esp_health_hr_open(&hr_cfg, &handle);
    if (err != ESP_HEALTH_ERR_OK || handle == NULL) {
        free(ring);
        return err;
    }

    const int total_sec = HR_S1_BVP_LEN / HR_TEST_SAMPLE_RATE;
    int ring_count = 0;

    for (int sec = 0; sec < total_sec; sec++) {
        const float *sec_buf = hr_s1_bvp + (sec * HR_TEST_SAMPLE_RATE);
        ring_count = hr_test_ring_push(ring, &ring_count, sec_buf, HR_TEST_SAMPLE_RATE);

        int elapsed_sec = sec + 1;
        if (!hr_example_should_process(elapsed_sec)) {
            continue;
        }

        int num_samples = hr_example_window_samples(elapsed_sec, HR_TEST_SAMPLE_RATE);
        if (num_samples > ring_count) {
            num_samples = ring_count;
        }
        if (num_samples > HR_TEST_ANALYSIS_SAMPLES) {
            num_samples = HR_TEST_ANALYSIS_SAMPLES;
        }
        const float *window = ring + (ring_count - num_samples);

        hr_result_t result = {0};
        float hr = -1.0f;
        if (esp_health_hr_process(handle, window, num_samples, &result) == ESP_HEALTH_ERR_OK &&
            result.avg_bpm >= 40U &&
            result.avg_bpm <= 200U) {
            hr = (float)result.avg_bpm;
        }

        hr_phase_stats_t *phase = hr_example_is_steady(elapsed_sec) ? steady : warmup;
        if (phase->first_process_sec == 0) {
            phase->first_process_sec = elapsed_sec;
        }
        phase->proc_total++;
        if (hr >= HR_TEST_MIN_BPM && hr <= HR_TEST_MAX_BPM) {
            phase->proc_valid++;
        }

        if (elapsed_sec % HR_TEST_LABEL_SEC == 0) {
            int label_idx = elapsed_sec / HR_TEST_LABEL_SEC - 1;
            if (label_idx >= 0 && label_idx < HR_S1_LABEL_LEN) {
                hr_test_stats_add(&phase->mae, hr, hr_s1_label[label_idx]);
            }
        }
    }

    esp_health_hr_close(handle);
    hr_test_stats_finish(&warmup->mae);
    hr_test_stats_finish(&steady->mae);
    free(ring);
    return ESP_HEALTH_ERR_OK;
}

static void hr_branch_open_close(void)
{
    BRANCH_LOG("TC-01", "open and close");
    esp_health_hr_cfg_t cfg = {
        .sample_rate = HR_TEST_SAMPLE_RATE,
    };
    esp_health_hr_handle_t handle = NULL;

    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_hr_open(&cfg, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    esp_health_hr_close(handle);
}

static void hr_branch_invalid_params(void)
{
    BRANCH_LOG("TC-02", "invalid parameters");
    esp_health_hr_cfg_t cfg = {
        .sample_rate = HR_TEST_SAMPLE_RATE,
    };
    esp_health_hr_handle_t handle = NULL;
    hr_result_t result = {0};
    float samples[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_open(NULL, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_open(&cfg, NULL));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_process(NULL, samples, 4, &result));

    cfg.sample_rate = 0;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_open(&cfg, &handle));
    cfg.sample_rate = 8;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_open(&cfg, &handle));
    cfg.sample_rate = 1001U;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_open(&cfg, &handle));

    cfg.sample_rate = HR_TEST_SAMPLE_RATE;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_hr_open(&cfg, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_process(handle, NULL, 4, &result));
    esp_health_hr_close(handle);
    esp_health_hr_close(NULL);
}

static void hr_branch_process_short_input(void)
{
    BRANCH_LOG("TC-03", "process requires enough samples (8 samples != 8 seconds)");
    esp_health_hr_cfg_t cfg = {
        .sample_rate = HR_TEST_SAMPLE_RATE,
    };
    esp_health_hr_handle_t handle = NULL;
    hr_result_t result = {0};
    /* 8 samples at 64 Hz is 125 ms, not an 8 s analysis window. */
    float samples[8] = {0};

    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_hr_open(&cfg, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_hr_process(handle, samples, 0, &result));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_hr_process(handle, samples, 1, &result));
    TEST_ASSERT_EQUAL(0, result.avg_bpm);
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_hr_process(handle, samples, 8, &result));
    TEST_ASSERT_EQUAL(0, result.avg_bpm);
    esp_health_hr_close(handle);
}

static void hr_branch_process_null_out(void)
{
    BRANCH_LOG("TC-04", "process rejects null output pointer");
    esp_health_hr_cfg_t cfg = {
        .sample_rate = HR_TEST_SAMPLE_RATE,
    };
    esp_health_hr_handle_t handle = NULL;
    float samples[HR_TEST_MIN_SAMPLES];

    memset(samples, 0, sizeof(samples));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_hr_open(&cfg, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER,
                      esp_health_hr_process(handle, samples, HR_TEST_MIN_SAMPLES, NULL));
    esp_health_hr_close(handle);
}

TEST_CASE("HR branch test", "[ESP_HEALTH_HR]")
{
    hr_branch_open_close();
    hr_branch_invalid_params();
    hr_branch_process_short_input();
    hr_branch_process_null_out();
}

TEST_CASE("HR example window: first process at 4s, hop 1s, cap 8s", "[ESP_HEALTH_HR]")
{
    TEST_ASSERT_EQUAL(4, HR_EXAMPLE_MIN_WINDOW_SEC);
    TEST_ASSERT_EQUAL(8, HR_EXAMPLE_ANALYSIS_SEC);
    TEST_ASSERT_EQUAL(1, HR_EXAMPLE_UPDATE_SEC);

    TEST_ASSERT_FALSE(hr_example_should_process(3));
    TEST_ASSERT_TRUE(hr_example_should_process(4));
    TEST_ASSERT_TRUE(hr_example_should_process(5));
    TEST_ASSERT_EQUAL(4, hr_example_window_sec(4));
    TEST_ASSERT_EQUAL(7, hr_example_window_sec(7));
    TEST_ASSERT_FALSE(hr_example_is_steady(7));
    TEST_ASSERT_EQUAL(8, hr_example_window_sec(8));
    TEST_ASSERT_TRUE(hr_example_is_steady(8));
    TEST_ASSERT_EQUAL(8, hr_example_window_sec(12));
    TEST_ASSERT_EQUAL(8 * HR_TEST_SAMPLE_RATE,
                      hr_example_window_samples(12, HR_TEST_SAMPLE_RATE));
}

TEST_CASE("HR dataset validation", "[ESP_HEALTH_HR]")
{
    hr_phase_stats_t warmup = {0};
    hr_phase_stats_t steady = {0};

    ESP_LOGI(TAG, "S1 snippet: %ds @ %d Hz, example replay 4s/1s hop/8s cap",
             HR_S1_DURATION_SEC, HR_S1_SAMPLE_RATE);
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, hr_test_run_example_replay(&warmup, &steady));

    TEST_ASSERT_EQUAL(HR_EXAMPLE_MIN_WINDOW_SEC, warmup.first_process_sec);
    TEST_ASSERT_EQUAL(HR_EXAMPLE_ANALYSIS_SEC - HR_EXAMPLE_MIN_WINDOW_SEC, warmup.proc_total);
    TEST_ASSERT_EQUAL(HR_EXAMPLE_ANALYSIS_SEC, steady.first_process_sec);
    TEST_ASSERT_GREATER_THAN(0, steady.proc_total);

    double steady_rate = hr_phase_valid_rate(&steady);
    ESP_LOGI(TAG, "warmup proc=%d valid=%d", warmup.proc_total, warmup.proc_valid);
    ESP_LOGI(TAG, "steady 8s proc=%d valid=%d rate=%.2f MAE=%.2f RMSE=%.2f (labeled %d/%d)",
             steady.proc_total, steady.proc_valid, steady_rate,
             steady.mae.mae, steady.mae.rmse, steady.mae.valid, steady.mae.total);

    TEST_ASSERT_GREATER_THAN_DOUBLE(S1_STEADY_VALID_RATE_MIN, steady_rate);
    TEST_ASSERT_GREATER_THAN(0, steady.mae.total);
    TEST_ASSERT_LESS_THAN_DOUBLE(S1_STEADY_MAE_MAX, steady.mae.mae);
}
