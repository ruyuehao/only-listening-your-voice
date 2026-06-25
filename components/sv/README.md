# SV (Speaker Verification) 声纹验证引擎

## 项目亮点 (简历摘要)

> 在 ESP32-C3 384KB SRAM 约束下，从零设计 x-vector mini 声纹嵌入模型 (TDNN×3 + Stats Pooling + FC×2, ~12.5K 参数, ≤15KB INT8)。采用 "加载→推理→卸载" 动态内存策略，SV 推理时峰值仅额外占用 63KB heap，完成后立即释放归零。配合余弦相似度阈值 0.70，实现"陌生人说同一唤醒词也无法触发"。GitHub 无 ESP32 平台 SV 开源实现，本项目 SV 部分完全自行攻关。

---

## 一、核心挑战

### 1.1 为什么这不是一个普通的分两类问题

```
KWS:  "这段声音是不是唤醒词?"       → 二分类, TTS 合成数据即可
SV:   "这段唤醒词是不是主人在说?"    → 度量学习, 需要真人多说话人数据

SV 的特殊难点:
1. 输入极度相似: 所有人都说同一句唤醒词 (文本相关 SV)
   区分靠的不是"内容", 而是"音色/语调/节奏"
2. 开集识别: 陌生人可能是训练时从未见过的人
   模型必须学"广义的说话人区分", 而非记住训练集的人
3. 384KB RAM 内必须完成: 训练→INT8量化→MCU推理
```

### 1.2 硬件极限分析

| 约束 | 值 | 工程应对 |
|------|-----|---------|
| SRAM 峰值 | ≤63KB (推理时) | 动态加载模型, 推理后立即 free |
| Flash | ≤15KB INT8 | x-vector mini ~12.5K 参数 |
| 推理延迟 | ≤150ms | 全部 TDNN Conv 沿频率轴 (k×1), ESP-NN 加速 |
| FPU | RV32F 单精度 | 余弦相似度 / L2-Norm 硬件加速 |
| 模板存储 | NVS 64 bytes | 16×float32, 惰性读写, commit 返回值检查 |

---

## 二、模型架构 (x-vector mini)

### 2.1 设计哲学

标准 x-vector (Snyder et al., 2018) 使用 TDNN (Time-Delay Neural Network) +
Statistics Pooling。核心思想:

1. **TDNN 沿时间轴滑动**: 每次卷积在 N 个相邻帧上操作, 捕获局部时序依赖
2. **Statistics Pooling**: 将所有帧的输出在时间维汇总为 mean + std,
   得到固定长度的 "段级表示" —— 无论输入说了多快多慢
3. **FC layers**: 将段级表示映射到低维 embedding 空间

本项目的 mini 版本砍掉了标准 x-vector 的 5 层 TDNN + 1500 维 embedding，
压缩到 3 层 TDNN + 16 维 embedding，适配 MCU 推理。

### 2.2 完整结构

```
输入: [1, 40, 40] INT8 spectrogram
  │  (40 帧 × 40 维, 0.4s 音频窗口, 共用 KWS frontend)
  │
  ├─ TDNN Layer 1:
  │    Conv2D(24, kernel=(5,1)) + ReLU
  │    输入 [1,40,40,1] → 输出 [1,36,40,24]
  │    参数: 24×5×1×1 + 24 = 144
  │    感受野: 50ms (5 帧 @ 10ms)
  │
  ├─ TDNN Layer 2:
  │    Conv2D(32, kernel=(3,1)) + ReLU
  │    输入 [1,36,40,24] → 输出 [1,34,40,32]
  │    参数: 32×3×1×24 + 32 = 2,336
  │    感受野: 80ms (累计 5+3-1 帧)
  │
  ├─ TDNN Layer 3:
  │    Conv2D(48, kernel=(3,1)) + ReLU
  │    输入 [1,34,40,32] → 输出 [1,32,40,48]
  │    参数: 48×3×1×32 + 48 = 4,656
  │    感受野: 110ms (累计 5+3+3-2 帧)
  │
  ├─ Statistics Pooling:
  │    Mean(T)  = ReduceMean(axis=time)     → [1,40,24] → flatten
  │    Std(T)   = Sqrt(ReduceMean((x-μ)²))  → 同
  │    Concat(Mean_flat, Std_flat)          → [1, 96]
  │    参数: 0
  │    物理含义: Mean = 声道形状, Std = 说话节奏/语调
  │
  ├─ Segment FC 1:
  │    FullyConnected(96 → 48) + ReLU
  │    参数: 96×48 + 48 = 4,656
  │
  ├─ Embedding FC 2:
  │    FullyConnected(48 → 16)  ← 16 维 embedding
  │    参数: 48×16 + 16 = 784
  │
  ├─ L2 Normalization:
  │    output = x / ||x||₂
  │
输出: [16] float L2-normalized Embedding

────────────────────────────────────────────
总参数: 144 + 2336 + 4656 + 0 + 4656 + 784 = 12,576
→ INT8 模型: ~15KB Flash
→ FP32 等价: ~50KB (4× 压缩比)
```

