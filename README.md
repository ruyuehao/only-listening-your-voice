# ESP32-C3 声纹唤醒系统

> **版本**: v1.4 | **平台**: ESP32-C3-DevKitM-1 (RISC-V 160MHz) | **框架**: ESP-IDF v5.5.4

基于 ESP32-C3 的离线声纹唤醒方案，实现"只认主人"的语音唤醒。
**KWS (DS-CNN) + SV (1D CNN)** 级联架构，384KB SRAM 约束下通过动态模型加载实现声纹验证。

## 架构

```
INMP441 → I2S → Microfrontend (13维 Mel spectrogram) → Feature Buffer (150帧环)
                                                            │
                              ┌─────────────────────────────┴──────────────┐
                              │                                            │
                        KWS (100帧×13)                               SV (40帧×13)
                        DS-CNN Conv→DWConv→FC                   1D CNN Conv1d×3→GAP→FC
                        11.4KB INT8 Flash                        14.3KB INT8 Flash
                        唤醒词置信度 (≥0.85)                        16维 Embedding
                              │                                   (余弦相似度 ≥0.70)
                              └──────────────┬──────────────────────────────┘
                                             │
                                     Decision FSM → WS2812 LED + UART JSON
                                         6任务 FreeRTOS 事件驱动
```

## 快速开始

### 环境要求

- ESP-IDF v5.5+
- Python 3.12+
- ESP32-C3-DevKitM-1 (或兼容板)

### 编译 & 烧录

```bash
cd esp32c3-voice-wake
idf.py set-target esp32c3
idf.py reconfigure    # 自动拉取 managed_components (esp-dsp, esp-tflite-micro, led_strip)
idf.py build
idf.py flash monitor
```

### 放置模型文件

模型文件在 `models_fat/` 目录中，编译时自动打包到 `models` FAT 分区：

```bash
# 确认目录中有模型文件
ls models_fat/
# model_kws.tflite  model_sv.tflite

# 编译会自动生成 FAT 镜像并烧录
idf.py build
```

### 声纹注册

1. 上电 → **绿灯 1Hz 闪烁** (IDLE)
2. **长按 BOOT 键 3 秒** → 蓝灯快闪 (ENROLL_TRIGGER)
3. 连续说 5 次唤醒词，每次间隔约 2 秒
4. 红灯长亮 2 秒 → 自动重启

## 硬件连接

### INMP441 麦克风

| INMP441 | ESP32-C3 | 说明 |
|---------|----------|------|
| VDD | 3.3V | **必须** 并联 100nF + 10μF 退耦电容到 GND，否则 ADC 噪声会严重劣化识别率 |
| GND | GND | 单根杜邦线即可，推荐 < 10cm |
| WS | GPIO4 | 声道选择（L/R 接 GND 时为左声道） |
| BCLK | GPIO5 | 位时钟，杜邦线尽量短（< 5cm 防串扰） |
| DOUT | GPIO6 | 数据输出 |
| L/R | GND | **不可悬空**，直接接 GND 选左声道 |

**建议：**
- VDD 退耦电容要紧靠 INMP441 的 VDD 引脚摆放
- BCLK/DOUT 两根信号线尽量短且远离电源走线
- 如果使用排线和母座，WS/BCLK/DOUT 三根信号线之间可夹一根 GND 线隔离（GND-WS-GND-BCLK-GND-DOUT），减少数字串扰

### 板载外设（无需额外接线）

| 器件 | 引脚 | 说明 |
|------|------|------|
| WS2812 RGB LED | GPIO8 | RMT 驱动，GRB 色序 |
| BOOT 按键 | GPIO9 | 长按 3s → 声纹注册；**上电勿按**（Strapping 引脚，按下会导致无法正常启动） |

### 通信 UART（接 4G / BLE 模组）

| ESP32-C3 | 对端 | 说明 |
|----------|------|------|
| GPIO21 (TX) | → 模组 RX | UART1，波特率在 `components/comm/` 配置 |
| GPIO20 (RX) | ← 模组 TX | 若对端电压为 5V，需加电平转换或分压 |

### 辅助 GPIO（可选调试）

| 引脚 | 用途 | 说明 |
|------|------|------|
| GPIO10 | KWS 推理耗时打桩 | 推理开始时拉高、结束拉低，逻辑分析仪测时间 |
| GPIO3 | SV 推理耗时打桩 | 同上 |

## 项目结构

