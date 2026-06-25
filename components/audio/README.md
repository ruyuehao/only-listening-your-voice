# Audio Capture Module (音频采集模块)

## 概述

驱动 **INMP441 MEMS 麦克风** 通过 I2S 接口采集 16kHz/16bit 单声道 PCM 音频，
写入线程安全环形缓冲区，事件驱动通知下游 KWS 任务。

## 硬件连接

| INMP441 引脚 | ESP32-C3 引脚 | 说明 |
|-------------|--------------|------|
| VDD | 3.3V | 并联 100nF + 10μF 退耦到 GND |
| GND | GND | 2 根杜邦线并联 |
| WS | GPIO4 | 声道选择 |
| BCLK | GPIO5 | 位时钟 (< 5cm) |
| DOUT | GPIO6 | 数据输出 |
| L/R | GND | 接地 = 左声道/单声道 |

## 架构

```
INMP441 → I2S (DMA) → task_audio_capture → RingBuffer (32KB)
                                │
                    每 100ms → EVENT_AUDIO_READY
                                │
                          Task_KWS (事件驱动)
```

## 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 采样率 | 16000 Hz | 16kHz |
| 位深 | 16-bit | I2S 标准格式 (Philips) |
| 声道 | 单声道 (左) | L/R 接地 |
| 环形缓冲区 | 32KB | 容纳 1s 音频 |
| DMA 缓冲 | 1024 采样 | 每帧 ~64ms |
| 通知间隔 | 100ms | EVENT_AUDIO_READY 事件 |

## 依赖

- ESP-IDF I2S STD Driver (v5.x)
- FreeRTOS Ring Buffer

## API

| 函数 | 说明 |
|------|------|
| `audio_capture_init()` | 初始化 I2S + 环形缓冲区 + 启动采集任务 |
| `audio_ringbuf_read(dst, size)` | 线程安全读取最近 N 字节 |
| `audio_ringbuf_available()` | 可读字节数 |
| `audio_ringbuf_watermark()` | 缓冲区水位 (0-100%) |
