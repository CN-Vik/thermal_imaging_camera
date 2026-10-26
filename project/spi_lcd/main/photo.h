/**
 * @file photo.h
 * @brief 热成像照片存储（压缩：存归一化温度矩阵，查看时再渲染）
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 归一化温度矩阵：32x24，每像素 0-255 */
#define PHOTO_NORM_BYTES (32 * 24)

/* 初始化存储分区 */
esp_err_t photo_init(void);

/* 最大可保存照片数 */
int photo_max(void);

/* 当前已保存照片数 */
int photo_count(void);

/* 保存一帧：norm 为 32x24 归一化温度（768 字节） */
esp_err_t photo_capture(const uint8_t *norm, int color_mode, float min_t, float max_t);

/* 读取第 logical_idx 张照片的归一化矩阵与元数据 */
esp_err_t photo_load(int logical_idx, uint8_t *out_norm, int *out_color_mode,
                     float *out_min_t, float *out_max_t);

/* 删除第 logical_idx 张照片 */
esp_err_t photo_delete(int logical_idx);

/* 删除全部照片 */
esp_err_t photo_delete_all(void);

#ifdef __cplusplus
}
#endif
