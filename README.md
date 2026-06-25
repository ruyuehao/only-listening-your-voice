# ESP32-C3 声纹唤醒系统

> **版本**: v1.3 | **平台**: ESP32-C3-DevKitM-1 | **框架**: ESP-IDF v5.4

基于 ESP32-C3 的本地声纹唤醒方案，实现"只认主人"的语音唤醒。
KWS (唤醒词检测) + SV (声纹验证) 级联架构，
384KB RAM 约束下通过动态模型加载实现声纹验证。

## 快速开始

### 环境要求

- ESP-IDF v5.4+
- Python 3.12+
- Docker (可选，已配置 devcontainer)

### 编译 & 烧录

```bash
cd esp32c3-voice-wake
idf.py set-target esp32c3
idf.py add-dependency "espressif/esp-tflite-micro"
idf.py add-dependency "espressif/led_strip"
idf.py build
idf.py flash monitor
```

### 放置模型文件

将 `model_kws.tflite` 和 `model_sv.tflite` 放入 `models/` 分区：

```bash
# 创建 models 目录并烧录
mkdir -p models
cp /path/to/model_kws.tflite models/
cp /path/to/model_sv.tflite models/

# 使用 partition table 工具的 fatfsgen 创建镜像
# 或使用 idf.py storage-flash
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
| WS | GPIO4 | |
| BCLK | GPIO5 | < 5cm |
| DOUT | GPIO6 | |
| L/R | GND | 不可悬空 |

## 项目结构

```
esp32c3-voice-wake/
├── main/                  # 入口 + CMake
├── config/                # pin_defs.h, sys_config.h
├── components/
│   ├── audio/             # I2S 驱动 + 环形缓冲区
│   ├── frontend/          # MFCC/Spectrogram 前端
│   ├── kws/               # KWS 唤醒词引擎 (TFLite)
│   ├── sv/                # SV 声纹验证引擎 (动态加载)
│   ├── decision/          # FSM 状态机 + LED + 注册
│   └── comm/              # UART JSON 事件上报
├── partitions.csv         # 分区表
├── sdkconfig.defaults     # SDK 默认配置
└── TASK_STATUS.md         # 开发任务状态
```

## 许可证

[待补充]
