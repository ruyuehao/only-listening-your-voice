# Acoustic Frontend Module (声学前端模块)

## 概述

实现与 **micro-wake-word audio_utils.py** 参数完全一致的 TFLM 兼容 Microfrontend：
40 维 Mel spectrogram + PCAN 噪声抑制，uint8 量化输出。

## 流水线

```
PCM (16kHz/16bit)
  → Frame: 30ms (480 samples) / step 10ms (160 samples)
  → Hann Window
  → 512-point Real FFT (esp-dsp 加速)
  → Mel Filterbank: 40 三角滤波器 (125–7500 Hz)
  → Log10 Energy
  → PCAN (Per-Channel AGC + Noise Suppression)
  → uint8 [0, 255] Quantization
```

## 参数

| 参数 | 值 | 来源 |
|------|-----|------|
| 采样率 | 16000 Hz | `audio_utils.py` |
| 帧长 | 30ms (480 samps) | `window_size_ms=30` |
| 帧步 | 10ms (160 samps) | `window_step_ms=10` |
| 滤波器数 | 40 channels | `num_channels=40` |
| 频率范围 | 125–7500 Hz | `lower/upper_band_limit` |
| PCAN | 启用 (α=0.01) | `enable_pcan=True` |
| 输出类型 | uint8 | 量化到 [0,255] |
| FFT 实现 | esp-dsp (dsps_fft2r_fc32) | 硬件加速 |

## 滑动窗口缓冲区

`feature_buffer` 累积最近 150 帧 (1.5s) 的 spectrogram：

| 用途 | 帧数 | 时长 |
|------|------|------|
| KWS 推理窗口 | 100 帧 | 1.0s |
| SV 推理窗口 | 40 帧 | 0.4s |

## 依赖

- `esp-dsp` (FFT 加速)
- FreeRTOS (Mutex 保护 feature_buffer)

## API

| 函数 | 说明 |
|------|------|
| `mf_init()` | 初始化 microfrontend 状态 |
| `mf_process_frame(handle, pcm, features)` | 处理一帧 → uint8[40] |
| `feature_buffer_init()` | 初始化滑动窗口缓冲区 |
| `feature_buffer_push(frame)` | 追加一帧 spectrogram |
| `feature_buffer_get_recent(dst, n)` | 获取最近 N 帧 |
| `feature_buffer_ready_for_kws()` | ≥ 100 帧就绪 |
| `feature_buffer_ready_for_sv()` | ≥ 40 帧就绪 |
