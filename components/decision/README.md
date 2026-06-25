# Decision FSM + LED Module (决策调度 + 状态指示)

## 概述

有限状态机驱动的系统调度器 + 板载 WS2812 RGB LED 实时状态指示 + BOOT 键声纹注册。

## 状态机

```
                    ┌─────────────────────────────┐
                    │         STATE_IDLE           │
                    │     绿灯 1Hz 闪烁            │
                    └──────────┬──────────────────┘
                               │ EVENT_KWS_TRIGGERED
                    ┌──────────▼──────────────────┐
                    │      STATE_VERIFYING         │
                    │     绿灯 2Hz 快闪            │
                    │     3s 超时 → IDLE          │
                    └──────────┬──────────────────┘
                               │ EVENT_SV_DONE
                    ┌──────────▼──────────────────┐
               ┌────┤  g_sv_similarity            │
               │    └─────────────────────────────┘
               │ ≥0.70          │ -2.0             │ <0.70 or -3.0
    ┌──────────▼──┐  ┌──────────▼──┐  ┌──────────▼──┐
    │  ACCEPTED   │  │  ACCEPTED   │  │  REJECTED   │
    │ 红灯 1s    │  │ (首次使用)  │  │ 红灯 1.5s  │
    └──────┬──────┘  └──────┬──────┘  └──────┬──────┘
           │                │                │
           └────────────────┴────────────────┘
                            │ 超时
                    ┌───────▼──────┐
                    │  STATE_IDLE  │
                    └──────────────┘

注册流程:
  IDLE → BOOT 3s → ENROLL_TRIGGER → RECORD_1..5 → SAVED → restart
```

## LED 颜色方案

| 状态 | 颜色 | 模式 |
|------|------|------|
| IDLE | 绿色 | 1Hz 闪烁 (500ms on/off) |
| VERIFYING | 绿色 | 2Hz 快闪 (250ms on/off) |
| ACCEPTED | 红色 | 常亮 1 秒 |
| REJECTED | 红色 | 5Hz 快速闪烁 (100ms on/off) |
| ENROLL_TRIGGER | 蓝色 | 2Hz 快闪 |
| ENROLL_RECORD | 蓝色 | 录制指示 |
| ENROLL_SAVED | 红色 | 常亮 2 秒 |

## 注册流程

1. **长按 BOOT 键 3 秒** (GPIO9, 50ms 消抖, 10s 超时保护)
2. 进入注册模式 (蓝灯)
3. 连续说 5 次唤醒词 (间隔 2s)
4. 每次调用 `sv_engine_extract()` 提取 Embedding
5. 计算 5 个 Embedding 的平均值
6. `sv_template_save()` → NVS (64 bytes)
7. 红灯 2s → `esp_restart()`

## 文件

| 文件 | 说明 |
|------|------|
| `fsm.h/c` | 有限状态机 (Mutex 保护) |
| `led_indicator.h/c` | WS2812 LED 驱动 (led_strip/RMT) |
| `enroll.h/c` | 声纹注册流程 |
| `task_decision.h/c` | 决策调度 + SV 结果判定 |

## 依赖

- `espressif/led_strip` (≥ 3.0.0)
- `sv` 组件 (sv_engine_extract / sv_template_save)
- `frontend` 组件 (feature_buffer)
- FreeRTOS (Task / Mutex / Event Groups)
