/*
 * frontend_pipeline.c — 声学前端流水线实现
 *
 * 10ms 定时器 → 从环形缓冲区读 480 采样 → micro_frontend → feature_buffer
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "micro_frontend.h"
#include "feature_buffer.h"
#include "audio_capture.h"

static const char *TAG = "FEAT_PIPE";

static micro_frontend_handle_t *s_mf_handle = NULL;
static TimerHandle_t             s_timer     = NULL;

/* ---- 前向滑动窗口 PCM 缓冲区 ---- */
/* 保留最近 480 采样 (30ms)，每 10ms 送入 160 新采样 */
static int16_t s_pcm_window[MF_WINDOW_SAMPLES] = {0};
static int     s_pcm_window_pos = 0;  /* 下一个写入位置 */

/* ================================================================
 * 10ms 定时器回调
 * ================================================================ */

static void timer_10ms_callback(TimerHandle_t xTimer)
{
    /* 1. 从环形缓冲区读取 160 新采样 (10ms @ 16kHz) */
    uint8_t raw[MF_STEP_SAMPLES * 2];  /* 160 × int16 = 320 bytes */
    esp_err_t ret = audio_ringbuf_read(raw, sizeof(raw));

    int16_t new_samples[MF_STEP_SAMPLES];

    if (ret == ESP_OK) {
        /* 字节序转换: little-endian int16 */
        for (int i = 0; i < MF_STEP_SAMPLES; i++) {
            new_samples[i] = (int16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
        }
    } else {
        /* 数据不足时补零 */
        memset(new_samples, 0, sizeof(new_samples));
    }

    /* 2. 更新滑动窗口 (环形写入) */
    /* 窗口: 480 采样 = 3 个 160-采样块 */
    /* 块数: 480 / 160 = 3 */
    int block_count = MF_WINDOW_SAMPLES / MF_STEP_SAMPLES;
    int block_idx = s_pcm_window_pos / MF_STEP_SAMPLES;  /* 0, 1, 2 */

    memcpy(&s_pcm_window[block_idx * MF_STEP_SAMPLES],
           new_samples, MF_STEP_SAMPLES * sizeof(int16_t));

    s_pcm_window_pos = (s_pcm_window_pos + MF_STEP_SAMPLES) % MF_WINDOW_SAMPLES;

    /* 3. 运行 micro_frontend */
    uint8_t features[MF_NUM_CHANNELS];
    ret = mf_process_frame(s_mf_handle, s_pcm_window, features);
    if (ret != ESP_OK) {
        return;
    }

    /* 4. 推入特征缓冲区 */
    feature_buffer_push(features);
}

/* ================================================================ */

esp_err_t frontend_pipeline_init(void)
{
    ESP_LOGI(TAG, "Initializing frontend pipeline...");

    /* 创建 microfrontend 实例 */
    s_mf_handle = mf_init();
    if (s_mf_handle == NULL) {
        ESP_LOGE(TAG, "Failed to init microfrontend");
        return ESP_FAIL;
    }

    /* 等待环形缓冲区积累至少 30ms 数据 (480 采样) */
    ESP_LOGI(TAG, "Waiting for audio buffer to fill...");
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 预填充 PCM 滑动窗口 */
    uint8_t initial_raw[MF_WINDOW_SAMPLES * 2];
    esp_err_t ret = audio_ringbuf_read(initial_raw, sizeof(initial_raw));
    if (ret == ESP_OK) {
        for (int i = 0; i < MF_WINDOW_SAMPLES; i++) {
            s_pcm_window[i] = (int16_t)(initial_raw[i * 2] | (initial_raw[i * 2 + 1] << 8));
        }
    }

    /* 创建 10ms FreeRTOS 定时器 */
    s_timer = xTimerCreate(
        "Timer_10ms",
        pdMS_TO_TICKS(10),
        pdTRUE,     /* 自动重载 */
        NULL,
        timer_10ms_callback
    );

    if (s_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t started = xTimerStart(s_timer, 0);
    if (started != pdPASS) {
        ESP_LOGE(TAG, "Failed to start timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Frontend pipeline running (10ms timer)");
    return ESP_OK;
}
