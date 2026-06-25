/*
 * task_sv.c — 声纹验证任务实现
 *
 * 等待 EVENT_KWS_TRIGGERED → 获取 40 帧特征 → sv_engine_verify()
 * → 设置 EVENT_SV_DONE
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "pin_defs.h"
#include "sys_config.h"
#include "sv_engine.h"
#include "feature_buffer.h"

static const char *TAG = "TASK_SV";

extern EventGroupHandle_t g_event_group;

/* 全局变量: SV 结果（SV 任务写入，Decision 任务读取） */
float g_sv_similarity = 0.0f;

static TaskHandle_t s_task_handle = NULL;

/* GPIO 打桩 */
static void profile_pin_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(PROFILE_SV_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PROFILE_SV_PIN, 0);
}

static void task_sv_main(void *pv_params)
{
    ESP_LOGI(TAG, "SV task started (stack free: %d)",
             uxTaskGetStackHighWaterMark(NULL));

    profile_pin_init();

    while (1) {
        /* 等待 KWS 触发 */
        EventBits_t bits = xEventGroupWaitBits(
            g_event_group,
            EVENT_KWS_TRIGGERED,
            pdFALSE,    /* 不消费 — Decision 也需要此事件 */
            pdFALSE,    /* 等待所有 */
            portMAX_DELAY
        );

        if (!(bits & EVENT_KWS_TRIGGERED)) {
            continue;
        }

        /* 检查特征缓冲区 */
        if (!feature_buffer_ready_for_sv()) {
            ESP_LOGW(TAG, "SV triggered but < 40 frames available");
            continue;
        }

        /* 获取 40 帧 */
        uint8_t sv_input[SV_INPUT_FRAMES * SV_FEATURE_DIM];
        esp_err_t ret = feature_buffer_get_recent(sv_input, SV_INPUT_FRAMES);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SV features");
            continue;
        }

        /* 推理 (GPIO 打桩) */
        gpio_set_level(PROFILE_SV_PIN, 1);
        int64_t t_start = esp_timer_get_time();

        float similarity = 0.0f;
        float embedding[SV_EMBEDDING_DIM];
        ret = sv_engine_verify(sv_input, &similarity, embedding);

        int64_t t_end = esp_timer_get_time();
        gpio_set_level(PROFILE_SV_PIN, 0);

        int32_t latency_us = (int32_t)(t_end - t_start);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SV verify failed: %d", ret);
            continue;
        }

        /* 日志 */
        ESP_LOGI(TAG, "SV: sim=%.4f (thresh=%.2f) | latency=%dus | stack=%d",
                 similarity, SV_THRESHOLD, latency_us,
                 uxTaskGetStackHighWaterMark(NULL));

        /* 写入全局结果供 Decision 任务读取 */
        g_sv_similarity = similarity;

        if (similarity >= SV_THRESHOLD) {
            ESP_LOGI(TAG, "*** VOICEPRINT MATCH ***");
        } else if (similarity >= 0.0f) {
            ESP_LOGI(TAG, "Voiceprint mismatch — REJECTED");
        } else if (similarity == -2.0f) {
            ESP_LOGW(TAG, "No template enrolled — ACCEPT by default");
            similarity = 1.0f;  /* 无模板时放行 */
        }

        /* 通知 Decision 任务 */
        xEventGroupSetBits(g_event_group, EVENT_SV_DONE);
    }
}

esp_err_t task_sv_create(void)
{
    BaseType_t created = xTaskCreate(
        task_sv_main,
        "Task_SV",
        STACK_SV,
        NULL,
        PRIO_SV,
        &s_task_handle
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SV task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "SV task created (prio=%d, stack=%d)", PRIO_SV, STACK_SV);
    return ESP_OK;
}
