/*
 * frontend_pipeline.c — 声学前端流水线实现
 *
 * 独立 FreeRTOS 任务 (优先级 3, 栈 6KB):
 *   每 10ms 从环形缓冲区取 160 采样 → 更新 480采样滑动窗口
 *   → micro_frontend → 40维 uint8 spectrogram → feature_buffer
 *
 * 与定时器回调版本的区别:
 *   - 可安全使用阻塞 API (任务上下文)
 *   - 独立栈空间避免溢出
 *   - 架构清晰: 数据生产者在任务中
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "micro_frontend.h"
#include "feature_buffer.h"
#include "audio_capture.h"
#include "sys_config.h"

static const char *TAG = "FEAT_PIPE";

static micro_frontend_handle_t *s_mf_handle = NULL;
static TaskHandle_t             s_task_handle = NULL;

/* ---- 前向滑动窗口 PCM 缓冲区 (480 采样 = 30ms @ 16kHz) ---- */
static int16_t s_pcm_window[MF_WINDOW_SAMPLES] = {0};

/* ================================================================
 * 流水线任务
 * ================================================================ */

static void task_frontend_pipeline(void *pv_params)
{
    ESP_LOGI(TAG, "Frontend pipeline task started (stack free: %d)",
             uxTaskGetStackHighWaterMark(NULL));

    int64_t last_log_ms = 0;
    int     frame_count = 0;
    int64_t next_wake_us = esp_timer_get_time();

    while (1) {
        /* ---- 精确 10ms 周期 ---- */
        int64_t now_us = esp_timer_get_time();
        int64_t sleep_us = next_wake_us - now_us;

        if (sleep_us > 0) {
            vTaskDelay(pdMS_TO_TICKS(sleep_us / 1000));
        }
        next_wake_us += 10000;  /* 10ms */

        /* ---- 1. 从环形缓冲区读取 160 新采样 (10ms @ 16kHz) ---- */
        uint8_t raw[MF_STEP_SAMPLES * 2];  /* 320 bytes */
        esp_err_t ret = audio_ringbuf_read(raw, sizeof(raw));
        int16_t new_samples[MF_STEP_SAMPLES];

        if (ret == ESP_OK) {
            for (int i = 0; i < MF_STEP_SAMPLES; i++) {
                new_samples[i] = (int16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
            }
        } else {
            memset(new_samples, 0, sizeof(new_samples));
        }

        /* ---- 2. 滚动滑动窗口 (类似环形缓冲区) ---- */
        memmove(s_pcm_window,
                s_pcm_window + MF_STEP_SAMPLES,
                (MF_WINDOW_SAMPLES - MF_STEP_SAMPLES) * sizeof(int16_t));
        memcpy(s_pcm_window + MF_WINDOW_SAMPLES - MF_STEP_SAMPLES,
               new_samples,
               MF_STEP_SAMPLES * sizeof(int16_t));

        /* ---- 3. 运行 micro_frontend ---- */
        uint8_t features[MF_NUM_CHANNELS];
        ret = mf_process_frame(s_mf_handle, s_pcm_window, features);
        if (ret != ESP_OK) {
            continue;
        }

        /* ---- 4. 推入特征缓冲区 ---- */
        feature_buffer_push(features);
        frame_count++;

        /* ---- 每 5s 日志 ---- */
        if (now_us - last_log_ms > 5000000) {
            last_log_ms = now_us;
            ESP_LOGD(TAG, "Frames: %d | Stack free: %d | Buffer: %d%%",
                     frame_count,
                     uxTaskGetStackHighWaterMark(NULL),
                     audio_ringbuf_watermark());
        }
    }
}

/* ================================================================
 * 公共 API
 * ================================================================ */

esp_err_t frontend_pipeline_init(void)
{
    ESP_LOGI(TAG, "Initializing frontend pipeline...");

    /* 创建 microfrontend 实例 */
    s_mf_handle = mf_init();
    if (s_mf_handle == NULL) {
        ESP_LOGE(TAG, "Failed to init microfrontend");
        return ESP_FAIL;
    }

    /* 等待环形缓冲区积累至少 30ms 数据 */
    ESP_LOGI(TAG, "Waiting for audio buffer to fill...");
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 预填充滑动窗口 */
    uint8_t initial_raw[MF_WINDOW_SAMPLES * 2];
    esp_err_t ret = audio_ringbuf_read(initial_raw, sizeof(initial_raw));
    if (ret == ESP_OK) {
        for (int i = 0; i < MF_WINDOW_SAMPLES; i++) {
            s_pcm_window[i] = (int16_t)(initial_raw[i * 2] |
                                        (initial_raw[i * 2 + 1] << 8));
        }
        ESP_LOGI(TAG, "PCM window pre-filled (480 samples)");
    } else {
        ESP_LOGW(TAG, "Could not pre-fill PCM window — starting empty");
    }

    /* 创建专用任务 */
    BaseType_t created = xTaskCreate(
        task_frontend_pipeline,
        "Task_Frontend",
        STACK_FRONTEND,
        NULL,
        PRIO_FRONTEND,
        &s_task_handle
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create frontend task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Frontend pipeline running (Task_Frontend, prio=%d, stack=%d)",
             PRIO_FRONTEND, STACK_FRONTEND * (int)sizeof(StackType_t));
    return ESP_OK;
}
