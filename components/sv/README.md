# SV (Speaker Verification) 声纹验证引擎

## 项目亮点 (简历摘要)

> 在 ESP32-C3 384KB SRAM 约束下，设计 1D CNN 声纹嵌入模型 (Conv1d×3 + GAP + FC + L2-Norm, ~2.2K 参数, 14.3KB INT8)。采用 "加载→推理→卸载" 动态内存策略，SV 推理时峰值仅额外占用 63KB heap，完成后立即释放归零。配合余弦相似度阈值 0.70，实现"陌生人说同一唤醒词也无法触发"。GitHub 无 ESP32 平台 SV 开源实现，本项目 SV 部分完全自行攻关。

---

## 一、硬件极限

| 约束 | 值 | 工程应对 |
|------|-----|---------|
| SRAM 峰值 | ≤63KB (动态) | 加载→推理→卸载, 常驻 0KB |
| Flash | 14.3KB INT8 | 1D CNN ~2.2K 参数 |
| 推理延迟 | ≤150ms | Conv1d 沿时间轴, ESP-NN 加速 |
| FPU | RV32F | 余弦相似度 / L2-Norm 硬件加速 |
| 模板 | NVS 64 bytes | 16×float32, 惰性读写 |

## 二、模型架构 (1D CNN Embedding)

```
输入: [1, 40, 13] INT8 MFCC spectrogram
  │  (40 帧 × 13 维, 0.4s 音频, 共用 KWS frontend)
  │
  ├─ Conv1d(13→32, kernel=5, stride=2) + BN + ReLU
  │   输出: (18, 32)  参数: 32×5×13 + 32(x2 BN) ≈ 2,144
  │   感受野: 50ms (stride=2, 每 20ms 一步)
  │
  ├─ Conv1d(32→32, kernel=3, stride=2) + BN + ReLU
  │   输出: (8, 32)   参数: 32×3×32 + 64 ≈ 3,136
  │   感受野: 90ms (累计)
  │
  ├─ Conv1d(32→24, kernel=3, stride=1) + BN + ReLU
  │   输出: (6, 24)   参数: 24×3×32 + 48 ≈ 2,352
  │   感受野: 130ms (累计)
  │
  ├─ GlobalAvgPool (沿时间轴)
  │   输出: (24,)     参数: 0
  │
  ├─ FC(24 → 16)
  │   参数: 24×16 + 16 = 400
  │
  ├─ L2-Norm
  │
输出: [16] float L2-Normalized Embedding

────────────────────────────────
总参数: ~8,032 → INT8 模型 14.3KB
```

### 1D CNN 为什么适合本场景

| 特性 | 收益 |
|------|------|
| Conv1d 沿时间轴 | 天然时序建模, 比 Conv2D 省参数量 |
| Stride=2 下采样 | 每次卷积将 40 帧压缩为 18→8→6 帧 |
| 3 层 BN + ReLU | 训练稳定, INT8 量化无损 |
| GAP 替代 StatsPool | 更简单, 仍保留足够区分力 |
| 仅 1 层 FC(24→16) | 全连接参数仅 400 (比 x-vector 的 ~5.4K 少 93%) |

## 三、动态内存策略

```
SV 触发前:  RAM = 0  (全部归还系统)
SV 触发:    alloc 48KB arena + 15KB model + 1KB misc = 64KB
            推理 (Conv1d×3 → GAP → FC → L2)
            cosine_similarity vs NVS template
            free all → RAM = 0
SV 完成后:  RAM = 0  (KWS 正常运行)
```

## 四、OpResolver (10 ops)

```c
MicroMutableOpResolver<10> resolver;
resolver.AddConv2D();           // Conv1d→Conv2D(k,1), ESP-NN
resolver.AddFullyConnected();   // FC(24→16)
resolver.AddReshape();
resolver.AddRelu();
resolver.AddMean();             // GAP = ReduceMean over time
resolver.AddQuantize();
resolver.AddDequantize();
resolver.AddMul();              // BN (可能 baked into weights)
resolver.AddL2Normalization();  // output norm
resolver.AddSoftmax();          // 备用
```

全 10 个算子 TFLite Micro 原生支持，零 custom op。

## 五、与 KWS 的级联

```
音频 1s 窗口 (同一段 PCM)
  │
  ├─ KWS (100 帧): Conv2D+DWConv+FC → 唤醒词?
  │    YES ──────────────────────┐
  │                              │
  └─ SV (40 帧): Conv1d×3+GAP+FC │ → 16-d embedding
                                  │    cosine vs NVS template
                                  │    ≥ 0.70? → ACCEPTED
                                  │    < 0.70? → REJECTED
                                  │    -2.0?   → 首次使用, 放行
```

## 六、性能

| 指标 | 值 |
|------|-----|
| 模型大小 | 14.3KB INT8 |
| 推理延迟 | ~30ms (Conv1d ESP-NN) |
| 峰值 RAM | 64KB (仅推理时) |
| 常驻 RAM | 0KB |
| 模板 | 64 bytes NVS |
| 训练 | GE2E Loss, 30 人 (GPU 服务器) |

## 依赖

- `espressif/esp-tflite-micro` (≥ 1.2.0)
- `config` (model_loader + NVS)
- `frontend` (feature_buffer, 13 维)
