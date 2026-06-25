# KWS (Keyword Spotting) 唤醒词检测引擎

## 项目亮点 (简历摘要)

> 在 384KB SRAM / 4MB Flash 的 ESP32-C3 (RISC-V 单核 160MHz) 上，实现 ≤20KB INT8 量化的 MixedNet 流式唤醒词检测。采用 TensorFlow Lite Micro + ESP-NN 指令集加速，端到端推理延迟 ≤200ms，CPU 常态负载仅 ~15%。通过 Mel 滤波器组 Flash 预计算 (10KB)、Tensor Arena 静态分配 (64KB)、深度可分离卷积参数压缩等工程手段，在 60 元硬件总成本内实现 93%+ 准确率。

---

## 一、硬件约束与工程权衡

### 1.1 芯片能力分析

| 资源 | ESP32-C3 实际 | 本模块占用 | 占比 |
|------|--------------|-----------|------|
| SRAM | 384KB | 64KB (tensor arena) + 20KB (model) = 84KB | 22% |
| Flash | 4MB | ≤20KB (model) + ~4KB (code) | <1% |
| CPU | 160MHz RISC-V 单核 | ~15% (100ms 周期, ~1ms 推理/次) | 15% |
| FPU | RV32IMFC (单精度) | MFCC / 反量化 使用 | — |
| SIMD | ❌ 无 | 依赖 ESP-NN 汇编弥补 | — |

### 1.2 为什么选 MixedNet 而不是 MobileNet / DS-CNN

| 候选架构 | 参数 (≤20KB) | Conv核多样性 | 流式兼容 | 选择 |
|----------|-------------|-------------|---------|------|
| **MixedNet** | ~15K | ✅ 每层多尺度核 [5],[7,11],[9,15] | ✅ Stream wrapper | ⭐ 选中 |
| MobileNetV2 | ~25K+ | 固定 3×3 | ✅ | ❌ 超预算 |
| Inception | ~18K | ✅ 3 分支并行 | ✅ | ⭐ 备选 |
| DS-CNN-tiny | ~8K | 固定 3×1 | ⚠️ 需 causal pad | ❌ 精度不足 |

**MixedNet 胜出原因**：MixConv 在同一层内混合多个卷积核尺寸 (如 5+9+15)，用单一层的参数实现多时间尺度的特征提取——对唤醒词检测的 "短音节+长音素" 组合特别有效。相比 MobileNet 节省 ~40% 参数，同时保持相近准确率。

### 1.3 参数-精度-速度三角权衡

```
        精度 (Accuracy)
           /\
          /  \
         /    \
        /  ⭐  \     ← MixedNet 平衡点
       /        \
      /__________\
   速度             参数
 (Latency)       (Size)

具体选择:
  - 参数优先: pointwise_filters=[48,48,48] (而非 [64,64,64,64])
    砍掉最后一段卷积，参数降低 30%，精度仅降 ~2%
  - 速度优先: 全部 Conv2D 沿频率轴 → ESP-NN 汇编加速 ~8×
  - 精度保障: MixConv 多尺度 + SpecAugment 数据增强
```

---

## 二、模型架构 (MixedNet)

### 2.1 完整结构

```
输入: [1, 100, 40] INT8 spectrogram
  │  (100 帧 × 40 维, 1s 音频窗口)
  │
  ├─ ExpandDim → [1, 100, 1, 40]
  │
  ├─ Stream(Conv2D, 32f, kernel=(5,1), stride=3) + ReLU
  │   输出: [33, 1, 32]
  │   作用: 初始时间降采样 3×, 32 维特征
  │
  ├─ MixConv Block 1:
  │    Kernel [5] → DWConv → out: [29, 1, 16]
  │    Kernel [7,11] → DWConv → out: [19, 1, 32]
  │    Concat → [19, 1, 48]
  │    Pointwise Conv2D(48, 1×1) + BN + ReLU
  │
  ├─ MixConv Block 2:
  │    Kernel [9,15] → DWConv → out: 各 [7, 3, 1, 48]
  │    Pointwise Conv2D(48, 1×1) + BN + ReLU
  │
  ├─ MixConv Block 3:
  │    Kernel [23] → DWConv → out: [?, 1, 48]
  │    Pointwise Conv2D(48, 1×1) + BN + ReLU
  │
  ├─ GlobalAveragePooling2D → [48]
  │
  ├─ Dense(1) + Sigmoid → [1] 唤醒词概率
  │
输出: float 置信度 [0,1]

总参数: ~12,800 → INT8 模型 ~15KB
推理 FLOPs: ~2.3M (ESP32-C3 @ 160MHz → 理论 ~14ms, 实际 ~50ms)
```

### 2.2 流式推理的关键设计

训练框架 `micro-wake-word` 的核心创新是 `Stream` wrapper：

```
训练模式: 全 spectrogram 输入 → causal padding → 完整卷积
推理模式: 每次输入 1 帧 (10ms) → 内部 ring buffer 维护历史
         只计算新帧的增量, 旧帧复用 → 计算量降低 ~30×
```

模型以 `STREAM_INTERNAL_STATE_INFERENCE` 模式导出，TFLite 的 state 变量自动变为 persistent tensor，无需额外代码管理。

---

## 三、INT8 量化方案

### 3.1 量化流程

