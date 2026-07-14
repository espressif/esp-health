/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Embedded sitting snippets from PhysioNet Pulse Transit Time PPG Dataset v1.1.0
 * (Mehrgardt et al., 2022, https://doi.org/10.13026/jpan-6n92):
 *   s1_sit, s3_sit — distal MAX30101 red (pleth_1) / IR (pleth_2)
 *   500 Hz original, decimated to 100 Hz; skip first 10 s, keep 30 s.
 * Reference SpO2 is the iHealth Air pulse-oximeter reading (start == end).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define SPO2_PTT_SAMPLE_RATE  100
#define SPO2_PTT_DURATION_SEC 30
#define SPO2_PTT_NREC         2
#define SPO2_PTT_LEN          (SPO2_PTT_SAMPLE_RATE * SPO2_PTT_DURATION_SEC)

extern const float spo2_ptt_red[SPO2_PTT_NREC][SPO2_PTT_LEN];
extern const float spo2_ptt_ir[SPO2_PTT_NREC][SPO2_PTT_LEN];
extern const float spo2_ptt_truth[SPO2_PTT_NREC];

#ifdef __cplusplus
}
#endif
