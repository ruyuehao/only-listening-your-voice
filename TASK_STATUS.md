# ESP32-C3 声纹唤醒系统 — 任务状态

> **开发环境**: ESP-IDF v5.4 / ESP32-C3-DevKitM-1  
> **PRD 版本**: v1.3（板载 RGB + BOOT 键复用）

---

## 总体进度

| 阶段 | 状态 | 交付物 |
|------|------|--------|
| 1. 项目骨架搭建 | ✅ 完成 | CMake / 依赖 / 分区表 / 配置头文件 |
| 2. 音频采集模块 | ✅ 完成 | I2S 驱动 + 环形缓冲区 (32KB) |
| 3. 声学前端 | ✅ 完成 | TFLM microfrontend + feature_buffer + pipeline |
| 4. KWS 唤醒词引擎 | ✅ 完成 | TFLite Micro + 事件驱动 Task_KWS |
| 5. 决策状态机 + LED | ✅ 完成 | 7状态 FSM + WS2812 6色模式 |
| 6. 声纹验证引擎 | ✅ 完成 | 动态加载/卸载 + 余弦相似度 |
| 7. 声纹注册流程 | ✅ 完成 | BOOT键长按 + 5次录制 + NVS存储 |
| 8. 通信上报模块 | ✅ 完成 | UART JSON + OK握手 |
| 9. 集成测试 | ⏳ 待硬件验证 | 延迟测量 + 准确率 + 压力测试 |

---

## 模块 README

| 模块 | 路径 | 状态 |
|------|------|------|
| 根任务状态 | TASK_STATUS.md | ✅ |
| 项目入口 | README.md | ✅ |
| 引脚配置 | config/pin_defs.h | ✅ |
| 系统配置 | config/sys_config.h | ✅ |
| 音频采集 | components/audio/README.md | ✅ |
| 声学前端 | components/frontend/README.md | ✅ |
| KWS 引擎 | components/kws/README.md | ✅ |
| SV 引擎 | components/sv/README.md | ✅ |
| 决策状态机 | components/decision/README.md | ✅ |
| 通信上报 | components/comm/README.md | ✅ |

---

## 文件清单 (33个文件)

```
esp32c3-voice-wake/
├── README.md                                    # 项目说明
├── TASK_STATUS.md                               # 本文件
├── CMakeLists.txt                               # 顶层CMake
├── partitions.csv                               # 分区表 (4MB flash)
├── sdkconfig.defaults                           # SDK默认配置
├── main/
│   ├── CMakeLists.txt                           # 主组件CMake
│   ├── main.c                                   # 入口(main) — 启动全流程
│   └── idf_component.yml                        # 组件依赖
├── config/
│   ├── pin_defs.h                               # GPIO引脚定义
│   └── sys_config.h                             # 系统参数 (阈值/栈/优先级)
├── components/
│   ├── audio/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── audio_capture.h                      # I2S驱动API
│   │   └── audio_capture.c                      # I2S+环形缓冲区实现
│   ├── frontend/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── micro_frontend.h                     # TFLM兼容MFCC API
│   │   ├── micro_frontend.c                     # Hann窗/FFT/Mel滤波/PCAN
│   │   ├── feature_buffer.h                     # 滑动窗口缓冲API
│   │   ├── feature_buffer.c                     # 环形特征缓冲实现
│   │   ├── frontend_pipeline.h                  # 10ms定时器流水线API
│   │   └── frontend_pipeline.c                  # 音频→特征 流水线
│   ├── kws/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── kws_engine.h                         # KWS TFLite API
│   │   ├── kws_engine.c                         # 模型加载/推理/INT8转换
│   │   ├── task_kws.h                           # KWS任务API
│   │   └── task_kws.c                           # 事件驱动KWS任务
│   ├── sv/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── sv_engine.h                          # SV TFLite + NVS API
│   │   ├── sv_engine.c                          # 动态加载/Embedding/余弦相似度
│   │   ├── task_sv.h                            # SV任务API
│   │   └── task_sv.c                            # 事件驱动SV任务
│   ├── decision/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── fsm.h                                # 状态机API
│   │   ├── fsm.c                                # 7状态流转实现
│   │   ├── led_indicator.h                      # WS2812 LED API
│   │   ├── led_indicator.c                      # 6色6模式LED驱动
│   │   ├── enroll.h                             # 注册流程API
│   │   ├── enroll.c                             # BOOT键+5次录制+NVS
│   │   ├── task_decision.h                      # 决策任务API
│   │   └── task_decision.c                      # FSM调度+LED刷新+超时
│   └── comm/
│       ├── CMakeLists.txt
│       ├── README.md
│       ├── event_reporter.h                     # UART上报API
│       └── event_reporter.c                     # JSON发送+OK握手
```

---

## 审计修复记录 (2026-06-25)

| 严重度 | 编号 | 问题 | 状态 |
|--------|------|------|------|
| 🔴 CRITICAL | C1 | sdkconfig ESP32S3→ESP32C3 CPU freq key | ✅ |
| 🔴 CRITICAL | C2 | 定时器回调阻塞 200ms | ✅ 改为独立Task |
| 🔴 CRITICAL | C3 | 定时器回调栈溢出 5.9KB | ✅ 改为独立Task(6KB) |
| 🔴 CRITICAL | C4 | SV resolver static 重复注册 | ✅ 移除static |
| 🔴 CRITICAL | C5 | 模型加载整分区(1MB) → OOM | ✅ FAT+fopen 按文件名读 |
| 🟠 HIGH | H1 | 任务栈 4 倍于 PRD (96KB) | ✅ 降至44KB |
| 🟠 HIGH | H2 | KWS 模型内存泄漏 20KB | ✅ 保存+释放 |
| 🟠 HIGH | H3 | SV 结果未接入(永远ACCEPTED) | ✅ g_sv_similarity |
| 🟠 HIGH | H4 | EVENT_TIMER_100MS 从未设置 | ✅ KWS改为轮询 |
| 🟡 MEDIUM | M1 | 无KWS触发冷却 | ✅ 2s冷却 |
| 🟡 MEDIUM | M2 | 按键死循环风险 | ✅ 10s超时 |
| 🟡 MEDIUM | M3 | 缺少 esp_timer.h | ✅ 补全 |
| 🟡 MEDIUM | M4 | EventGroup 参数错误 | ✅ TRUE→FALSE |

| 项目 | 说明 |
|------|------|
| KWS 模型 | `model_kws.tflite` — 需用 micro-wake-word 训练框架生成 |
| SV 模型 | `model_sv.tflite` — 需自定义训练 16维 Embedding 模型 |
| 模型烧录 | 将 .tflite 文件烧入 models 分区 |
| 硬件测试 | GPIO 打桩延迟测量 / 准确率验证 / 1h 压力测试 |
| SDK 配置 | 需在 ESP-IDF 环境中运行 `idf.py set-target esp32c3` 生成完整 sdkconfig |
