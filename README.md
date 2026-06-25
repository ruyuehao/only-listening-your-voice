# ESP32-C3 声纹唤醒系统

> **版本**: v1.3 | **平台**: ESP32-C3-DevKitM-1 | **框架**: ESP-IDF v5.4

基于 ESP32-C3 的本地声纹唤醒方案，实现"只认主人"的语音唤醒。
KWS (唤醒词检测) + SV (声纹验证) 级联架构，384KB SRAM 约束下通过动态模型加载实现声纹验证。

## 架构

```
INMP441 → I2S → microfrontend (40dim spectrogram) → feature_buffer
                                                        │
                             ┌──────────────────────────┴──────────────┐
                             │                                          │
                       KWS (100帧×40)                           SV (40帧×40)
                       MixedNet CNN                            x-vector mini
                       ≤20KB INT8 Flash                        ≤15KB INT8 Flash
                       唤醒词置信度                             16维 Embedding
                             │                                          │
                             └──────────────┬───────────────────────────┘
                                            │
                                     Decision FSM → LED + UART JSON
```

## 快速开始

### 环境要求

- ESP-IDF v5.4+
- Python 3.12+

### 编译 & 烧录

```bash
cd esp32c3-voice-wake
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

### 放置模型文件

将 `model_kws.tflite` 和 `model_sv.tflite` 放入 `models/` FAT 分区：

```bash
mkdir -p models_fat
cp model_kws.tflite models_fat/
cp model_sv.tflite models_fat/

# 生成 FAT 镜像并烧录
python $IDF_PATH/components/fatfs/fatfsgen.py models_fat models.img
parttool.py write_partition --partition-name=models --input=models.img
```

### 声纹注册

1. 上电 → 绿灯 1Hz 闪烁 (IDLE)
2. **长按 BOOT 键 3 秒** → 蓝灯快闪 (注册模式)
3. 连续说 5 次唤醒词，每次间隔约 2 秒
4. 红灯长亮 2 秒 → 自动重启

## 硬件连接

| INMP441 | ESP32-C3 | 说明 |
|---------|----------|------|
| VDD | 3.3V | 并联 100nF + 10μF 退耦 |
| GND | GND | 2 根并线 |
| WS | GPIO4 | 声道选择 |
| BCLK | GPIO5 | < 5cm |
| DOUT | GPIO6 | 数据输出 |
| L/R | GND | 不可悬空 |

## 项目结构

```
esp32c3-voice-wake/
├── main/                  # 系统入口
├── config/                # 引脚/参数/模型加载
├── tools/                 # filterbank 预计算脚本
├── components/
│   ├── audio/             # I2S 驱动 + 环形缓冲区
│   ├── frontend/          # 声学前端 (MFCC/PCAN)
│   ├── kws/               # KWS 唤醒词引擎 (MixedNet)
│   ├── sv/                # SV 声纹验证 (x-vector mini)
│   ├── decision/          # FSM + LED + 注册
│   └── comm/              # UART JSON 上报
├── partitions.csv         # 分区表 (4MB Flash)
├── sdkconfig.defaults     # SDK 默认配置
├── TASK_STATUS.md         # 任务状态追踪
└── tech-stack.md          # 技术栈详解
```

## 资源占用

| 资源 | 占用 | 总量 | 余量 |
|------|------|------|------|
| SRAM (峰值) | ~232KB | 384KB | 152KB (40%) |
| Flash | ~400KB 固件 + 35KB 模型 | 4MB | >3.5MB |
| CPU (常态) | ~20% | 160MHz | 充裕 |

## 文档索引

| 文档 | 说明 |
|------|------|
| [TASK_STATUS.md](TASK_STATUS.md) | 开发进度 + 审计修复记录 |
| [tech-stack.md](tech-stack.md) | 全模块技术栈详情 + 内存布局 |
| [components/audio/README.md](components/audio/README.md) | 音频采集模块 |
| [components/frontend/README.md](components/frontend/README.md) | 声学前端模块 |
| [components/kws/README.md](components/kws/README.md) | KWS 唤醒词引擎 |
| [components/sv/README.md](components/sv/README.md) | SV 声纹验证引擎 |
| [components/decision/README.md](components/decision/README.md) | 决策调度 + LED |
| [components/comm/README.md](components/comm/README.md) | 通信上报模块 |
