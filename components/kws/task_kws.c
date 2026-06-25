/*
 * task_kws.c — KWS 事件驱动任务实现
 *
 * 工作流程:
 *   1. 等待 EVENT_AUDIO_READY (每 100ms)
 *   2. 检查 feature_buffer ≥ 100 帧
 *   3. 获取最近 100 帧 → kws_engine_infer()
 *   4. 置信度 ≥ KWS_THRESHOLD → xEventGroupSetBits(EVENT_KWS_TRIGGERED)
 *   5. 串口日志: 置信度、推断耗时
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "pin_defs.h"
#include "sys_config.h"
#include "kws_engine.h"
#include "feature_buffer.h"

static const char *TAG = "TASK_KWS";

/* 全局事件组引用 */
extern EventGroupHandle_t g_event_group;

/* ---- 任务句柄 ---- */
static TaskHandle_t s_task_handle = NULL;

/* ================================================================
 * GPIO 性能打桩
 * ================================================================ */

static void profile_pin_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(PROFILE_KWS_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PROFILE_KWS_PIN, 0);
}

static inline void profile_high(void) { gpio_set_level(PROFILE_KWS_PIN, 1); }
static inline void profile_low(void)  { gpio_set_level(PROFILE_KWS_PIN, 0); }

/* ================================================================
 * KWS 任务主循环
 * ================================================================ */

static void task_kws_main(void *pv_params)
{
    ESP_LOGI(TAG, "KWS task started (stack free: %d)",
             uxTaskGetStackHighWaterMark(NULL));

    profile_pin_init();

    /* 等待的事件位: AUDIO_READY | TIMER_100MS */
    const EventBits_t wait_bits = EVENT_AUDIO_READY | EVENT_TIMER_100MS;
    uint32_t          inference_count = 0;

    while (1) {
        /* 阻塞等待事件 */
        EventBits_t bits = xEventGroupWaitBits(
            g_event_group,
            wait_bits,
            pdTRUE,     /* 消费 (清零) 事件位 */
            pdTRUE,     /* 等待所有位中任意一个 */
            pdMS_TO_TICKS(200)
        );

        if (bits == 0) {
            /* 超时 — 无音频数据 */
            ESP_LOGW(TAG, "No audio event in 200ms — check I2S");
            continue;
        }

        /* 检查是否有足够帧数 */
        if (!feature_buffer_ready_for_kws()) {
            continue;
        }

        /* --- 获取 100 帧 spectrogram --- */
        uint8_t kws_input[KWS_INPUT_FRAMES * KWS_FEATURE_DIM];
        esp_err_t ret = feature_buffer_get_recent(kws_input, KWS_INPUT_FRAMES);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get features: %d", ret);
            continue;
        }

        /* --- 推理 (GPIO 打桩) --- */
        profile_high();
        int64_t t_start = esp_timer_get_time();

        float confidence = 0.0f;
        ret = kws_engine_infer(kws_input, &confidence);

        int64_t t_end = esp_timer_get_time();
        profile_low();

        inference_count++;

        /* --- 日志 --- */
        int32_t latency_us = (int32_t)(t_end - t_start);
        ESP_LOGI(TAG, "[%d] KWS conf=%.4f | latency=%dus | stack=%d",
                 inference_count, confidence, latency_us,
                 uxTaskGetStackHighWaterMark(NULL));

        /* --- 阈值判断 --- */
        if (confidence >= KWS_THRESHOLD) {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED (conf=%.4f >= %.2f) ***",
                     confidence, KWS_THRESHOLD);
            xEventGroupSetBits(g_event_group, EVENT_KWS_TRIGGERED);
        }
    }
}

/* ================================================================
 * 公共 API
 * ================================================================ */

esp_err_t task_kws_create(void)
{
    BaseType_t created = xTaskCreate(
        task_kws_main,
        "Task_KWS",
        STACK_KWS,
        NULL,
        PRIO_KWS,
        &s_task_handle
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create KWS task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "KWS task created (prio=%d, stack=%d)", PRIO_KWS, STACK_KWS);
    return ESP_OK;
}
