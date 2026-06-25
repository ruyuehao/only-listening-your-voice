/*
 * sv_engine.h — 声纹验证引擎 (Speaker Verification)
 *
 * 动态加载 INT8 TFLite 模型，提取 16 维声纹 Embedding，
 * 与 NVS 注册模板做余弦相似度比对。
 *
 * 模型规格:
 *   输入: [1, 40, 40] INT8 spectrogram (40 帧 × 40 维)
 *   输出: 16 维 Embedding (INT8 → float)
 *   大小: ≤ 15KB Flash
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
#define SV_FEATURE_DIM    40       /* 每帧特征维度 */
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
