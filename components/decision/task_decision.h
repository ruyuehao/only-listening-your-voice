/*
 * task_decision.h — 决策调度 FreeRTOS 任务
 *
 * 核心状态机调度器:
 *   - 等待 EVENT_KWS_TRIGGERED → STATE_LISTENING → 触发 SV
 *   - 等待 EVENT_SV_DONE → 根据相似度 → ACCEPTED / REJECTED
 *   - 1s 后自动回 STATE_IDLE
 *   - 周期刷新 LED 状态
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t task_decision_create(void);

#ifdef __cplusplus
}
#endif