```
esp32c3-voice-wake/
├── main/                        # 系统入口 (app_main → 初始化 → 创建任务)
├── components/
│   ├── config/                  # 引脚定义 / 系统参数 / FAT 模型加载器
│   ├── audio/                   # I2S 驱动 (INMP441) + RingBuffer (32KB)
│   ├── frontend/                # 声学前端 (Hann+FFT+Mel+PCAN @ 10ms)
│   ├── kws/                     # KWS 引擎 (DS-CNN, TFLite Micro, 常驻64KB)
│   ├── sv/                      # SV 引擎 (1D CNN, 动态加载, 余弦相似度)
│   ├── decision/                # FSM 状态机 + WS2812 LED + BOOT 注册
│   └── comm/                    # UART JSON 事件上报
├── managed_components/          # ESP-IDF 组件管理器依赖
│   ├── espressif/esp-tflite-micro   # TFLite Micro 推理框架 (v1.3.7)
│   ├── espressif/esp-nn             # ESP-NN 内核加速 (v1.2.3)
│   ├── espressif/esp-dsp             # ESP-DSP FFT 加速
│   └── espressif/led_strip          # WS2812 RMT 驱动 (v3.0.3)
├── models_fat/                  # 模型文件 (烧录到 FAT 分区)
│   ├── model_kws.tflite         # DS-CNN 唤醒词模型 (11.4KB)
│   └── model_sv.tflite          # 1D CNN 声纹模型 (14.3KB)
├── config/                      # 软链接 → components/config
├── partitions.csv               # 分区表: nvs/phy_init/factory(2MB)/models(FAT,1MB)
├── sdkconfig.defaults           # SDK 默认配置 (160MHz, 关WiFi/BT, 4MB Flash)
├── CMakeLists.txt               # 顶层 CMake
├── dependencies.lock            # 组件依赖锁
└── tech-stack.md                # 技术栈详解 & 内存布局
```

## FreeRTOS 任务

| 任务 | 优先级 | 栈 | 触发方式 |
|------|--------|-----|---------|
| AudioCapture | 5 | 8KB | I2S DMA 阻塞读 @ 100ms |
| Frontend | 4 | 6KB | 10ms 精确周期 |
| KWS | 3 | 12KB | EVENT_AUDIO_READY, 100ms 轮询 |
| SV | 3 | 10KB | EVENT_KWS_TRIGGERED |
| Decision | 2 | 6KB | 50ms 轮询 + 事件驱动 |
| Enroll | 1 | 6KB | GPIO9 长按 3s |

## 资源占用

| 资源 | 占用 | 总量 | 余量 |
|------|------|------|------|
| SRAM (BSS) | ~69KB | 384KB | — |
| SRAM (峰值 heap) | ~150KB | 384KB | ~165KB (43%) |
| Flash (固件) | ~400KB | 4MB | >3.5MB |
| Flash (模型 FAT) | ~26KB | 1MB | >0.97MB |

## 管理组件依赖

| 组件 | 用途 | 声明位置 |
|------|------|----------|
| `espressif/esp-tflite-micro` | TFLite Micro 推理框架 | `main/idf_component.yml` |
| `espressif/led_strip` | WS2812 LED RMT 驱动 | `main/idf_component.yml` |
| `espressif/esp-dsp` | FFT 加速 (esp-dsp dsps_fft2r) | `main/idf_component.yml` |

## 展望 — 功能扩展方向

### 一、外设控制

SV 识别通过后除亮灯外，可驱动实际设备：

- **电锁/电磁铁**：GPIO 直驱继电器（5V 带光耦隔离），实现"声纹开门"
- **OLED 显示屏**：I2C 挂载 SSD1306，识别通过后显示欢迎语或用户姓名
- **PWM 调光**：GPIO + MOS 管驱动灯带，不同用户对应不同色温/亮度
- **UART 唤醒 4G 模组**：识别事件通过 UART 触发模组上线，推送至阿里云/Home Assistant

### 二、无线联动

当前关 WiFi/BT 以节省 RAM，预留了无线扩展空间：

- **BLE 绑定手机**：打开 BLE 广播，手机 App 配对后接收识别通知 / 远程注册声纹
- **WiFi + MQTT**：识别通过后连路由器，发送 JSON 到自建 MQTT Broker 或天猫精灵/小爱同学
- **ESP-NOW 组网**：低功耗场景下 SV trigger 远端设备（如开灯、开窗帘），无需路由器

### 三、多用户场景

FAT 分区有余量（>0.97 MB），可保存多个声纹模板：

- 注册阶段分配用户 ID，SV 匹配返回"主人 A / 主人 B / 陌生人"
- 不同 ID 触发不同动作：主人→开锁，孩子→推送到家长手机，访客→播报"有客到"
- 配合 UART 上报带用户 ID 的事件 JSON，服务端做细粒度授权

### 四、语音交互闭环

- **语音播报**：I2S 回放（或片上 DAC + NS4168 小功放），识别成功后播放"主人您好"，失败播放"验证失败"
- **KWS 二次唤醒**：开门后进入对讲模式，再次说出唤醒词关闭对讲
- **可定制音库**：PCM 音频以 SPIFFS 或 FAT 分区存储，OTA 远程更新词库

## 文档索引

| 文档 | 说明 |
|------|------|
| [tech-stack.md](tech-stack.md) | 全模块技术栈详情 + 内存布局 |
| [TASK_STATUS.md](TASK_STATUS.md) | 开发进度 + 审计修复记录 |
| [components/audio/README.md](components/audio/README.md) | 音频采集模块 |
| [components/frontend/README.md](components/frontend/README.md) | 声学前端模块 |
| [components/kws/README.md](components/kws/README.md) | KWS 唤醒词引擎 |
| [components/sv/README.md](components/sv/README.md) | SV 声纹验证引擎 |
| [components/decision/README.md](components/decision/README.md) | 决策调度 + LED |
| [components/comm/README.md](components/comm/README.md) | 通信上报模块 |
