#include "vl_freertos_can.h"

#if defined(VL_FREERTOS_PREFIXED_HEADERS)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#endif

#define VL_QUEUE_FRAMES 48U
#define VL_CAPTURE_FAULT UINT32_C(1)
#define VL_TX_COMPLETE UINT32_C(2)

static StaticQueue_t rx_queue_control;
static uint8_t rx_queue_memory[VL_QUEUE_FRAMES * sizeof(vl_can_frame_t)];
static QueueHandle_t rx_queue;
static TaskHandle_t owner_task;

static TickType_t ticks_for_ms(uint32_t milliseconds)
{
    /* Round up to support ports with a tick slower than one millisecond. */
    const uint64_t ticks = (((uint64_t)milliseconds * (uint64_t)configTICK_RATE_HZ) +
                            UINT64_C(999)) / UINT64_C(1000);
    const uint64_t limit = (uint64_t)portMAX_DELAY - UINT64_C(1);
    return (TickType_t)((ticks < limit) ? ticks : limit);
}

bool vl_freertos_can_init(void)
{
    owner_task = xTaskGetCurrentTaskHandle();
    rx_queue = xQueueCreateStatic(VL_QUEUE_FRAMES, sizeof(vl_can_frame_t), rx_queue_memory,
                                  &rx_queue_control);
    return rx_queue != NULL;
}

static bool notify_isr(uint32_t bits)
{
    BaseType_t higher = pdFALSE;
    (void)xTaskNotifyFromISR(owner_task, bits, eSetBits, &higher);
    return higher == pdTRUE;
}

bool vl_freertos_can_push_isr(const vl_can_frame_t *frame)
{
    BaseType_t higher = pdFALSE;
    if (xQueueSendFromISR(rx_queue, frame, &higher) != pdPASS)
    {
        (void)xTaskNotifyFromISR(owner_task, VL_CAPTURE_FAULT, eSetBits, &higher);
    }
    return higher == pdTRUE;
}

bool vl_freertos_can_fault_isr(void)
{
    return notify_isr(VL_CAPTURE_FAULT);
}

bool vl_freertos_can_tx_done_isr(bool success)
{
    return notify_isr(success ? VL_TX_COMPLETE : VL_CAPTURE_FAULT);
}

bool vl_freertos_can_fault_pending(void)
{
    uint32_t bits = 0U;
    return (xTaskNotifyWait(0U, UINT32_MAX, &bits, 0U) == pdPASS) &&
           ((bits & VL_CAPTURE_FAULT) != 0U);
}

vl_rx_status_t vl_freertos_can_receive(vl_can_frame_t *frame, uint32_t timeout_ms)
{
    vl_rx_status_t result = VL_RX_FAULT;
    if (!vl_freertos_can_fault_pending())
    {
        const BaseType_t received = xQueueReceive(rx_queue, frame, ticks_for_ms(timeout_ms));
        if (!vl_freertos_can_fault_pending())
        {
            result = (received == pdPASS) ? VL_RX_FRAME : VL_RX_TIMEOUT;
        }
    }
    return result;
}

bool vl_freertos_can_wait_tx(uint32_t timeout_ms)
{
    uint32_t bits = 0U;
    return (xTaskNotifyWait(0U, UINT32_MAX, &bits, ticks_for_ms(timeout_ms)) == pdPASS) &&
           (bits == VL_TX_COMPLETE);
}

void vl_freertos_can_pace(void *context, uint32_t delay_ms)
{
    (void)context;
    vTaskDelay(ticks_for_ms(delay_ms));
}
