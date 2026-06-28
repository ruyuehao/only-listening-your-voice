/*
 * sv_engine.h — 声纹验证引擎 (1D CNN Speaker Embedding)
 *
 * 模型架构: 1D CNN (Conv1d×3 + GAP + FC + L2-Norm)
 *   Conv1d(13→32, k5, s2) → BN → ReLU
 *   → Conv1d(32→32, k3, s2) → BN → ReLU
 *   → Conv1d(32→24, k3, s1) → BN → ReLU
 *   → GlobalAvgPool → FC(24→16) → L2-Norm
 *
 *   输入: [1, 40, 13] INT8 spectrogram (40 帧 × 13 维 MFCC, 共用 KWS frontend)
 *   输出: 16 维 L2-Normalized Embedding
 *   大小: ≤ 15KB Flash INT8 (实际 14.3KB)
 *
 * 训练: GE2E Loss (Generalized End-to-End), 30人 × ≥40次唤醒词录音
 *
 * 内存策略: 加载 → 推理 → 立即卸载（峰值仅 tensor_arena 48KB）
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 模型维度 ---- */
#define SV_INPUT_FRAMES   40       /* 输入帧数 (0.4s @ 10ms) */
#define SV_FEATURE_DIM    13       /* 每帧特征维度 (13维 MFCC, 与 KWS 共享 frontend) */
#define SV_EMBEDDING_DIM  16       /* 输出 Embedding 维度 */

/* ---- API ---- */

/**
 * @brief 加载 SV 模型并推理，返回与模板的相似度
 *
 * 加载模型 → 提取 Embedding → 读取 NVS 模板 → 计算余弦相似度 → 卸载模型
 *
 * @param input     uint8[SV_INPUT_FRAMES × SV_FEATURE_DIM] spectrogram
 * @param[out] similarity  余弦相似度 [-1.0, 1.0]，-2.0 表示无模板
 * @param[out] embedding   16 维输出 Embedding（用于注册）
 * @return ESP_OK
 */
esp_err_t sv_engine_verify(const uint8_t *input,
                           float *similarity,
                           float *embedding);

/**
 * @brief 仅提取 Embedding（用于注册录制）
 *
 * 不进行 NVS 比对
 *
 * @param input         输入 spectrogram
 * @param[out] embedding 16 维 float embedding
 * @return ESP_OK
 */
esp_err_t sv_engine_extract(const uint8_t *input, float *embedding);

/**
 * @brief 保存声纹模板到 NVS
 *
 * @param embedding  16 维 float embedding
 * @return ESP_OK
 */
esp_err_t sv_template_save(const float *embedding);

/**
 * @brief 从 NVS 读取声纹模板
 *
 * @param[out] embedding 16 维 float embedding
 * @return ESP_OK 成功, ESP_ERR_NOT_FOUND 无模板
 */
esp_err_t sv_template_load(float *embedding);

/**
 * @brief 检查 NVS 中是否存在模板
 */
bool sv_template_exists(void);

#ifdef __cplusplus
}
#endif
