#ifndef VL_TWAI_BENCH_H
#define VL_TWAI_BENCH_H

#include "vl_can.h"

/* Available only in the bench profile. Call from the task that started TWAI. */
bool vl_twai_send(void *context, const vl_can_frame_t *frame);
uint32_t vl_twai_clock_ms(void *context);
bool vl_twai_stop(void);

#endif
