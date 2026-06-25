/*
 * micro_frontend.c — TFLM 兼容 Microfrontend 实现
 *
 * 使用 esp-dsp 的 FFT 加速，实现与 micro-wake-word audio_utils.py
 * 完全一致的 Mel spectrogram 预处理流水线。
 */

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_dsp.h"
#include "micro_frontend.h"

static const char *TAG = "MFE";

/* ---- 预计算常数 ---- */

/* Hann 窗口: w[n] = 0.5 * (1 - cos(2πn / (N-1))), N=480 */
static float s_hann_window[MF_WINDOW_SAMPLES];

/* Mel 滤波器组: [40][257] float = 41KB BSS
 * TODO: 预计算为 const 头文件放入 Flash 可节省 41KB RAM
 */
static float s_mel_filterbank[MF_NUM_CHANNELS][MF_FFT_SIZE / 2 + 1];

/* 标记是否已完成全局初始化 */
static bool s_initialized = false;

/* ================================================================
 * Mel 频率转换
 * ================================================================ */

static float hz_to_mel(float hz)
{
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel)
{
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

/* ================================================================
 * FFT bin → 频率
 * ================================================================ */

static float bin_to_freq(int bin)
{
    return (float)bin * MF_SAMPLE_RATE / (float)MF_FFT_SIZE;
}

/* ================================================================
 * 初始化：Hann 窗口 + Mel 滤波器组
 * ================================================================ */

static void compute_hann_window(void)
{
    int n = MF_WINDOW_SAMPLES;
    for (int i = 0; i < n; i++) {
        s_hann_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (float)(n - 1)));
    }
    ESP_LOGI(TAG, "Hann window computed: %d samples", n);
}

static void compute_mel_filterbank(void)
{
    int num_bins = MF_FFT_SIZE / 2 + 1;  /* 257 个正频率 bin */

    float mel_low  = hz_to_mel(MF_LOWER_BAND_HZ);
    float mel_high = hz_to_mel(MF_UPPER_BAND_HZ);

    /* 在 Mel 域均匀采样 MF_NUM_CHANNELS + 2 个点 */
    int num_mel_pts = MF_NUM_CHANNELS + 2;
    float mel_step = (mel_high - mel_low) / (float)(MF_NUM_CHANNELS + 1);

    float *mel_pts = (float *)calloc(num_mel_pts, sizeof(float));
    float *hz_pts  = (float *)calloc(num_mel_pts, sizeof(float));
    int   *bin_pts = (int *)  calloc(num_mel_pts, sizeof(int));

    for (int i = 0; i < num_mel_pts; i++) {
        mel_pts[i] = mel_low + mel_step * i;
        hz_pts[i]  = mel_to_hz(mel_pts[i]);
        bin_pts[i] = (int)(hz_pts[i] * MF_FFT_SIZE / (float)MF_SAMPLE_RATE);
    }

    /* 清零滤波器组 */
    memset(s_mel_filterbank, 0, sizeof(s_mel_filterbank));

    /* 对每个 Mel 通道建立三角滤波器 */
    for (int ch = 0; ch < MF_NUM_CHANNELS; ch++) {
        int left  = bin_pts[ch];
        int peak  = bin_pts[ch + 1];
        int right = bin_pts[ch + 2];

        /* 左斜坡: left → peak */
        for (int bin = left; bin <= peak; bin++) {
            if (peak > left) {
                s_mel_filterbank[ch][bin] = (float)(bin - left) / (float)(peak - left);
            }
        }
        /* 右斜坡: peak → right */
        for (int bin = peak; bin <= right; bin++) {
            if (right > peak) {
                s_mel_filterbank[ch][bin] = (float)(right - bin) / (float)(right - peak);
            }
        }
    }

    free(mel_pts);
    free(hz_pts);
    free(bin_pts);

    ESP_LOGI(TAG, "Mel filterbank computed: %d channels, %d bins, %.0f-%.0f Hz",
             MF_NUM_CHANNELS, num_bins, MF_LOWER_BAND_HZ, MF_UPPER_BAND_HZ);
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
    compute_mel_filterbank();
    s_initialized = true;
    ESP_LOGI(TAG, "Microfrontend initialized OK");
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
     * Step 2: Mel 滤波器组 → 每通道能量
     * ================================================================ */

    float channel_energy[MF_NUM_CHANNELS];
    for (int ch = 0; ch < MF_NUM_CHANNELS; ch++) {
        float energy = 0.0f;
        for (int bin = 0; bin <= MF_FFT_SIZE / 2; bin++) {
            energy += power_spec[bin] * s_mel_filterbank[ch][bin];
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
