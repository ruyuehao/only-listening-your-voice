/*
 * task_decision.c — 决策调度任务实现
 *
 * 调度逻辑:
 *   1. 初始 STATE_IDLE
 *   2. 收到 EVENT_KWS_TRIGGERED → STATE_VERIFYING → 触发 Task_SV
 *   3. KWS 置信度 > 0 → STATE_LISTENING
 *   4. 收到 EVENT_SV_DONE + result → STATE_ACCEPTED or STATE_REJECTED
 *   5. 1s 后回 STATE_IDLE
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "sys_config.h"
#include "fsm.h"
#include "led_indicator.h"

static const char *TAG = "TASK_DECISION";

extern EventGroupHandle_t g_event_group;
extern float g_sv_similarity;     /* SV 任务写入的相似度 */

static TaskHandle_t s_task_handle = NULL;
static int64_t      s_state_enter_us = 0;  /* 进入当前状态的时间 */

/* ================================================================ */

static void task_decision_main(void *pv_params)
{
    ESP_LOGI(TAG, "Decision task started (stack free: %d)",
             uxTaskGetStackHighWaterMark(NULL));

    fsm_init();
    s_state_enter_us = esp_timer_get_time();

    /* 等待的事件位 */
    const EventBits_t wait_bits = EVENT_KWS_TRIGGERED | EVENT_SV_DONE;

    while (1) {
        fsm_state_t state = fsm_get_state();

        /* ---- 周期刷新 LED ---- */
        led_update_for_state(state);

        /* ---- 超时回 IDLE ---- */
        int64_t now_us = esp_timer_get_time();
        int64_t elapsed_ms = (now_us - s_state_enter_us) / 1000;

        if (state == STATE_ACCEPTED && elapsed_ms >= 1000) {
            ESP_LOGI(TAG, "ACCEPTED → IDLE (1s timeout)");
            fsm_transition(STATE_IDLE);
            s_state_enter_us = now_us;
            continue;
        }

        if (state == STATE_REJECTED && elapsed_ms >= 1500) {
            ESP_LOGI(TAG, "REJECTED → IDLE (1.5s timeout)");
            fsm_transition(STATE_IDLE);
            s_state_enter_us = now_us;
            continue;
        }

        /* ---- 事件处理 ---- */
        TickType_t timeout = pdMS_TO_TICKS(50);  /* 50ms 轮询（LED 刷新） */

        /* IDLE 状态: 等待 KWS 触发 */
        if (state == STATE_IDLE) {
            EventBits_t bits = xEventGroupWaitBits(
                g_event_group, EVENT_KWS_TRIGGERED,
                pdTRUE,    /* 消费 */
                pdTRUE,    /* 任意 */
                timeout
            );

            if (bits & EVENT_KWS_TRIGGERED) {
                ESP_LOGI(TAG, "KWS triggered!");
                fsm_transition(STATE_VERIFYING);
                s_state_enter_us = esp_timer_get_time();
                /* TODO Phase 6: 触发 Task_SV 推理 */
            }
            continue;
        }

        /* VERIFYING 状态: 等待 SV 结果 (3s 超时回 IDLE) */
        if (state == STATE_VERIFYING) {
            if (elapsed_ms >= 3000) {
                ESP_LOGW(TAG, "VERIFYING timeout (3s) — returning to IDLE");
                fsm_transition(STATE_IDLE);
                s_state_enter_us = esp_timer_get_time();
                continue;
            }

            EventBits_t bits = xEventGroupWaitBits(
                g_event_group, EVENT_SV_DONE,
                pdTRUE,
                pdFALSE,
                pdMS_TO_TICKS(200)
            );

            if (bits & EVENT_SV_DONE) {
                float sim = g_sv_similarity;

                if (sim < -2.5f) {
                    /* -3.0: 模型加载失败 → 拒绝 */
                    ESP_LOGW(TAG, "SV model load failed (sim=%.2f) — REJECTED", sim);
                    fsm_transition(STATE_REJECTED);
                } else if (sim == -2.0f) {
                    /* 无模板: 首次使用，放行 */
                    ESP_LOGI(TAG, "No template enrolled — ACCEPTED (first-use policy)");
                    fsm_transition(STATE_ACCEPTED);
                } else if (sim >= SV_THRESHOLD) {
                    ESP_LOGI(TAG, "SV match (sim=%.4f >= %.2f) — ACCEPTED",
                             sim, SV_THRESHOLD);
                    fsm_transition(STATE_ACCEPTED);
                } else {
                    ESP_LOGI(TAG, "SV mismatch (sim=%.4f < %.2f) — REJECTED",
                             sim, SV_THRESHOLD);
                    fsm_transition(STATE_REJECTED);
                }
                s_state_enter_us = esp_timer_get_time();
            }
            continue;
        }

        /* 其他状态: 继续轮询 */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ================================================================ */

esp_err_t task_decision_create(void)
{
    BaseType_t created = xTaskCreate(
        task_decision_main,
        "Task_Decision",
        STACK_DECISION,
        NULL,
        PRIO_DECISION,
        &s_task_handle
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create decision task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Decision task created (prio=%d, stack=%d)",
             PRIO_DECISION, STACK_DECISION);
    return ESP_OK;
}
