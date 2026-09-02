/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HR_S1_SAMPLE_RATE  64
#define HR_S1_DURATION_SEC 90
#define HR_S1_LABEL_SEC    2
#define HR_S1_BVP_LEN      (HR_S1_SAMPLE_RATE * HR_S1_DURATION_SEC)
#define HR_S1_LABEL_LEN    (HR_S1_DURATION_SEC / HR_S1_LABEL_SEC)

extern const float hr_s1_bvp[HR_S1_BVP_LEN];
extern const float hr_s1_label[HR_S1_LABEL_LEN];

#ifdef __cplusplus
}
#endif
