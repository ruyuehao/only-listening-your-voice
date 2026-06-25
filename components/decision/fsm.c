/*
 * fsm.c — 决策状态机实现 (线程安全)
 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "fsm.h"

static const char *TAG = "FSM";

static fsm_state_t       s_current_state = STATE_IDLE;
static SemaphoreHandle_t s_fsm_mutex     = NULL;

/* ---- 状态名称表 ---- */
static const char *s_state_names[] = {
    [STATE_IDLE]            = "IDLE",
    [STATE_LISTENING]       = "LISTENING",
    [STATE_VERIFYING]       = "VERIFYING",
    [STATE_ACCEPTED]        = "ACCEPTED",
    [STATE_REJECTED]        = "REJECTED",
    [STATE_ENROLL_TRIGGER]  = "ENROLL_TRIGGER",
    [STATE_ENROLL_RECORD]   = "ENROLL_RECORD",
    [STATE_ENROLL_SAVED]    = "ENROLL_SAVED",
};

void fsm_init(void)
{
    s_fsm_mutex = xSemaphoreCreateMutex();
    configASSERT(s_fsm_mutex != NULL);
    s_current_state = STATE_IDLE;
    ESP_LOGI(TAG, "FSM initialized: %s", fsm_state_name(s_current_state));
}

fsm_state_t fsm_get_state(void)
{
    if (s_fsm_mutex == NULL) return STATE_IDLE;
    xSemaphoreTake(s_fsm_mutex, portMAX_DELAY);
    fsm_state_t state = s_current_state;
    xSemaphoreGive(s_fsm_mutex);
    return state;
}

void fsm_transition(fsm_state_t new_state)
{
    if (s_fsm_mutex == NULL) return;

    xSemaphoreTake(s_fsm_mutex, portMAX_DELAY);

    if (new_state == s_current_state) {
        xSemaphoreGive(s_fsm_mutex);
        return;
    }

    ESP_LOGI(TAG, "State transition: %s → %s",
             fsm_state_name(s_current_state),
             fsm_state_name(new_state));

    s_current_state = new_state;
    xSemaphoreGive(s_fsm_mutex);
}

const char *fsm_state_name(fsm_state_t state)
{
    if (state > STATE_ENROLL_SAVED || state < 0) {
        return "UNKNOWN";
    }
    return s_state_names[state];
}
