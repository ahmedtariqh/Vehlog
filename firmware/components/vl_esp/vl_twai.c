#include "vl_twai.h"

#include "sdkconfig.h"
#include "vl_freertos_can.h"
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
#include "vl_twai_bench.h"
#include "esp_timer.h"
#endif

#include "driver/gpio.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "hal/twai_types.h"

#define VL_SCAN_TX_MS 60U

static twai_node_handle_t node;
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
static gpio_num_t arm_pin;
#endif

static bool on_rx(twai_node_handle_t handle, const twai_rx_done_event_data_t *event,
                  void *context)
{
    vl_can_frame_t frame = {0};
    twai_frame_t input = {
        .buffer = frame.bytes,
        .buffer_len = sizeof(frame.bytes)
    };
    bool wake_task;
    uint16_t length;

    (void)event;
    (void)context;
    if ((twai_node_receive_from_isr(handle, &input) == ESP_OK) && (input.header.dlc <= 15U))
    {
        frame.time_us = input.header.timestamp;
        frame.id = input.header.id;
        frame.dlc = (uint8_t)input.header.dlc;
        frame.extended = input.header.ide != 0U;
        frame.fd = input.header.fdf != 0U;
        frame.brs = input.header.brs != 0U;
        frame.esi = input.header.esi != 0U;
        frame.remote = input.header.rtr != 0U;
        length = frame.dlc;
        if (frame.fd)
        {
            length = twaifd_dlc2len(frame.dlc);
        }
        else if (length > VL_CAN_CLASSIC_BYTES)
        {
            length = VL_CAN_CLASSIC_BYTES;
        }
        else
        {
            /* The DLC directly describes the Classical CAN payload. */
        }
        frame.length = frame.remote ? 0U : (uint8_t)length;
        wake_task = vl_freertos_can_push_isr(&frame);
    }
    else
    {
        wake_task = vl_freertos_can_fault_isr();
    }
    return wake_task;
}

static bool on_error(twai_node_handle_t handle, const twai_error_event_data_t *event,
                     void *context)
{
    (void)handle;
    (void)event;
    (void)context;
    return vl_freertos_can_fault_isr();
}

#if defined(CONFIG_VEHLOG_BENCH_SCAN)
static bool on_tx(twai_node_handle_t handle, const twai_tx_done_event_data_t *event,
                  void *context)
{
    (void)handle;
    (void)context;
    return vl_freertos_can_tx_done_isr(event->is_tx_success);
}

static bool bench_armed(void)
{
    return gpio_get_level(arm_pin) == 0;
}
#endif

bool vl_twai_start(const vl_twai_config_t *config)
{
    twai_onchip_node_config_t node_config = {0};
    const twai_event_callbacks_t callbacks = {
        .on_rx_done = on_rx,
        .on_error = on_error,
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
        .on_tx_done = on_tx,
#endif
    };
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
    twai_mask_filter_config_t filter = {0};
#endif
    esp_err_t result;

    if ((!GPIO_IS_VALID_OUTPUT_GPIO(config->tx_gpio)) || (!GPIO_IS_VALID_GPIO(config->rx_gpio)) ||
        (config->tx_gpio == config->rx_gpio))
    {
        return false;
    }
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
    if (config->bench_scan)
    {
        if ((!GPIO_IS_VALID_GPIO(config->arm_gpio)) || (config->arm_gpio == config->rx_gpio) ||
            (config->arm_gpio == config->tx_gpio))
        {
            return false;
        }
        arm_pin = (gpio_num_t)config->arm_gpio;
        if ((gpio_set_direction(arm_pin, GPIO_MODE_INPUT) != ESP_OK) ||
            (gpio_set_pull_mode(arm_pin, GPIO_PULLUP_ONLY) != ESP_OK) || (!bench_armed()))
        {
            return false;
        }
    }
#else
    if (config->bench_scan)
    {
        return false;
    }
#endif
    if (!vl_freertos_can_init())
    {
        return false;
    }
    node_config.io_cfg.tx = (gpio_num_t)config->tx_gpio;
    node_config.io_cfg.rx = (gpio_num_t)config->rx_gpio;
    node_config.io_cfg.quanta_clk_out = (gpio_num_t)-1;
    node_config.io_cfg.bus_off_indicator = (gpio_num_t)-1;
    node_config.bit_timing.bitrate = config->nominal_bps;
    node_config.data_timing.bitrate = config->data_bps;
    node_config.tx_queue_depth = 1U;
    node_config.fail_retry_cnt = 0;
    node_config.timestamp_resolution_hz = 1000000U;
    node_config.flags.enable_listen_only = !config->bench_scan;
    result = twai_new_node_onchip(&node_config, &node);
    if (result == ESP_OK)
    {
        result = twai_node_register_event_callbacks(node, &callbacks, NULL);
#if defined(CONFIG_VEHLOG_BENCH_SCAN)
        if ((result == ESP_OK) && config->bench_scan)
        {
            filter.is_ext = config->extended;
            if (config->exact_response)
            {
                filter.id = config->response_id;
                filter.mask = config->extended ? UINT32_C(0x1FFFFFFF) : UINT32_C(0x7FF);
            }
            else
            {
                filter.id = config->extended ? UINT32_C(0x18DAF100) : UINT32_C(0x7E8);
                filter.mask = config->extended ? UINT32_C(0x1FFFFF00) : UINT32_C(0x7F8);
            }
            result = twai_node_config_mask_filter(node, 0U, &filter);
        }
#endif
        if (result == ESP_OK)
        {
            result = twai_node_enable(node);
        }
        if (result != ESP_OK)
        {
            (void)twai_node_delete(node);
            node = NULL;
        }
    }
    return result == ESP_OK;
}

vl_rx_status_t vl_twai_receive(void *context, vl_can_frame_t *frame, uint32_t timeout_ms)
{
    (void)context;
    return vl_freertos_can_receive(frame, timeout_ms);
}

#if defined(CONFIG_VEHLOG_BENCH_SCAN)
bool vl_twai_send(void *context, const vl_can_frame_t *frame)
{
    static twai_frame_t output;
    static uint8_t payload[VL_CAN_CLASSIC_BYTES];
    static bool tx_fault;
    uint8_t i;
    bool success = false;
    (void)context;
    if (bench_armed() && (!tx_fault) && (!vl_freertos_can_fault_pending()) &&
        (frame->length == VL_CAN_CLASSIC_BYTES))
    {
        output.header.id = frame->id;
        output.header.ide = frame->extended;
        output.buffer = payload;
        output.buffer_len = sizeof(payload);
        for (i = 0U; i < VL_CAN_CLASSIC_BYTES; ++i)
        {
            payload[i] = frame->bytes[i];
        }
        if (twai_node_transmit(node, &output, 0) == ESP_OK)
        {
            success = vl_freertos_can_wait_tx(VL_SCAN_TX_MS);
        }
        tx_fault = !success;
    }
    return success;
}

uint32_t vl_twai_clock_ms(void *context)
{
    (void)context;
    return (uint32_t)((uint64_t)esp_timer_get_time() / UINT64_C(1000));
}

bool vl_twai_stop(void)
{
    return twai_node_disable(node) == ESP_OK;
}
#endif
