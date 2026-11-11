#include "vl_scan.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#define CASE_LIMIT 8U

typedef struct
{
    vl_can_frame_t queue[CASE_LIMIT];
    vl_sample_t samples[CASE_LIMIT];
    uint8_t payloads[CASE_LIMIT][16];
    uint32_t now;
    size_t waiting;
    size_t index;
    size_t emitted;
    size_t requests;
    size_t flows;
    bool extended;
    bool fault;
    bool malformed;
} fixture_t;

static void queue_frame(fixture_t *fixture, uint32_t id, const uint8_t *bytes,
                        uint8_t size)
{
    vl_can_frame_t frame = {0};
    uint8_t i;
    assert(fixture->waiting < CASE_LIMIT);
    frame.id = id;
    frame.time_us = ((uint64_t)fixture->now + UINT64_C(1)) * UINT64_C(1000);
    frame.extended = fixture->extended;
    frame.length = size;
    frame.dlc = size;
    for (i = 0U; i < size; ++i)
    {
        frame.bytes[i] = bytes[i];
    }
    fixture->queue[fixture->waiting] = frame;
    ++fixture->waiting;
}

static bool send_frame(void *context, const vl_can_frame_t *frame)
{
    fixture_t *fixture = context;
    const uint32_t ecu0 = fixture->extended ? UINT32_C(0x18DAF110) : UINT32_C(0x7E8);
    const uint32_t ecu1 = fixture->extended ? UINT32_C(0x18DAF111) : UINT32_C(0x7E9);
    const uint32_t func = fixture->extended ? UINT32_C(0x18DB33F1) : UINT32_C(0x7DF);
    const uint32_t phys0 = fixture->extended ? UINT32_C(0x18DA10F1) : UINT32_C(0x7E0);
    const uint32_t phys1 = fixture->extended ? UINT32_C(0x18DA11F1) : UINT32_C(0x7E1);
    const uint8_t page0[] = {6U, 0x41U, 0U, 0x00U, 0x18U, 0U, 1U};
    const uint8_t page1[] = {6U, 0x41U, 0U, 0x08U, 0U, 0U, 0U};
    const uint8_t second[] = {6U, 0x41U, 0x20U, 0U, 2U, 0U, 0U};
    const uint8_t rpm[] = {4U, 0x41U, 0x0CU, 0x1FU, 0x41U};
    const uint8_t negative[] = {3U, 0x7FU, 1U, 0x11U};
    const uint8_t coolant[] = {3U, 0x41U, 5U, 0U};
    const uint8_t first[] = {0x10U, 10U, 0x41U, 0x2FU, 0xAAU, 2U, 3U, 4U};
    uint8_t consecutive[] = {0x21U, 5U, 6U, 7U, 8U};
    assert(frame->extended == fixture->extended);
    assert(frame->length == 8U);
    fixture->waiting = 0U;
    fixture->index = 0U;
    if (frame->bytes[0] == 0x30U)
    {
        assert(frame->id == phys0);
        ++fixture->flows;
        if (fixture->malformed)
        {
            consecutive[0] = 0x22U;
        }
        queue_frame(fixture, ecu0, consecutive, sizeof(consecutive));
    }
    else
    {
        assert(frame->bytes[0] == 2U);
        assert(frame->bytes[1] == 1U);
        ++fixture->requests;
        switch (frame->bytes[2])
        {
            case 0x00U:
                assert(frame->id == func);
                queue_frame(fixture, ecu0, page0, sizeof(page0));
                queue_frame(fixture, ecu1, page1, sizeof(page1));
                break;
            case 0x05U:
                assert(frame->id == phys1);
                queue_frame(fixture, ecu1, coolant, sizeof(coolant));
                break;
            case 0x0CU:
                assert(frame->id == phys0);
                queue_frame(fixture, ecu0, rpm, sizeof(rpm));
                break;
            case 0x0DU:
                assert(frame->id == phys0);
                queue_frame(fixture, ecu0, negative, sizeof(negative));
                break;
            case 0x20U:
                assert(frame->id == phys0);
                queue_frame(fixture, ecu0, second, sizeof(second));
                break;
            case 0x2FU:
                assert(frame->id == phys0);
                queue_frame(fixture, ecu0, first, sizeof(first));
                break;
            default:
                assert(false);
                break;
        }
    }
    return true;
}

static vl_rx_status_t receive_frame(void *context, vl_can_frame_t *frame, uint32_t timeout)
{
    fixture_t *fixture = context;
    if (fixture->fault)
    {
        return VL_RX_FAULT;
    }
    if (fixture->index < fixture->waiting)
    {
        *frame = fixture->queue[fixture->index];
        ++fixture->index;
        ++fixture->now;
        return VL_RX_FRAME;
    }
    fixture->now += timeout;
    return VL_RX_TIMEOUT;
}

static uint32_t clock_ms(void *context)
{
    const fixture_t *fixture = context;
    return fixture->now;
}

