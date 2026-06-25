# ESP32-C3 声纹唤醒系统 — 技术栈

> 版本: v1.3 | 目标芯片: ESP32-C3-DevKitM-1 (RISC-V 160MHz, 384KB SRAM, 4MB Flash)

---

## 全景图

```
┌─────────────────────────────────────────────────────────┐
│ AI 推理层                                                │
│  TensorFlow Lite Micro (esp-tflite-micro)               │
│   ├─ ESP-NN 内核加速 (Conv2D/DWConv/FC ~8× speedup)    │
│   ├─ MicroMutableOpResolver (12+ ops)                  │
│   └─ 模型格式: FlatBuffers INT8 (.tflite)               │
├─────────────────────────────────────────────────────────┤
│ 信号处理层                                               │
│  ESP-DSP (FFT 加速) + 自实现 Mel Filterbank + PCAN      │
│  micro-wake-word audio_utils.py 参数对标                │
├─────────────────────────────────────────────────────────┤
│ 驱动层                                                   │
│  ESP-IDF I2S STD Driver  |  ESP-IDF UART Driver         │
│  led_strip (RMT/WS2812)  |  NVS Flash                   │
│  FAT Filesystem (wear-leveling)                         │
├─────────────────────────────────────────────────────────┤
│ 操作系统层 / 并发                                        │
│  FreeRTOS Tasks (5+1)   |  Event Groups  |  Mutex       │
│  Ring Buffer (BYTEBUF)  |  100ms/50ms/10ms 周期调度      │
├─────────────────────────────────────────────────────────┤
│ 硬件层                                                   │
│  ESP32-C3 (RISC-V 160MHz, 384KB SRAM, 4MB Flash)        │
│  INMP441 I2S Mic  |  WS2812 RGB LED  |  BOOT Button     │
└─────────────────────────────────────────────────────────┘
```

---

## 1. config/ — 系统配置层

| 文件 | 技术 | 说明 |
|------|------|------|
| `pin_defs.h` | ESP-IDF GPIO Driver | GPIO 引脚宏定义 (I2S / UART / LED / BOOT / Profile) |
| `sys_config.h` | FreeRTOS Config + C 预处理器 | 阈值 / 栈大小 / 任务优先级 / 事件位 集中管理 |
| `model_loader.c` | **ESP-IDF FAT Filesystem** (`esp_vfs_fat`) + **wear-leveling** (`spi_flash_wl`) | 惰性挂载 models FAT 分区, `fopen` / `fread` 按文件名加载 `.tflite` |

---

## 2. audio/ — 音频采集

| 组件 | 技术栈 | 详情 |
|------|--------|------|
| I2S 驱动 | ESP-IDF **I2S STD Driver** (v5.x 新 API) | `i2s_chan_handle_t` + `i2s_std_config_t`, Philips 标准时序, Master 模式 |
| DMA 缓冲 | ESP-IDF I2S DMA | `i2s_channel_read()` 阻塞读取, 1024 采样 / 帧, 100ms 超时 |
| 环形缓冲区 | FreeRTOS **Ring Buffer** (`RINGBUF_TYPE_BYTEBUF`) | 32KB, 线程安全, `xRingbufferSend` / `xRingbufferReceiveUpTo` |
| 采集任务 | FreeRTOS **Task** (prio 5, 8KB 栈) | 事件驱动: 每 100ms 累积触发 `EVENT_AUDIO_READY` |
| 硬件 | **INMP441** MEMS 麦克风 | I2S 接口, 16kHz / 16-bit / mono, L/R 引脚接地 = 左声道 |

---

## 3. frontend/ — 声学前端

