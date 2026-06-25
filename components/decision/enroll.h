/*
 * enroll.h — 声纹注册流程
 *
 * BOOT 键 (GPIO9) 长按 3s → 进入注册模式:
 *   1. 蓝灯 2Hz 快闪 (ENROLL_TRIGGER)
 *   2. 连续录制 5 次唤醒词 (ENROLL_RECORD)
 *   3. 计算平均 Embedding → NVS → 红灯 2s (ENROLL_SAVED)
 *   4. esp_restart()
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 BOOT 键监控 + 注册流程任务
 *
 * 监控 GPIO9 长按 (3s) → 启动注册
 *
 * @return ESP_OK
 */
esp_err_t enroll_task_create(void);

#ifdef __cplusplus
}
#endif
