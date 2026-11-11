#include "vl_scan.h"

#include <stddef.h>

#define VL_SCAN_ECUS 16U
#define VL_SCAN_QUERY_MS 400U
#define VL_SCAN_PACE_MS 120U
#define VL_SCAN_RX_BUDGET 4096U
#define VL_SCAN_SEGMENT_MS 1000U
#define VL_SCAN_PENDING_MS 5000U
#define VL_SCAN_TOTAL_MS 30000U
#define VL_SUPPORT_PDU_BYTES 6U

typedef struct
{
    uint32_t address;
    uint32_t first_page;
    uint64_t time_us;
    uint8_t first_pdu[VL_SUPPORT_PDU_BYTES];
} vl_ecu_t;

static void emit(const vl_scan_port_t *port, const vl_read_request_t *read,
                 uint64_t time_us, const uint8_t *payload, uint16_t size,
                 const vl_obd_result_t *decoded, vl_sample_status_t status)
{
    vl_sample_t sample = {0};
    sample.ecu = read->response_id;
    sample.extended = read->extended;
    sample.pid = read->selector;
    sample.service = read->service;
    sample.freeze_frame = read->freeze_frame;
    sample.time_us = time_us;
    sample.result = *decoded;
    sample.status = status;
    sample.size = size;
    sample.payload = payload;
    port->emit(port->context, &sample);
}

static uint32_t time_left(uint32_t now, uint32_t start, uint32_t budget)
{
    const uint32_t elapsed = now - start;
    return (elapsed < budget) ? (budget - elapsed) : 0U;
}

static bool valid_port(const vl_scan_port_t *port)
{
    return (port != NULL) && (port->send != NULL) && (port->receive != NULL) &&
           (port->clock_ms != NULL) && (port->pace != NULL) && (port->emit != NULL);
}

static uint8_t request_size(uint8_t service)
{
    uint8_t size;
    switch (service)
    {
        case 0x01U:
        case 0x06U:
        case 0x09U: size = 2U; break;
        case 0x02U: size = 3U; break;
        case 0x03U:
        case 0x07U:
        case 0x0AU: size = 1U; break;
        default: size = 0U; break;
    }
    return size;
}

static void pack_request(vl_can_frame_t *frame, const vl_read_request_t *read)
{
    const vl_can_frame_t empty = {0};
    const uint8_t pci = read->address_byte ? 1U : 0U;
    const uint8_t size = request_size(read->service);
    *frame = empty;
    frame->id = read->request_id;
    frame->extended = read->extended;
    frame->length = VL_CAN_CLASSIC_BYTES;
    frame->dlc = VL_CAN_CLASSIC_BYTES;
    if (read->address_byte)
    {
        frame->bytes[0] = read->tx_address;
    }
    frame->bytes[pci] = size;
    frame->bytes[pci + 1U] = read->service;
    if (size > 1U)
    {
        frame->bytes[pci + 2U] = read->selector;
    }
    if (size > 2U)
    {
        frame->bytes[pci + 3U] = read->freeze_frame;
    }
}

static void pack_flow(vl_can_frame_t *frame, const vl_read_request_t *read)
{
    vl_obd_flow(frame, read->request_id, read->extended);
    frame->bytes[1] = read->block_size;
    frame->bytes[2] = read->separation_time;
    if (read->address_byte)
    {
        frame->bytes[3] = frame->bytes[2];
        frame->bytes[2] = frame->bytes[1];
        frame->bytes[1] = frame->bytes[0];
        frame->bytes[0] = read->tx_address;
    }
}

static bool matches_read(const vl_tp_rx_t *rx, const vl_read_request_t *read)
{
    const uint8_t size = request_size(read->service);
    return (rx->used > size) && (rx->bytes[0] == (uint8_t)(read->service + 0x40U)) &&
           ((size < 2U) || (rx->bytes[1] == read->selector)) &&
           ((size < 3U) || (rx->bytes[2] == read->freeze_frame));
}

