#ifndef VL_ISOTP_H
#define VL_ISOTP_H

#include "vl_can.h"

#define VL_ISOTP_CAPACITY 4095U

typedef enum
{
    VL_TP_INVALID,
    VL_TP_MORE,
    VL_TP_FLOW_REQUIRED,
    VL_TP_COMPLETE,
    VL_TP_UNRELATED
} vl_tp_status_t;

typedef struct
{
    uint8_t bytes[VL_ISOTP_CAPACITY];
    uint16_t size;
    uint16_t used;
    uint8_t next_seq;
    bool receiving;
} vl_tp_rx_t;

/* One transfer from one preselected ECU on Classical CAN. */
void vl_tp_reset(vl_tp_rx_t *rx);
vl_tp_status_t vl_tp_accept_addressed(vl_tp_rx_t *rx, const vl_can_frame_t *frame,
                                      bool address_byte, uint8_t address);
vl_tp_status_t vl_tp_accept(vl_tp_rx_t *rx, const vl_can_frame_t *frame);

#endif
