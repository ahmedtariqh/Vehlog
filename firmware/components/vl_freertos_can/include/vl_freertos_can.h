#ifndef VL_FREERTOS_CAN_H
#define VL_FREERTOS_CAN_H

#include "vl_can.h"

/* Single channel. Initialize once from the task that owns the bus and its notifications. */
bool vl_freertos_can_init(void);

/* ISR functions return true when their caller should request an ISR yield. */
bool vl_freertos_can_push_isr(const vl_can_frame_t *frame);
bool vl_freertos_can_fault_isr(void);
bool vl_freertos_can_tx_done_isr(bool success);

vl_rx_status_t vl_freertos_can_receive(vl_can_frame_t *frame, uint32_t timeout_ms);
bool vl_freertos_can_fault_pending(void);
bool vl_freertos_can_wait_tx(uint32_t timeout_ms);
void vl_freertos_can_pace(void *context, uint32_t delay_ms);

#endif
