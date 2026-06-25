/*
 * event_reporter.h — 通信上报模块
 *
 * 验证通过后通过 UART 发送 JSON 事件给 4G/BLE 模组
 *
 * JSON 格式:
 *   {"dev":"esp32c3_kws_01","evt":"wake","conf":0.92,"sim":0.83,"ts":1720000000}
 *
 * 协议: 发送 → 等待 "OK\r\n" → 500ms 超时重试 1 次
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 UART 通信
 *
 * UART1: TX=GPIO21, RX=GPIO20, 115200 baud
 */
esp_err_t event_reporter_init(void);

/**
 * @brief 发送唤醒事件 JSON
 *
 * @param confidence  KWS 置信度
 * @param similarity  SV 余弦相似度
 * @return ESP_OK
 */
esp_err_t event_reporter_send_wake(float confidence, float similarity);

#ifdef __cplusplus
}
#endif
