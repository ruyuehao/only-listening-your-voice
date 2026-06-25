# KWS Engine (唤醒词检测引擎)

## 概述

100ms 定期轮询的唤醒词检测引擎。
使用 TFLite Micro + ESP-NN 加速，在 ESP32-C3 上运行 INT8 MixedNet 模型。

## 架构

```
Task_Frontend (10ms) → feature_buffer (150帧)
                              │
                    Task_KWS (100ms 轮询)
                              │
              feature_buffer_get_recent(100帧)
                              │
              kws_engine_infer() [TFLite Micro + ESP-NN]
                              │
              confidence ≥ 0.85 → EVENT_KWS_TRIGGERED
                              │
                    Task_Decision (→ Task_SV)
```

## 模型规格

| 参数 | 值 |
|------|-----|
| 架构 | **MixedNet** (MixConv) |
| 训练框架 | micro-wake-word (Kevin Ahrendt) |
| 输入 | [1, 100, 40] INT8 spectrogram |
| 输出 | Softmax [2] (非唤醒词 / 唤醒词) |
| Flash 占用 | ≤ 20KB |
| Tensor Arena | 64KB BSS (常驻) |
| 推理延迟 | ≤ 200ms (GPIO10 打桩测量) |
| 触发阈值 | 置信度 ≥ 0.85 |
| 冷却时间 | 2s (防重复触发) |

## OpResolver (12 ops)

| 算子 | ESP-NN 加速 |
|------|------------|
| Conv2D | ✅ |
| DepthwiseConv2D | ✅ |
| FullyConnected | ✅ |
| Softmax | ✅ |
| AveragePool2D | ✅ |
| MaxPool2D | ✅ |
| Reshape / Pad / Add | 标准 |
| Relu / Quantize / Dequantize | 标准 |

## 文件

| 文件 | 说明 |
|------|------|
| `kws_engine.h` | TFLite Micro 封装 API |
| `kws_engine.c` | 模型加载 (model_loader) / 推理 / 反量化 |
| `task_kws.h` | KWS 任务接口 |
| `task_kws.c` | 100ms 轮询 + 2s 冷却 |

## 依赖

- `espressif/esp-tflite-micro` (≥ 1.2.0)
- `config` 组件 (model_loader)
- `frontend` 组件 (feature_buffer)
