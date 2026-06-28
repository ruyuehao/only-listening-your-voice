/*
 * model_loader.h — 从 FAT 分区加载 .tflite 模型文件
 *
 * 替代直接 esp_partition_read (读整个分区) 的错误做法，
 * 正确挂载 FAT 文件系统后按文件名读取。
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 从 models 分区读取指定 .tflite 文件
 *
 * 自动挂载/卸载 FAT 文件系统，按文件名读取，
 * 返回 heap 分配的 buffer（调用方负责 free）。
 *
 * @param filename  模型文件名 (如 "model_kws.tflite")
 * @param[out] out_size  文件大小
 * @return heap 分配的 buffer，失败返回 NULL
 */
uint8_t *model_loader_read(const char *filename, size_t *out_size);

#ifdef __cplusplus
}
#endif
