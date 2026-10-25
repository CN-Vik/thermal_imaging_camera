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

/* 滑动列表菜单相关控件与状态 */
static lv_obj_t *s_menu_backdrop = NULL;
static lv_obj_t *s_menu_title    = NULL;
static lv_obj_t *s_list_cont     = NULL;
static lv_obj_t *s_mode_btns[IR_COLOR_MODE_MAX] = {NULL};
static lv_obj_t *s_mode_chks[IR_COLOR_MODE_MAX] = {NULL};

static ir_color_mode_t s_backup_mode  = IR_COLOR_RED_HOT; // 打开菜单前的模式（供取消恢复）
static ir_color_mode_t s_pending_mode = IR_COLOR_RED_HOT; // 列表中当前点选预览的模式

static const uint32_t s_mode_dot_hex[IR_COLOR_MODE_MAX] = {
    0xFFFFFF, // White Hot
    0x707070, // Black Hot
    0x00E676, // Rainbow
    0xFF9100, // Ironbow
    0xFF1744, // Red Hot
    0xFF3D00, // Lava
    0x00E5FF, // Arctic
    0x76FF03, // Green Hot
    0x2979FF, // Cold-Hot
};

/* 切换 UI 控件在【实时模式】与【相册模式】下的显隐状态 */


/* 拍照按钮回调 */


/* 进入相册按钮回调 */


/* 返回实时模式按钮回调 */


/* 相册上一张/下一张翻页回调 */


/* 相册删除当前照片回调 */


/* 快速创建半透明胶囊按钮的辅助函数 */


/* 刷新滑动列表中选中项的高亮样式与右侧勾选标记 */
static void update_menu_active_style(ir_color_mode_t active_mode)
{
    for (int i = 0; i < IR_COLOR_MODE_MAX; i++) {
        if (!s_mode_btns[i]) continue;
        if (i == (int)active_mode) {
            lv_obj_set_style_bg_color(s_mode_btns[i], lv_color_hex(0x004D61), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_mode_btns[i], LV_OPA_90, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_mode_btns[i], lv_color_hex(0x00E5FF), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_mode_btns[i], 2, LV_PART_MAIN);
            if (s_mode_chks[i]) lv_label_set_text(s_mode_chks[i], "ON");
        } else {
            lv_obj_set_style_bg_color(s_mode_btns[i], lv_color_hex(0x22262E), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_mode_btns[i], LV_OPA_80, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_mode_btns[i], lv_color_hex(0x484E58), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_mode_btns[i], 1, LV_PART_MAIN);
            if (s_mode_chks[i]) lv_label_set_text(s_mode_chks[i], "");
        }
    }
    if (s_menu_title) {
        lv_label_set_text_fmt(s_menu_title, "SELECT: %s", ir_color_get_mode_name(active_mode));
    }
}

/* 1. 在滑动列表中点击某一行：仅选中并实时预览背景颜色，不关闭弹窗 */
static void mode_item_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    s_pending_mode = (ir_color_mode_t)(uintptr_t)lv_event_get_user_data(e);
    update_menu_active_style(s_pending_mode);

    // 实时更新底层热成像颜色查找表，让用户透过半透明弹窗直接看到效果
    ir_camera_set_color_mode(s_pending_mode);
}

