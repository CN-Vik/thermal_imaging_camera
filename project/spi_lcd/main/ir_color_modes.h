#pragma once

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_COLOR_WHITE_HOT = 0,  // 1. 白热：低温黑 -> 高温白
    IR_COLOR_BLACK_HOT,      // 2. 黑热：低温白 -> 高温黑
    IR_COLOR_RAINBOW,        // 3. 彩虹：暗蓝 -> 青 -> 绿 -> 黄 -> 红
    IR_COLOR_IRONBOW,        // 4. 铁红：黑紫 -> 红 -> 橙黄 -> 白
    IR_COLOR_RED_HOT,        // 5. 高热红：黑白灰底 + 白光包边 + 鲜红热源核
    IR_COLOR_LAVA,           // 6. 熔岩：深黑 -> 栗红 -> 鲜橙 -> 金黄 -> 亮白
    IR_COLOR_ARCTIC,         // 7. 极地：深蓝 -> 冰青 -> 银白 -> 暖金高光
    IR_COLOR_GREEN_HOT,      // 8. 绿热：纯黑 -> 暗绿 -> 荧光翠绿 -> 亮白绿（夜视风）
    IR_COLOR_COLD_HOT,       // 9. 冷热双警示：低温标蓝 + 中温灰阶 + 高温标红
    IR_COLOR_MODE_MAX        // 模式总数（方便 UI 下拉框或按键循环切换）
} ir_color_mode_t;

uint16_t ir_temp_to_white_hot_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_black_hot_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_rainbow_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_ironbow_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_red_hot_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_lava_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_arctic_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_inferno_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_green_hot_rgb565(float val, float min_t, float max_t);
uint16_t ir_temp_to_cold_hot_rgb565(float val, float min_t, float max_t);

// 统一入口：根据 mode 选择颜色映射
uint16_t ir_temp_to_rgb565(ir_color_mode_t mode, float val,
                           float min_t, float max_t);

// 生成 256 级 RGB565 颜色查找表（供双线性插值快速查表使用）
void ir_color_generate_lut(ir_color_mode_t mode, uint16_t *lut);

// 获取色彩模式名称字符串（供 LVGL UI 标签或菜单显示）
const char *ir_color_get_mode_name(ir_color_mode_t mode);

#ifdef __cplusplus
}
#endif