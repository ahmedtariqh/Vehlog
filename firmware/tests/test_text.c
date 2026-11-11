#include "vl_text.h"

#include <assert.h>
#include <string.h>

static void check_capture(void)
{
    vl_can_frame_t frame = {0};
    char line[VL_TEXT_CAPTURE_CAPACITY];
    frame.time_us = UINT64_C(1234567);
    frame.id = UINT32_C(0x7E8);
    frame.length = 3U;
    frame.dlc = 3U;
    frame.bytes[0] = 0x41U;
    frame.bytes[1] = 0x0DU;
    frame.bytes[2] = 0x58U;
    assert(vl_text_capture(&frame, "can0", line, sizeof(line)));
    assert(strcmp(line, "(1.234567) can0 7E8#410D58\n") == 0);

    frame.extended = true;
    frame.id = UINT32_C(0x18DAF110);
    frame.fd = true;
    frame.brs = true;
    frame.esi = true;
    frame.length = 64U;
    frame.dlc = 15U;
    frame.time_us = UINT64_MAX;
    assert(vl_text_capture(&frame, "remote-can_1234", line, sizeof(line)));
    assert(strstr(line, "remote-can_1234 18DAF110##7410D58") != NULL);
    assert(strlen(line) == strlen("(18446744073709.551615) remote-can_1234 18DAF110##7\n") +
                           (2U * VL_CAN_BYTES));

    frame.fd = false;
    frame.brs = false;
    frame.esi = false;
    frame.remote = true;
    frame.length = 0U;
    frame.time_us = 0U;
    assert(vl_text_capture(&frame, "can1", line, sizeof(line)));
    assert(strcmp(line, "(0.000000) can1 18DAF110#R8_F\n") == 0);
    assert(!vl_text_capture(&frame, "can1\ncan2", line, sizeof(line)));
    assert(!vl_text_capture(&frame, "0123456789abcdef", line, sizeof(line)));
    assert(!vl_text_capture(&frame, "can1", line, 1U));
    assert(line[0] == '\0');
}

static void check_diagnostic(void)
{
    vl_sample_t sample = {0};
    uint8_t payload[VL_ISOTP_CAPACITY] = {0};
    char line[VL_TEXT_SAMPLE_CAPACITY];
    sample.ecu = UINT32_C(0x7E8);
    sample.pid = 5U;
    sample.service = 1U;
    sample.payload = payload;
    sample.status = VL_SAMPLE_DATA;
    sample.time_us = UINT64_C(1234567);
    sample.result.unit = VL_UNIT_MILLI_C;
    sample.result.value_milli = -INT32_C(1000);
    sample.size = 3U;
    payload[0] = 0x41U;
    payload[1] = 0x05U;
    payload[2] = 0x27U;
    assert(vl_text_sample(&sample, line, sizeof(line)));
    assert(strcmp(line, "{\"v\":1,\"record\":\"diag\",\"service\":1,\"ecu\":2024,"
                        "\"extended\":false,\"pid\":5,\"status\":\"data\",\"mono_us\":1234567,"
                        "\"bitmap\":0,\"nrc\":0,\"unit\":\"degC\",\"value_milli\":-1000,"
                        "\"pdu\":\"410527\"}\n") == 0);
    sample.size = VL_ISOTP_CAPACITY;
    sample.ecu = UINT32_MAX;
    sample.time_us = UINT64_MAX;
    sample.status = VL_SAMPLE_SUPPORTED_PAGE;
    sample.result.bitmap = UINT32_MAX;
    sample.result.value_milli = INT32_MIN;
    sample.result.negative_code = UINT8_MAX;
    sample.pid = UINT8_MAX;
    assert(vl_text_sample(&sample, line, sizeof(line)));
    sample.size = VL_ISOTP_CAPACITY + 1U;
    assert(!vl_text_sample(&sample, line, sizeof(line)));
    assert(line[0] == '\0');
    assert(vl_text_scan_end(VL_SCAN_PARTIAL, true, line, sizeof(line)));
    assert(strcmp(line, "{\"v\":1,\"record\":\"scan_end\",\"status_code\":4,\"stopped\":true}\n") == 0);
}

int main(void)
{
    check_capture();
    check_diagnostic();
    return 0;
}
