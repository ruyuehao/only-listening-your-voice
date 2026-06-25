/*
 * task_sv.h — 声纹验证 FreeRTOS 任务
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t task_sv_create(void);

#ifdef __cplusplus
}
#endif
