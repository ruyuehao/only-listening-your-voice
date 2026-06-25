# SV Engine (声纹验证引擎)

## 概述

**本项目核心创新模块** — GitHub 无 ESP32 平台 SV 开源实现，自行攻关。

动态加载/卸载 TFLite 模型，提取 16 维声纹 Embedding，与 NVS 注册模板做余弦相似度比对。

## 架构

```
KWS Trigger → Task_SV:
  1. 动态加载 model_sv.tflite (≤15KB)
  2. feature_buffer → 40 帧 spectrogram
  3. TFLite INT8 推理 → 16-dim Embedding
  4. NVS 读取注册模板 (64 bytes)
  5. 余弦相似度 → 比对 → ACCEPTED / REJECTED
  6. 卸载模型释放 RAM
```

## 模型规格

| 参数 | 值 |
|------|-----|
| 输入 | [1, 40, 40] INT8 spectrogram |
| 输出 | 16 维 Embedding |
| Flash 占用 | ≤ 15KB |
| Tensor Arena | 48KB (动态分配/释放) |
| 推理延迟 | ≤ 150ms |
| 阈值 | 余弦相似度 ≥ 0.70 |

## 余弦相似度

```
cos(θ) = A·B / (‖A‖‖B‖)
```

- ≥ 0.70 → 主人 → ACCEPTED
- < 0.70 → 陌生人 → REJECTED
- = -2.0 → 无模板（未注册） → 放行

## NVS 模板

| 参数 | 值 |
|------|-----|
| Namespace | `sv_enroll` |
| Key | `sv_template` |
| 大小 | 64 bytes (16 × float32) |

## 内存管理

SV 模型采用 **加载→推理→卸载** 策略：
- 加载前: SV RAM = 0
- 推理中: SV RAM ≈ 48KB (tensor arena) + 15KB (model buffer)
- 卸载后: SV RAM = 0 (KWS 常驻 64KB 不受影响)

## 文件

| 文件 | 说明 |
|------|------|
| `sv_engine.h` | SV API + NVS 模板管理 |
| `sv_engine.c` | 模型加载/卸载/推理/余弦相似度 |
| `task_sv.h` | SV 任务接口 |
| `task_sv.c` | 事件驱动 FreeRTOS 任务 |

## 依赖

- `espressif/esp-tflite-micro` (>= 1.2.0)
- frontend 组件 (feature_buffer)
- NVS Flash