static vl_scan_status_t discover(const vl_scan_port_t *port, vl_tp_rx_t *rx, bool extended,
                                 vl_ecu_t ecus[VL_SCAN_ECUS], uint8_t *used)
{
    vl_scan_status_t status = VL_SCAN_OK;
    vl_can_frame_t frame = {0};
    vl_obd_result_t decoded;
    vl_rx_status_t received;
    uint32_t start;
    uint32_t remaining;
    uint16_t inspected = 0U;
    uint8_t i;
    uint8_t j;

    vl_tp_reset(rx);
    vl_obd_request(&frame, extended ? UINT32_C(0x18DB33F1) : UINT32_C(0x7DF), extended, 0U);
    if (!port->send(port->context, &frame))
    {
        status = VL_SCAN_PORT_ERROR;
    }
    else
    {
        start = port->clock_ms(port->context);
        remaining = time_left(port->clock_ms(port->context), start, VL_SCAN_QUERY_MS);
        while ((status == VL_SCAN_OK) && (remaining != 0U))
        {
            received = port->receive(port->context, &frame, remaining);
            if (received == VL_RX_FAULT)
            {
                status = VL_SCAN_PORT_ERROR;
            }
            else if (received == VL_RX_TIMEOUT)
            {
                break;
            }
            else
            {
                ++inspected;
                if (inspected >= VL_SCAN_RX_BUDGET)
                {
                    status = VL_SCAN_CAPACITY;
                }
                else if ((frame.extended == extended) && vl_obd_response_id(&frame) &&
                         (frame.length >= 2U) && ((frame.bytes[0] >> 4U) == 0U) &&
                         (vl_tp_accept(rx, &frame) == VL_TP_COMPLETE))
                {
                    decoded = vl_obd_decode(rx, 0U);
                    if (decoded.kind == VL_OBD_BITMAP)
                    {
                        i = 0U;
                        while ((i < *used) && (ecus[i].address != frame.id))
                        {
                            ++i;
                        }
                        if (i == *used)
                        {
                            if (*used < VL_SCAN_ECUS)
                            {
                                ecus[i].address = frame.id;
                                ecus[i].first_page = decoded.bitmap;
                                ecus[i].time_us = frame.time_us;
                                for (j = 0U; j < VL_SUPPORT_PDU_BYTES; ++j)
                                {
                                    ecus[i].first_pdu[j] = rx->bytes[j];
                                }
                                ++(*used);
                            }
                            else
                            {
                                status = VL_SCAN_CAPACITY;
                            }
                        }
                        else if (ecus[i].first_page != decoded.bitmap)
                        {
                            status = VL_SCAN_PARTIAL;
                        }
                        else
                        {
                            /* Same ECU and bitmap already recorded. */
                        }
                    }
                }
            }
            remaining = time_left(port->clock_ms(port->context), start, VL_SCAN_QUERY_MS);
        }
        port->pace(port->context, VL_SCAN_PACE_MS);
    }
    return status;
}

