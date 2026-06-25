/*
 * led_indicator.h — WS2812 RGB LED 状态指示
 *
 * 使用板载 WS2812 (GPIO8) 显示系统状态
 *
 * 颜色方案:
 *   IDLE:              绿色 1Hz 闪烁
 *   LISTENING:         绿色常亮
 *   VERIFYING:         绿色 2Hz 快闪
 *   ACCEPTED:          红色常亮 1 秒
 *   REJECTED:          红色闪烁 3 次
 *   ENROLL_TRIGGER:    蓝色 2Hz 快闪
 *   ENROLL_RECORD:     蓝色闪烁 (录制指示)
 *   ENROLL_SAVED:      红色常亮 2 秒
 */

#pragma once

#include "esp_err.h"
#include "fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- RGB 颜色 ---- */
typedef struct {
    uint8_t r, g, b;
} led_color_t;

/* 预设颜色 */
#define LED_COLOR_OFF      ((led_color_t){0, 0, 0})
#define LED_COLOR_RED      ((led_color_t){255, 0, 0})
#define LED_COLOR_GREEN    ((led_color_t){0, 255, 0})
#define LED_COLOR_BLUE     ((led_color_t){0, 0, 255})
#define LED_COLOR_YELLOW   ((led_color_t){255, 255, 0})
#define LED_COLOR_WHITE    ((led_color_t){255, 255, 255})

/* ---- API ---- */

/**
 * @brief 初始化 WS2812 LED 驱动
 */
esp_err_t led_indicator_init(void);

/**
 * @brief 设置 LED 颜色 (非阻塞)
 */
void led_set_color(const led_color_t *color);

/**
 * @brief 根据状态机更新 LED 模式
 *
 * 由 Task_Decision 周期调用
 */
void led_update_for_state(fsm_state_t state);

#ifdef __cplusplus
}
#endif
