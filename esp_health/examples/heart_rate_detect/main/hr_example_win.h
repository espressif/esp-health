/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define HR_EXAMPLE_MIN_WINDOW_SEC  4
#define HR_EXAMPLE_ANALYSIS_SEC    8
#define HR_EXAMPLE_UPDATE_SEC      1

static inline int hr_example_should_process(int elapsed_sec)
{
    return elapsed_sec >= HR_EXAMPLE_MIN_WINDOW_SEC &&
           (elapsed_sec % HR_EXAMPLE_UPDATE_SEC) == 0;
}

static inline int hr_example_window_sec(int elapsed_sec)
{
    if (elapsed_sec < HR_EXAMPLE_MIN_WINDOW_SEC) {
        return 0;
    }
    if (elapsed_sec > HR_EXAMPLE_ANALYSIS_SEC) {
        return HR_EXAMPLE_ANALYSIS_SEC;
    }
    return elapsed_sec;
}

static inline int hr_example_window_samples(int elapsed_sec, int sample_rate)
{
    return hr_example_window_sec(elapsed_sec) * sample_rate;
}

static inline int hr_example_is_steady(int elapsed_sec)
{
    return elapsed_sec >= HR_EXAMPLE_ANALYSIS_SEC;
}

#ifdef __cplusplus
}
#endif
