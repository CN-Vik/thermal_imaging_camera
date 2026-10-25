#pragma once

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_COLOR_WHITE_HOT = 0,  // 低温黑 -> 高温白
    IR_COLOR_BLACK_HOT,      // 低温白 -> 高温黑
    IR_COLOR_RAINBOW,        // 蓝 -> 青 -> 绿 -> 黄 -> 红
    IR_COLOR_IRONBOW         // 铁红伪彩（默认）
} ir_color_mode_t;

uint16_t ir_temp_to_white_hot_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_black_hot_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_rainbow_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_ironbow_rgb565(float val, float min_t, float max_t);

// 统一入口：根据 mode 选择颜色映射
uint16_t ir_temp_to_rgb565(ir_color_mode_t mode, float val,
                           float min_t, float max_t);

// 生成 256 级 RGB565 颜色查找表（供双线性插值快速查表使用）
void ir_color_generate_lut(ir_color_mode_t mode, uint16_t *lut);

#ifdef __cplusplus
}
#endif