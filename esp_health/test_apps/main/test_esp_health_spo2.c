/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_health_spo2.h"
#include "spo2_ptt_data.h"

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif  /* M_PI */

#define TAG  "TEST_SPO2"

#define BRANCH_LOG(tc, desc)  ESP_LOGI(TAG, "---- %s : %s ----", (tc), (desc))

#define SPO2_TEST_WIN_SEC    5
#define SPO2_TEST_HOP_SEC    1
#define SPO2_MAE_MAX         2.0
#define SPO2_VALID_RATE_MIN  0.80

typedef struct {
    double  mae;
    double  rmse;
    int     total;
    int     valid;
    double  abs_sum;
    double  sq_sum;
} spo2_stats_t;

static void spo2_stats_init(spo2_stats_t *s)
{
    memset(s, 0, sizeof(*s));
}

static void spo2_stats_add(spo2_stats_t *s, int ok, float pred, float truth)
{
    s->total++;
    if (!ok) {
        return;
    }
    s->valid++;
    double err = (double)pred - (double)truth;
    s->abs_sum += fabs(err);
    s->sq_sum += err * err;
}

static void spo2_stats_finish(spo2_stats_t *s)
{
    if (s->valid > 0) {
        s->mae = s->abs_sum / (double)s->valid;
        s->rmse = sqrt(s->sq_sum / (double)s->valid);
    }
}

static void spo2_branch_open_close(void)
{
    BRANCH_LOG("TC-01", "open and close");
    esp_health_spo2_handle_t handle = NULL;
    esp_health_spo2_cfg_t cfg = ESP_HEALTH_SPO2_CFG_DEFAULT();

    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    esp_health_spo2_reset(handle);
    esp_health_spo2_close(handle);
    esp_health_spo2_reset(NULL);
    esp_health_spo2_close(NULL);
}

static void spo2_branch_invalid_params(void)
{
    BRANCH_LOG("TC-02", "invalid parameters");
    esp_health_spo2_handle_t handle = NULL;
    esp_health_spo2_cfg_t cfg = ESP_HEALTH_SPO2_CFG_DEFAULT();
    float ir[4] = {0};
    float red[4] = {0};
    esp_health_spo2_result_t result = {0};

    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(NULL, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, NULL));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_process(NULL, ir, red, 4, &result));

    cfg.sample_rate = 0;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));
    cfg.sample_rate = 8;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));
    cfg.sample_rate = 1001U;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));

    cfg.sample_rate = 100;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_process(handle, ir, red, 0, &result));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_process(handle, ir, red, 1, &result));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_process(handle, NULL, red, 4, &result));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_process(handle, ir, NULL, 4, &result));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_process(handle, ir, red, 4, NULL));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir, red, 4, &result));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result.spo2);
    esp_health_spo2_close(handle);

    cfg = (esp_health_spo2_cfg_t)ESP_HEALTH_SPO2_CFG_DEFAULT();
    cfg.min_correlation = -0.1f;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));
    cfg.min_correlation = 1.1f;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));
    cfg.min_correlation = NAN;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));

    cfg = (esp_health_spo2_cfg_t)ESP_HEALTH_SPO2_CFG_DEFAULT();
    cfg.min_autocorrelation_ratio = -0.1f;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));
    cfg.min_autocorrelation_ratio = 1.1f;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));
    cfg.min_autocorrelation_ratio = NAN;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_INVALID_PARAMETER, esp_health_spo2_open(&cfg, &handle));

    cfg = (esp_health_spo2_cfg_t)ESP_HEALTH_SPO2_CFG_DEFAULT();
    cfg.min_correlation = 0.0f;
    cfg.min_autocorrelation_ratio = 1.0f;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg, &handle));
    esp_health_spo2_close(handle);
}

static void spo2_branch_non_positive_dc(void)
{
    BRANCH_LOG("TC-03", "no valid SpO2 on non-positive DC");
    esp_health_spo2_handle_t handle = NULL;
    esp_health_spo2_cfg_t cfg = ESP_HEALTH_SPO2_CFG_DEFAULT();
    float ir[8] = {0};
    float red[8] = {0};
    esp_health_spo2_result_t result = {0};

    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir, red, 8, &result));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result.spo2);
    esp_health_spo2_close(handle);
}

TEST_CASE("SpO2 branch test", "[esp_health_spo2]")
{
    spo2_branch_open_close();
    spo2_branch_invalid_params();
    spo2_branch_non_positive_dc();
}

TEST_CASE("SpO2 synthetic dual-channel sine", "[esp_health_spo2]")
{
    const int fs = 100;
    const int n = fs * 5;
    static float ir[500];
    static float red[500];
    const float f_hz = 1.2f;

    for (int i = 0; i < n; i++) {
        float s = sinf(2.0f * (float)M_PI * f_hz * (float)i / (float)fs);
        ir[i] = 100000.0f + 2000.0f * s;
        red[i] = 80000.0f + 1500.0f * s;
    }

    esp_health_spo2_handle_t handle = NULL;
    esp_health_spo2_cfg_t cfg = ESP_HEALTH_SPO2_CFG_DEFAULT();
    cfg.min_correlation = 0.0f;
    cfg.min_autocorrelation_ratio = 0.0f;
    esp_health_spo2_result_t result = {0};

    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir, red, n, &result));
    TEST_ASSERT_TRUE(result.spo2 >= 70.0f);
    TEST_ASSERT_TRUE(result.spo2 <= 100.0f);
    esp_health_spo2_reset(handle);
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir, red, n, &result));
    float spo2_default = result.spo2;
    esp_health_spo2_close(handle);

    esp_health_spo2_cfg_t cfg_frac = ESP_HEALTH_SPO2_CFG_DEFAULT();
    cfg_frac.min_correlation = 0.0f;
    cfg_frac.min_autocorrelation_ratio = 0.0f;
    cfg_frac.calib.c = 94.845f;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg_frac, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir, red, n, &result));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.6f, result.spo2 - spo2_default);
    esp_health_spo2_close(handle);
}

