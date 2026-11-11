#ifndef VL_OBD_H
#define VL_OBD_H

#include "vl_isotp.h"

typedef enum
{
    VL_OBD_INVALID,
    VL_OBD_BITMAP,
    VL_OBD_DATA,
    VL_OBD_NEGATIVE
} vl_obd_kind_t;

typedef enum
{
    VL_UNIT_NONE,
    VL_UNIT_MILLI_RPM,
    VL_UNIT_MILLI_KPH,
    VL_UNIT_MILLI_C,
    VL_UNIT_MILLI_PERCENT
} vl_obd_unit_t;

typedef struct
{
    vl_obd_kind_t kind;
    vl_obd_unit_t unit;
    int32_t value_milli;
    uint32_t bitmap;
    uint8_t pid;
    uint8_t negative_code;
} vl_obd_result_t;

bool vl_obd_response_id(const vl_can_frame_t *frame);
uint32_t vl_obd_physical_id(uint32_t response_id, bool extended);
void vl_obd_request(vl_can_frame_t *frame, uint32_t id, bool extended, uint8_t pid);
void vl_obd_flow(vl_can_frame_t *frame, uint32_t id, bool extended);
vl_obd_result_t vl_obd_decode(const vl_tp_rx_t *rx, uint8_t expected_pid);
bool vl_obd_bitmap_bit(uint32_t bitmap, uint8_t offset);

#endif
