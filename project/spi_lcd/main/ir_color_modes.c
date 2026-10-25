#include "ir_color_modes.h"

/**
 * 内部归一化与对比度重映射：
 * 在不改动 ir_mlx90640_app.c 原有量程系数的前提下，
 * 消除 [0.00 ~ 0.14] 的背景底噪灰雾，并将手掌所在的 [0.14 ~ 0.82] 拉伸至满量程 [0.0 ~ 1.0]。
 */
static float normalize_temperature(float val, float min_t, float max_t)
{
    if (max_t <= min_t)
    {
        max_t = min_t + 0.1f;
    }

    float norm = (val - min_t) / (max_t - min_t);

    // 针对原版自动量程留白做内部拉伸，使 32~34℃ 手掌能打满色阶顶部
    const float lo_clip = 0.14f;
    const float hi_clip = 0.82f;

    if (norm <= lo_clip)
    {
        return 0.0f;
    }
    if (norm >= hi_clip)
    {
        return 1.0f;
    }

    norm = (norm - lo_clip) / (hi_clip - lo_clip);
    // 轻度 S 曲线增强明暗对比
    return norm * norm * (3.0f - 2.0f * norm);
}

/** 1. 白热：深黑背景 -> 银白手指 -> 亮白掌心 */
uint16_t ir_temp_to_white_hot_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    const uint8_t gray = (uint8_t)(norm * 255.0f + 0.5f);
    return lv_color_make(gray, gray, gray).full;
}

/** 2. 黑热：净白背景 -> 深灰手指 -> 浓黑掌心 */
uint16_t ir_temp_to_black_hot_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    const uint8_t gray = (uint8_t)((1.0f - norm) * 255.0f + 0.5f);
    return lv_color_make(gray, gray, gray).full;
}

/** 3. 彩虹伪彩：暗蓝背景 -> 青绿边缘 -> 黄橙手指 -> 鲜红掌心 */
uint16_t ir_temp_to_rainbow_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.20f)
    { // 暗夜蓝 -> 亮蓝（压暗背景不刺眼）
        float t = norm / 0.20f;
        b = (uint8_t)(30.0f + t * 225.0f);
    }
    else if (norm < 0.45f)
    { // 蓝 -> 青 -> 绿
        float t = (norm - 0.20f) / 0.25f;
        g = (uint8_t)(t * 255.0f);
        b = (uint8_t)((1.0f - t) * 255.0f);
    }
    else if (norm < 0.72f)
    { // 绿 -> 亮黄
        float t = (norm - 0.45f) / 0.27f;
        r = (uint8_t)(t * 255.0f);
        g = 255;
    }
    else
    { // 黄 -> 橙 -> 纯红
        float t = (norm - 0.72f) / 0.28f;
        r = 255;
        g = (uint8_t)((1.0f - t) * 255.0f);
    }

    return lv_color_make(r, g, b).full;
}

/** 4. 铁红伪彩：黑紫 -> 紫红 -> 橙黄 -> 炽白 */
uint16_t ir_temp_to_ironbow_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.25f)
    {
        float t = norm * 4.0f;
        r = (uint8_t)(t * 40.0f);
        g = 0;
        b = (uint8_t)(t * 160.0f);
    }
    else if (norm < 0.50f)
    {
        float t = (norm - 0.25f) * 4.0f;
        r = (uint8_t)(40.0f + t * 180.0f);
        g = 0;
        b = (uint8_t)(160.0f - t * 100.0f);
    }
    else if (norm < 0.75f)
    {
        float t = (norm - 0.50f) * 4.0f;
        r = 255;
        g = (uint8_t)(t * 180.0f);
        b = (uint8_t)(60.0f - t * 60.0f);
    }
    else
    {
        float t = (norm - 0.75f) * 4.0f;
        r = 255;
        g = (uint8_t)(180.0f + t * 75.0f);
        b = (uint8_t)(t * 255.0f);
    }

    return lv_color_make(r, g, b).full;
}

