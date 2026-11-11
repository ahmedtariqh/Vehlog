#ifndef VL_SCAN_H
#define VL_SCAN_H

#include "vl_obd.h"

typedef enum
{
    VL_SAMPLE_SUPPORTED_PAGE,
    VL_SAMPLE_DATA,
    VL_SAMPLE_NEGATIVE,
    VL_SAMPLE_NO_RESPONSE,
    VL_SAMPLE_INCOMPLETE
} vl_sample_status_t;

typedef enum
{
    VL_SCAN_OK,
    VL_SCAN_NO_ECU,
    VL_SCAN_PORT_ERROR,
    VL_SCAN_CAPACITY,
    VL_SCAN_PARTIAL,
    VL_SCAN_INVALID_ARGUMENT
} vl_scan_status_t;

typedef struct
{
    uint32_t ecu;
    bool extended;
    uint8_t pid;
    uint8_t service;
    uint8_t freeze_frame;
    vl_sample_status_t status;
    vl_obd_result_t result;
    uint64_t time_us;
    const uint8_t *payload;
    uint16_t size;
} vl_sample_t;

/* Callbacks run synchronously in the scan owner. The millisecond clock may wrap.
 * Receive distinguishes a quiet bus from a link/capture fault. Emit consumes a
 * borrowed sample, so it must copy any data retained after it returns. */
typedef struct
{
    void *context;
    bool (*send)(void *context, const vl_can_frame_t *frame);
    vl_rx_status_t (*receive)(void *context, vl_can_frame_t *frame, uint32_t timeout_ms);
    uint32_t (*clock_ms)(void *context);
    void (*pace)(void *context, uint32_t delay_ms);
    void (*emit)(void *context, const vl_sample_t *sample);
} vl_scan_port_t;

typedef struct
{
    vl_tp_rx_t receive;
} vl_scan_workspace_t;

typedef struct
{
    uint32_t response_id;
    uint32_t request_id;
    bool extended;
    bool address_byte;
    uint8_t tx_address;
    uint8_t rx_address;
    uint8_t service;
    uint8_t selector;
    uint8_t freeze_frame;
    uint8_t block_size;
    uint8_t separation_time;
} vl_read_request_t;

/* The workspace belongs to the caller and is reused across transactions. */
vl_scan_status_t vl_scan_once(const vl_scan_port_t *port, vl_scan_workspace_t *workspace,
                              bool extended);
vl_scan_status_t vl_scan_read(const vl_scan_port_t *port, vl_scan_workspace_t *workspace,
                              const vl_read_request_t *read);

#endif