static vl_scan_status_t query(const vl_scan_port_t *port, vl_tp_rx_t *rx,
                              const vl_read_request_t *read, uint32_t *bitmap)
{
    vl_scan_status_t status = VL_SCAN_OK;
    vl_sample_status_t outcome = VL_SAMPLE_NO_RESPONSE;
    vl_can_frame_t frame = {0};
    vl_can_frame_t request;
    vl_obd_result_t decoded = {0};
    uint32_t start;
    uint32_t activity;
    uint32_t wait = VL_SCAN_QUERY_MS;
    uint64_t evidence_time = 0U;
    uint16_t inspected = 0U;
    uint8_t in_block = 0U;

    decoded.pid = read->selector;
    vl_tp_reset(rx);
    pack_request(&request, read);
    if (!port->send(port->context, &request))
    {
        return VL_SCAN_PORT_ERROR;
    }

    start = port->clock_ms(port->context);
    activity = start;
    while (status == VL_SCAN_OK)
    {
        const uint32_t now = port->clock_ms(port->context);
        const uint32_t total_left = time_left(now, start, VL_SCAN_TOTAL_MS);
        const uint32_t stage_left = time_left(now, activity, wait);
        const uint32_t remaining = (total_left < stage_left) ? total_left : stage_left;
        vl_rx_status_t received;
        vl_tp_status_t assembled;
        if (remaining == 0U)
        {
            break;
        }
        received = port->receive(port->context, &frame, remaining);
        if (received == VL_RX_FAULT)
        {
            status = VL_SCAN_PORT_ERROR;
            break;
        }
        if (received == VL_RX_TIMEOUT)
        {
            break;
        }
        ++inspected;
        if (inspected >= VL_SCAN_RX_BUDGET)
        {
            status = VL_SCAN_CAPACITY;
            break;
        }
        if ((frame.id != read->response_id) || (frame.extended != read->extended) ||
            frame.fd || frame.remote)
        {
            continue;
        }
        assembled = vl_tp_accept_addressed(rx, &frame, read->address_byte, read->rx_address);
        if (assembled == VL_TP_UNRELATED)
        {
            continue;
        }
        evidence_time = frame.time_us;
        outcome = VL_SAMPLE_INCOMPLETE;
        if (assembled == VL_TP_FLOW_REQUIRED)
        {
            if (matches_read(rx, read))
            {
                in_block = 0U;
                pack_flow(&request, read);
                if (!port->send(port->context, &request))
                {
                    status = VL_SCAN_PORT_ERROR;
                }
                activity = port->clock_ms(port->context);
                wait = VL_SCAN_SEGMENT_MS;
            }
            else
            {
                vl_tp_reset(rx);
            }
        }
        else if (assembled == VL_TP_MORE)
        {
            if (read->block_size != 0U)
            {
                ++in_block;
                if (in_block == read->block_size)
                {
                    in_block = 0U;
                    pack_flow(&request, read);
                    if (!port->send(port->context, &request))
                    {
                        status = VL_SCAN_PORT_ERROR;
                    }
                }
            }
            activity = port->clock_ms(port->context);
            wait = VL_SCAN_SEGMENT_MS;
        }
        else if (assembled == VL_TP_COMPLETE)
        {
            if ((rx->used == 3U) && (rx->bytes[0] == 0x7FU) && (rx->bytes[1] == read->service))
            {
                decoded.kind = VL_OBD_NEGATIVE;
                decoded.negative_code = rx->bytes[2];
                outcome = VL_SAMPLE_NEGATIVE;
                if (decoded.negative_code != 0x78U)
                {
                    break;
                }
                activity = port->clock_ms(port->context);
                wait = VL_SCAN_PENDING_MS;
            }
            else if (matches_read(rx, read))
            {
                if (read->service == 0x01U)
                {
                    decoded = vl_obd_decode(rx, read->selector);
                }
                else
                {
                    decoded.kind = VL_OBD_DATA;
                    decoded.negative_code = 0U;
                }
                if (decoded.kind == VL_OBD_BITMAP)
                {
                    *bitmap = decoded.bitmap;
                    outcome = VL_SAMPLE_SUPPORTED_PAGE;
                    break;
                }
                if (decoded.kind == VL_OBD_DATA)
                {
                    outcome = VL_SAMPLE_DATA;
                    break;
                }
            }
            else
            {
                /* Unmatched complete reply. Keep it as incomplete evidence. */
            }
        }
        else
        {
            /* Invalid data never resets the deadline. */
        }
    }
    emit(port, read, evidence_time, rx->bytes, rx->used, &decoded, outcome);
    port->pace(port->context, VL_SCAN_PACE_MS);
    if ((status == VL_SCAN_OK) && (outcome != VL_SAMPLE_DATA) &&
        (outcome != VL_SAMPLE_SUPPORTED_PAGE))
    {
        status = VL_SCAN_PARTIAL;
    }
    return status;
}