/**
 * 5. 高热红模式：
 *    - 0.00 ~ 0.60: 纯黑 -> 深灰 -> 浅白灰（干净的黑白灰背景与低温轮廓）
 *    - 0.60 ~ 0.70: 提亮至纯白高光环（形成热源外围的白色发光包边）
 *    - 0.70 ~ 1.00: 纯白迅速切入高饱和度鲜红（突出高温热源核心）
 */
uint16_t ir_temp_to_red_hot_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.60f)
    {
        float t = norm / 0.60f;
        uint8_t gray = (uint8_t)(t * 215.0f);
        r = gray;
        g = gray;
        b = gray;
    }
    else if (norm < 0.70f)
    {
        float t = (norm - 0.60f) / 0.10f;
        uint8_t white = (uint8_t)(215.0f + t * 40.0f);
        r = 255;
        g = white;
        b = white;
    }
    else
    {
        float t = (norm - 0.70f) / 0.30f;
        r = 255;
        uint8_t gb = (uint8_t)((1.0f - t) * (1.0f - t) * 255.0f);
        g = gb;
        b = gb;
    }

    return lv_color_make(r, g, b).full;
}

/**
 * 6. 熔岩模式 (Lava)：
 *    - 0.00 ~ 0.33: 纯黑 -> 暗栗红 -> 鲜红（背景深邃无杂色）
 *    - 0.33 ~ 0.70: 鲜红 -> 亮橙 -> 金黄（手指与手掌主体层次丰富）
 *    - 0.70 ~ 1.00: 金黄 -> 炽白高光（掌心高温核心）
 */
uint16_t ir_temp_to_lava_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.33f)
    {
        float t = norm / 0.33f;
        r = (uint8_t)(t * 220.0f);
        g = 0;
        b = 0;
    }
    else if (norm < 0.70f)
    {
        float t = (norm - 0.33f) / 0.37f;
        r = (uint8_t)(220.0f + t * 35.0f);
        g = (uint8_t)(t * 200.0f);
        b = 0;
    }
    else
    {
        float t = (norm - 0.70f) / 0.30f;
        r = 255;
        g = (uint8_t)(200.0f + t * 55.0f);
        b = (uint8_t)(t * 220.0f);
    }

    return lv_color_make(r, g, b).full;
}

/**
 * 7. 极地模式 (Arctic)：
 *    - 0.00 ~ 0.30: 深蓝黑 -> 冰川蓝
 *    - 0.30 ~ 0.65: 冰川蓝 -> 亮青色 -> 银白
 *    - 0.65 ~ 1.00: 银白 -> 暖金黄（冰冷背景中凸显暖色手掌）
 */
uint16_t ir_temp_to_arctic_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.30f)
    {
        float t = norm / 0.30f;
        r = 0;
        g = (uint8_t)(t * 110.0f);
        b = (uint8_t)(25.0f + t * 195.0f);
    }
    else if (norm < 0.65f)
    {
        float t = (norm - 0.30f) / 0.35f;
        r = (uint8_t)(t * 225.0f);
        g = (uint8_t)(110.0f + t * 135.0f);
        b = (uint8_t)(220.0f + t * 35.0f);
    }
    else
    {
        float t = (norm - 0.65f) / 0.35f;
        r = (uint8_t)(225.0f + t * 30.0f);
        g = (uint8_t)(245.0f - t * 25.0f);
        b = (uint8_t)(255.0f * (1.0f - t * 0.85f));
    }

    return lv_color_make(r, g, b).full;
}


/**
 * 9. 绿热夜视模式 (Green Hot)：
 *    - 0.00 ~ 0.25: 纯黑 -> 暗森林绿
 *    - 0.25 ~ 0.72: 暗绿 -> 荧光翠绿（人眼高敏感绿阶，凸显手指纹理）
 *    - 0.72 ~ 1.00: 荧光绿 -> 亮白绿高光（掌心高温泛白）
 */
uint16_t ir_temp_to_green_hot_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.72f)
    {
        float t = norm / 0.72f;
        r = (uint8_t)(t * t * 45.0f);
        g = (uint8_t)(t * 240.0f);
        b = (uint8_t)(t * t * 45.0f);
    }
    else
    {
        float t = (norm - 0.72f) / 0.28f;
        r = (uint8_t)(45.0f + t * 200.0f);
        g = (uint8_t)(240.0f + t * 15.0f);
        b = (uint8_t)(45.0f + t * 200.0f);
    }

    return lv_color_make(r, g, b).full;
}