| 组件 | 技术栈 | 详情 |
|------|--------|------|
| FFT 加速 | **ESP-DSP** (`dsps_fft2r_fc32`) | 512 点实数 FFT, ESP32-C3 RISC-V 汇编优化 |
| 窗函数 | 自实现 **Hann Window** | 480 采样点 (30ms @ 16kHz) |
| Mel 滤波器组 | 自实现三角滤波器 | 40 通道, 125–7500 Hz, `hz_to_mel` / `mel_to_hz` 转换 |
| PCAN 降噪 | 自实现 **Per-Channel AGC + Noise Suppression** | α = 0.01 指数平滑噪声估计, 增益归一化 |
| 特征输出 | uint8 [0,255] spectrogram | 40 维 / 帧 @ 10ms 帧步 |
| 滑动窗口缓冲 | FreeRTOS **Mutex** + 环形数组 | 150 帧 (1.5s) 环形缓冲, 线程安全读写 |
| 流水线任务 | FreeRTOS **Task** (prio 4, 6KB 栈) | 10ms 精确周期, `esp_timer_get_time` 自适应补偿 |

> **参数对标**: 完全参照 TFLM `micro_frontend` + micro-wake-word `audio_utils.py`

---

## 4. kws/ — 唤醒词引擎

| 组件 | 技术栈 | 详情 |
|------|--------|------|
| 推理框架 | **TensorFlow Lite Micro** (`esp-tflite-micro` 组件) | ESP-IDF 官方组件, 含 **ESP-NN** 内核加速 (Conv2D / DWConv / FC 提速 ~8×) |
| 模型格式 | FlatBuffers (`.tflite`) | INT8 量化, `kTfLiteOk` 状态检查 |
| 模型架构 | **MixedNet** (MixConv) | micro-wake-word 训练框架, ≤20KB Flash |
| 输入转换 | uint8 → INT8 (减 128) | microfrontend [0,255] → TFLite [-128,127] |
| 输出反量化 | `(int8 − zero_point) × scale` | Softmax 2 类, 取 index[1] 为唤醒词置信度 |
| OpResolver | `MicroMutableOpResolver<12>` | 注册 12 个算子, 自动链接 ESP-NN |
| Tensor Arena | 64KB BSS (16 字节对齐) | `__attribute__((aligned(16)))` 静态分配 |
| KWS 任务 | FreeRTOS **Task** (prio 3, 12KB 栈) | 100ms 轮询 `feature_buffer_ready_for_kws()`, 2s 冷却防重复触发 |
| 性能打桩 | GPIO | GPIO10 拉高 / 拉低, 示波器测量推理延迟 |

---

## 5. sv/ — 声纹验证引擎

| 组件 | 技术栈 | 详情 |
|------|--------|------|
| 推理框架 | **TensorFlow Lite Micro** (动态加载) | 每次验证独立创建 / 销毁 `MicroInterpreter`, 不占用常驻 RAM |
| 模型架构 | 自定义 CNN → 16 维 Embedding | ≤15KB Flash, 输入 40 帧 × 40 维 |
| 内存策略 | **Heap Allocation** (`heap_caps_aligned_alloc`) | 48KB tensor arena + 模型 buffer 动态分配, 推理后立即 `free` |
| 声纹比对 | **余弦相似度** (`A·B / ‖A‖·‖B‖`) | 16 维 float32 向量比对, 阈值 0.70 |
| 模板存储 | ESP-IDF **NVS** (`nvs_set_blob` / `nvs_get_blob`) | 64 字节 (16 × float32), namespace `sv_enroll`, key `sv_template` |
| 模型加载 | `model_loader_read("model_sv.tflite")` | FAT 文件系统, `fopen` / `fread`, 惰性挂载 |
| SV 任务 | FreeRTOS **Task** (prio 3, 10KB 栈) | 事件驱动: 等 `EVENT_KWS_TRIGGERED` → 推理 → 设 `EVENT_SV_DONE` |
| 性能打桩 | GPIO | GPIO3 拉高 / 拉低 |

---

## 6. decision/ — 决策调度 + 交互

