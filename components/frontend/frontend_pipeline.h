/*
 * frontend_pipeline.h — 声学前端处理流水线
 *
 * 每 10ms 触发一次:
 *   环形缓冲区 → 取 480 采样 (30ms窗口) → mf_process_frame → feature_buffer_push
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化声学前端处理流水线
 *
 * 创建 microfrontend 实例 + 启动 10ms FreeRTOS 定时器
 */
esp_err_t frontend_pipeline_init(void);

#ifdef __cplusplus
}
#endif
