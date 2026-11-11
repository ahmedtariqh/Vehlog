#include "vl_text.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char hex_digits[] = "0123456789ABCDEF";

static void bytes_hex(const uint8_t *bytes, uint16_t count, char *text)
{
    uint16_t i;
    for (i = 0U; i < count; ++i)
    {
        text[2U * i] = hex_digits[bytes[i] >> 4U];
        text[(2U * i) + 1U] = hex_digits[bytes[i] & 0x0FU];
    }
    text[2U * count] = '\0';
}

static bool finish_line(int written, char *output, size_t capacity)
{
    const bool fits = (written >= 0) && ((size_t)written < capacity);
    if (!fits)
    {
        output[0] = '\0';
    }
    return fits;
}

static bool valid_interface(const char *name)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-";
    size_t i = 0U;
    if (name == NULL)
    {
        return false;
    }
    while ((i < 16U) && (name[i] != '\0'))
    {
        if (strchr(alphabet, (int)name[i]) == NULL)
        {
            return false;
        }
        ++i;
    }
    return (i != 0U) && (i < 16U);
}

bool vl_text_capture(const vl_can_frame_t *frame, const char *interface_name,
                     char *output, size_t capacity)
{
    char body[(2U * VL_CAN_BYTES) + 1U];
    char suffix[3] = {0};
    char flags[3] = {0};
    uint8_t fd_flags;
    int written;

    if ((output == NULL) || (capacity == 0U))
    {
        return false;
    }
    output[0] = '\0';
    if ((frame == NULL) || (!valid_interface(interface_name)) || (frame->dlc > 15U) ||
        (frame->length > (frame->fd ? VL_CAN_BYTES : VL_CAN_CLASSIC_BYTES)) ||
        (frame->remote && (frame->fd || (frame->length != 0U))) ||
        (frame->id > (frame->extended ? UINT32_C(0x1FFFFFFF) : UINT32_C(0x7FF))))
    {
        return false;
    }
    bytes_hex(frame->bytes, frame->length, body);
    if (frame->fd)
    {
        fd_flags = (uint8_t)(4U + (frame->brs ? 1U : 0U) + (frame->esi ? 2U : 0U));
        flags[0] = '#';
        flags[1] = hex_digits[fd_flags];
    }
    else
    {
        if (frame->remote)
        {
            body[0] = 'R';
            body[1] = hex_digits[(frame->dlc <= VL_CAN_CLASSIC_BYTES) ?
                                 frame->dlc : VL_CAN_CLASSIC_BYTES];
            body[2] = '\0';
        }
        if (frame->dlc > VL_CAN_CLASSIC_BYTES)
        {
            suffix[0] = '_';
            suffix[1] = hex_digits[frame->dlc];
        }
    }
    written = snprintf(output, capacity,
                       "(%" PRIu64 ".%06" PRIu64 ") %s %0*" PRIX32 "#%s%s%s\n",
                       frame->time_us / UINT64_C(1000000), frame->time_us % UINT64_C(1000000),
                       interface_name, frame->extended ? 8 : 3, frame->id, flags, body, suffix);
    return finish_line(written, output, capacity);
}

static const char *sample_status(vl_sample_status_t status)
{
    switch (status)
    {
        case VL_SAMPLE_SUPPORTED_PAGE: return "supported_page";
        case VL_SAMPLE_DATA: return "data";
        case VL_SAMPLE_NEGATIVE: return "negative";
        case VL_SAMPLE_NO_RESPONSE: return "no_response";
        case VL_SAMPLE_INCOMPLETE: return "incomplete";
        default: return "internal_error";
    }
}

static const char *metric_unit(vl_obd_unit_t unit)
{
    switch (unit)
    {
        case VL_UNIT_MILLI_RPM: return "rpm";
        case VL_UNIT_MILLI_KPH: return "km/h";
        case VL_UNIT_MILLI_C: return "degC";
        case VL_UNIT_MILLI_PERCENT: return "%";
        case VL_UNIT_NONE:
        default: return "";
    }
}

bool vl_text_sample(const vl_sample_t *sample, char *output, size_t capacity)
{
    int written;
    size_t used;
    const size_t tail_size = 4U;
    char freeze_frame[24] = {0};
    if ((output == NULL) || (capacity == 0U))
    {
        return false;
    }
    output[0] = '\0';
    if ((sample == NULL) || (sample->size > VL_ISOTP_CAPACITY) ||
        ((sample->size != 0U) && (sample->payload == NULL)))
    {
        return false;
    }
    if (sample->service == 0x02U)
    {
        written = snprintf(freeze_frame, sizeof(freeze_frame), ",\"freeze_frame\":%u",
                           (unsigned int)sample->freeze_frame);
        if (!finish_line(written, freeze_frame, sizeof(freeze_frame)))
        {
            return false;
        }
    }
    written = snprintf(output, capacity,
                       "{\"v\":1,\"record\":\"diag\",\"service\":%u,\"ecu\":%" PRIu32
                       ",\"extended\":%s,\"pid\":%u,\"status\":\"%s\",\"mono_us\":%" PRIu64
                       ",\"bitmap\":%" PRIu32 ",\"nrc\":%u,\"unit\":\"%s\",\"value_milli\":%" PRId32
                       "%s,\"pdu\":\"",
                       (unsigned int)sample->service, sample->ecu,
                       sample->extended ? "true" : "false", (unsigned int)sample->pid,
                       sample_status(sample->status), sample->time_us, sample->result.bitmap,
                       (unsigned int)sample->result.negative_code, metric_unit(sample->result.unit),
                       sample->result.value_milli, freeze_frame);
    if (!finish_line(written, output, capacity))
    {
        return false;
    }
    used = (size_t)written;
    if ((capacity - used) < ((2U * (size_t)sample->size) + tail_size))
    {
        output[0] = '\0';
        return false;
    }
    bytes_hex(sample->payload, sample->size, &output[used]);
    used += 2U * (size_t)sample->size;
    output[used] = '\"';
    output[used + 1U] = '}';
    output[used + 2U] = '\n';
    output[used + 3U] = '\0';
    return true;
}

bool vl_text_scan_end(vl_scan_status_t result, bool stopped, char *output, size_t capacity)
{
    int written;
    if ((output == NULL) || (capacity == 0U))
    {
        return false;
    }
    written = snprintf(output, capacity,
                       "{\"v\":1,\"record\":\"scan_end\",\"status_code\":%u,\"stopped\":%s}\n",
                       (unsigned int)result, stopped ? "true" : "false");
    return finish_line(written, output, capacity);
}