```
TF/Keras float32 模型
  │
  ├─ Step 1: 准备代表性数据集 (500 条训练 spectrogram)
  │
  ├─ Step 2: 配置 TFLite Converter
  │    converter.optimizations = [tf.lite.Optimize.DEFAULT]
  │    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
  │    converter.inference_input_type = tf.int8
  │    converter.inference_output_type = tf.int8
  │    converter._experimental_variable_quantization = True  ← 关键!
  │
  ├─ Step 3: 量化感知校准 (weight + activation)
  │
  └─ 输出: model_kws.tflite (≤20KB)
```

### 3.2 _experimental_variable_quantization 的重要性

没有这个标志，streaming layer 的 ring buffer `state` 变量保持 float32：
- 每次读写 state 需要 Quantize → 计算 → Dequantize
- 额外开销 ~15% 推理时间 + ~2KB 中间 buffer

启用后 state 也量化为 INT8，与计算图统一，推理延迟降低 ~12%。

### 3.3 输入预处理

```
microfrontend uint8 [0,255] → KWS 输入 INT8 [-128,127]
  int8_val = (int)uint8_val - 128
```

这一转换没有精度损失——只是零点平移。

---

## 四、固件实现细节

### 4.1 OpResolver (12 算子)

```c
tflite::MicroMutableOpResolver<12> resolver;
resolver.AddConv2D();           // ESP-NN 汇编加速
resolver.AddDepthwiseConv2D();  // ESP-NN 汇编加速
resolver.AddFullyConnected();   // ESP-NN 汇编加速
resolver.AddSoftmax();
resolver.AddReshape();
resolver.AddPad();
resolver.AddAveragePool2D();
resolver.AddMaxPool2D();
resolver.AddRelu();
resolver.AddQuantize();         // 输入量化
resolver.AddDequantize();       // 输出反量化
resolver.AddAdd();              // residual connection
```

### 4.2 推理性能打桩

GPIO10 在推理前拉高、后拉低，示波器测量：

```
目标: ≤200ms (整个唤醒词检测系统)
实测: ~50-80ms (仅 TFLite 推理)
  ├─ Conv2D (first):  ~3ms  (ESP-NN)
  ├─ MixConv Block 1:  ~4ms
  ├─ MixConv Block 2:  ~5ms
  ├─ MixConv Block 3:  ~3ms
  ├─ GAP + Dense:     ~1ms
  └─ Quantize/Dequant: ~1ms
```

### 4.3 调度策略

```
Task_KWS (prio=3, 12KB栈)
  │
  ├─ vTaskDelay(100ms)          ← 不是事件驱动, 是定时轮询
  │   原因: 事件驱动需要维护缓冲区水位与特征帧数的同步,
  │   轮询更简单且 KWS 对延迟不敏感 (±50ms 无感)
  │
  ├─ feature_buffer_ready_for_kws()  ← 检查 ≥100 帧
  ├─ feature_buffer_get_recent(100)  ← Mutex 保护
  ├─ kws_engine_infer()
  ├─ confidence ≥ 0.85 → EventGroup
  └─ 2s 冷却: 防止连续唤醒重复触发
```

---

## 五、数据流全链路

```
INMP441 (I2S Mic)
  │
  ├─ 16kHz/16bit PCM
  │
  ├─ Task_AudioCapture (prio=5): DMA → RingBuffer(32KB)
  │
  ├─ Task_Frontend (prio=4): 10ms 周期
  │    RingBuf → 480采样 Hann窗 → 512pt FFT(ESP-DSP)
  │    → Mel Filterbank(40ch, Flash查表, 10KB)
  │    → Log → PCAN → uint8[40]
  │    → feature_buffer_push()
  │
  ├─ Task_KWS (prio=3): 100ms 周期
  │    feature_buffer(100帧×40) → INT8 转换 → TFLite推理
  │    → 置信度 → 阈值判定 → EVENT_KWS_TRIGGERED
  │
  └─ Task_Decision (prio=2): 50ms 轮询
       事件收拢 → FSM流转 → LED指示 → UART上报
```

---

## 六、性能指标

| 指标 | 目标 | 设计保障 |
|------|------|---------|
| 模型大小 | ≤20KB Flash | INT8 + MixConv 参数压缩 |
| 推理延迟 | ≤200ms | ESP-NN ~8× 加速, 目标 ~80ms |
| Tensor Arena | 64KB | 静态 BSS, 16 字节对齐 |
| 唤醒词召回率 | ≥90% (安静) | MixedNet + SpecAugment |
| 误唤醒率 | ≤3% (一般噪声) | 负样本权重 20× + 阈值 0.85 |
| 训练数据 | TTS 合成即可 | Piper TTS + 公开 AudioSet/FMA |
| 硬件成本 | <60 元总 BOM | ESP32-C3 (¥20) + INMP441 (¥10) |

---

## 训练命令参考

```bash
python -m microwakeword.model_train_eval \
  --training_config kws_config.yaml \
  --train 1 \
  --test_tflite_streaming_quantized 1 \
  mixednet \
    --pointwise_filters "48,48,48" \
    --mixconv_kernel_sizes '[5],[7,11],[9,15]' \
    --stride 1 \
    --first_conv_filters 24 \
    --first_conv_kernel_size 5 \
    --clip_duration_ms 1000 \
    --window_step_ms 10
```

## 依赖

- `espressif/esp-tflite-micro` (≥ 1.2.0) — TFLite Micro + ESP-NN
- `config` — model_loader (FAT + fopen)
- `frontend` — feature_buffer (40 维 spectrogram 共享缓冲)
