/*
 * audio_capture.h — I2S 音频采集模块接口
 *
 * 驱动 INMP441 MEMS 麦克风，通过 I2S 接口采集 16kHz/16bit 单声道 PCM，
 * 写入线程安全环形缓冲区，以事件驱动方式通知下游 KWS 任务。
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 初始化 ---- */

/**
 * @brief 初始化 I2S 驱动并创建环形缓冲区
 *
 * 配置 I2S STD 模式：16kHz / 16-bit / 单声道
 * 引脚：WS=GPIO4, BCLK=GPIO5, DOUT=GPIO6
 * 分配 DMA 缓冲区并启动持续采集
 *
 * @return ESP_OK 成功，否则失败
 */
esp_err_t audio_capture_init(void);

/**
 * @brief 获取一次 I2S 采集数据（事件驱动回调）
 *
 * 由 I2S 事件回调调用，将 DMA 缓冲数据写入环形缓冲区
 * 每次累积 100ms 等价数据时通知事件组
 *
 * @param src     I2S DMA 缓冲区指针
 * @param size    字节数
 * @return ESP_OK 成功
 */
esp_err_t audio_capture_feed(const uint8_t *src, size_t size);

/* ---- 环形缓冲区访问 ---- */

/**
 * @brief 从环形缓冲区读取最近 N 个字节的音频数据
 *
 * 线程安全 — 可由 KWS/SV 任务在任意上下文调用
 *
 * @param dst     目标缓冲区
 * @param size    要读取的字节数
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 数据不足
 */
esp_err_t audio_ringbuf_read(uint8_t *dst, size_t size);

/**
 * @brief 获取环形缓冲区中当前可读字节数
 */
size_t audio_ringbuf_available(void);

/**
 * @brief 获取环形缓冲区水位（已用 / 总量，百分比 0..100）
 */
uint8_t audio_ringbuf_watermark(void);

#ifdef __cplusplus
}
#endif
