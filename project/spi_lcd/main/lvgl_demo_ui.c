#include "lvgl.h"
#include "ir_mlx90640_app.h"
#include "ir_color_modes.h"

lv_obj_t *img_thermal = NULL;
lv_obj_t *label_title = NULL;
lv_obj_t *label_fps_float = NULL;
lv_obj_t *label_info = NULL;
lv_obj_t *tag_max = NULL;
lv_obj_t *tag_min = NULL;

static lv_obj_t *touch_cursor = NULL;
static lv_obj_t *touch_label  = NULL;

/* 色彩模式弹窗菜单相关控件 */
static lv_obj_t *s_menu_backdrop = NULL;
static lv_obj_t *s_mode_btns[IR_COLOR_MODE_MAX] = {NULL};

/* 每种色彩模式的代表色（用于菜单按钮左侧的色彩指示点） */
static const uint32_t s_mode_dot_hex[IR_COLOR_MODE_MAX] = {
    0xFFFFFF, // IR_COLOR_WHITE_HOT : 亮白
    0x606060, // IR_COLOR_BLACK_HOT : 深灰黑
    0x00E676, // IR_COLOR_RAINBOW   : 彩虹翠绿
    0xFF9100, // IR_COLOR_IRONBOW   : 铁红橙黄
    0xFF1744, // IR_COLOR_RED_HOT   : 高热鲜红
    0xFF3D00, // IR_COLOR_LAVA      : 熔岩赤红
    0x00E5FF, // IR_COLOR_ARCTIC    : 极地冰青
    0x76FF03, // IR_COLOR_GREEN_HOT : 夜视荧光绿
    0x2979FF, // IR_COLOR_COLD_HOT  : 冷热警示蓝
};