TEST_CASE("SpO2 ungated 0 vs default gates", "[esp_health_spo2]")
{
    const int fs = 100;
    const int n = fs * 5;
    static float ir_sine[500];
    static float red_mismatch[500];
    static float ir_noise[500];
    static float red_match[500];
    uint32_t rng = 1u;

    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)fs;
        rng = rng * 1664525u + 1013904223u;
        float nse = (float)(rng >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f;
        ir_sine[i] = 100000.0f + 2000.0f * sinf(2.0f * (float)M_PI * 1.2f * t);
        red_mismatch[i] = 80000.0f + 1500.0f * sinf(2.0f * (float)M_PI * 3.7f * t);
        /* Same shape on IR/Red (high correlation) but no pulse period. */
        ir_noise[i] = 100000.0f + 2500.0f * nse;
        red_match[i] = 80000.0f + 2000.0f * nse;
    }

    esp_health_spo2_handle_t handle = NULL;
    esp_health_spo2_result_t result = {0};
    float spo2_corr_gate = -1.0f;
    float spo2_aut_gate = -1.0f;
    float spo2_ungated = -1.0f;
    float corr_mismatch = 0.0f;
    float q_noise = 0.0f;

    esp_health_spo2_cfg_t cfg_def = ESP_HEALTH_SPO2_CFG_DEFAULT();
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg_def, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir_sine, red_mismatch, n, &result));
    corr_mismatch = result.correlation;
    spo2_corr_gate = result.spo2;
    esp_health_spo2_reset(handle);
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir_noise, red_match, n, &result));
    q_noise = result.quality_ratio;
    spo2_aut_gate = result.spo2;
    esp_health_spo2_close(handle);
    handle = NULL;

    TEST_ASSERT_TRUE(corr_mismatch < 0.8f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_corr_gate);
    TEST_ASSERT_TRUE(q_noise < 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_aut_gate);

    esp_health_spo2_cfg_t cfg_off = ESP_HEALTH_SPO2_CFG_DEFAULT();
    cfg_off.min_correlation = 0.0f;
    cfg_off.min_autocorrelation_ratio = 0.0f;
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg_off, &handle));
    TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_process(handle, ir_noise, red_match, n, &result));
    spo2_ungated = result.spo2;
    esp_health_spo2_close(handle);

    TEST_ASSERT_TRUE(spo2_ungated >= 70.0f);
    TEST_ASSERT_TRUE(spo2_ungated <= 100.0f);
}

TEST_CASE("SpO2 PhysioNet PTT sitting validation", "[esp_health_spo2]")
{
    const int fs = SPO2_PTT_SAMPLE_RATE;
    const int win = fs * SPO2_TEST_WIN_SEC;
    const int hop = fs * SPO2_TEST_HOP_SEC;
    spo2_stats_t stats = {0};
    spo2_stats_init(&stats);

    ESP_LOGI(TAG, "PTT-PPG s1_sit/s3_sit: %ds @ %d Hz, %d records (embedded, no SD)",
             SPO2_PTT_DURATION_SEC, fs, SPO2_PTT_NREC);

    for (int r = 0; r < SPO2_PTT_NREC; r++) {
        esp_health_spo2_cfg_t cfg = ESP_HEALTH_SPO2_CFG_DEFAULT();
        cfg.sample_rate = (uint32_t)fs;
        cfg.min_correlation = 0.0f;
        cfg.min_autocorrelation_ratio = 0.0f;
        esp_health_spo2_handle_t handle = NULL;
        TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, esp_health_spo2_open(&cfg, &handle));

        const float *ir = spo2_ptt_ir[r];
        const float *red = spo2_ptt_red[r];
        float truth = spo2_ptt_truth[r];

        for (int off = 0; off + win <= SPO2_PTT_LEN; off += hop) {
            esp_health_spo2_result_t result = {0};
            esp_health_err_t err = esp_health_spo2_process(handle, ir + off, red + off, win, &result);
            TEST_ASSERT_EQUAL(ESP_HEALTH_ERR_OK, err);
            spo2_stats_add(&stats,
                           result.spo2 >= 70.0f &&
                               result.spo2 <= 100.0f,
                           result.spo2, truth);
        }
        esp_health_spo2_close(handle);
    }

    spo2_stats_finish(&stats);
    ESP_LOGI(TAG, "PTT valid=%d total=%d MAE=%.2f RMSE=%.2f",
             stats.valid, stats.total, stats.mae, stats.rmse);
    TEST_ASSERT_GREATER_THAN(0, stats.total);
    TEST_ASSERT_GREATER_THAN_DOUBLE(SPO2_VALID_RATE_MIN,
                                    (double)stats.valid / (double)stats.total);
    TEST_ASSERT_LESS_THAN_DOUBLE(SPO2_MAE_MAX, stats.mae);
}