static vl_scan_status_t read_ecu(const vl_scan_port_t *port, vl_tp_rx_t *rx,
                                 const vl_ecu_t *ecu, bool extended)
{
    uint16_t base = 0U;
    uint8_t offset;
    uint32_t bitmap = ecu->first_page;
    vl_scan_status_t status = VL_SCAN_OK;
    vl_scan_status_t next;
    vl_read_request_t read = {
        .response_id = ecu->address,
        .request_id = vl_obd_physical_id(ecu->address, extended),
        .extended = extended,
        .service = 0x01U,
        .separation_time = 10U
    };

    for (;;)
    {
        for (offset = 1U; offset < 32U; ++offset)
        {
            if (vl_obd_bitmap_bit(bitmap, offset))
            {
                read.selector = (uint8_t)(base + offset);
                next = query(port, rx, &read, &bitmap);
                if (next == VL_SCAN_PARTIAL)
                {
                    status = next;
                }
                else if (next != VL_SCAN_OK)
                {
                    return next;
                }
                else
                {
                    /* This data response does not change the current page. */
                }
            }
        }
        if ((base == 224U) || (!vl_obd_bitmap_bit(bitmap, 32U)))
        {
            break;
        }
        base += 32U;
        read.selector = (uint8_t)base;
        next = query(port, rx, &read, &bitmap);
        if (next != VL_SCAN_OK)
        {
            return next;
        }
    }
    return status;
}

vl_scan_status_t vl_scan_once(const vl_scan_port_t *port, vl_scan_workspace_t *workspace,
                              bool extended)
{
    vl_ecu_t ecus[VL_SCAN_ECUS] = {0};
    vl_read_request_t read = {0};
    vl_obd_result_t decoded = {0};
    vl_scan_status_t status = VL_SCAN_INVALID_ARGUMENT;
    vl_scan_status_t next;
    uint8_t used = 0U;
    uint8_t i;
    if (valid_port(port) && (workspace != NULL))
    {
        status = discover(port, &workspace->receive, extended, ecus, &used);
        if ((status == VL_SCAN_OK) && (used == 0U))
        {
            status = VL_SCAN_NO_ECU;
        }
        if (status == VL_SCAN_OK)
        {
            read.extended = extended;
            read.service = 0x01U;
            decoded.kind = VL_OBD_BITMAP;
            for (i = 0U; i < used; ++i)
            {
                read.response_id = ecus[i].address;
                decoded.bitmap = ecus[i].first_page;
                emit(port, &read, ecus[i].time_us, ecus[i].first_pdu, VL_SUPPORT_PDU_BYTES,
                     &decoded, VL_SAMPLE_SUPPORTED_PAGE);
            }
            for (i = 0U; i < used; ++i)
            {
                next = read_ecu(port, &workspace->receive, &ecus[i], extended);
                if (next == VL_SCAN_PARTIAL)
                {
                    status = next;
                }
                else if (next != VL_SCAN_OK)
                {
                    status = next;
                    break;
                }
                else
                {
                    /* Continue with the next discovered ECU. */
                }
            }
        }
    }
    return status;
}

vl_scan_status_t vl_scan_read(const vl_scan_port_t *port, vl_scan_workspace_t *workspace,
                              const vl_read_request_t *read)
{
    uint32_t bitmap = 0U;
    uint32_t max_id;
    if ((!valid_port(port)) || (workspace == NULL) || (read == NULL) ||
        (request_size(read->service) == 0U))
    {
        return VL_SCAN_INVALID_ARGUMENT;
    }
    max_id = read->extended ? UINT32_C(0x1FFFFFFF) : UINT32_C(0x7FF);
    if ((read->request_id > max_id) || (read->response_id > max_id) ||
        ((read->separation_time > 0x7FU) &&
         ((read->separation_time < 0xF1U) || (read->separation_time > 0xF9U))))
    {
        return VL_SCAN_INVALID_ARGUMENT;
    }
    return query(port, &workspace->receive, read, &bitmap);
}