/* 刷新菜单中当前选中模式的高亮样式 */
static void update_menu_active_style(ir_color_mode_t active_mode)
{
    for (int i = 0; i < IR_COLOR_MODE_MAX; i++) {
        if (!s_mode_btns[i]) continue;

        if (i == (int)active_mode) {
            lv_obj_set_style_bg_color(s_mode_btns[i], lv_color_hex(0x004D61), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_mode_btns[i], LV_OPA_90, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_mode_btns[i], lv_color_hex(0x00E5FF), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_mode_btns[i], 2, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(s_mode_btns[i], lv_color_hex(0x1E1E1E), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_mode_btns[i], LV_OPA_80, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_mode_btns[i], lv_color_hex(0x555555), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_mode_btns[i], 1, LV_PART_MAIN);
        }
    }
}

/* 点击某个色彩模式按钮的回调 */
static void mode_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    ir_color_mode_t mode = (ir_color_mode_t)(uintptr_t)lv_event_get_user_data(e);

    // 1. 切换热成像颜色查找表
    ir_camera_set_color_mode(mode);

    // 2. 更新按钮高亮状态与顶部标题栏文字
    update_menu_active_style(mode);
    if (label_title) {
        lv_label_set_text_fmt(label_title, "MODE: %s", ir_color_get_mode_name(mode));
    }

    // 3. 隐藏菜单弹窗
    if (s_menu_backdrop) {
        lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 点击半透明背景遮罩或关闭按钮时隐藏菜单 */
static void menu_close_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *cur_target = lv_event_get_current_target(e);

    // 仅当直接点击遮罩外部区域或关闭按钮自身时才关闭，防止点击卡片内部误关
    if (target == cur_target && s_menu_backdrop) {
        lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 创建全屏半透明遮罩与双列色彩模式选择弹窗 */
static void create_color_mode_menu(lv_obj_t *parent)
{
    // 1. 全屏半透明暗色遮罩层 (240x280)
    s_menu_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(s_menu_backdrop);
    lv_obj_set_size(s_menu_backdrop, 240, 280);
    lv_obj_set_pos(s_menu_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_menu_backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_menu_backdrop, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_menu_backdrop, menu_close_event_cb, LV_EVENT_CLICKED, NULL);

    // 2. 居中弹窗面板 (224x248)
    lv_obj_t *panel = lv_obj_create(s_menu_backdrop);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 224, 248);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101216), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 14, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE); // 阻断点击穿透到 backdrop

    // 3. 弹窗标题与右上角关闭按钮
    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(title, "COLOR PALETTE");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

    lv_obj_t *btn_close = lv_obj_create(panel);
    lv_obj_remove_style_all(btn_close);
    lv_obj_set_size(btn_close, 28, 24);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_add_flag(btn_close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_close, menu_close_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_x = lv_label_create(btn_close);
    lv_obj_set_style_text_color(lbl_x, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(lbl_x, "X");
    lv_obj_center(lbl_x);

    // 4. 双列网格容器承载 9 个颜色模式按钮
    lv_obj_t *grid = lv_obj_create(panel);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 204, 204);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 6, LV_PART_MAIN);

    for (int i = 0; i < IR_COLOR_MODE_MAX; i++) {
        lv_obj_t *btn = lv_obj_create(grid);
        lv_obj_remove_style_all(btn);

        // 前 8 个模式每行 2 个 (宽 98px)，第 9 个模式占满整行 (宽 204px)
        int btn_w = (i == IR_COLOR_MODE_MAX - 1 && (IR_COLOR_MODE_MAX % 2 != 0)) ? 204 : 98;
        lv_obj_set_size(btn, btn_w, 34);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, mode_btn_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        // 左侧代表色小圆点
        lv_obj_t *dot = lv_obj_create(btn);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(s_mode_dot_hex[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 8, 0);

        // 模式英文名称
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(lbl, ir_color_get_mode_name((ir_color_mode_t)i));
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 24, 0);

        s_mode_btns[i] = btn;
    }

    // 初始化高亮当前默认模式并隐藏菜单
    update_menu_active_style(ir_camera_get_color_mode());
    lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void screen_touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    // 如果菜单已经打开，不再显示底层十字触摸光标
    if (s_menu_backdrop && !lv_obj_has_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // 长按屏幕：隐藏触摸光标并弹出色彩模式菜单
    if (code == LV_EVENT_LONG_PRESSED) {
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);

        if (s_menu_backdrop) {
            update_menu_active_style(ir_camera_get_color_mode());
            lv_obj_clear_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);

        // 圆形准星中心对齐手指触点
        lv_obj_set_pos(touch_cursor, p.x - 15, p.y - 15);
        lv_obj_clear_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);

        // 浮动坐标文字显示在手指斜上方
        lv_label_set_text_fmt(touch_label, "X:%d Y:%d", p.x, p.y);
        lv_obj_set_pos(touch_label, (p.x > 50) ? (p.x - 30) : 5, (p.y > 30) ? (p.y - 25) : 5);
        lv_obj_clear_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
    } 
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // 1. 全屏底图：热成像图像作为最底层基底铺满整个屏幕 (240x280)
    img_thermal = lv_img_create(scr);
    lv_img_set_src(img_thermal, &s_thermal_img_dsc);
    lv_img_set_zoom(img_thermal, 256);
    lv_img_set_antialias(img_thermal, false);
    lv_obj_set_pos(img_thermal, 0, 0);
    lv_obj_set_size(img_thermal, 240, 280);
    lv_obj_clear_flag(img_thermal, LV_OBJ_FLAG_CLICKABLE); // 允许事件穿透热成像底图

    // 2. 顶部居中半透明 HUD 标题栏
    lv_obj_t *top_capsule = lv_obj_create(scr);
    lv_obj_remove_style_all(top_capsule);
    lv_obj_set_size(top_capsule, 180, 24);
    lv_obj_align(top_capsule, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(top_capsule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_capsule, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_capsule, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(top_capsule, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_style_border_opa(top_capsule, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(top_capsule, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(top_capsule, LV_OBJ_FLAG_CLICKABLE); // 防止阻挡长按事件

    label_title = lv_label_create(top_capsule);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_fmt(label_title, "MODE: %s", ir_color_get_mode_name(ir_camera_get_color_mode()));
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 0);

    // 3. 左上角悬浮帧率小药丸
    label_fps_float = lv_label_create(scr);
    lv_obj_set_style_bg_color(label_fps_float, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label_fps_float, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(label_fps_float, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(label_fps_float, lv_color_make(0, 230, 118), LV_PART_MAIN);
    lv_obj_set_style_border_opa(label_fps_float, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_fps_float, lv_color_make(0, 230, 118), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_fps_float, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(label_fps_float, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(label_fps_float, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(label_fps_float, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_pos(label_fps_float, 8, 36);
    lv_label_set_text(label_fps_float, "-- FPS");

    // 4. 右上角发射率悬浮小药丸
    lv_obj_t *tag_emissivity = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_emissivity, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_emissivity, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(tag_emissivity, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tag_emissivity, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_style_border_opa(tag_emissivity, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_emissivity, lv_color_make(220, 220, 220), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_emissivity, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_emissivity, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_emissivity, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_emissivity, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_align(tag_emissivity, LV_ALIGN_TOP_RIGHT, -8, 36);
    lv_label_set_text(tag_emissivity, "e:0.95");

    // 5. 动态最高温指示气泡
    tag_max = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_max, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_max, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(tag_max, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tag_max, lv_color_make(255, 80, 80), LV_PART_MAIN);
    lv_obj_set_style_border_opa(tag_max, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_max, lv_color_make(255, 80, 80), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_max, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_max, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_max, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_max, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_label_set_text(tag_max, "");

    // 6. 动态最低温指示气泡
    tag_min = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_min, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_min, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(tag_min, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tag_min, lv_color_make(60, 200, 255), LV_PART_MAIN);
    lv_obj_set_style_border_opa(tag_min, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_min, lv_color_make(60, 200, 255), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_min, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_min, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_min, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_min, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_label_set_text(tag_min, "");

    // 7. 底部仪表盘卡片
    lv_obj_t *bottom_capsule = lv_obj_create(scr);
    lv_obj_remove_style_all(bottom_capsule);
    lv_obj_set_size(bottom_capsule, 226, 56);
    lv_obj_align(bottom_capsule, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(bottom_capsule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom_capsule, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom_capsule, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bottom_capsule, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bottom_capsule, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(bottom_capsule, 16, LV_PART_MAIN);
    lv_obj_clear_flag(bottom_capsule, LV_OBJ_FLAG_CLICKABLE); // 防止阻挡长按事件

    label_info = lv_label_create(bottom_capsule);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label_info, "Calibrating Sensor...");

    // 8. 青色高亮圆环光标与坐标文字
    touch_cursor = lv_obj_create(scr);
    lv_obj_remove_style_all(touch_cursor);
    lv_obj_set_size(touch_cursor, 30, 30);
    lv_obj_set_style_radius(touch_cursor, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(touch_cursor, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(touch_cursor, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(touch_cursor, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(touch_cursor, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(touch_cursor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);

    touch_label = lv_label_create(scr);
    lv_obj_set_style_text_color(touch_label, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(touch_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_clear_flag(touch_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);

    // 9. 创建长按弹出的色彩模式菜单（默认隐藏在最顶层）
    create_color_mode_menu(scr);

    // 10. 屏幕对象监听触摸与长按事件
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_touch_event_cb, LV_EVENT_ALL, NULL);
}