### 2.3 为什么 Stats Pooling 而不是 GAP

| | GAP (Global Average Pooling) | Stats Pooling (Mean + Std) |
|---|-----|------|
| 输出维度 | 48 | 96 |
| 信息量 | 仅保留均值 (静态特征) | 均值 + 标准差 (动态特征) |
| 对语速敏感度 | 高 (0.4s vs 0.6s 音频 → 不同均值) | 低 (std 部分补偿时长差异) |
| 同唤醒词区分力 | 较弱 | **更强** ← 关键! |
| 参数增加 | 0 | 0 (无参数层) |
| 下游 FC 参数 | 48×48 + 48×16 | 96×48 + 48×16 |
| TFLite 实现 | AvgPool2D (原生) | MEAN+SUB+SQUARE+SQRT+CONCAT (全原生) |

在同唤醒词场景中，所有人都说同一句 "起飞"，区分 "谁在说" 的关键是**怎么说的**:
- 语速: 0.3s vs 0.5s 说完
- 语调: 升调 vs 降调
- 重音: 起~飞 vs 起-飞

Stats Pooling 的 std 维度天然编码了这些 "怎么说的" 动态特征。

---

## 三、训练策略

### 3.1 Triplet Loss + Center Loss

```
Triplet Loss (类间推开):
  L_triplet = max(||anchor - positive||² - ||anchor - negative||² + margin, 0)
  margin = 0.3

Center Loss (类内紧凑):
  L_center = ||anchor - c_y||²
  (c_y 是说话人 y 的 embedding 中心, 随训练更新)

Total Loss:
  L = L_triplet + 0.1 × L_center
```

### 3.2 数据需求

| 数据 | 数量 | 说明 |
|------|------|------|
| 说话人 | 30-50 人 | 越多越好 |
| 每人录音 | ≥20 次同一唤醒词 | 不同距离 (30cm/1m/2m), 不同环境 |
| 采样率 | 16kHz | 与 INMP441 一致 |
| 预处理 | 40 维 microfrontend spectrogram | 与 KWS 共享 pipeline |
| 三元组构造 | 同人不同录音=Anchor+Positive, 不同人=Negative | 在线随机采样 |

### 3.3 INT8 量化

与 KWS 相同流程: 代表数据集校准 → TFLite INT8 full-integer quantization。
关键参数: `_experimental_variable_quantization = True` (变量也量化为 INT8)。

---

## 四、固件实现细节

### 4.1 动态内存策略 (核心创新)

```
SV 触发前:
  SV 占用 RAM = 0  (全部释放给 KWS / 系统)

SV 触发:
  1. heap_caps_aligned_alloc(48KB arena)
  2. model_loader_read("model_sv.tflite") → ~15KB model buffer
  3. MicroMutableOpResolver<14> → new MicroInterpreter
  4. Invoke() → 提取 16 维 embedding
  5. NVS 读取模板 (64 bytes)
  6. 余弦相似度比对
  7. free(model_buffer) → free(arena) → delete interpreter

SV 完成后:
  SV 占用 RAM = 0  ← 回到触发前状态

峰值: 48KB(arena) + 15KB(model) + ~0.5KB(NVS) = ~63KB
持续时间: ~50ms (推理 + 比对)
```

这个策略将 SV 对系统的影响降到最低——KWS 正常运行的 99% 时间里, SV 完全不占用 RAM。

### 4.2 OpResolver (14 ops, 全 TFLite Micro 原生)

```c
tflite::MicroMutableOpResolver<14> resolver;
resolver.AddConv2D();           // TDNN 1-3, ESP-NN 加速
resolver.AddDepthwiseConv2D();  // (预留, DWConv 变体)
resolver.AddFullyConnected();   // Segment FC + Embedding FC
resolver.AddReshape();          // 维度变换
resolver.AddRelu();             // 激活
resolver.AddQuantize();         // 输入量化
resolver.AddDequantize();       // 输出反量化
resolver.AddMean();             // Stats Pooling: mean
resolver.AddSub();              // Stats Pooling: x - mean
resolver.AddSquare();           // Stats Pooling: (x-mean)²
resolver.AddSqrt();             // Stats Pooling: √variance  ← 已验证原生支持
resolver.AddConcatenation();    // Stats Pooling: concat(mean, std)
resolver.AddL2Normalization();  // 输出归一化
```

