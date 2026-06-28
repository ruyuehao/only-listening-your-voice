/*
 * micro_frontend.h — TFLM 兼容 Microfrontend 声学前端
 *
 * 将 PCM 音频流转换为 Mel-scale 13维 spectrogram（uint8），
 * 参数与 micro-wake-word audio_utils.py 严格一致。
 *
 * 流水线:
 *   PCM → Frame(30ms/10ms) → Hann Window → 512-FFT → Mel Filterbank(13ch)
 *       → Log → PCAN → uint8
 *
 * 参考:
 *   microwakeword/audio/audio_utils.py (generate_features_for_clip)
 *   tensorflow/lite/experimental/microfrontend (TFLM reference)
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 输出维度 ---- */
#define MF_FRAME_LEN_MS     30      /* 帧长 30ms */
#define MF_FRAME_STEP_MS    10      /* 帧步 10ms */
#define MF_NUM_CHANNELS     13      /* Mel 滤波器数量 (13维 MFCC, 与 PRD/模型一致) */
#define MF_SAMPLE_RATE      16000   /* 与 AUDIO_SAMPLE_RATE 一致 */
#define MF_FFT_SIZE         512     /* FFT 点数（30ms window → 480 采样，补零到 512） */
#define MF_WINDOW_SAMPLES   480     /* 30ms × 16kHz */
#define MF_STEP_SAMPLES     160     /* 10ms × 16kHz */
#define MF_LOWER_BAND_HZ    125     /* 下截止频率 */
#define MF_UPPER_BAND_HZ    7500    /* 上截止频率 */
#define MF_PCAN_ENABLE      1       /* PCAN 噪声抑制 */
#define MF_OUT_SCALE        64      /* output = log_energy * scale → uint8 [0,255] */

/* ---- 状态容器 ---- */

/**
 * @brief Microfrontend 状态（保持跨帧 PCAN 内部状态）
 */
typedef struct {
    float pcan_state[MF_NUM_CHANNELS];  /* PCAN 每通道噪声估计 */
    int   pcan_init;                    /* 是否已初始化 */
} micro_frontend_handle_t;

/* ---- API ---- */

/**
 * @brief 初始化 microfrontend 状态
 *
 * 分配并初始化 PCAN 噪声估计器
 *
 * @return 句柄，失败返回 NULL
 */
micro_frontend_handle_t *mf_init(void);

/**
 * @brief 处理一帧 PCM 数据，输出 13 维 uint8 spectrogram
 *
 * 每帧处理 MF_WINDOW_SAMPLES (480) 个采样，
 * 帧间步长为 MF_STEP_SAMPLES (160)。
 * 调用方负责维护 PCM 数据的滑动窗口。
 *
 * @param handle    microfrontend 句柄
 * @param pcm_frame 480 个 int16 采样（30ms @ 16kHz）
 * @param features  输出 uint8[13] Mel spectrogram
 * @return ESP_OK 成功
 */
esp_err_t mf_process_frame(micro_frontend_handle_t *handle,
                           const int16_t *pcm_frame,
                           uint8_t *features);

/**
 * @brief 释放 microfrontend 资源
 */
void mf_deinit(micro_frontend_handle_t *handle);

#ifdef __cplusplus
}
#endif
