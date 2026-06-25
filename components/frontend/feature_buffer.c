/*
 * feature_buffer.c — 滑动窗口 Spectrogram 缓冲区实现
 *
 * 环形缓冲区：存储最近 FB_MAX_FRAMES 帧的 40 维 spectrogram
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "feature_buffer.h"

static const char *TAG = "FEAT_BUF";

/* ---- 环形缓冲区 ---- */
static uint8_t s_buffer[FB_MAX_FRAMES][FB_NUM_CHANNELS];
static int      s_head     = 0;   /* 下一帧写入位置 */
static int      s_count    = 0;   /* 当前帧数 (≤ FB_MAX_FRAMES) */
static SemaphoreHandle_t s_mutex = NULL;

esp_err_t feature_buffer_init(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
    s_head  = 0;
    s_count = 0;

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Feature buffer initialized: %d frames × %d channels",
             FB_MAX_FRAMES, FB_NUM_CHANNELS);
    return ESP_OK;
}

esp_err_t feature_buffer_push(const uint8_t *frame)
{
    if (s_mutex == NULL) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    memcpy(s_buffer[s_head], frame, FB_NUM_CHANNELS);
    s_head = (s_head + 1) % FB_MAX_FRAMES;
    if (s_count < FB_MAX_FRAMES) {
        s_count++;
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t feature_buffer_get_recent(uint8_t *dst, int n_frames)
{
    if (s_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (n_frames > FB_MAX_FRAMES) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_count < n_frames) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FINISHED;
    }

    /* 从 (s_head - n_frames) 到 (s_head - 1) 按时间顺序拷贝 */
    int start = (s_head - n_frames + FB_MAX_FRAMES) % FB_MAX_FRAMES;
    for (int i = 0; i < n_frames; i++) {
        int idx = (start + i) % FB_MAX_FRAMES;
        memcpy(&dst[i * FB_NUM_CHANNELS], s_buffer[idx], FB_NUM_CHANNELS);
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

int feature_buffer_count(void)
{
    if (s_mutex == NULL) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int count = s_count;
    xSemaphoreGive(s_mutex);
    return count;
}

bool feature_buffer_ready_for_kws(void)
{
    return feature_buffer_count() >= 100;
}

bool feature_buffer_ready_for_sv(void)
{
    return feature_buffer_count() >= 40;
}