/**
 * 10. 冷热双警示模式 (Cold-Hot Isotherm)：
 *     - 0.00 ~ 0.18: 低温区标为亮冰蓝 -> 深蓝
 *     - 0.18 ~ 0.72: 中温环境区为干净黑白灰阶 (40 -> 235)
 *     - 0.72 ~ 1.00: 高温区由白切入高饱和鲜红
 */
uint16_t ir_temp_to_cold_hot_rgb565(float val, float min_t, float max_t)
{
    const float norm = normalize_temperature(val, min_t, max_t);
    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.18f)
    {
        // 低温冷点：冰青蓝 -> 宝蓝
        float t = norm / 0.18f;
        r = 0;
        g = (uint8_t)((1.0f - t) * 180.0f);
        b = (uint8_t)(255.0f - t * 90.0f);
    }
    else if (norm < 0.72f)
    {
        // 中间过渡：黑白灰阶
        float t = (norm - 0.18f) / 0.54f;
        uint8_t gray = (uint8_t)(40.0f + t * 205.0f);
        r = gray;
        g = gray;
        b = gray;
    }
    else
    {
        // 高温热点：白光边 -> 鲜红核
        float t = (norm - 0.72f) / 0.28f;
        r = 255;
        uint8_t gb = (uint8_t)((1.0f - t) * (1.0f - t) * 245.0f);
        g = gb;
        b = gb;
    }

    return lv_color_make(r, g, b).full;
}

uint16_t ir_temp_to_rgb565(ir_color_mode_t mode, float val,
                           float min_t, float max_t)
{
    switch (mode)
    {
    case IR_COLOR_WHITE_HOT:
        return ir_temp_to_white_hot_rgb565(val, min_t, max_t);
    case IR_COLOR_BLACK_HOT:
        return ir_temp_to_black_hot_rgb565(val, min_t, max_t);
    case IR_COLOR_RAINBOW:
        return ir_temp_to_rainbow_rgb565(val, min_t, max_t);
    case IR_COLOR_RED_HOT:
        return ir_temp_to_red_hot_rgb565(val, min_t, max_t);
    case IR_COLOR_LAVA:
        return ir_temp_to_lava_rgb565(val, min_t, max_t);
    case IR_COLOR_ARCTIC:
        return ir_temp_to_arctic_rgb565(val, min_t, max_t);
    case IR_COLOR_GREEN_HOT:
        return ir_temp_to_green_hot_rgb565(val, min_t, max_t);
    case IR_COLOR_COLD_HOT:
        return ir_temp_to_cold_hot_rgb565(val, min_t, max_t);
    case IR_COLOR_IRONBOW:
    default:
        return ir_temp_to_ironbow_rgb565(val, min_t, max_t);
    }
}

void ir_color_generate_lut(ir_color_mode_t mode, uint16_t *lut)
{
    if (!lut)
        return;
    for (int i = 0; i < 256; i++)
    {
        lut[i] = ir_temp_to_rgb565(mode, (float)i, 0.0f, 255.0f);
    }
}

const char *ir_color_get_mode_name(ir_color_mode_t mode)
{
    switch (mode)
    {
    case IR_COLOR_WHITE_HOT:
        return "White Hot";
    case IR_COLOR_BLACK_HOT:
        return "Black Hot";
    case IR_COLOR_RAINBOW:
        return "Rainbow";
    case IR_COLOR_IRONBOW:
        return "Ironbow";
    case IR_COLOR_RED_HOT:
        return "Red Hot";
    case IR_COLOR_LAVA:
        return "Lava";
    case IR_COLOR_ARCTIC:
        return "Arctic";
    case IR_COLOR_GREEN_HOT:
        return "Green Hot";
    case IR_COLOR_COLD_HOT:
        return "Cold-Hot";
    default:
        return "Ironbow";
    }
}