static void pace(void *context, uint32_t delay)
{
    fixture_t *fixture = context;
    assert(delay >= 100U);
    fixture->now += delay;
}

static void emit_sample(void *context, const vl_sample_t *sample)
{
    fixture_t *fixture = context;
    assert(fixture->emitted < CASE_LIMIT);
    fixture->samples[fixture->emitted] = *sample;
    assert(sample->size <= sizeof(fixture->payloads[0]));
    if (sample->size != 0U)
    {
        uint16_t i;
        for (i = 0U; i < sample->size; ++i)
        {
            fixture->payloads[fixture->emitted][i] = sample->payload[i];
        }
        fixture->samples[fixture->emitted].payload = fixture->payloads[fixture->emitted];
    }
    ++fixture->emitted;
}

static void test_scan(bool extended, bool malformed)
{
    fixture_t fixture = {0};
    vl_scan_workspace_t workspace;
    const vl_scan_port_t port = {&fixture, send_frame, receive_frame, clock_ms, pace, emit_sample};
    fixture.extended = extended;
    fixture.malformed = malformed;
    fixture.now = UINT32_MAX - UINT32_C(5);
    assert(vl_scan_once(&port, &workspace, extended) == VL_SCAN_PARTIAL);
    assert(fixture.requests == 6U);
    assert(fixture.flows == 1U);
    assert(fixture.emitted == 7U);
    assert(fixture.samples[0].status == VL_SAMPLE_SUPPORTED_PAGE);
    assert(fixture.samples[0].size == 6U && fixture.samples[0].payload[0] == 0x41U);
    assert(fixture.samples[1].status == VL_SAMPLE_SUPPORTED_PAGE);
    assert(fixture.samples[0].ecu != fixture.samples[1].ecu);
    assert(fixture.samples[2].result.unit == VL_UNIT_MILLI_RPM);
    assert(fixture.samples[2].result.value_milli == INT32_C(2000250));
    assert(fixture.samples[3].status == VL_SAMPLE_NEGATIVE);
    assert(fixture.samples[3].result.negative_code == 0x11U);
    assert(fixture.samples[4].pid == 0x20U);
    if (malformed)
    {
        assert(fixture.samples[5].status == VL_SAMPLE_INCOMPLETE);
    }
    else
    {
        assert(fixture.samples[5].status == VL_SAMPLE_DATA);
        assert(fixture.samples[5].size == 10U);
        assert(fixture.samples[5].payload[9] == 8U);
        assert(fixture.samples[5].result.unit == VL_UNIT_NONE);
    }
    assert(fixture.samples[6].result.value_milli == -INT32_C(40000));
    fixture.fault = true;
    assert(vl_scan_once(&port, &workspace, extended) == VL_SCAN_PORT_ERROR);
}

static void test_transport_limits(void)
{
    vl_tp_rx_t rx = {0};
    vl_can_frame_t frame = {0};
    frame.length = 8U;
    frame.bytes[0] = 0x10U;
    frame.bytes[1] = 7U;
    assert(vl_tp_accept(&rx, &frame) == VL_TP_INVALID);
    frame.bytes[1] = 129U;
    assert(vl_tp_accept(&rx, &frame) == VL_TP_FLOW_REQUIRED);
    frame.bytes[0] = 0x22U;
    assert(vl_tp_accept(&rx, &frame) == VL_TP_INVALID);
    assert(!rx.receiving);
    frame.fd = true;
    frame.bytes[0] = 0x03U;
    assert(vl_tp_accept(&rx, &frame) == VL_TP_INVALID);
    assert(vl_obd_bitmap_bit(UINT32_C(0x80000001), 1U));
    assert(vl_obd_bitmap_bit(UINT32_C(0x80000001), 32U));
    assert(!vl_obd_bitmap_bit(UINT32_C(0xFFFFFFFF), 0U));
    assert(!vl_obd_bitmap_bit(UINT32_C(0xFFFFFFFF), 33U));
    assert(vl_scan_once(NULL, NULL, false) == VL_SCAN_INVALID_ARGUMENT);
}

typedef struct
{
    uint32_t now;
    uint16_t next;
    unsigned int replies;
    unsigned int flows;
    unsigned int samples;
    bool pending_only;
} long_fixture_t;

static bool send_long(void *context, const vl_can_frame_t *frame)
{
    long_fixture_t *fixture = context;
    assert(frame->id == UINT32_C(0x18DA10F1));
    assert(frame->extended);
    assert(frame->bytes[0] == 0x10U);
    if (frame->bytes[1] == 0x30U)
    {
        assert(frame->bytes[2] == 8U);
        assert(frame->bytes[3] == 10U);
        ++fixture->flows;
    }
    else
    {
        assert(frame->bytes[1] == 2U);
        assert(frame->bytes[2] == 9U && frame->bytes[3] == 2U);
    }
    return true;
}

