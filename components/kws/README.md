# KWS Engine (唤醒词检测引擎)

## 概述

事件驱动的唤醒词检测引擎。使用 TFLite Micro + ESP-NN 加速在 ESP32-C3 上运行
INT8 量化模型，检测自定义唤醒词。

## 架构

```
Task_AudioCapture → EVENT_AUDIO_READY (每 100ms)
                         ↓
                    Task_KWS (事件驱动)
                         ↓
              feature_buffer_get_recent(100帧)
                         ↓
              kws_engine_infer() [TFLite Micro]
                         ↓
              confidence ≥ 0.85 → EVENT_KWS_TRIGGERED
                         ↓
                    Task_Decision (FSM)
```

## 模型规格

| 参数 | 值 |
|------|-----|
| 输入 | [1, 100, 40] INT8 spectrogram |
| 输出 | Softmax [2] (非唤醒词/唤醒词) |
| Flash 占用 | ≤ 20KB |
| Tensor Arena | 64KB (常驻) |
| 推理延迟 | ≤ 200ms (GPIO 打桩测量) |
| 阈值 | 0.85 |

## 算子

| 算子 | ESP-NN 加速 |
|------|------------|
| Conv2D | ✅ |
| DepthwiseConv2D | ✅ |
| FullyConnected | ✅ |
| Softmax | ✅ |
| AveragePool2D | ✅ |
| Reshape / Pad / Add | 标准 |

## 文件

| 文件 | 说明 |
|------|------|
| `kws_engine.h` | TFLite Micro 封装 API |
| `kws_engine.c` | 模型加载 / 推理 / 量化转换 |
| `task_kws.h` | KWS 任务接口 |
| `task_kws.c` | 事件驱动 FreeRTOS 任务 |

## 依赖

- `espressif/esp-tflite-micro` (>= 1.2.0)
- frontend 组件 (feature_buffer)
- GPIO 打桩引脚: GPIO10 (PROFILE_KWS_PIN)
