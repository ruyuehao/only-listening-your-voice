/*
 * fsm.h — 决策有限状态机
 *
 * 状态流转:
 *   IDLE → LISTENING → VERIFYING → ACCEPTED / REJECTED
 *                      ↑ KWS ≥ 0.85    │              │
 *                      └────────────────┴──────────────┘ (1s 后回 IDLE)
 *
 * 注册流程:
 *   IDLE → ENROLL_TRIGGER → RECORD_1..5 → CALCULATE → SAVED → IDLE
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 状态枚举 ---- */
typedef enum {
    STATE_IDLE = 0,         /* 待机 (绿灯 1Hz 闪烁) */
    STATE_LISTENING,        /* 监听中 (绿灯常亮) */
    STATE_VERIFYING,        /* 验证中 (绿灯 2Hz 快闪) */
    STATE_ACCEPTED,         /* 通过 (红灯常亮 1s) */
    STATE_REJECTED,         /* 拒绝 (红灯闪烁 3 次) */
    STATE_ENROLL_TRIGGER,   /* 注册触发 (蓝灯 2Hz 快闪) */
    STATE_ENROLL_RECORD,    /* 录制模式 (蓝灯快闪，仅首次循环) */
    STATE_ENROLL_COUNTDOWN, /* 准备 (黄色 1Hz 闪烁) */
    STATE_ENROLL_SPEAK,     /* 请说唤醒词 (绿色常亮) */
    STATE_ENROLL_SAVED,     /* 注册完成 (红灯长亮 2s) */
} fsm_state_t;

/* ---- API ---- */

/**
 * @brief 初始化状态机
 */
void fsm_init(void);

/**
 * @brief 获取当前状态
 */
fsm_state_t fsm_get_state(void);

/**
 * @brief 状态转换（事件驱动）
 *
 * @param new_state  目标状态
 */
void fsm_transition(fsm_state_t new_state);

/**
 * @brief 获取状态名称字符串（用于日志）
 */
const char *fsm_state_name(fsm_state_t state);

#ifdef __cplusplus
}
#endif
