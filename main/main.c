/*
 * main.c — ESP32-C3 声纹唤醒系统入口
 *
 * 架构: KWS (常驻) + SV (动态加载) 级联，事件驱动 FreeRTOS 任务
 *
 * 任务列表:
 *   Task_AudioCapture  (P5, 4KB)  — I2S 采集 + 环形缓冲区
 *   Task_KWS           (P4, 8KB)  — 唤醒词检测 (TFLite)
 *   Task_SV            (P3, 6KB)  — 声纹验证 (动态加载)
 *   Task_Decision      (P2, 2KB)  — 状态机调度 + LED + 上报
 *   Task_Enroll        (P1, 4KB)  — BOOT 键长按注册
 *
 * 启动流程:
 *   1. NVS / GPIO / LED / I2S / KWS / SV / UART 依序初始化
 *   2. 创建 FreeRTOS 任务
 *   3. 进入 STATE_IDLE (绿灯 1Hz)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "pin_defs.h"
#include "sys_config.h"

/* 子系统 */
#include "audio_capture.h"
#include "micro_frontend.h"
#include "feature_buffer.h"
#include "frontend_pipeline.h"
#include "kws_engine.h"
#include "task_kws.h"
#include "sv_engine.h"
#include "task_sv.h"
#include "fsm.h"
#include "led_indicator.h"
#include "enroll.h"
#include "task_decision.h"
#include "event_reporter.h"

static const char *TAG = "MAIN";

/* ---- 全局事件组（各 ISR/Task 间通信） ---- */
EventGroupHandle_t g_event_group = NULL;

/* ---- 前置声明 ---- */
static void prv_boot_strapping_check(void);
static esp_err_t prv_subsystems_init(void);

/* ================================================================
 * app_main
 * ================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " ESP32-C3 Voice Wake-up System v1.3");
    ESP_LOGI(TAG, " Build: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, " Chip:  %s (rev %d), %d cores @ %d MHz",
             esp_chip_info_get_model_name(),
             esp_chip_info_get_revision(),
             esp_chip_info_get_cores(),
             esp_chip_info_get_cpu_freq());
    ESP_LOGI(TAG, " Flash: %d MB", esp_chip_info_get_flash_size() / (1024 * 1024));
    ESP_LOGI(TAG, "========================================");

    /* --- 1. NVS 初始化 --- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupted, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "[OK] NVS initialized");

    /* --- 2. BOOT Strapping 自检 --- */
    prv_boot_strapping_check();

    /* --- 3. 全局事件组 --- */
    g_event_group = xEventGroupCreate();
    configASSERT(g_event_group != NULL);
    ESP_LOGI(TAG, "[OK] Event group created");

    /* --- 4. 子系统初始化 --- */
    ret = prv_subsystems_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Subsystem init failed — halting");
        return;
    }

    /* --- 5. 创建 FreeRTOS 任务 --- */
    ESP_LOGI(TAG, "Creating FreeRTOS tasks...");

    ESP_ERROR_CHECK(task_kws_create());
    ESP_ERROR_CHECK(task_sv_create());
    ESP_ERROR_CHECK(task_decision_create());
    ESP_ERROR_CHECK(enroll_task_create());

    /* AudioCapture 在 audio_capture_init() 中已创建 */

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " System ready — STATE_IDLE");
    ESP_LOGI(TAG, " Say the wake word or");
    ESP_LOGI(TAG, " long-press BOOT key (3s) to enroll");
    ESP_LOGI(TAG, "========================================");

    /* FreeRTOS 调度器由 ESP-IDF 自动启动 */
}

/* ================================================================
 * BOOT Strapping 自检
 * ================================================================ */
static void prv_boot_strapping_check(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(BOOT_BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
        ESP_LOGW(TAG, "==========================================");
        ESP_LOGW(TAG, " BOOT key pressed during boot!");
        ESP_LOGW(TAG, " Release the button to continue.");
        ESP_LOGW(TAG, " Restarting in 3 seconds...");
        ESP_LOGW(TAG, "==========================================");

        for (int i = 3; i > 0; i--) {
            ESP_LOGW(TAG, " %d...", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        esp_restart();
    }

    ESP_LOGI(TAG, "[OK] BOOT strapping check passed (GPIO%d = HIGH)", BOOT_BUTTON_PIN);
}

/* ================================================================
 * 子系统初始化
 * ================================================================ */
static esp_err_t prv_subsystems_init(void)
{
    esp_err_t ret;

    /* ---- LED ---- */
    ret = led_indicator_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed");
        return ret;
    }
    ESP_LOGI(TAG, "[OK] LED initialized");

    /* ---- I2S Audio Capture ---- */
    ret = audio_capture_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio capture init failed");
        return ret;
    }
    ESP_LOGI(TAG, "[OK] Audio capture started");

    /* ---- Feature Buffer ---- */
    ret = feature_buffer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Feature buffer init failed");
        return ret;
    }
    ESP_LOGI(TAG, "[OK] Feature buffer initialized");

    /* ---- Frontend Pipeline (10ms timer → MFCC → feature_buffer) ---- */
    ret = frontend_pipeline_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Frontend pipeline init failed");
        return ret;
    }
    ESP_LOGI(TAG, "[OK] Frontend pipeline running");

    /* ---- KWS Engine ---- */
    ret = kws_engine_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "KWS engine init failed (running in NO-OP mode)");
        /* 非致命 — 可在无模型时继续运行 */
    } else {
        ESP_LOGI(TAG, "[OK] KWS engine initialized");
    }

    /* ---- UART Communication ---- */
    ret = event_reporter_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UART init failed (continuing without reporting)");
    } else {
        ESP_LOGI(TAG, "[OK] UART communication initialized");
    }

    return ESP_OK;
}
