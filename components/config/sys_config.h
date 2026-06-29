/*
 * sys_config.h — 系统级参数配置
 * 所有可调阈值 / 缓冲区大小集中管理
 */

#pragma once

#include <stdint.h>

/* ================================================================
 * 音频参数（与 TFLM microfrontend 保持一致）
 * ================================================================ */
#define AUDIO_SAMPLE_RATE       16000       // Hz
#define AUDIO_BITS_PER_SAMPLE   16          // 16-bit PCM
#define AUDIO_I2S_BUFFER_SIZE   1024        // I2S DMA buffer (samples)

/* ================================================================
 * 环形缓冲区
 * ================================================================ */
#define RINGBUFFER_SIZE_BYTES   (32 * 1024) // 1s @ 16kHz/16bit = 32KB
#define RINGBUFFER_WAIT_TICKS   pdMS_TO_TICKS(100)

/* ================================================================
 * KWS 唤醒词引擎
 * ================================================================ */
#define KWS_WINDOW_MS           1000        // 推理窗口: 1 秒
#define KWS_INTERVAL_MS         100         // 滑动间隔: 100ms
#define KWS_THRESHOLD           0.85f       // 唤醒词置信度阈值
#define KWS_MODEL_FILENAME      "/models/model_kws.tflite"
#define KWS_TENSOR_ARENA_SIZE   (64 * 1024) // 64KB tensor arena

/* ================================================================
 * SV 声纹验证引擎
 * ================================================================ */
#define SV_THRESHOLD            0.70f       // 余弦相似度阈值
#define SV_EMBEDDING_DIM        16          // 声纹 Embedding 维度
#define SV_MODEL_FILENAME       "/models/model_sv.tflite"
#define SV_TENSOR_ARENA_SIZE    (48 * 1024) // 48KB tensor arena（动态分配）

/* ================================================================
 * 声纹注册
 * ================================================================ */
#define ENROLL_LONG_PRESS_MS    3000        // 长按 3s 进入注册模式
#define ENROLL_SAMPLE_COUNT     5           // 录制 5 次唤醒词
#define ENROLL_SAMPLE_INTERVAL_MS  2000     // 每次间隔 2s
#define DEBOUNCE_MS             50          // 按键消抖 50ms
#define NVS_NAMESPACE           "sv_enroll"
#define NVS_KEY_TEMPLATE        "sv_template"

/* ================================================================
 * 通信上报
 * ================================================================ */
#define COMM_UART_BAUDRATE      115200
#define COMM_RESPONSE_TIMEOUT_MS   500     // 回显超时
#define COMM_MAX_RETRIES        1           // 最大重试次数

/* ================================================================
 * FreeRTOS 任务栈大小（单位：字，ESP32-C3 上 4 字节/字）
 *
 * ESP32-C3 SRAM 总量 384KB，系统保留 ~100KB，可用 ~284KB。
 * 当前分配控制在 ~50KB（栈合计），为 tensor arena 和模型留足空间。
 * ================================================================ */
#define STACK_AUDIO_CAPTURE     2048    /*  8KB — I2S DMA 循环 + 环形缓冲区写入 */
#define STACK_FRONTEND          1536    /*  6KB — 10ms 流水线: PCM窗口+MFCC+push */
#define STACK_KWS               3072    /* 12KB — TFLite 推理 + 4KB 临时输入缓冲 */
#define STACK_SV                2560    /* 10KB — SV 模型加载+推理(堆分配为主) */
#define STACK_DECISION          1536    /*  6KB — FSM 状态机 + LED 刷新 */
#define STACK_COMM              1536    /*  6KB — UART JSON 上报（预留） */

/* ================================================================
 * 任务优先级（FreeRTOS 数值越大优先级越高，最大 configMAX_PRIORITIES-1）
 * ESP-IDF 默认 configMAX_PRIORITIES = 25
 *
 * 优先级设计原则:
 *   - 数据生产者 (Audio > Frontend) > 数据消费者 (KWS/SV) > 调度 (Decision/Enroll)
 *   - 同层消费者可平等
 * ================================================================ */
#define PRIO_AUDIO_CAPTURE      5
#define PRIO_FRONTEND           4
#define PRIO_KWS                3
#define PRIO_SV                 3
#define PRIO_DECISION           2
#define PRIO_ENROLL             1
#define PRIO_COMM               1

/* ================================================================
 * 事件组位定义
 * ================================================================ */
#define EVENT_AUDIO_READY       (1 << 0)    /* AudioCapture: 100ms PCM 累积 (预留) */
#define EVENT_KWS_TRIGGERED     (1 << 1)    /* KWS: 唤醒词检测到 */
#define EVENT_SV_STARTED        (1 << 2)    /* SV: 验证开始（通知 Decision 切换状态） */
#define EVENT_SV_DONE           (1 << 3)    /* SV: 声纹验证完成 */
/* (1 << 4) reserved */