/* 2. 点击底部【OK 确认】按钮：正式保存所选颜色并关闭弹窗 */
static void menu_confirm_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    ir_camera_set_color_mode(s_pending_mode);
    if (label_title) {
        lv_label_set_text_fmt(label_title, "MODE: %s", ir_color_get_mode_name(s_pending_mode));
    }
    if (s_menu_backdrop) {
        lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 3. 点击底部【BACK 取消】或背景遮罩：恢复打开前的原颜色模式并关闭弹窗 */
static void menu_cancel_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *cur_target = lv_event_get_current_target(e);
    if (target != cur_target) return;

    // 恢复进入菜单前的颜色
    ir_camera_set_color_mode(s_backup_mode);
    if (label_title) {
        lv_label_set_text_fmt(label_title, "MODE: %s", ir_color_get_mode_name(s_backup_mode));
    }
    if (s_menu_backdrop) {
        lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 创建“纵向滑动列表 + 底部取消/确认按键”弹窗 */
static void create_color_mode_menu(lv_obj_t *parent)
{
    // 1. 全屏半透明遮罩 (点击外部自动取消)
    s_menu_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(s_menu_backdrop);
    lv_obj_set_size(s_menu_backdrop, 240, 280);
    lv_obj_set_pos(s_menu_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_menu_backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_menu_backdrop, LV_OPA_40, LV_PART_MAIN);
    lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_menu_backdrop, menu_cancel_event_cb, LV_EVENT_CLICKED, NULL);

    // 2. 居中主面板 (216x246)
    lv_obj_t *panel = lv_obj_create(s_menu_backdrop);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 216, 246);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101318), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_80, LV_PART_MAIN); // 80% 透明度，方便透视预览背景热像
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 16, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    // 3. 顶部当前预览模式标题
    s_menu_title = lv_label_create(panel);
    lv_obj_set_style_text_color(s_menu_title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_menu_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(s_menu_title, "SELECT PALETTE");
    lv_obj_align(s_menu_title, LV_ALIGN_TOP_MID, 0, 10);

    // 4. 中间单列纵向可滑动列表容器 (宽 196px，高 152px，一次显示约 3.5 行)
    s_list_cont = lv_obj_create(panel);
    lv_obj_remove_style_all(s_list_cont);
    lv_obj_set_size(s_list_cont, 196, 152);
    lv_obj_align(s_list_cont, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(s_list_cont, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list_cont, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_radius(s_list_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list_cont, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list_cont, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list_cont, LV_SCROLLBAR_MODE_ACTIVE);

    // 往滑动列表中添加 9 个全宽大尺寸选项条 (184x42)
    for (int i = 0; i < IR_COLOR_MODE_MAX; i++) {
        lv_obj_t *btn = lv_obj_create(s_list_cont);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 184, 42);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, mode_item_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        // 左侧颜色指示圆点 (14x14)
        lv_obj_t *dot = lv_obj_create(btn);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(s_mode_dot_hex[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 12, 0);

        // 中间模式名称
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(lbl, ir_color_get_mode_name((ir_color_mode_t)i));
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 36, 0);

        // 右侧选中状态标识 ("ON")
        lv_obj_t *chk = lv_label_create(btn);
        lv_obj_set_style_text_color(chk, lv_color_hex(0x00E5FF), LV_PART_MAIN);
        lv_obj_set_style_text_font(chk, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(chk, "");
        lv_obj_align(chk, LV_ALIGN_RIGHT_MID, -12, 0);

        s_mode_btns[i] = btn;
        s_mode_chks[i] = chk;
    }

    // 5. 底部左侧【BACK 取消】大按钮 (92x38)
    lv_obj_t *btn_cancel = lv_obj_create(panel);
    lv_obj_remove_style_all(btn_cancel);
    lv_obj_set_size(btn_cancel, 92, 38);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x2C3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xFF5252), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_cancel, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 10, LV_PART_MAIN);
    lv_obj_add_flag(btn_cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_cancel, menu_cancel_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_obj_set_style_text_color(lbl_cancel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(lbl_cancel, "BACK");
    lv_obj_center(lbl_cancel);

    // 6. 底部右侧【OK 确认】大按钮 (92x38)
    lv_obj_t *btn_ok = lv_obj_create(panel);
    lv_obj_remove_style_all(btn_ok);
    lv_obj_set_size(btn_ok, 92, 38);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x00695C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_ok, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x00E676), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_ok, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_ok, lv_color_hex(0x00E676), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_ok, 10, LV_PART_MAIN);
    lv_obj_add_flag(btn_ok, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_ok, menu_confirm_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_obj_set_style_text_color(lbl_ok, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_ok, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(lbl_ok, "OK");
    lv_obj_center(lbl_ok);

    update_menu_active_style(ir_camera_get_color_mode());
    lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void screen_touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    if (s_menu_backdrop && !lv_obj_has_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // 长按屏幕：记录当前模式，弹出滑动列表并自动滚动到当前选中项
    if (code == LV_EVENT_LONG_PRESSED) {
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);

        if (s_menu_backdrop) {
            s_backup_mode  = ir_camera_get_color_mode();
            s_pending_mode = s_backup_mode;
            update_menu_active_style(s_pending_mode);
            lv_obj_clear_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);

            // 自动把当前选中的模式滚动到列表视野中央
            if (s_mode_btns[s_pending_mode]) {
                lv_obj_scroll_to_view(s_mode_btns[s_pending_mode], LV_ANIM_OFF);
            }
        }
        return;
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        lv_obj_set_pos(touch_cursor, p.x - 15, p.y - 15);
        lv_obj_clear_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
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

    // 1. 全屏热成像底图
    img_thermal = lv_img_create(scr);
    lv_img_set_src(img_thermal, &s_thermal_img_dsc);
    lv_img_set_zoom(img_thermal, 256);
    lv_img_set_antialias(img_thermal, false);
    lv_obj_set_pos(img_thermal, 0, 0);
    lv_obj_set_size(img_thermal, 240, 280);
    lv_obj_clear_flag(img_thermal, LV_OBJ_FLAG_CLICKABLE);

    // 2. 顶部标题胶囊
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
    lv_obj_clear_flag(top_capsule, LV_OBJ_FLAG_CLICKABLE);

    label_title = lv_label_create(top_capsule);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_fmt(label_title, "MODE: %s", ir_color_get_mode_name(ir_camera_get_color_mode()));
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 0);

    // 3. 左上角帧率小药丸
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

    // 4. 右上角发射率小药丸
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

    // 5. 动态最高温与最低温指示气泡
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

    // 6. 底部仪表盘卡片
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
    lv_obj_clear_flag(bottom_capsule, LV_OBJ_FLAG_CLICKABLE);

    label_info = lv_label_create(bottom_capsule);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label_info, "Calibrating Sensor...");

    // 7. 触摸准星与长按滑动列表菜单
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

    create_color_mode_menu(scr);

    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_touch_event_cb, LV_EVENT_ALL, NULL);
}

