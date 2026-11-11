#include "vl_isotp.h"

#include <stddef.h>

void vl_tp_reset(vl_tp_rx_t *rx)
{
    rx->size = 0U;
    rx->used = 0U;
    rx->next_seq = 0U;
    rx->receiving = false;
}

static void copy_bytes(vl_tp_rx_t *rx, const vl_can_frame_t *frame, uint8_t first,
                       uint16_t count)
{
    uint16_t i;
    for (i = 0U; i < count; ++i)
    {
        rx->bytes[rx->used + i] = frame->bytes[first + i];
    }
    rx->used += count;
}

vl_tp_status_t vl_tp_accept_addressed(vl_tp_rx_t *rx, const vl_can_frame_t *frame,
                                      bool address_byte, uint8_t address)
{
    vl_tp_status_t result = VL_TP_INVALID;
    const uint8_t pci = address_byte ? 1U : 0U;
    const uint8_t data = (uint8_t)(pci + 1U);
    uint8_t kind;
    uint8_t low;
    uint16_t count;
    uint16_t remaining;

    if ((rx == NULL) || (frame == NULL))
    {
        return result;
    }
    if (address_byte && (frame->length != 0U) && (frame->bytes[0] != address))
    {
        return VL_TP_UNRELATED;
    }
    if ((!frame->remote) && (!frame->fd) && (frame->length > data) &&
        (frame->length <= VL_CAN_CLASSIC_BYTES))
    {
        kind = (uint8_t)(frame->bytes[pci] >> 4U);
        low = (uint8_t)(frame->bytes[pci] & 0x0FU);
        if ((!rx->receiving) && (kind == 0U) && (low != 0U) &&
            ((uint32_t)low + data <= frame->length))
        {
            rx->used = 0U;
            rx->size = low;
            copy_bytes(rx, frame, data, low);
            result = VL_TP_COMPLETE;
        }
        else if ((!rx->receiving) && (kind == 1U) && (frame->length == VL_CAN_CLASSIC_BYTES))
        {
            count = (uint16_t)(((uint32_t)low << 8U) | (uint32_t)frame->bytes[data]);
            if ((count > (VL_CAN_CLASSIC_BYTES - data)) && (count <= VL_ISOTP_CAPACITY))
            {
                rx->used = 0U;
                rx->size = count;
                rx->receiving = true;
                rx->next_seq = 1U;
                copy_bytes(rx, frame, (uint8_t)(data + 1U),
                           (uint16_t)(VL_CAN_CLASSIC_BYTES - data - 1U));
                result = VL_TP_FLOW_REQUIRED;
            }
        }
        else if (rx->receiving && (kind == 2U) && (low == rx->next_seq) &&
                 (rx->used < rx->size) && (rx->size <= VL_ISOTP_CAPACITY))
        {
            remaining = rx->size - rx->used;
            count = (uint16_t)((uint32_t)frame->length - data);
            if ((count >= remaining) || (frame->length == VL_CAN_CLASSIC_BYTES))
            {
                if (count > remaining)
                {
                    count = remaining;
                }
                copy_bytes(rx, frame, data, count);
                rx->next_seq = (uint8_t)((rx->next_seq + 1U) & 0x0FU);
                result = VL_TP_MORE;
                if (rx->used == rx->size)
                {
                    rx->receiving = false;
                    result = VL_TP_COMPLETE;
                }
            }
        }
        else
        {
            /* Unexpected PCI or sequence number. */
        }
    }
    if (result == VL_TP_INVALID)
    {
        vl_tp_reset(rx);
    }
    return result;
}

vl_tp_status_t vl_tp_accept(vl_tp_rx_t *rx, const vl_can_frame_t *frame)
{
    return vl_tp_accept_addressed(rx, frame, false, 0U);
}
