#include "ir_color_modes.h"

static float normalize_temperature(float val, float min_t, float max_t)
{
    if (max_t <= min_t) {
        max_t = min_t + 0.1f;
    }

    float norm = (val - min_t) / (max_t - min_t);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return norm;
}

/** 白热：低温纯黑 -> 高温纯白 */
uint16_t ir_temp_to_white_hot_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    const uint8_t gray = (uint8_t)(norm * 255.0f + 0.5f);
    return lv_color_make(gray, gray, gray).full;
}

/** 黑热：低温纯白 -> 高温纯黑 */
uint16_t ir_temp_to_black_hot_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    const uint8_t gray = (uint8_t)((1.0f - norm) * 255.0f + 0.5f);
    return lv_color_make(gray, gray, gray).full;
}

/** 彩虹伪彩：纯蓝 -> 青 -> 绿 -> 黄 -> 纯红 */
uint16_t ir_temp_to_rainbow_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (norm < 0.25f) {             // 蓝 -> 青
        g = (uint8_t)(norm * 4.0f * 255.0f);
        b = 255;
    } else if (norm < 0.5f) {       // 青 -> 绿
        g = 255;
        b = (uint8_t)((1.0f - (norm - 0.25f) * 4.0f) * 255.0f);
    } else if (norm < 0.75f) {      // 绿 -> 黄
        r = (uint8_t)((norm - 0.5f) * 4.0f * 255.0f);
        g = 255;
    } else {                        // 黄 -> 红
        r = 255;
        g = (uint8_t)((1.0f - (norm - 0.75f) * 4.0f) * 255.0f);
    }

    return lv_color_make(r, g, b).full;
}

/** 铁红伪彩：黑紫 -> 红 -> 橙黄 -> 白 */
uint16_t ir_temp_to_ironbow_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (norm < 0.25f) {
        float t = norm * 4.0f;
        r = (uint8_t)(t * 40.0f);
        g = 0;
        b = (uint8_t)(t * 160.0f);
    } else if (norm < 0.50f) {
        float t = (norm - 0.25f) * 4.0f;
        r = (uint8_t)(40.0f + t * 180.0f);
        g = 0;
        b = (uint8_t)(160.0f - t * 100.0f);
    } else if (norm < 0.75f) {
        float t = (norm - 0.50f) * 4.0f;
        r = 255;
        g = (uint8_t)(t * 180.0f);
        b = (uint8_t)(60.0f - t * 60.0f);
    } else {
        float t = (norm - 0.75f) * 4.0f;
        r = 255;
        g = (uint8_t)(180.0f + t * 75.0f);
        b = (uint8_t)(t * 255.0f);
    }

    return lv_color_make(r, g, b).full;
}

uint16_t ir_temp_to_rgb565(ir_color_mode_t mode, float val,
                           float min_t, float max_t)
{
    switch (mode) {
    case IR_COLOR_WHITE_HOT:
        return ir_temp_to_white_hot_rgb565(val, min_t, max_t);
    case IR_COLOR_BLACK_HOT:
        return ir_temp_to_black_hot_rgb565(val, min_t, max_t);
    case IR_COLOR_RAINBOW:
        return ir_temp_to_rainbow_rgb565(val, min_t, max_t);
    case IR_COLOR_IRONBOW:
    default:
        return ir_temp_to_ironbow_rgb565(val, min_t, max_t);
    }
}

void ir_color_generate_lut(ir_color_mode_t mode, uint16_t *lut)
{
    if (!lut) return;
    for (int i = 0; i < 256; i++) {
        lut[i] = ir_temp_to_rgb565(mode, (float)i, 0.0f, 255.0f);
    }
}