static uint8_t long_payload(uint16_t index)
{
    return (uint8_t)(index & 0xFFU);
}

static vl_rx_status_t receive_long(void *context, vl_can_frame_t *frame, uint32_t wait)
{
    long_fixture_t *fixture = context;
    uint8_t i;
    uint32_t advance;
    const vl_can_frame_t empty = {0};
    *frame = empty;
    frame->id = UINT32_C(0x18DAF110);
    frame->extended = true;
    frame->length = 8U;
    frame->dlc = 8U;
    frame->bytes[0] = 0xF1U;
    ++fixture->replies;
    if (fixture->replies <= 2U || fixture->pending_only)
    {
        advance = (fixture->replies == 1U) ? 300U : 4800U;
        frame->bytes[1] = 3U;
        frame->bytes[2] = 0x7FU;
        frame->bytes[3] = 9U;
        frame->bytes[4] = 0x78U;
    }
    else if (fixture->replies == 3U)
    {
        /* Same CAN ID, another extended address. Must not change timing. */
        advance = 1000U;
        frame->bytes[0] = 0xF2U;
        frame->bytes[1] = 3U;
    }
    else if (fixture->replies == 4U)
    {
        advance = 3000U;
        frame->bytes[1] = 0x1FU;
        frame->bytes[2] = 0xFFU;
        frame->bytes[3] = 0x49U;
        frame->bytes[4] = 0x02U;
        for (i = 5U; i < 8U; ++i)
        {
            frame->bytes[i] = long_payload((uint16_t)(i - 3U));
        }
        fixture->next = 5U;
    }
    else
    {
        const unsigned int packet = fixture->replies - 4U;
        advance = 10U;
        assert(fixture->flows == ((packet - 1U) / 8U) + 1U);
        frame->bytes[1] = (uint8_t)(0x20U | (packet & 0x0FU));
        for (i = 2U; i < 8U && fixture->next < VL_ISOTP_CAPACITY; ++i)
        {
            frame->bytes[i] = long_payload(fixture->next);
            ++fixture->next;
        }
    }
    if (advance > wait)
    {
        assert(fixture->pending_only);
        fixture->now += wait;
        return VL_RX_TIMEOUT;
    }
    fixture->now += advance;
    frame->time_us = (uint64_t)fixture->now * UINT64_C(1000);
    return VL_RX_FRAME;
}

static uint32_t clock_long(void *context)
{
    const long_fixture_t *fixture = context;
    return fixture->now;
}

static void pace_long(void *context, uint32_t delay)
{
    long_fixture_t *fixture = context;
    fixture->now += delay;
}

static void emit_long(void *context, const vl_sample_t *sample)
{
    long_fixture_t *fixture = context;
    uint16_t i;
    ++fixture->samples;
    assert(sample->service == 9U && sample->pid == 2U);
    if (fixture->pending_only)
    {
        assert(sample->status == VL_SAMPLE_NEGATIVE);
        assert(sample->result.negative_code == 0x78U);
        return;
    }
    assert(sample->status == VL_SAMPLE_DATA);
    assert(sample->size == VL_ISOTP_CAPACITY);
    assert(sample->payload[0] == 0x49U && sample->payload[1] == 2U);
    for (i = 2U; i < sample->size; ++i)
    {
        assert(sample->payload[i] == long_payload(i));
    }
}

static void test_long_read(void)
{
    long_fixture_t fixture = { .now = UINT32_MAX - 20U };
    vl_scan_workspace_t workspace;
    const vl_scan_port_t port = {&fixture, send_long, receive_long, clock_long, pace_long, emit_long};
    vl_read_request_t read = {
        .response_id = UINT32_C(0x18DAF110), .request_id = UINT32_C(0x18DA10F1),
        .extended = true, .address_byte = true, .tx_address = 0x10U, .rx_address = 0xF1U,
        .service = 9U, .selector = 2U, .block_size = 8U, .separation_time = 10U
    };
    assert(vl_scan_read(&port, &workspace, &read) == VL_SCAN_OK);
    assert(fixture.samples == 1U && fixture.next == VL_ISOTP_CAPACITY);
    assert(fixture.replies == 686U);
    assert(fixture.flows == 86U);

    fixture = (long_fixture_t){ .pending_only = true };
    assert(vl_scan_read(&port, &workspace, &read) == VL_SCAN_PARTIAL);
    assert(fixture.now == 30120U && fixture.samples == 1U);

    read.service = 4U;
    assert(vl_scan_read(&port, &workspace, &read) == VL_SCAN_INVALID_ARGUMENT);
    read.service = 8U;
    assert(vl_scan_read(&port, &workspace, &read) == VL_SCAN_INVALID_ARGUMENT);
}

int main(void)
{
    test_transport_limits();
    test_long_read();
    test_scan(false, false);
    test_scan(true, false);
    test_scan(false, true);
    (void)puts("Vehlog core fixtures passed.");
    return 0;
}
