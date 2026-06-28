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
| 4. KWS 唤醒词引擎 | ✅ 完成 | TFLite Micro (MixedNet) + 100ms 轮询 Task_KWS |
| 5. 决策状态机 + LED | ✅ 完成 | 7状态 FSM + WS2812 6色模式 + 注册 |
| 6. 声纹验证引擎 | ✅ 完成 | x-vector mini (TDNN+StatsPool) + 动态加载 |
| 7. 声纹注册流程 | ✅ 完成 | BOOT键长按 + 5次录制 + NVS存储 |
| 8. 通信上报模块 | ✅ 完成 | UART JSON + OK握手 |
| 9. 集成测试 | ⏳ 待硬件 | 延迟测量 + 准确率 + 压力测试 |

---

## 模块 README

| 模块 | 路径 | 状态 |
|------|------|------|
| 项目入口 | [README.md](README.md) | ✅ |
| 任务状态 | [TASK_STATUS.md](TASK_STATUS.md) | ✅ |
| 技术栈 | [tech-stack.md](tech-stack.md) | ✅ |
| 引脚配置 | [config/pin_defs.h](config/pin_defs.h) | ✅ |
| 系统配置 | [config/sys_config.h](config/sys_config.h) | ✅ |
| 音频采集 | [components/audio/README.md](components/audio/README.md) | ✅ |
| 声学前端 | [components/frontend/README.md](components/frontend/README.md) | ✅ |
| KWS 引擎 | [components/kws/README.md](components/kws/README.md) | ✅ |
| SV 引擎 | [components/sv/README.md](components/sv/README.md) | ✅ |
| 决策状态机 | [components/decision/README.md](components/decision/README.md) | ✅ |
| 通信上报 | [components/comm/README.md](components/comm/README.md) | ✅ |

---

## 文件清单 (56个文件)

```
esp32c3-voice-wake/
├── README.md
├── TASK_STATUS.md
├── tech-stack.md
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── main.c
├── config/
│   ├── CMakeLists.txt
│   ├── pin_defs.h
│   ├── sys_config.h
│   ├── model_loader.h
│   └── model_loader.c
├── tools/
│   └── generate_filterbank.py          # Mel filterbank 预计算脚本
├── components/
│   ├── audio/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── audio_capture.h
│   │   └── audio_capture.c
│   ├── frontend/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── micro_frontend.h
│   │   ├── micro_frontend.c
│   │   ├── mel_filterbank_const.h       # 预计算 filterbank (10KB Flash)
│   │   ├── feature_buffer.h
│   │   ├── feature_buffer.c
│   │   ├── frontend_pipeline.h
│   │   └── frontend_pipeline.c
│   ├── kws/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── kws_engine.h
│   │   ├── kws_engine.c
│   │   ├── task_kws.h
│   │   └── task_kws.c
│   ├── sv/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── sv_engine.h
│   │   ├── sv_engine.c
│   │   ├── task_sv.h
│   │   └── task_sv.c
│   ├── decision/
│   │   ├── CMakeLists.txt
│   │   ├── README.md
│   │   ├── fsm.h
│   │   ├── fsm.c
│   │   ├── led_indicator.h
│   │   ├── led_indicator.c
│   │   ├── enroll.h
│   │   ├── enroll.c
│   │   ├── task_decision.h
│   │   └── task_decision.c
│   └── comm/
│       ├── CMakeLists.txt
│       ├── README.md
│       ├── event_reporter.h
│       └── event_reporter.c
```

---

## 审计修复记录

### 第一轮 (2026-06-25)

| 严重度 | 编号 | 问题 | 状态 |
|--------|------|------|------|
| 🔴 | C1 | sdkconfig ESP32S3→ESP32C3 CPU freq key | ✅ |
| 🔴 | C2 | 定时器回调阻塞 200ms | ✅ 改为独立Task |
| 🔴 | C3 | 定时器回调栈溢出 5.9KB | ✅ 改为独立Task(6KB) |
| 🔴 | C4 | SV resolver static 重复注册 | ✅ 移除static |
| 🔴 | C5 | 模型加载整分区(1MB)→OOM | ✅ FAT+fopen |
| 🟠 | H1 | 任务栈 4×PRD (96KB) | ✅ 降至44KB |
| 🟠 | H2 | KWS 模型内存泄漏 20KB | ✅ 保存+释放 |
| 🟠 | H3 | SV 结果未接入(永远ACCEPTED) | ✅ g_sv_similarity |
| 🟠 | H4 | EVENT_TIMER_100MS 从未设置 | ✅ KWS改为轮询 |
| 🟡 | M1 | 无KWS触发冷却 | ✅ 2s冷却 |
| 🟡 | M2 | 按键死循环风险 | ✅ 10s超时 |
| 🟡 | M3 | 缺少 esp_timer.h | ✅ 补全 |
| 🟡 | M4 | EventGroup 参数错误 | ✅ TRUE→FALSE |

### 第二轮 (2026-06-25)

| 严重度 | 编号 | 问题 | 状态 |
|--------|------|------|------|
| 🔴 | B1 | config/ 头文件 INCLUDE_DIRS 缺失 | ✅ config组件化 |
| 🔴 | C1 | g_sv_similarity 写入顺序错误 | ✅ 移至bypass后 |
| 🔴 | R1 | KWS OpResolver 泄漏 | ✅ s_resolver+deinit |
| 🟠 | B2 | 4组件 REQUIRES 缺失 | ✅ 完整修复 |
| 🟠 | C2 | VERIFYING 状态无超时 | ✅ 3s回IDLE |
| 🟠 | D2 | 无模板行为矛盾 | ✅ 统一ACCEPTED |
| 🟠 | R2 | UART driver 泄漏 | ✅ 错误路径清理 |
| 🟠 | R3 | nvs_commit 未检查 | ✅ 返回值检查 |
| 🟡 | C3 | FSM 多任务访问无保护 | ✅ Mutex |
| 🟡 | R4 | I2S portMAX_DELAY | ✅ 100ms超时 |

### 优化记录

| 日期 | 优化 | 收益 |
|------|------|------|
| 2026-06-25 | Mel filterbank 预计算 Flash 常量 | 释放 41KB SRAM |
| 2026-06-25 | TFLite SQRT 验证 + SV resolver | Stats Pooling 全原生支持 |
| 2026-06-28 | 13维 MFCC 对齐 (固件→模型) | Feature buf 1.95KB, KWS 11.4KB, SV 14.3KB |
| 2026-06-28 | 架构对齐: KWS DS-CNN, SV 1D CNN | 参数量 ~2K(SV), 模型峰值 219KB (余量 43%) |

---

## 模型状态

| 模型 | 架构 | INT8 大小 | 准确率 | 状态 |
|------|------|----------|--------|------|
| KWS | DS-CNN (Conv+DWConv+FC) | 11.4 KB ONNX | 89.29% | 待 ONNX→TFLite |
| SV | 1D CNN (Conv1d×3+GAP+FC) | 14.3 KB ONNX | — | 待 ONNX→TFLite |

转换脚本: `tools/convert_kws.py` / `tools/convert_sv.py` (在 GPU 服务器运行)

## 待完成

| 项目 | 说明 |
|------|------|
| ONNX→TFLite 转换 | 在 GPU 服务器运行 convert_kws.py / convert_sv.py |
| 模型烧录 | .tflite → models_fat/ → FAT 镜像 → Flash |
| 硬件测试 | GPIO 打桩 / 准确率 / 1h 压力测试 |
| SDK 配置 | `idf.py set-target esp32c3` 生成完整 sdkconfig |
