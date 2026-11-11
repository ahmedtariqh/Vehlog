#ifndef VL_TWAI_H
#define VL_TWAI_H

#include "vl_can.h"

typedef struct
{
    int tx_gpio;
    int rx_gpio;
    int arm_gpio;
    uint32_t nominal_bps;
    uint32_t data_bps;
    uint32_t response_id;
    bool exact_response;
    bool bench_scan;
    bool extended;
} vl_twai_config_t;

/* Start once from the sole application task. */
bool vl_twai_start(const vl_twai_config_t *config);
vl_rx_status_t vl_twai_receive(void *context, vl_can_frame_t *frame, uint32_t timeout_ms);
#endif
