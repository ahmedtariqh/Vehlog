#ifndef VL_CAN_H
#define VL_CAN_H

#include <stdbool.h>
#include <stdint.h>

#define VL_CAN_BYTES 64U
#define VL_CAN_CLASSIC_BYTES 8U

typedef struct
{
    uint64_t time_us;
    uint32_t id;
    uint8_t bytes[VL_CAN_BYTES];
    uint8_t length;
    uint8_t dlc;
    bool extended;
    bool fd;
    bool brs;
    bool esi;
    bool remote;
} vl_can_frame_t;

typedef enum
{
    VL_RX_TIMEOUT,
    VL_RX_FRAME,
    VL_RX_FAULT
} vl_rx_status_t;

#endif
