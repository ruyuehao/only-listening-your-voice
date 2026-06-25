/*
 * enroll.c — 声纹注册流程实现
 *
 * BOOT 键 (GPIO9) 检测:
 *   - 中断驱动 (FALLING edge)
 *   - 50ms 软件消抖
 *   - 长按 ≥ 3s → 注册模式
 *
 * 注册流程:
 *   Record 1..5 → SV 提取 Embedding → 平均 → NVS → 重启
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "pin_defs.h"
#include "sys_config.h"
#include "fsm.h"
#include "led_indicator.h"
#include "sv_engine.h"
#include "feature_buffer.h"

static const char *TAG = "ENROLL";

extern EventGroupHandle_t g_event_group;

static TaskHandle_t s_task_handle = NULL;

/* ================================================================
 * 按键检测（轮询 + 消抖）
 * ================================================================ */

static bool button_is_pressed(void)
{
    return gpio_get_level(BOOT_BUTTON_PIN) == 0;  /* 低电平 = 按下 */
}

static bool wait_button_stable(bool expected, int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (button_is_pressed() == expected) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }
    return false;
}

/* ================================================================
 * 注册录制
 * ================================================================ */

static esp_err_t enroll_record_sample(float *embedding)
{
    /* 等待足够的音频帧 */
    ESP_LOGI(TAG, "Waiting for audio...");
    uint32_t waited = 0;
    while (!feature_buffer_ready_for_sv() && waited < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited++;
    }

    if (!feature_buffer_ready_for_sv()) {
        ESP_LOGE(TAG, "Timeout waiting for audio data");
        return ESP_ERR_TIMEOUT;
    }

    /* 获取 40 帧 spectrogram */
    uint8_t sv_input[SV_INPUT_FRAMES * SV_FEATURE_DIM];
    esp_err_t ret = feature_buffer_get_recent(sv_input, SV_INPUT_FRAMES);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 提取 Embedding */
    ret = sv_engine_extract(sv_input, embedding);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SV extract failed: %d", ret);
        return ret;
    }

    /* 日志输出 Embedding 前几个值 */
    ESP_LOGI(TAG, "Embedding: [%.4f, %.4f, %.4f, %.4f...]",
             embedding[0], embedding[1], embedding[2], embedding[3]);
    return ESP_OK;
}

/* ================================================================
 * 注册主流程
 * ================================================================ */

static void enroll_flow(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Enroll Mode Started");
    ESP_LOGI(TAG, "========================================");

    fsm_transition(STATE_ENROLL_TRIGGER);
    vTaskDelay(pdMS_TO_TICKS(500));

    float embeddings[ENROLL_SAMPLE_COUNT][SV_EMBEDDING_DIM];
    memset(embeddings, 0, sizeof(embeddings));

    for (int i = 0; i < ENROLL_SAMPLE_COUNT; i++) {
        ESP_LOGI(TAG, "--- Record %d/%d ---", i + 1, ENROLL_SAMPLE_COUNT);
        fsm_transition(STATE_ENROLL_RECORD);

        /* 等用户说唤醒词 */
        vTaskDelay(pdMS_TO_TICKS(ENROLL_SAMPLE_INTERVAL_MS));

        esp_err_t ret = enroll_record_sample(embeddings[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Record %d failed: %d — aborting enrollment", i + 1, ret);
            fsm_transition(STATE_IDLE);
            return;
        }

        /* 串口日志 */
        printf("[ENROLL] Record %d/%d OK\n", i + 1, ENROLL_SAMPLE_COUNT);
    }

    /* ---- 计算平均 Embedding ---- */
    ESP_LOGI(TAG, "Calculating average embedding...");
    float avg_embedding[SV_EMBEDDING_DIM] = {0};
    for (int i = 0; i < ENROLL_SAMPLE_COUNT; i++) {
        for (int d = 0; d < SV_EMBEDDING_DIM; d++) {
            avg_embedding[d] += embeddings[i][d];
        }
    }
    for (int d = 0; d < SV_EMBEDDING_DIM; d++) {
        avg_embedding[d] /= (float)ENROLL_SAMPLE_COUNT;
    }

    ESP_LOGI(TAG, "Avg Embedding: [%.4f, %.4f, %.4f, %.4f...]",
             avg_embedding[0], avg_embedding[1],
             avg_embedding[2], avg_embedding[3]);

    /* ---- 保存到 NVS ---- */
    esp_err_t ret = sv_template_save(avg_embedding);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save template: %d", ret);
        fsm_transition(STATE_IDLE);
        return;
    }

    /* ---- 完成 ---- */
    fsm_transition(STATE_ENROLL_SAVED);
    ESP_LOGI(TAG, "Enrollment complete! Restarting in 2s...");

    /* 红灯长亮 2 秒 */
    for (int i = 0; i < 20; i++) {
        led_update_for_state(STATE_ENROLL_SAVED);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("[ENROLL] Template saved. Restarting...\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/* ================================================================
 * BOOT 键监控任务
 * ================================================================ */

static void task_enroll_main(void *pv_params)
{
    ESP_LOGI(TAG, "Enroll/Boot monitor started (GPIO%d)", BOOT_BUTTON_PIN);

    /* 配置 GPIO9 输入（外部上拉） */
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(BOOT_BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    while (1) {
        /* 等待按键按下 */
        if (!wait_button_stable(true, DEBOUNCE_MS)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* 长按检测: 持续按下 ≥ 3s */
        int64_t press_start = esp_timer_get_time();

        while (button_is_pressed()) {
            int64_t elapsed_ms = (esp_timer_get_time() - press_start) / 1000;

            if (elapsed_ms >= ENROLL_LONG_PRESS_MS) {
                ESP_LOGI(TAG, "BOOT key long-press detected (%lldms)", elapsed_ms);
                enroll_flow();
                /* enroll_flow 调用 esp_restart()，不会返回 */
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        /* 短按（< 3s）忽略 */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t enroll_task_create(void)
{
    BaseType_t created = xTaskCreate(
        task_enroll_main,
        "Task_Enroll",
        4096,  /* 额外栈空间 */
        NULL,
        1,     /* 低优先级 */
        &s_task_handle
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create enroll task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Enroll task created (GPIO%d, long-press=%dms)",
             BOOT_BUTTON_PIN, ENROLL_LONG_PRESS_MS);
    return ESP_OK;
}