| 组件 | 技术栈 | 详情 |
|------|--------|------|
| FSM 状态机 | C 枚举 + FreeRTOS **Mutex** | 7 状态 (IDLE / LISTENING / VERIFYING / ACCEPTED / REJECTED / ENROLL_TRIGGER / ENROLL_RECORD / ENROLL_SAVED) |
| RGB LED | ESP-IDF **`led_strip`** 组件 (RMT 后端) | GPIO8, WS2812 (GRB 色序), 10MHz RMT 时钟, 6 种颜色/模式 |
| BOOT 按键 | GPIO 轮询 + 50ms **软件消抖** | GPIO9 (Strapping Pin), 3s 长按 → 注册模式, 10s 超时保护 |
| 注册流程 | NVS 写入 + SV 引擎 (`sv_engine_extract`) | 5 次录制 → 平均 Embedding → `sv_template_save` → `esp_restart` |
| Decision 任务 | FreeRTOS **Task** (prio 2, 6KB 栈) | 50ms 轮询 FSM + LED 刷新, 1s/1.5s 超时回 IDLE |
| 结果读取 | 全局 `float g_sv_similarity` | SV 任务写入, Decision 任务读取, `EVENT_SV_DONE` 作为内存屏障 |

---

## 7. comm/ — 通信上报

| 组件 | 技术栈 | 详情 |
|------|--------|------|
| 物理层 | ESP-IDF **UART Driver** (v5.x) | UART1: TX = GPIO21, RX = GPIO20, 115200-8-N-1, 无流控 |
| 数据格式 | JSON (手动 `snprintf` 拼接) | `{"dev":"esp32c3_kws_01","evt":"wake","conf":0.92,"sim":0.83,"ts":…}` |
| 握手协议 | 发送 → 等待 `OK\r\n` → 超时重试 | `uart_read_bytes` 100ms 轮询, 500ms 总超时, 最多 1 次重试 |
| 错误恢复 | `uart_driver_delete` 清理 | init 失败路径正确释放 UART 资源 |

---

## 8. main/ — 系统入口

| 组件 | 技术栈 | 详情 |
|------|--------|------|
| 芯片信息 | ESP-IDF **System API** (`esp_chip_info`) | 打印型号 / 版本 / 核数 / 频率 / Flash 大小 |
| 存储初始化 | ESP-IDF **NVS** (`nvs_flash_init`) | 损坏时自动擦除重建 |
| Strapping 检查 | GPIO 输入检测 | GPIO9 上电低电平 → 3s 警告 → `esp_restart()` |
| 事件通信 | FreeRTOS **Event Groups** | 全局 `g_event_group`, 4 个事件位 |
| 构建系统 | **CMake** + ESP-IDF 组件管理器 | `idf_component.yml` 声明 `esp-tflite-micro` + `led_strip` 依赖 |

---

## 任务一览

| 任务名 | 优先级 | 栈 | 周期 / 触发 |
|--------|--------|-----|-------------|
| `Task_AudioCapture` | 5 | 8KB | I2S DMA 阻塞读取, 100ms 事件触发 |
| `Task_Frontend` | 4 | 6KB | 10ms 精确周期 |
| `Task_KWS` | 3 | 12KB | 100ms 轮询 |
| `Task_SV` | 3 | 10KB | EVENT_KWS_TRIGGERED |
| `Task_Decision` | 2 | 6KB | 50ms 轮询 + 事件处理 |
| `Task_Enroll` | 1 | 6KB | GPIO9 按键监控 |

---

## 内存布局

| 区域 | 大小 | 类型 |
|------|------|------|
| KWS Tensor Arena (常驻) | 64KB | BSS |
| Mel Filterbank | 41KB | BSS (TODO: → Flash) |
| Feature Buffer | 6KB | BSS |
| Hann Window | 2KB | BSS |
| PCM Window | 1KB | BSS |
| 环形缓冲区 | 32KB | Heap (FreRTOS RingBuf) |
| KWS 模型数据 | ~20KB | Heap |
| SV Session (峰值) | ~63KB | Heap (48KB arena + 15KB model) |
| 任务栈 × 6 | ~44KB | Heap (FreeRTOS TCBs) |
| **峰值总计** | **~273KB** | 384KB 总量, 余量 ~111KB |
