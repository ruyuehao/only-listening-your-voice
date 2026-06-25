# Acoustic Frontend Module (声学前端模块)

## 概述

实现与 **micro-wake-word audio_utils.py** 参数完全一致的 TFLM 兼容 Microfrontend：
40 维 Mel spectrogram + PCAN 噪声抑制，uint8 量化输出。

**Mel 滤波器组已优化**: 从运行时计算 41KB float BSS → 预计算 10KB uint8 Flash 常量 (`mel_filterbank_const.h`)。

## 流水线

```
PCM (16kHz/16bit)
  → Frame: 30ms (480 samples) / step 10ms (160 samples)
  → Hann Window
  → 512-point Real FFT (esp-dsp dsps_fft2r_fc32)
  → Mel Filterbank: 40 三角滤波器 (125–7500 Hz) — Flash 查表
  → Log Energy
  → PCAN (Per-Channel AGC + Noise Suppression, α=0.01)
  → uint8 [0, 255] Quantization
```

## 关键优化

| 优化 | 说明 | 收益 |
|------|------|------|
| **Filterbank Flash 预计算** | 运行时 float 计算 → 编译时 uint8 常量 | **释放 41KB SRAM** |
| **ESP-DSP FFT** | dsps_fft2r_fc32 RISC-V 汇编 | 每帧 ~0.5ms |
| **滑动窗口用 memmove** | 避免环形索引开销 | CPU 友好 |

## 参数

| 参数 | 值 | 来源 |
|------|-----|------|
| 采样率 | 16000 Hz | audio_utils.py |
| 帧长 | 30ms (480 samples) | window_size_ms=30 |
| 帧步 | 10ms (160 samples) | window_step_ms=10 |
| 滤波器数 | 40 channels | num_channels=40 |
| 频率范围 | 125–7500 Hz | lower/upper_band_limit |
| PCAN | 启用 (α=0.01) | enable_pcan=True |
| 输出类型 | uint8 [0,255] |
| FFT 实现 | esp-dsp (dsps_fft2r_fc32) |

## 滑动窗口缓冲区

`feature_buffer` 累积最近 150 帧 (1.5s) 的 spectrogram，供 KWS 和 SV 共享：

| 用途 | 帧数 | 时长 | 维度 |
|------|------|------|------|
| KWS 推理窗口 | 100 帧 | 1.0s | 100×40 |
| SV 推理窗口 | 40 帧 | 0.4s | 40×40 |

## 依赖

- `esp-dsp` (FFT 加速)
- FreeRTOS (Task + Mutex)
- `mel_filterbank_const.h` (自动生成, `tools/generate_filterbank.py`)

## 重新生成 Filterbank

如果修改了 Mel 参数 (通道数/频率范围), 重新运行:

```bash
python tools/generate_filterbank.py > components/frontend/mel_filterbank_const.h
```

## API

| 函数 | 说明 |
|------|------|
| `mf_init()` | 初始化 microfrontend 状态 (Hann窗+esp-dsp) |
| `mf_process_frame(handle, pcm, features)` | 处理一帧 → uint8[40] |
| `feature_buffer_init()` | 初始化滑动窗口缓冲区 |
| `feature_buffer_push(frame)` | 追加一帧 spectrogram |
| `feature_buffer_get_recent(dst, n)` | 获取最近 N 帧 |
| `feature_buffer_ready_for_kws()` | ≥ 100 帧就绪 |
| `feature_buffer_ready_for_sv()` | ≥ 40 帧就绪 |
| `frontend_pipeline_init()` | 启动 10ms 流水线 Task |
