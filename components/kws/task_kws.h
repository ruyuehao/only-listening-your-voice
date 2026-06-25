/*
 * task_kws.h — KWS 事件驱动 FreeRTOS 任务
 *
 * 等待 EVENT_AUDIO_READY → 检查 feature_buffer ≥ 100 帧 → 推理
 * → 置信度 ≥ KWS_THRESHOLD → 触发 EVENT_KWS_TRIGGERED
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 KWS 任务
 *
 * 优先级 4，栈 8KB
 * 事件驱动: 等待 EVENT_AUDIO_READY | EVENT_TIMER_100MS
 *
 * @return ESP_OK
 */
esp_err_t task_kws_create(void);

#ifdef __cplusplus
}
#endif
