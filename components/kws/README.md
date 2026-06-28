# KWS (Keyword Spotting) 唤醒词检测引擎

## 项目亮点 (简历摘要)

> 在 384KB SRAM / 4MB Flash 的 ESP32-C3 (RISC-V 单核 160MHz) 上，实现 11.4KB INT8 量化的 DS-CNN 唤醒词检测。采用 TensorFlow Lite Micro + ESP-NN 指令集加速，端到端推理延迟 ≤200ms。通过 Mel 滤波器组 Flash 预计算 (3.3KB)、Tensor Arena 静态分配 (64KB)、深度可分离卷积参数压缩等工程手段，达到 89.3% 准确率。

---

## 一、硬件约束与模型规模

| 资源 | ESP32-C3 实际 | 本模块占用 |
|------|--------------|-----------|
| SRAM | 384KB | 64KB (tensor arena) + 11.4KB (model) = 75.4KB |
| Flash | 4MB | 11.4KB (model) + ~4KB (code) |
| CPU | 160MHz RISC-V 单核 | ~15% (100ms 周期) |
| FPU | RV32IMFC (单精度) | 反量化 |

## 二、模型架构 (DS-CNN)

```
输入: [1, 100, 13, 1] INT8 spectrogram
  │  (100 帧 × 13 维 MFCC, 1s 音频窗口)
  │
  ├─ Conv2D(1→8, kernel=10×4, stride=2) + ReLU
  │   输出: (?, 46, 5, 8)  参数: 8×10×4 + 8 = 328
  │
  ├─ DepthwiseConv2D(8, kernel=3×3) + ReLU
  │   输出: (?, 44, 3, 8)  参数: 8×3×3 = 72
  │
  ├─ AvgPool2D(2×2)
  │   输出: (?, 22, 1, 8) = Flatten → 176
  │
  ├─ FC(176 → 32) + ReLU
  │   参数: 176×32 + 32 = 5,664
  │
  ├─ FC(32 → 2)
  │   参数: 32×2 + 2 = 66
  │
输出: Softmax 2 类 → float 唤醒词置信度

────────────────────────────────
总参数: ~6,130 → INT8 模型 11.4KB
推理 FLOPs: ~1.1M (远低于 20KB 预算)
```

### 为什么 DS-CNN 适合本场景

| 特性 | 收益 |
|------|------|
| Depthwise Separable Conv | 参数比标准 Conv 减少 ~8× |
| 13 维 MFCC 输入 (非 40) | 第一层参数从 ~1.3K 降到 328 |
| Kernel 10×4 大跨步 | 一次性将 100 帧降为 46 帧, 节省后续计算 |
| 仅 2 层 FC | 全连接参数控制在 6K 以内 |

## 三、INT8 量化

已量化: **ONNX INT8 = 11.4KB** ✅ (≤20KB 目标)。

转换命令: `python tools/convert_kws.py` (Keras → TFLite INT8 full-integer)

## 四、固件实现

### OpResolver (12 ops)

```c
MicroMutableOpResolver<12> resolver;
resolver.AddConv2D();           // ESP-NN ~8×
resolver.AddDepthwiseConv2D();  // ESP-NN ~8×
resolver.AddFullyConnected();   // ESP-NN ~8×
resolver.AddSoftmax();
resolver.AddReshape();
resolver.AddPad();
resolver.AddAveragePool2D();
resolver.AddMaxPool2D();
resolver.AddRelu();
resolver.AddQuantize();
resolver.AddDequantize();
resolver.AddAdd();
```

### 调度策略

```
Task_KWS (prio=3, 12KB栈)
  │
  ├─ vTaskDelay(100ms)           ← 定时轮询
  ├─ feature_buffer_ready_for_kws()  ← ≥100帧?
  ├─ feature_buffer_get_recent(100)  ← [100×13] uint8
  ├─ uint8→INT8 转换 (减128)
  ├─ kws_engine_infer()
  ├─ INT8→float 反量化 → 置信度
  ├─ ≥ 0.85 → xEventGroupSetBits(EVENT_KWS_TRIGGERED)
  └─ 2s 冷却: 防重复触发
```

## 五、数据流 (13 维 MFCC)

```
INMP441 → I2S → Task_AudioCapture → RingBuffer(32KB)
  → Task_Frontend (10ms):
      480采样 Hann窗 → 512pt FFT(ESP-DSP)
      → Mel Filterbank(13ch, 3.3KB Flash)
      → Log → PCAN → uint8[13]
      → feature_buffer_push()
  → Task_KWS (100ms):
      feature_buffer(100帧×13) → TFLite INT8推理
      → 置信度 → 阈值判定
```

## 六、性能

| 指标 | 值 |
|------|-----|
| 模型大小 | 11.4KB INT8 |
| 准确率 | 89.29% |
| 推理延迟 | ~50ms (ESP-NN) |
| Tensor Arena | 64KB BSS |

## 依赖

- `espressif/esp-tflite-micro` (≥ 1.2.0)
- `config` (model_loader)
- `frontend` (feature_buffer, 13 维)