**全 14 个算子均为 TFLite Micro 原生 BuiltinOperator，零 custom op。**
SQRT 在 TFLite Micro 源码 (`MicroMutableOpResolver::AddSqrt`) 中证实可用。

### 4.3 余弦相似度

```c
cos(θ) = A·B / (||A|| × ||B||)

A: 实时提取的 embedding  [16] float
B: NVS 存储的模板       [16] float

相似度阈值: 0.70
  ≥ 0.70 → 主人  → ACCEPTED
  0.0 ~ 0.70 → 陌生人 → REJECTED
  -2.0      → 无模板  → ACCEPTED (首次使用放行)
  -3.0      → 模型加载失败 → REJECTED
```

由于输出已 L2 归一化, `||A|| = ||B|| = 1`, 余弦相似度退化为 `dot(A, B)`,
计算量从 3 个向量操作降到 1 个点积。

### 4.4 NVS 模板管理

```
存储: nvs_set_blob("sv_enroll", "sv_template", float[16], 64 bytes)
读取: nvs_get_blob(...)
commit: 必须检查返回值! (flash 写入可能失败)
磨损: 仅在注册时写入一次, 正常使用只读, 无磨损问题
```

---

## 五、与 KWS 的级联设计

```
音频 1s 窗口
  │
  ├─ KWS (100 帧, 前 1s):  检测唤醒词
  │    置信度 ≥ 0.85 ?
  │      YES ──────────────────────┐
  │      NO  → 丢弃, 继续监听      │
  │                                │
  └─ SV (40 帧, 后 0.4s):  ←──────┘ (同段音频)
        动态加载模型
        提取 16 维 embedding
        余弦相似度 vs NVS 模板
          ≥ 0.70 ?
            YES → ACCEPTED (LED 红 + UART JSON)
            NO  → REJECTED (LED 红闪)

设计要点:
  - KWS 和 SV 共享同一段 1s 音频窗口的同一份 40 维 spectrogram
  - KWS 取 100 帧 (1s), SV 取后 40 帧 (0.4s)
  - 零额外音频采集, 零额外前端处理
  - KWS 常驻 RAM, SV 动态加载 → 峰值控制
```

---

## 六、性能指标

| 指标 | 目标 | 设计保障 |
|------|------|---------|
| 模型大小 | ≤15KB Flash | x-vector mini ~12.5K 参数 |
| 推理延迟 | ≤150ms | 3 TDNN Conv ESP-NN 加速, ~18ms |
| 峰值 RAM | ≤63KB (仅推理时) | 动态加载/卸载 |
| 常驻 RAM | 0KB | 完全释放 |
| 主人召回率 | ≥90% (安静) | Triplet + Center Loss |
| 陌生人误接受率 | ≤3% | 余弦阈值 0.70 |
| Embedding 维度 | 16 | L2 归一化 |
| 模板大小 | 64 bytes NVS | 16 × float32 |
| 数据需求 | 30-50 真人 × 20次 | 无法用 TTS 替代 |

---

## 七、训练命令参考

```python
# 基于 TensorFlow/Keras 自定义训练
import tensorflow as tf

model = build_xvector_mini()  # TDNN×3 + StatsPool + FC×2

# Triplet Loss
def triplet_loss(y_true, y_pred, margin=0.3):
    N = tf.shape(y_pred)[0] // 3
    anchor, positive, negative = y_pred[:N], y_pred[N:2*N], y_pred[2*N:]
    pos_dist = tf.reduce_sum(tf.square(anchor - positive), axis=1)
    neg_dist = tf.reduce_sum(tf.square(anchor - negative), axis=1)
    return tf.reduce_mean(tf.maximum(pos_dist - neg_dist + margin, 0.0))

# Center Loss (需维护每类中心)
class CenterLossLayer(tf.keras.layers.Layer):
    def __init__(self, num_classes, feat_dim):
        super().__init__()
        self.centers = tf.Variable(tf.zeros([num_classes, feat_dim]))

    def call(self, features, labels):
        centers_batch = tf.gather(self.centers, labels)
        loss = tf.reduce_mean(tf.square(features - centers_batch))
        # EMA 更新中心: centers = 0.9×old + 0.1×new
        return loss

model.compile(optimizer='adam',
              loss=triplet_loss,
              metrics=['accuracy'])
model.fit(triplet_dataset, epochs=100)

# TFLite 转换 (同 KWS 流程)
```

## 依赖

- `espressif/esp-tflite-micro` (≥ 1.2.0) — TFLite Micro
- `config` — model_loader + NVS
- `frontend` — feature_buffer (共享 40 维 spectrogram)
- `nvs_flash` — 声纹模板存储
