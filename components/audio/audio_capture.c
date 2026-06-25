/*
 * audio_capture.c — I2S 音频采集模块实现
 *
 * ESP-IDF I2S STD Driver + FreeRTOS Ring Buffer
 * 目标: INMP441 @ 16kHz/16bit/mono, 事件驱动通知下游
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include "pin_defs.h"
#include "sys_config.h"

static const char *TAG = "AUDIO_CAP";

/* ================================================================
 * 全局状态
 * ================================================================ */

static i2s_chan_handle_t  s_i2s_handle  = NULL;   /* I2S 通道句柄 */
static RingbufHandle_t    s_ringbuf     = NULL;   /* 环形缓冲区句柄 */
static TaskHandle_t       s_task_handle = NULL;   /* 采集任务句柄 */

/* 100ms 等价字节数: 16000 Hz * 2 bytes/sample * 0.1s = 3200 bytes */
#define FRAME_NOTIFY_BYTES  (AUDIO_SAMPLE_RATE * (AUDIO_BITS_PER_SAMPLE / 8) / 10)

/* 引用 main.c 中的全局事件组 */
extern EventGroupHandle_t g_event_group;

/* ================================================================
 * 环形缓冲区实现
 * ================================================================ */

esp_err_t audio_ringbuf_read(uint8_t *dst, size_t size)
{
    if (s_ringbuf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t received = 0;
    uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf, &received,
                                                       pdMS_TO_TICKS(200), size);
    if (data == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(dst, data, received);
    vRingbufferReturnItem(s_ringbuf, data);

    /* 如果读到的数据不够，返回错误 */
    if (received < size) {
        return ESP_ERR_NOT_FINISHED;
    }

    return ESP_OK;
}

size_t audio_ringbuf_available(void)
{
    if (s_ringbuf == NULL) return 0;

    /* xRingbufferGetCurFreeSize 返回空闲空间 */
    return RINGBUFFER_SIZE_BYTES - xRingbufferGetCurFreeSize(s_ringbuf, NULL);
}

uint8_t audio_ringbuf_watermark(void)
{
    size_t avail = audio_ringbuf_available();
    return (uint8_t)((avail * 100) / RINGBUFFER_SIZE_BYTES);
}

/* ================================================================
 * I2S 音频采集任务
 *
 * 优先级 5（最高），栈 4KB
 * 阻塞等待 I2S DMA 数据就绪 → 写入环形缓冲区
 * 每累积 FRAME_NOTIFY_BYTES → 通知事件组 EVENT_AUDIO_READY
 * ================================================================ */

static void task_audio_capture(void *pv_params)
{
    ESP_LOGI(TAG, "Audio capture task started (stack: %d bytes)",
             uxTaskGetStackHighWaterMark(NULL));

    size_t accumulated = 0;
    int64_t last_log_ms = 0;

    while (1) {
        /* 从 I2S 读取一帧 PCM 数据 */
        size_t bytes_read = 0;
        uint8_t i2s_buf[AUDIO_I2S_BUFFER_SIZE];
        esp_err_t ret = i2s_channel_read(s_i2s_handle,
                                         i2s_buf,
                                         sizeof(i2s_buf),
                                         &bytes_read,
                                         portMAX_DELAY);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S read failed: %s (0x%x)", esp_err_to_name(ret), ret);
            continue;
        }

        if (bytes_read == 0) continue;

        /* 写入环形缓冲区（线程安全） */
        BaseType_t done = xRingbufferSend(s_ringbuf,
                                          i2s_buf,
                                          bytes_read,
                                          pdMS_TO_TICKS(10));
        if (done != pdTRUE) {
            ESP_LOGW(TAG, "Ring buffer full, dropping %d bytes", bytes_read);
            continue;
        }

        accumulated += bytes_read;

        /* 每累积 ~100ms 数据 → 通知事件组 */
        if (accumulated >= FRAME_NOTIFY_BYTES) {
            accumulated = 0;
            xEventGroupSetBits(g_event_group, EVENT_AUDIO_READY);
        }

        /* 每 5s 打印一次水位日志 */
        int64_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now_ms - last_log_ms > 5000) {
            last_log_ms = now_ms;
            ESP_LOGD(TAG, "Buffer: %d%% | Stack free: %d",
                     audio_ringbuf_watermark(),
                     uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

/* ================================================================
 * 公共 API
 * ================================================================ */

esp_err_t audio_capture_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S audio capture...");

    /* ---- 1. 创建环形缓冲区 ---- */
    s_ringbuf = xRingbufferCreate(RINGBUFFER_SIZE_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (s_ringbuf == NULL) {
        ESP_LOGE(TAG, "Failed to create ring buffer (%d bytes)", RINGBUFFER_SIZE_BYTES);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Ring buffer created: %d bytes", RINGBUFFER_SIZE_BYTES);

    /* ---- 2. 配置 I2S STD 通道 ---- */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_i2s_handle));

    /* ---- 3. 配置 I2S STD 模式 ---- */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
                         AUDIO_BITS_PER_SAMPLE, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED,
        },
    };

    /* 声道掩码: 单声道左声道 */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_handle, &std_cfg));

    /* ---- 4. 启动 I2S ---- */
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_handle));
    ESP_LOGI(TAG, "I2S started: %d Hz / %d-bit mono (WS=%d, BCK=%d, DOUT=%d)",
             AUDIO_SAMPLE_RATE, AUDIO_BITS_PER_SAMPLE,
             I2S_WS_PIN, I2S_BCLK_PIN, I2S_DOUT_PIN);

    /* ---- 5. 创建采集任务 ---- */
    BaseType_t created = xTaskCreate(
        task_audio_capture,
        "Task_AudioCapture",
        STACK_AUDIO_CAPTURE,
        NULL,
        PRIO_AUDIO_CAPTURE,
        &s_task_handle
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio capture task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Audio capture initialized OK");
    return ESP_OK;
}

esp_err_t audio_capture_feed(const uint8_t *src, size_t size)
{
    /* 直接写入环形缓冲区（用于 I2S 回调场景） */
    BaseType_t done = xRingbufferSendFromISR(s_ringbuf,
                                              (void *)src,
                                              size,
                                              NULL);
    return (done == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}
