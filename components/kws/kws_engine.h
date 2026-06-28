/*
 * kws_engine.h — 唤醒词检测引擎 (TFLite Micro)
 *
 * 模型架构: DS-CNN (Depthwise Separable CNN)
 *   Conv2D(1→8, kernel=10×4, stride=2) → ReLU
 *   → DepthwiseConv2D(8, kernel=3×3) → ReLU
 *   → AvgPool2D(2×2) → Flatten → FC(176→32) → ReLU → FC(32→2)
 *
 *   输入: [1, 100, 13] INT8 spectrogram (100 帧 × 13 维 MFCC)
 *   输出: Softmax 2 类 (非唤醒词 / 唤醒词)
 *   大小: ≤ 12KB INT8 Flash
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 模型维度 ---- */
#define KWS_INPUT_FRAMES  100       /* 输入帧数 (1.0s @ 10ms) */
#define KWS_FEATURE_DIM   13        /* 每帧特征维度 (13维 MFCC) */
#define KWS_NUM_CLASSES   2         /* 输出类别数 */

/* ---- API ---- */

/**
 * @brief 加载 KWS 模型并初始化 TFLite Micro 解释器
 *
 * 从 models 分区读取 model_kws.tflite
 *
 * @return ESP_OK / ESP_FAIL
 */
esp_err_t kws_engine_init(void);

/**
 * @brief 运行一次推理
 *
 * @param input    uint8[KWS_INPUT_FRAMES × KWS_FEATURE_DIM] 输入 spectrogram
 * @param[out] confidence  唤醒词置信度 [0.0, 1.0]
 * @return ESP_OK 成功
 */
esp_err_t kws_engine_infer(const uint8_t *input, float *confidence);

/**
 * @brief 释放 KWS 引擎资源
 */
void kws_engine_deinit(void);

#ifdef __cplusplus
}
#endif
