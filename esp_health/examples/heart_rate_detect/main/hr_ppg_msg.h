/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Data task posts one kind per queue item (samples travel with it).
 * A single int is copied by the FreeRTOS queue: no shared detect_flag.
 */
#define HR_STREAM_SAMPLES     0
#define HR_STREAM_FINGER_OFF  1
#define HR_STREAM_FIFO_GAP    2

typedef struct {
    int worn;      /* 1 after SAMPLES, 0 after FINGER_OFF */
    int drop_ring; /* 1 after FINGER_OFF or FIFO_GAP */
} hr_stream_state_t;

static inline void hr_stream_apply(hr_stream_state_t *st, int kind)
{
    if (st == NULL) {
        return;
    }
    if (kind == HR_STREAM_FIFO_GAP) {
        st->drop_ring = 1;
        return;
    }
    if (kind == HR_STREAM_FINGER_OFF) {
        st->worn = 0;
        st->drop_ring = 1;
        return;
    }
    st->worn = 1;
    st->drop_ring = 0;
}

static inline int hr_stream_consistent(const hr_stream_state_t *st, int kind)
{
    if (st == NULL) {
        return 0;
    }
    if (kind == HR_STREAM_FINGER_OFF) {
        return st->worn == 0 && st->drop_ring == 1;
    }
    if (kind == HR_STREAM_FIFO_GAP) {
        return st->drop_ring == 1;
    }
    if (kind == HR_STREAM_SAMPLES) {
        return st->worn == 1 && st->drop_ring == 0;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
