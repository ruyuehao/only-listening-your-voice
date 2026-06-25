# SV Engine (声纹验证引擎)

## 概述

**本项目核心创新模块** — GitHub 无 ESP32 平台 SV 开源实现，自行攻关。

采用 **x-vector mini** 架构（TDNN + Stats Pooling），动态加载/卸载 TFLite 模型，
提取 16 维 L2-normalized Embedding，与 NVS 注册模板做余弦相似度比对。

## 模型架构 (x-vector mini)

```
输入: [1, 40, 40] INT8 spectrogram (共用 KWS frontend)
    │
    ├─ TDNN1: Conv2D(24, 5×1) + ReLU        → (36, 40, 24)   param:   144
    ├─ TDNN2: Conv2D(32, 3×1) + ReLU        → (34, 40, 32)   param:  2336
    ├─ TDNN3: Conv2D(48, 3×1) + ReLU        → (32, 40, 48)   param:  4656
    │
    ├─ Stats Pooling (沿时间轴):
    │    Mean(T) + Std(T) → Concat          → (96)           param:     0
    │
    ├─ FC1 (96 → 48) + ReLU                 → (48)           param:  4656
    ├─ FC2 (48 → 16) + L2-Norm              → (16)           param:   784
    │
输出: [16] float L2-normalized Embedding
─────────────────────────────────────────────────
参数总计: ~12,576 → INT8 模型 ≤15KB Flash
```

### 为什么选 x-vector

| 对比 | 3 Conv + GAP | x-vector mini |
|------|-------------|---------------|
| 时序建模 | GAP 丢弃时间结构 | Stats Pooling 保留均值+方差 |
| 动态特征 | 无 | 标准差捕获说话节奏/语调 |
| 参数 | ~9K | ~12.5K (仍在15KB内) |
| 同唤醒词区分力 | 较弱 | 更强（大家都说同一句，区分靠怎么说） |

### Stats Pooling 实现

```
Mean(T)  = ReduceMean(features, axis=time)     # 静态声道特征
Std(T)   = Sqrt(ReduceMean((f - Mean)², axis=time))  # 动态韵律特征
Output   = Concat(Mean, Std)                    # 96维段级向量
```

在 TFLite 中用 5 个内置算子: `MEAN` + `SUB` + `SQUARE` + `SQRT` + `CONCATENATION`

## 训练策略

| 组件 | 说明 |
|------|------|
| Loss | **Triplet Loss** (margin=0.3) + **Center Loss** (λ=0.1) |
| 正样本 | 同人不同录音 (Anchor↔Positive) |
| 负样本 | 不同人录音 (Anchor↔Negative) |
| 数据 | 30-50 人 × 每人 ≥20 次同一唤醒词 |
| 量化 | INT8 full-integer quantization

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
