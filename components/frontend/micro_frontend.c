/*
 * micro_frontend.c — TFLM 兼容 Microfrontend 实现
 *
 * 使用 esp-dsp 的 FFT 加速，实现与 micro-wake-word audio_utils.py
 * 完全一致的 Mel spectrogram 预处理流水线。
 */

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_dsp.h"
#include "micro_frontend.h"
#include "mel_filterbank_const.h"

static const char *TAG = "MFE";

/* ---- 预计算常数 ---- */

/* Hann 窗口: w[n] = 0.5 * (1 - cos(2πn / (N-1))), N=480 */
static float s_hann_window[MF_WINDOW_SAMPLES];

/* 标记是否已完成全局初始化 */
static bool s_initialized = false;

/* ================================================================
 * 初始化：Hann 窗口 + ESP-DSP
 * ================================================================ */

static void compute_hann_window(void)
{
    int n = MF_WINDOW_SAMPLES;
    for (int i = 0; i < n; i++) {
        s_hann_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (float)(n - 1)));
    }
    ESP_LOGI(TAG, "Hann window computed: %d samples", n);
}

/* ================================================================
 * 全局初始化（惰性，首次调用时触发）
 * ================================================================ */

static void ensure_initialized(void)
{
    if (s_initialized) return;

    /* 初始化 esp-dsp */
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, 1024);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp-dsp FFT init failed: %d", ret);
        return;
    }

    compute_hann_window();
    s_initialized = true;
    ESP_LOGI(TAG, "Microfrontend initialized (Mel filterbank: %d bytes Flash)",
             MEL_FB_CHANNELS * MEL_FB_BINS);
}

/* ================================================================
 * PCAN: Per-Channel Automatic Gain + Noise Suppression
 *
 * 每通道独立估计噪声底噪，自动增益归一化。
 * 参考 TFLM microfrontend 的 PCAN 实现：
 *   noise_estimate = (1 - alpha) * noise_estimate + alpha * current_energy
 *   gain = 1 / max(noise_estimate, min_signal)
 *   output = gain * input
 *
 * α = 0.01 对应 ~100 帧适应时间
 * ================================================================ */

#define PCAN_ALPHA          0.01f       /* 噪声估计平滑系数 */
#define PCAN_STRENGTH       0.95f       /* PCAN 强度 (1.0 = full, 0.0 = none) */
#define PCAN_MIN_SIGNAL     1e-5f       /* 最小信号值 */

static void apply_pcan(micro_frontend_handle_t *handle,
                        float *channel_energy /* [MF_NUM_CHANNELS] */)
{
    for (int ch = 0; ch < MF_NUM_CHANNELS; ch++) {
        float energy = channel_energy[ch];

        if (!handle->pcan_init) {
            /* 首次：直接设置为当前能量 */
            handle->pcan_state[ch] = energy;
        } else {
            /* 平滑更新噪声估计 */
            handle->pcan_state[ch] =
                (1.0f - PCAN_ALPHA) * handle->pcan_state[ch] +
                PCAN_ALPHA * energy;
        }

        /* 增益 = 1 / noise_estimate，clip 最小值 */
        float noise = handle->pcan_state[ch];
        if (noise < PCAN_MIN_SIGNAL) noise = PCAN_MIN_SIGNAL;

        float gain = 1.0f / noise;
        float suppressed = energy * (1.0f - PCAN_STRENGTH) +
                           energy * gain * PCAN_STRENGTH;

        /* 回写 */
        channel_energy[ch] = suppressed;
    }

    handle->pcan_init = 1;
}

/* ================================================================
 * 公共 API
 * ================================================================ */

micro_frontend_handle_t *mf_init(void)
{
    ensure_initialized();
    if (!s_initialized) return NULL;

    micro_frontend_handle_t *handle = calloc(1, sizeof(micro_frontend_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }

    handle->pcan_init = 0;
    return handle;
}

esp_err_t mf_process_frame(micro_frontend_handle_t *handle,
                            const int16_t *pcm_frame,
                            uint8_t *features)
{
    if (handle == NULL || pcm_frame == NULL || features == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* ================================================================
     * Step 1: 加窗 + FFT
     * ================================================================ */

    /* 准备 FFT 输入：应用 Hann 窗口，补零到 FFT_SIZE */
    float fft_input[MF_FFT_SIZE * 2];  /* [real, imag] 交错 */
    memset(fft_input, 0, sizeof(fft_input));

    for (int i = 0; i < MF_WINDOW_SAMPLES; i++) {
        fft_input[i * 2] = (float)pcm_frame[i] * s_hann_window[i] / 32768.0f;
    }

    /* 调用 esp-dsp 实数 FFT */
    dsps_fft2r_fc32(fft_input, MF_FFT_SIZE);

    /* 转换为功率谱: |X[k]|² = real² + imag² (只取正频率 bins) */
    float power_spec[MF_FFT_SIZE / 2 + 1];
    for (int k = 0; k <= MF_FFT_SIZE / 2; k++) {
        float re = fft_input[k * 2];
        float im = fft_input[k * 2 + 1];
        power_spec[k] = re * re + im * im;
    }

    /* ================================================================
     * Step 2: Mel 滤波器组 → 每通道能量 (Flash 查表)
     * ================================================================ */

    float channel_energy[MF_NUM_CHANNELS];
    for (int ch = 0; ch < MF_NUM_CHANNELS; ch++) {
        float energy = 0.0f;
        for (int bin = 0; bin <= MF_FFT_SIZE / 2; bin++) {
            energy += power_spec[bin] * mel_filterbank_get(ch, bin);
        }
        /* 数值稳定性：避免 log(0) */
        if (energy < 1e-10f) energy = 1e-10f;
        channel_energy[ch] = logf(energy + 1.0f);
    }

    /* ================================================================
     * Step 3: PCAN 噪声抑制（可选）
     * ================================================================ */
#if MF_PCAN_ENABLE
    apply_pcan(handle, channel_energy);
#endif

    /* ================================================================
     * Step 4: 量化到 uint8 [0, 255]
     *
     * 对应 audio_utils.py 的 output_scale。
     * log 范围约 [-10, +5]，缩放后 clip 到 [0, 255]
     * ================================================================ */

    for (int ch = 0; ch < MF_NUM_CHANNELS; ch++) {
        float val = channel_energy[ch] * MF_OUT_SCALE;
        if (val < 0.0f) val = 0.0f;
        if (val > 255.0f) val = 255.0f;
        features[ch] = (uint8_t)val;
    }

    return ESP_OK;
}

void mf_deinit(micro_frontend_handle_t *handle)
{
    if (handle != NULL) {
        free(handle);
    }
}
