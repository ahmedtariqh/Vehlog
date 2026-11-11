#include "sdkconfig.h"
#include "vl_text.h"
#include "vl_twai.h"

#if defined(CONFIG_VEHLOG_BENCH_SCAN)
#include "vl_freertos_can.h"
#include "vl_twai_bench.h"
#endif

#include <stdio.h>

void app_main(void);

#if defined(CONFIG_VEHLOG_BENCH_SCAN)
static void print_sample(void *context, const vl_sample_t *sample)
{
    static char line[VL_TEXT_SAMPLE_CAPACITY];
    bool *const output_ok = context;
    if ((!vl_text_sample(sample, line, sizeof(line))) || (fputs(line, stdout) == EOF))
    {
        *output_ok = false;
    }
}

static void run(bool extended)
{
    char line[VL_TEXT_END_CAPACITY];
    bool output_ok = true;
    const vl_scan_port_t port = {
        .context = &output_ok,
        .send = vl_twai_send,
        .receive = vl_twai_receive,
        .clock_ms = vl_twai_clock_ms,
        .pace = vl_freertos_can_pace,
        .emit = print_sample
    };
    static vl_scan_workspace_t workspace;
    vl_scan_status_t result;
#if defined(CONFIG_VEHLOG_BENCH_SINGLE_READ)
    const vl_read_request_t read = {
        .response_id = CONFIG_VEHLOG_BENCH_RESPONSE_ID,
        .request_id = CONFIG_VEHLOG_BENCH_REQUEST_ID,
        .extended = extended,
#if defined(CONFIG_VEHLOG_BENCH_ADDRESS_BYTE)
        .address_byte = true,
        .tx_address = CONFIG_VEHLOG_BENCH_TX_ADDRESS,
        .rx_address = CONFIG_VEHLOG_BENCH_RX_ADDRESS,
#endif
        .service = CONFIG_VEHLOG_BENCH_SERVICE,
        .selector = CONFIG_VEHLOG_BENCH_SELECTOR,
        .freeze_frame = CONFIG_VEHLOG_BENCH_FREEZE_FRAME,
        .block_size = CONFIG_VEHLOG_BENCH_BLOCK_SIZE,
        .separation_time = CONFIG_VEHLOG_BENCH_SEPARATION
    };
    result = vl_scan_read(&port, &workspace, &read);
#else
    result = vl_scan_once(&port, &workspace, extended);
#endif
    const bool stopped = vl_twai_stop();
    if (!output_ok)
    {
        result = VL_SCAN_PORT_ERROR;
    }
    if (vl_text_scan_end(result, stopped, line, sizeof(line)))
    {
        (void)fputs(line, stdout);
    }
}
#else
static void run(void)
{
    char line[VL_TEXT_CAPTURE_CAPACITY];
    vl_can_frame_t frame;
    for (;;)
    {
        const vl_rx_status_t status = vl_twai_receive(NULL, &frame, 1000U);
        if (status == VL_RX_FRAME)
        {
            if ((!vl_text_capture(&frame, "can0", line, sizeof(line))) ||
                (fputs(line, stdout) == EOF))
            {
                (void)puts("DROPCOUNT: unknown (capture output error)");
            }
        }
        else if (status == VL_RX_FAULT)
        {
            (void)puts("DROPCOUNT: unknown (TWAI bus/receive error or software queue overflow)");
        }
        else
        {
            /* No frame was observed during this interval. */
        }
    }
}
#endif

void app_main(void)
{
    const vl_twai_config_t config = {
        .tx_gpio = CONFIG_VEHLOG_TX_GPIO,
        .rx_gpio = CONFIG_VEHLOG_RX_GPIO,
        .nominal_bps = CONFIG_VEHLOG_NOMINAL_BPS,
        .data_bps = CONFIG_VEHLOG_DATA_BPS,
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
        .bench_scan = true,
        .arm_gpio = CONFIG_VEHLOG_BENCH_ARM_GPIO,
#if defined(CONFIG_VEHLOG_BENCH_SINGLE_READ)
        .exact_response = true,
        .response_id = CONFIG_VEHLOG_BENCH_RESPONSE_ID,
#endif
#if defined(CONFIG_VEHLOG_BENCH_EXTENDED)
        .extended = true
#else
        .extended = false
#endif
#else
        .bench_scan = false,
        .arm_gpio = -1,
        .extended = false
#endif
    };

    if (vl_twai_start(&config))
    {
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
        run(config.extended);
#else
        run();
#endif
    }
    else
    {
        (void)puts("# Vehlog stopped: missing/invalid board configuration, arm input or driver failure");
    }
}
