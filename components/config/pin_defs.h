/*
 * pin_defs.h — ESP32-C3 声纹唤醒系统引脚定义
 * 版本: v1.3（板载 RGB + BOOT 键复用）
 */

#pragma once

#include "driver/gpio.h"

/* ================================================================
 * I2S 接口 — INMP441 麦克风
 * ================================================================ */
#define I2S_PORT            I2S_NUM_0
#define I2S_WS_PIN          GPIO_NUM_4    // 声道选择
#define I2S_BCLK_PIN        GPIO_NUM_5    // 位时钟（< 5cm 杜邦线防串扰）
#define I2S_DOUT_PIN        GPIO_NUM_6    // 数据输入

/* ================================================================
 * 板载外设 — RGB LED + BOOT 键
 * ================================================================ */
#define LED_WS2812_PIN      GPIO_NUM_8    // 板载 WS2812 RGB LED
#define BOOT_BUTTON_PIN     GPIO_NUM_9    // 板载 BOOT 键（Strapping Pin！上电勿按）

/* ================================================================
 * UART — 通信上报（接 4G/BLE 模组）
 * ================================================================ */
#define UART_PORT           UART_NUM_1
#define UART_TX_PIN         GPIO_NUM_21   // → 模组 RX
#define UART_RX_PIN         GPIO_NUM_20   // ← 模组 TX

/* ================================================================
 * 辅助 GPIO — 性能测量打桩
 * ================================================================ */
#define PROFILE_KWS_PIN     GPIO_NUM_10   // KWS 推理耗时打桩
#define PROFILE_SV_PIN      GPIO_NUM_3    // SV 推理耗时打桩
