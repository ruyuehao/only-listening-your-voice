/*
 * feature_buffer.h — 滑动窗口 Spectrogram 缓冲区
 *
 * 累积 micro_frontend 输出的逐帧 40 维 spectrogram，
 * 为 KWS (100 帧) 和 SV (40 帧) 提供不同长度的滑动窗口视图。
 *
 * 由于 KWS 和 SV 使用同一段音频，从同一缓冲区取不同帧数即可：
 *   KWS: 最近 100 帧 → [100 × 40] uint8 spectrogram
 *   SV:  最近 40 帧  → [40 × 40]  uint8 spectrogram
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 缓冲区维度 ---- */
#define FB_MAX_FRAMES       150     /* 最多保留 150 帧 (1.5s @ 10ms) */
#define FB_NUM_CHANNELS     40      /* 与 micro_frontend 一致 */

/* ---- API ---- */

/**
 * @brief 初始化特征缓冲区
 */
esp_err_t feature_buffer_init(void);

/**
 * @brief 追加一帧 spectrogram
 *
 * @param frame  uint8[40] Mel spectrogram (1 帧 @ 10ms)
 * @return ESP_OK 成功
 */
esp_err_t feature_buffer_push(const uint8_t *frame);

/**
 * @brief 获取最近 N 帧（用于 KWS/SV 推理）
 *
 * 数据按时间顺序排列：[0] = 最早帧, [N-1] = 最新帧
 *
 * @param[out] dst     目标缓冲区 [N × 40] uint8
 * @param      n_frames 需要的帧数
 * @return ESP_OK 成功, ESP_ERR_NOT_FINISHED 缓冲区帧数不足
 */
esp_err_t feature_buffer_get_recent(uint8_t *dst, int n_frames);

/**
 * @brief 返回当前累积的帧数
 */
int feature_buffer_count(void);

/**
 * @brief 检查是否有足够帧数用于 KWS 推理 (≥100)
 */
bool feature_buffer_ready_for_kws(void);

/**
 * @brief 检查是否有足够帧数用于 SV 推理 (≥40)
 */
bool feature_buffer_ready_for_sv(void);

#ifdef __cplusplus
}
#endif
