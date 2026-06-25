/*
 * led_indicator.c — WS2812 RGB LED 驱动
 *
 * 基于 espressif/led_strip 组件驱动 GPIO8 板载 WS2812
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "led_strip.h"

#include "pin_defs.h"
#include "led_indicator.h"

static const char *TAG = "LED";

/* ---- 全局句柄 ---- */
static led_strip_handle_t s_led_strip = NULL;

/* ---- LED 闪烁状态 ---- */
static led_color_t s_current_color = LED_COLOR_OFF;
static bool        s_blink_phase  = false;  /* true=亮, false=灭 */
static int64_t     s_last_toggle_us = 0;

/* ================================================================ */
esp_err_t led_indicator_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num   = LED_WS2812_PIN,
        .max_leds         = 1,               /* 板载仅 1 颗 WS2812 */
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
        .flags            = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,  /* 10MHz */
        .flags             = {
            .with_dma = false,
        },
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 关灯 */
    led_strip_set_pixel(s_led_strip, 0, 0, 0, 0);
    led_strip_refresh(s_led_strip);

    ESP_LOGI(TAG, "WS2812 LED initialized on GPIO%d", LED_WS2812_PIN);
    return ESP_OK;
}

void led_set_color(const led_color_t *color)
{
    if (s_led_strip == NULL) return;

    s_current_color = *color;
    s_blink_phase   = true;

    led_strip_set_pixel(s_led_strip, 0,
                        color->g, color->r, color->b);  /* GRB 格式 */
    led_strip_refresh(s_led_strip);
}

void led_update_for_state(fsm_state_t state)
{
    if (s_led_strip == NULL) return;

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - s_last_toggle_us;

    switch (state) {

    case STATE_IDLE:
        /* 绿色 1Hz 闪烁: 500ms on / 500ms off */
        if (elapsed_us >= 500000) {
            s_blink_phase = !s_blink_phase;
            s_last_toggle_us = now_us;

            if (s_blink_phase) {
                led_strip_set_pixel(s_led_strip, 0, 255, 0, 0);  /* GRB: G=255 */
            } else {
                led_strip_set_pixel(s_led_strip, 0, 0, 0, 0);
            }
            led_strip_refresh(s_led_strip);
        }
        break;

    case STATE_LISTENING:
        /* 绿色常亮 */
        if (!s_blink_phase || elapsed_us > 1000000) {
            s_blink_phase = true;
            led_strip_set_pixel(s_led_strip, 0, 255, 0, 0);
            led_strip_refresh(s_led_strip);
        }
        break;

    case STATE_VERIFYING:
        /* 绿色 2Hz 快闪: 250ms on / 250ms off */
        if (elapsed_us >= 250000) {
            s_blink_phase = !s_blink_phase;
            s_last_toggle_us = now_us;

            if (s_blink_phase) {
                led_strip_set_pixel(s_led_strip, 0, 255, 0, 0);
            } else {
                led_strip_set_pixel(s_led_strip, 0, 0, 0, 0);
            }
            led_strip_refresh(s_led_strip);
        }
        break;

    case STATE_ACCEPTED:
        /* 红色常亮（由 Decision 任务在 1s 后切回 IDLE） */
        led_strip_set_pixel(s_led_strip, 0, 0, 255, 0);  /* GRB: R=255 */
        led_strip_refresh(s_led_strip);
        break;

    case STATE_REJECTED:
        /* 红色 5Hz 闪烁: 100ms on / 100ms off */
        if (elapsed_us >= 100000) {
            s_blink_phase = !s_blink_phase;
            s_last_toggle_us = now_us;

            if (s_blink_phase) {
                led_strip_set_pixel(s_led_strip, 0, 0, 255, 0);
            } else {
                led_strip_set_pixel(s_led_strip, 0, 0, 0, 0);
            }
            led_strip_refresh(s_led_strip);
        }
        break;

    case STATE_ENROLL_TRIGGER:
    case STATE_ENROLL_RECORD:
        /* 蓝色 2Hz 快闪 */
        if (elapsed_us >= 250000) {
            s_blink_phase = !s_blink_phase;
            s_last_toggle_us = now_us;

            if (s_blink_phase) {
                led_strip_set_pixel(s_led_strip, 0, 0, 0, 255);  /* GRB: B=255 */
            } else {
                led_strip_set_pixel(s_led_strip, 0, 0, 0, 0);
            }
            led_strip_refresh(s_led_strip);
        }
        break;

    case STATE_ENROLL_SAVED:
        /* 红色常亮（由 Enroll 任务在 2s 后重启） */
        led_strip_set_pixel(s_led_strip, 0, 0, 255, 0);
        led_strip_refresh(s_led_strip);
        break;

    default:
        break;
    }
}
