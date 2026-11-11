#include "vl_obd.h"

bool vl_obd_response_id(const vl_can_frame_t *frame)
{
    bool matched;
    if (frame->extended)
    {
        matched = (frame->id & UINT32_C(0xFFFFFF00)) == UINT32_C(0x18DAF100);
    }
    else
    {
        matched = (frame->id >= UINT32_C(0x7E8)) && (frame->id <= UINT32_C(0x7EF));
    }
    return matched;
}

uint32_t vl_obd_physical_id(uint32_t response_id, bool extended)
{
    uint32_t result;
    if (extended)
    {
        result = UINT32_C(0x18DA00F1) | ((response_id & UINT32_C(0xFF)) << 8U);
    }
    else
    {
        result = response_id - UINT32_C(8);
    }
    return result;
}

static void init_tx(vl_can_frame_t *frame, uint32_t id, bool extended)
{
    const vl_can_frame_t empty = {0};
    *frame = empty;
    frame->id = id;
    frame->extended = extended;
    frame->length = VL_CAN_CLASSIC_BYTES;
    frame->dlc = VL_CAN_CLASSIC_BYTES;
}

void vl_obd_request(vl_can_frame_t *frame, uint32_t id, bool extended, uint8_t pid)
{
    init_tx(frame, id, extended);
    frame->bytes[0] = 0x02U;
    frame->bytes[1] = 0x01U;
    frame->bytes[2] = pid;
}

void vl_obd_flow(vl_can_frame_t *frame, uint32_t id, bool extended)
{
    init_tx(frame, id, extended);
    frame->bytes[0] = 0x30U;
    frame->bytes[2] = 10U;
}

bool vl_obd_bitmap_bit(uint32_t bitmap, uint8_t offset)
{
    bool supported = false;
    if ((offset >= 1U) && (offset <= 32U))
    {
        supported = ((bitmap >> (32U - (uint32_t)offset)) & UINT32_C(1)) != UINT32_C(0);
    }
    return supported;
}

vl_obd_result_t vl_obd_decode(const vl_tp_rx_t *rx, uint8_t expected_pid)
{
    vl_obd_result_t result = {0};
    uint16_t length = rx->used;
    uint8_t a;
    uint16_t rpm;
    result.kind = VL_OBD_INVALID;
    result.pid = expected_pid;

    if ((length == 3U) && (rx->bytes[0] == 0x7FU) && (rx->bytes[1] == 0x01U))
    {
        result.kind = VL_OBD_NEGATIVE;
        result.negative_code = rx->bytes[2];
    }
    else if ((length >= 3U) && (length <= VL_ISOTP_CAPACITY) &&
             (rx->bytes[0] == 0x41U) && (rx->bytes[1] == expected_pid))
    {
        a = rx->bytes[2];
        if ((expected_pid & 0x1FU) == 0U)
        {
            if (length == 6U)
            {
                result.kind = VL_OBD_BITMAP;
                result.bitmap = ((uint32_t)a << 24U) | ((uint32_t)rx->bytes[3] << 16U) |
                                ((uint32_t)rx->bytes[4] << 8U) | (uint32_t)rx->bytes[5];
            }
        }
        else
        {
            result.kind = VL_OBD_DATA;
            switch (expected_pid)
            {
                case 0x05U:
                    if (length == 3U)
                    {
                        result.unit = VL_UNIT_MILLI_C;
                        result.value_milli = ((int32_t)a - INT32_C(40)) * INT32_C(1000);
                    }
                    break;
                case 0x0CU:
                    if (length == 4U)
                    {
                        rpm = (uint16_t)(((uint16_t)a << 8U) | (uint16_t)rx->bytes[3]);
                        result.unit = VL_UNIT_MILLI_RPM;
                        result.value_milli = (int32_t)rpm * INT32_C(250);
                    }
                    break;
                case 0x0DU:
                    if (length == 3U)
                    {
                        result.unit = VL_UNIT_MILLI_KPH;
                        result.value_milli = (int32_t)a * INT32_C(1000);
                    }
                    break;
                case 0x2FU:
                    if (length == 3U)
                    {
                        result.unit = VL_UNIT_MILLI_PERCENT;
                        result.value_milli = (((int32_t)a * INT32_C(100000)) + INT32_C(127)) /
                                             INT32_C(255);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    else
    {
        /* A different service/PID is not evidence for this query. */
    }
    return result;
}
