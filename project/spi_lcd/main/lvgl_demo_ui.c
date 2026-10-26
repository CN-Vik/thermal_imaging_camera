#include <stdlib.h>
#include <stdio.h>
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "ir_mlx90640_app.h"
#include "ir_color_modes.h"
#include "dev_sys.h"
#include "lang.h"
#include "photo.h"

#define UI_SCREEN_W        240
#define UI_SCREEN_H        280
#define TOUCH_CURSOR_SIZE  30
#define TOUCH_LABEL_W      96
#define TOUCH_LABEL_H      16

static inline lv_coord_t ui_clamp(lv_coord_t v, lv_coord_t lo, lv_coord_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

lv_obj_t *img_thermal = NULL;
lv_obj_t *label_title = NULL;
lv_obj_t *label_fps_float = NULL;
lv_obj_t *label_info = NULL;
lv_obj_t *tag_max = NULL;
lv_obj_t *tag_min = NULL;

static lv_obj_t *touch_cursor = NULL;
static lv_obj_t *touch_label  = NULL;

/* 多级菜单相关控件与状态：
   一级菜单 = 模式配置（色彩模式 / 语言 / 拍照 / 相册 / 关机）
   二级菜单 = 色彩模式调色板 / 语言选择 / 相册 */
static lv_obj_t *s_menu_backdrop   = NULL;   // 全屏半透明遮罩（点空白处关闭）
static lv_obj_t *s_main_menu_panel = NULL;   // 一级菜单面板
static lv_obj_t *s_main_menu_list  = NULL;   // 一级菜单滚动列表
static lv_obj_t *s_palette_panel   = NULL;   // 二级：色彩模式
static lv_obj_t *s_language_panel  = NULL;   // 二级：语言选择
static lv_obj_t *s_gallery_panel   = NULL;   // 二级：相册
static lv_obj_t *s_gallery_list    = NULL;   // 相册滚动列表
static lv_obj_t *s_gallery_title   = NULL;   // 相册标题（含计数）

static lv_obj_t *s_menu_title = NULL;        // 二级标题（SELECT: xxx）
static lv_obj_t *s_list_cont  = NULL;        // 调色板滑动列表容器
static lv_obj_t *s_mode_btns[IR_COLOR_MODE_MAX] = {NULL};
static lv_obj_t *s_mode_chks[IR_COLOR_MODE_MAX] = {NULL};

static ir_color_mode_t s_backup_mode  = IR_COLOR_RED_HOT; // 打开菜单前的模式（供取消恢复）
static ir_color_mode_t s_pending_mode = IR_COLOR_RED_HOT; // 列表中当前点选预览的模式

static lv_obj_t *s_ui_root = NULL;   // 主界面根容器（供语言切换后重建菜单）

/* 照片查看器 + 顶部提示 */
static lv_obj_t *s_viewer_overlay = NULL;
static lv_obj_t *s_viewer_img     = NULL;
static lv_obj_t *s_viewer_title   = NULL;
static uint8_t  *s_photo_buf      = NULL;
static lv_img_dsc_t s_photo_dsc;
static int        s_viewer_idx    = -1;
static lv_point_t s_viewer_swipe_start = {0, 0};

static lv_obj_t *s_toast          = NULL;
static lv_obj_t *s_toast_lbl      = NULL;
static lv_timer_t *s_toast_timer  = NULL;

/* 前向声明（函数间相互调用） */
static void create_settings_menu(lv_obj_t *parent);
static void apply_language_change(void);
static void rebuild_gallery_list(void);
static void open_viewer(int idx);
static void close_viewer(void);
static void show_toast(const char *msg);
static bool menu_click_guard(void);

/* 左右滑动状态：按下记录起点，移动过程中达到阈值立即切换（不依赖松手事件） */
static lv_point_t s_swipe_start = {0, 0};
static bool s_swipe_tracking = false;
static bool s_swipe_fired    = false;
static bool s_swipe_moved    = false;

/* 双击拍照计数 + 长按打开菜单时刻（用于防误触） */
static uint32_t s_tap_count = 0;
static uint32_t s_tap_last_ms = 0;
static uint32_t s_menu_open_ms = 0;

#define COLOR_SWIPE_MIN_DX       40   // 水平移动超过 40px 切换模式
#define COLOR_SWIPE_MOVE_PX      12   // 超过 12px 视为“在滑动”，不再触发长按菜单

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
        lv_label_set_text_fmt(s_menu_title, lang_str(STR_SELECT_FMT), ir_color_get_mode_name(active_mode));
    }
}

/* 一级 / 二级菜单面板切换辅助函数 */
static void menu_hide_all(void)
{
    if (s_main_menu_panel) lv_obj_add_flag(s_main_menu_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_palette_panel)   lv_obj_add_flag(s_palette_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_language_panel)  lv_obj_add_flag(s_language_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_gallery_panel)   lv_obj_add_flag(s_gallery_panel, LV_OBJ_FLAG_HIDDEN);
}

static void menu_show_main(void)
{
    menu_hide_all();
    if (s_main_menu_panel) lv_obj_clear_flag(s_main_menu_panel, LV_OBJ_FLAG_HIDDEN);
}

static void menu_show_palette(void)
{
    menu_hide_all();
    if (s_palette_panel) lv_obj_clear_flag(s_palette_panel, LV_OBJ_FLAG_HIDDEN);
}

static void menu_show_language(void)
{
    menu_hide_all();
    if (s_language_panel) lv_obj_clear_flag(s_language_panel, LV_OBJ_FLAG_HIDDEN);
}

static void menu_show_gallery(void)
{
    menu_hide_all();
    rebuild_gallery_list();
    if (s_gallery_panel) lv_obj_clear_flag(s_gallery_panel, LV_OBJ_FLAG_HIDDEN);
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

/* 2. 点击底部【OK 确认】按钮：正式保存所选颜色并关闭整个菜单 */
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

/* 3. 点击二级菜单底部【BACK 返回】：撤销本次预览并回到一级菜单 */
static void palette_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    // 撤销预览，恢复进入菜单前的颜色模式
    ir_camera_set_color_mode(s_backup_mode);
    if (label_title) {
        lv_label_set_text_fmt(label_title, "MODE: %s", ir_color_get_mode_name(s_backup_mode));
    }
    menu_show_main();
}

/* 4. 点击背景遮罩：无论在哪一级，都恢复原颜色模式并关闭整个菜单 */
static void menu_backdrop_cancel_cb(lv_event_t *e)
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

/* 5. 一级菜单点击【COLOR MODE】：进入二级调色板菜单 */
static void main_menu_color_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;

    s_pending_mode = s_backup_mode;
    update_menu_active_style(s_pending_mode);

    // 自动把当前选中的模式滚动到列表视野中央
    if (s_mode_btns[s_pending_mode]) {
        lv_obj_scroll_to_view(s_mode_btns[s_pending_mode], LV_ANIM_OFF);
    }

    menu_show_palette();
}

/* ================= 通用 UI 构建辅助 ================= */

/* 创建居中主面板 */
static lv_obj_t *create_panel(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, w, h);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101318), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 16, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    return panel;
}

/* 创建面板顶部标题 */
static lv_obj_t *create_title(lv_obj_t *panel, const char *text)
{
    lv_obj_t *t = lv_label_create(panel);
    lv_obj_set_style_text_color(t, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(t, lang_font(), LV_PART_MAIN);
    lv_label_set_text(t, text);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
    return t;
}

/* 创建面板内滚动列表容器 */
static lv_obj_t *create_list_container(lv_obj_t *panel, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *cont = lv_obj_create(panel);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, w, h);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(cont, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_radius(cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(cont, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_ACTIVE);
    return cont;
}

/* 创建列表条目按钮（按下抬起 CLICKED 触发），red=纯红、arrow=右侧 ">" */
static lv_obj_t *create_list_entry(lv_obj_t *parent, const char *text, bool red, bool arrow,
                                   lv_event_cb_t cb, void *user_data, lv_obj_t **label_out)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 184, 48);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, red ? lv_color_hex(0xD50000) : lv_color_hex(0x22262E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, red ? LV_OPA_COVER : LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, red ? lv_color_hex(0xFF1744) : lv_color_hex(0x2C3038), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, red ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, red ? lv_color_hex(0xFF5252) : lv_color_hex(0x484E58), LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, lang_font(), LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
    if (label_out) *label_out = lbl;

    if (arrow) {
        lv_obj_t *ind = lv_label_create(btn);
        lv_obj_set_style_text_color(ind, lv_color_hex(0x00E5FF), LV_PART_MAIN);
        lv_obj_set_style_text_font(ind, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(ind, ">");
        lv_obj_align(ind, LV_ALIGN_RIGHT_MID, -16, 0);
    }
    return btn;
}

/* ================= 顶部提示 toast ================= */
static void toast_clear_cb(lv_timer_t *t)
{
    (void)t;
    if (s_toast) lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    s_toast_timer = NULL;
}

static void show_toast(const char *msg)
{
    if (!s_toast) return;
    lv_label_set_text(s_toast_lbl, msg);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    if (s_toast_timer) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    s_toast_timer = lv_timer_create(toast_clear_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

/* ================= 拍照 / 相册 / 语言回调 ================= */

static void main_menu_language_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;
    menu_show_language();
}

/* 拍照：保存归一化温度矩阵（压缩存储），相册查看时再渲染（三击屏幕触发） */
static uint8_t s_norm_tmp[PHOTO_NORM_BYTES];

static void do_capture(void)
{
    float mn = 0.0f, mx = 0.0f;
    ir_camera_get_norm_matrix(s_norm_tmp);
    ir_camera_get_snapshot_range(&mn, &mx);
    esp_err_t err = photo_capture(s_norm_tmp, (int)ir_camera_get_color_mode(), mn, mx);
    show_toast((err == ESP_OK) ? lang_str(STR_SAVED) : lang_str(STR_FULL));
}

static void main_menu_gallery_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;
    menu_show_gallery();
}

static void language_item_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    app_lang_t l = (app_lang_t)(uintptr_t)lv_event_get_user_data(e);
    lang_set(l);
    apply_language_change();
}

static void gallery_entry_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    open_viewer(idx);
}

static void gallery_delete_all_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    photo_delete_all();
    rebuild_gallery_list();
}

static void viewer_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    close_viewer();
    menu_show_gallery();
}

static void viewer_delete_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_viewer_idx >= 0) {
        photo_delete(s_viewer_idx);
    }
    close_viewer();
    menu_show_gallery();
}

/* 语言切换异步回调：在 LVGL 下一帧安全删除旧菜单并重建 */
static void apply_language_async_cb(void *user_data)
{
    (void)user_data;
    if (s_menu_backdrop) {
        lv_obj_del(s_menu_backdrop);
        s_menu_backdrop = NULL;
    }
    if (label_info) {
        lv_obj_set_style_text_font(label_info, lang_font(), LV_PART_MAIN);
    }
    create_settings_menu(s_ui_root);
}

/* 语言切换后：用 LVGL 异步回调延迟重建，避免在点击回调里删除/重建对象导致卡死 */
static void apply_language_change(void)
{
    lv_async_call(apply_language_async_cb, NULL);
}

/* ================= 照片查看器 ================= */

static void close_viewer(void)
{
    if (s_viewer_overlay) lv_obj_add_flag(s_viewer_overlay, LV_OBJ_FLAG_HIDDEN);
    s_viewer_idx = -1;
}

/* 相册查看器：左右滑动切换照片 */
static void viewer_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &s_viewer_swipe_start);
    } else if (code == LV_EVENT_RELEASED) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int dx = p.x - s_viewer_swipe_start.x;
        int dy = p.y - s_viewer_swipe_start.y;
        if (abs(dx) > 40 && abs(dx) > abs(dy)) {
            int n = photo_count();
            if (n > 1) {
                int ni = s_viewer_idx + (dx < 0 ? 1 : -1);
                if (ni < 0) ni = 0;
                if (ni >= n) ni = n - 1;
                if (ni != s_viewer_idx) {
                    open_viewer(ni);
                }
            }
        }
    }
}

static void open_viewer(int idx)
{
    if (!s_photo_buf || !s_viewer_img || !s_viewer_overlay) return;
    int cm = 0;
    if (photo_load(idx, s_norm_tmp, &cm, NULL, NULL) != ESP_OK) return;

    s_viewer_idx = idx;
    ir_camera_render_norm(s_norm_tmp, (ir_color_mode_t)cm, (uint16_t *)s_photo_buf);

    s_photo_dsc.header.always_zero = 0;
    s_photo_dsc.header.w = MLX_INTERP_W;
    s_photo_dsc.header.h = MLX_INTERP_H;
    s_photo_dsc.data_size = MLX_INTERP_PIXELS * sizeof(uint16_t);
    s_photo_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_photo_dsc.data = s_photo_buf;

    lv_img_set_src(s_viewer_img, &s_photo_dsc);
    if (s_viewer_title) {
        lv_label_set_text_fmt(s_viewer_title, "%s %d", lang_str(STR_PHOTO), idx + 1);
    }
    lv_obj_move_foreground(s_viewer_overlay);
    lv_obj_clear_flag(s_viewer_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* ================= 相册列表 ================= */

static void rebuild_gallery_list(void)
{
    if (!s_gallery_list) return;
    lv_obj_clean(s_gallery_list);

    int n = photo_count();
    if (s_gallery_title) {
        lv_label_set_text_fmt(s_gallery_title, "%s: %d", lang_str(STR_GALLERY), n);
    }

    if (n == 0) {
        lv_obj_t *lbl = lv_label_create(s_gallery_list);
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, lang_font(), LV_PART_MAIN);
        lv_label_set_text(lbl, lang_str(STR_NO_PHOTOS));
        lv_obj_center(lbl);
        return;
    }

    for (int i = 0; i < n; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d", lang_str(STR_PHOTO), i + 1);
        create_list_entry(s_gallery_list, buf, false, true, gallery_entry_click_cb,
                          (void *)(uintptr_t)i, NULL);
    }

    create_list_entry(s_gallery_list, lang_str(STR_DELETE_ALL), true, false,
                      gallery_delete_all_cb, NULL, NULL);
}

/* 通用：返回一级菜单 */
static void menu_back_to_main_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    menu_show_main();
}

/* 菜单刚打开后的短时间内忽略点击，避免长按松手时误触第一项进入二级菜单 */
static bool menu_click_guard(void)
{
    return (lv_tick_elaps(s_menu_open_ms) < 400);
}

/* 关机按钮：带防误触保护 */
static void power_button_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;
    power_off_event_cb(e);
}

/* 底部“返回”按钮（用于语言/相册二级面板） */
static lv_obj_t *create_back_button(lv_obj_t *panel, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(panel);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 92, 38);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF5252), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, lang_font(), LV_PART_MAIN);
    lv_label_set_text(lbl, lang_str(STR_BACK));
    lv_obj_center(lbl);
    return btn;
}

/* 创建“多级菜单”：
   一级菜单 = 模式配置（色彩模式 / 语言 / 拍照 / 相册 / 关机）
   二级菜单 = 色彩模式调色板 / 语言选择 / 相册 */
static void create_settings_menu(lv_obj_t *parent)
{
    // 1. 全屏半透明遮罩 (点击外部自动取消)
    s_menu_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(s_menu_backdrop);
    lv_obj_set_size(s_menu_backdrop, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(s_menu_backdrop, 0, 0);
    lv_obj_clear_flag(s_menu_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_menu_backdrop, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_menu_backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_menu_backdrop, LV_OPA_40, LV_PART_MAIN);
    lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_menu_backdrop, menu_backdrop_cancel_cb, LV_EVENT_CLICKED, NULL);

    /* ================= 一级菜单：模式配置 ================= */
    s_main_menu_panel = create_panel(s_menu_backdrop, 216, 264);
    create_title(s_main_menu_panel, lang_str(STR_MODE_CONFIG));
    s_main_menu_list = create_list_container(s_main_menu_panel, 196, 210);

    create_list_entry(s_main_menu_list, lang_str(STR_COLOR_MODE), false, true, main_menu_color_cb, NULL, NULL);
    create_list_entry(s_main_menu_list, lang_str(STR_LANGUAGE),  false, true, main_menu_language_cb, NULL, NULL);
    create_list_entry(s_main_menu_list, lang_str(STR_GALLERY),   false, true, main_menu_gallery_cb, NULL, NULL);
    create_list_entry(s_main_menu_list, lang_str(STR_POWER_OFF), true,  false, power_button_cb, NULL, NULL);

    /* ================= 二级菜单：色彩模式调色板 ================= */
    // 2. 居中主面板 (216x246)
    s_palette_panel = lv_obj_create(s_menu_backdrop);
    lv_obj_remove_style_all(s_palette_panel);
    lv_obj_set_size(s_palette_panel, 216, 246);
    lv_obj_align(s_palette_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_palette_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_palette_panel, lv_color_hex(0x101318), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_palette_panel, LV_OPA_80, LV_PART_MAIN); // 80% 透明度，方便透视预览背景热像
    lv_obj_set_style_border_width(s_palette_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_palette_panel, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_palette_panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(s_palette_panel, 16, LV_PART_MAIN);
    lv_obj_add_flag(s_palette_panel, LV_OBJ_FLAG_CLICKABLE);

    // 3. 顶部当前预览模式标题
    s_menu_title = lv_label_create(s_palette_panel);
    lv_obj_set_style_text_color(s_menu_title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_menu_title, lang_font(), LV_PART_MAIN);
    lv_label_set_text(s_menu_title, lang_str(STR_COLOR_MODE));
    lv_obj_align(s_menu_title, LV_ALIGN_TOP_MID, 0, 10);

    // 4. 中间单列纵向可滑动列表容器 (宽 196px，高 152px，一次显示约 3.5 行)
    s_list_cont = lv_obj_create(s_palette_panel);
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

    // 5. 底部左侧【BACK 返回一级菜单】大按钮 (92x38)
    lv_obj_t *btn_back = lv_obj_create(s_palette_panel);
    lv_obj_remove_style_all(btn_back);
    lv_obj_set_size(btn_back, 92, 38);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x2C3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xFF5252), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_back, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN);
    lv_obj_add_flag(btn_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_back, palette_back_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_obj_set_style_text_color(lbl_back, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_back, lang_font(), LV_PART_MAIN);
    lv_label_set_text(lbl_back, lang_str(STR_BACK));
    lv_obj_center(lbl_back);

    // 6. 底部右侧【OK 确认】大按钮 (92x38)
    lv_obj_t *btn_ok = lv_obj_create(s_palette_panel);
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
    lv_obj_set_style_text_font(lbl_ok, lang_font(), LV_PART_MAIN);
    lv_label_set_text(lbl_ok, lang_str(STR_OK));
    lv_obj_center(lbl_ok);

    /* ================= 二级：语言选择 ================= */
    s_language_panel = create_panel(s_menu_backdrop, 216, 246);
    create_title(s_language_panel, lang_str(STR_LANG_TITLE));
    {
        lv_obj_t *lang_list = create_list_container(s_language_panel, 196, 152);
        const app_lang_t order[LANG_COUNT] = { LANG_ZH, LANG_EN, LANG_RU, LANG_JA, LANG_KO };
        for (int i = 0; i < LANG_COUNT; i++) {
            app_lang_t l = order[i];
            lv_obj_t *btn = lv_obj_create(lang_list);
            lv_obj_remove_style_all(btn);
            lv_obj_set_size(btn, 184, 40);
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x22262E), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btn, LV_OPA_90, LV_PART_MAIN);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x004D61), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x484E58), LV_PART_MAIN);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(btn, language_item_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)l);

            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_font(lbl, lang_font_for(l), LV_PART_MAIN);
            lv_label_set_text(lbl, lang_name(l));
            lv_obj_center(lbl);
        }
    }
    create_back_button(s_language_panel, menu_back_to_main_cb);

    /* ================= 二级：相册 ================= */
    s_gallery_panel = create_panel(s_menu_backdrop, 216, 246);
    s_gallery_title = create_title(s_gallery_panel, lang_str(STR_GALLERY));
    s_gallery_list = create_list_container(s_gallery_panel, 196, 152);
    create_back_button(s_gallery_panel, menu_back_to_main_cb);

    update_menu_active_style(ir_camera_get_color_mode());
    menu_show_main();
    lv_obj_add_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
}

/* 切换到下一个/上一个颜色模式，并同步更新顶部标题 */
static void cycle_color_mode(int direction)
{
    int mode = (int)ir_camera_get_color_mode();
    mode = (mode + direction + IR_COLOR_MODE_MAX) % IR_COLOR_MODE_MAX;

    ir_color_mode_t next_mode = (ir_color_mode_t)mode;
    ir_camera_set_color_mode(next_mode);

    if (label_title) {
        lv_label_set_text_fmt(label_title, "MODE: %s",
                              ir_color_get_mode_name(next_mode));
    }
}

static void screen_touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    if (s_menu_backdrop && !lv_obj_has_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN)) {
        s_swipe_tracking = false;
        s_swipe_moved = false;
        s_swipe_fired = false;
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* 双击屏幕 → 拍照（菜单未打开时；滑动不算点击） */
    if (code == LV_EVENT_CLICKED) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int dx = p.x - s_swipe_start.x;
        int dy = p.y - s_swipe_start.y;
        if (abs(dx) > COLOR_SWIPE_MOVE_PX || abs(dy) > COLOR_SWIPE_MOVE_PX) {
            return; /* 是滑动，不算点击 */
        }
        uint32_t now = lv_tick_get();
        if (now - s_tap_last_ms < 700) {
            s_tap_count++;
        } else {
            s_tap_count = 1;
        }
        s_tap_last_ms = now;
        if (s_tap_count >= 2) {
            s_tap_count = 0;
            do_capture();
        }
        return;
    }

    /* 记录手指按下位置 */
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &s_swipe_start);
        s_swipe_tracking = true;
        s_swipe_fired = false;
        s_swipe_moved = false;
    }

    // 长按屏幕：弹出“模式配置”一级菜单（如果手指已经移动过，说明是在滑动，不弹菜单）
    if (code == LV_EVENT_LONG_PRESSED) {
        if (s_swipe_moved) return;

        s_swipe_tracking = false;
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);

        if (s_menu_backdrop) {
            s_backup_mode  = ir_camera_get_color_mode();
            s_pending_mode = s_backup_mode;
            update_menu_active_style(s_pending_mode);
            menu_show_main();                      // 长按先打开一级“模式配置”菜单
            s_menu_open_ms = lv_tick_get();        // 记录打开时刻，用于防误触
            lv_obj_clear_flag(s_menu_backdrop, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);

        int dx = p.x - s_swipe_start.x;
        int dy = p.y - s_swipe_start.y;

        if (abs(dx) > COLOR_SWIPE_MOVE_PX || abs(dy) > COLOR_SWIPE_MOVE_PX) {
            s_swipe_moved = true;
        }

        /* 达到阈值立即切换：不依赖松手坐标，快速/斜向滑动也能生效 */
        if (s_swipe_tracking && !s_swipe_fired &&
            abs(dx) >= COLOR_SWIPE_MIN_DX && abs(dx) > abs(dy)) {
            cycle_color_mode(dx < 0 ? +1 : -1);
            s_swipe_fired = true;
        }

        lv_obj_set_pos(touch_cursor,
                       ui_clamp(p.x - TOUCH_CURSOR_SIZE / 2, 0, UI_SCREEN_W - TOUCH_CURSOR_SIZE),
                       ui_clamp(p.y - TOUCH_CURSOR_SIZE / 2, 0, UI_SCREEN_H - TOUCH_CURSOR_SIZE));
        lv_obj_clear_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text_fmt(touch_label, "X:%d Y:%d", p.x, p.y);
        lv_obj_set_pos(touch_label,
                       ui_clamp(p.x + 10, 0, UI_SCREEN_W - TOUCH_LABEL_W),
                       ui_clamp(p.y + 10, 0, UI_SCREEN_H - TOUCH_LABEL_H));
        lv_obj_clear_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_swipe_tracking = false;
        s_swipe_fired = false;
        s_swipe_moved = false;
        lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    /* ================= 关键修复 1：屏幕本身禁止滚动 ================= */
    /* LVGL v8 的 screen 默认带 SCROLLABLE/ELASTIC/MOMENTUM，
       并且 scroll_dir=LV_DIR_ALL、滚动条=AUTO。
       关掉以后，拖动永远不会变成“滚屏”。 */
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_scroll_to(scr, 0, 0, LV_ANIM_OFF);

    /* 兜底：万一还有哪里露出来，露的是黑底而不是白屏 */
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* ================= 关键修复 2：所有 UI 放进等大的根容器 ================= */
    /* 屏幕的直接子对象只有 ui_root（正好 240x280），因此 screen 的滚动范围
       永远是 0；就算内部的气泡/准星超出屏幕，也只影响 ui_root，不影响 scr。 */
    lv_obj_t *ui_root = lv_obj_create(scr);
    lv_obj_remove_style_all(ui_root);
    lv_obj_set_pos(ui_root, 0, 0);
    lv_obj_set_size(ui_root, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_clear_flag(ui_root, LV_OBJ_FLAG_CLICKABLE);      /* 点击穿透到 scr，滑动事件仍在 scr 上处理 */
    lv_obj_clear_flag(ui_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui_root, LV_SCROLLBAR_MODE_OFF);
    s_ui_root = ui_root;

    // 1. 全屏热成像底图
    img_thermal = lv_img_create(ui_root);
    lv_img_set_src(img_thermal, &s_thermal_img_dsc);
    lv_img_set_zoom(img_thermal, 256);
    lv_img_set_antialias(img_thermal, false);
    lv_obj_set_pos(img_thermal, 0, 0);
    lv_obj_set_size(img_thermal, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_clear_flag(img_thermal, LV_OBJ_FLAG_CLICKABLE);

    // 2. 顶部标题胶囊
    lv_obj_t *top_capsule = lv_obj_create(ui_root);
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
    label_fps_float = lv_label_create(ui_root);
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
    lv_obj_t *tag_emissivity = lv_label_create(ui_root);
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
    tag_max = lv_label_create(ui_root);
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

    tag_min = lv_label_create(ui_root);
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
    lv_obj_t *bottom_capsule = lv_obj_create(ui_root);
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
    lv_obj_set_style_text_font(label_info, lang_font(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label_info, lang_str(STR_CALIBRATING));

    // 7. 触摸准星与长按滑动列表菜单
    touch_cursor = lv_obj_create(ui_root);
    lv_obj_remove_style_all(touch_cursor);
    lv_obj_set_size(touch_cursor, TOUCH_CURSOR_SIZE, TOUCH_CURSOR_SIZE);
    lv_obj_set_style_radius(touch_cursor, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(touch_cursor, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(touch_cursor, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(touch_cursor, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(touch_cursor, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(touch_cursor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(touch_cursor, LV_OBJ_FLAG_HIDDEN);

    touch_label = lv_label_create(ui_root);
    lv_obj_set_size(touch_label, TOUCH_LABEL_W, TOUCH_LABEL_H);
    lv_label_set_long_mode(touch_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(touch_label, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(touch_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_clear_flag(touch_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);

    // 8. 弹窗提示（居中，2 秒后消失）
    s_toast = lv_obj_create(ui_root);
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_size(s_toast, 168, 56);
    lv_obj_align(s_toast, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x101318), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_radius(s_toast, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_toast, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(0x00E676), LV_PART_MAIN);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    s_toast_lbl = lv_label_create(s_toast);
    lv_obj_set_style_text_color(s_toast_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_toast_lbl, lang_font(), LV_PART_MAIN);
    lv_label_set_text(s_toast_lbl, "");
    lv_obj_center(s_toast_lbl);

    // 9. 照片查看器全屏
    s_viewer_overlay = lv_obj_create(ui_root);
    lv_obj_remove_style_all(s_viewer_overlay);
    lv_obj_set_size(s_viewer_overlay, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(s_viewer_overlay, 0, 0);
    lv_obj_clear_flag(s_viewer_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_viewer_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_viewer_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_viewer_overlay, LV_OBJ_FLAG_CLICKABLE); /* 吸收点击，防止穿透到底层菜单 */
    lv_obj_add_event_cb(s_viewer_overlay, viewer_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_viewer_overlay, viewer_touch_cb, LV_EVENT_RELEASED, NULL);

    s_viewer_img = lv_img_create(s_viewer_overlay);
    lv_obj_set_pos(s_viewer_img, 0, 0);
    lv_obj_set_size(s_viewer_img, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_clear_flag(s_viewer_img, LV_OBJ_FLAG_CLICKABLE);

    s_viewer_title = lv_label_create(s_viewer_overlay);
    lv_obj_set_style_bg_color(s_viewer_title, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_viewer_title, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_viewer_title, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_viewer_title, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_viewer_title, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_viewer_title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_viewer_title, lang_font(), LV_PART_MAIN);
    lv_label_set_text(s_viewer_title, "");
    lv_obj_align(s_viewer_title, LV_ALIGN_TOP_MID, 0, 6);

    {
        lv_obj_t *vback = lv_obj_create(s_viewer_overlay);
        lv_obj_remove_style_all(vback);
        lv_obj_set_size(vback, 100, 36);
        lv_obj_align(vback, LV_ALIGN_BOTTOM_LEFT, 8, -8);
        lv_obj_set_style_bg_color(vback, lv_color_hex(0x2C3038), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vback, LV_OPA_90, LV_PART_MAIN);
        lv_obj_set_style_radius(vback, 10, LV_PART_MAIN);
        lv_obj_add_flag(vback, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(vback, viewer_back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lb = lv_label_create(vback);
        lv_obj_set_style_text_color(lb, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lb, lang_font(), LV_PART_MAIN);
        lv_label_set_text(lb, lang_str(STR_BACK));
        lv_obj_center(lb);

        lv_obj_t *vdel = lv_obj_create(s_viewer_overlay);
        lv_obj_remove_style_all(vdel);
        lv_obj_set_size(vdel, 100, 36);
        lv_obj_align(vdel, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
        lv_obj_set_style_bg_color(vdel, lv_color_hex(0xD50000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vdel, LV_OPA_90, LV_PART_MAIN);
        lv_obj_set_style_radius(vdel, 10, LV_PART_MAIN);
        lv_obj_add_flag(vdel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(vdel, viewer_delete_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *ld = lv_label_create(vdel);
        lv_obj_set_style_text_color(ld, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(ld, lang_font(), LV_PART_MAIN);
        lv_label_set_text(ld, lang_str(STR_DELETE));
        lv_obj_center(ld);
    }
    lv_obj_add_flag(s_viewer_overlay, LV_OBJ_FLAG_HIDDEN);

    // 10. 分配照片渲染缓冲（优先 PSRAM，用于相册查看时实时渲染）
    s_photo_buf = (uint8_t *)heap_caps_malloc(MLX_INTERP_PIXELS * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    // 11. 分配照片渲染缓冲（优先 PSRAM，用于相册查看时实时渲染）
    s_photo_buf = (uint8_t *)heap_caps_malloc(MLX_INTERP_PIXELS * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_photo_buf) {
        s_photo_buf = (uint8_t *)heap_caps_malloc(MLX_INTERP_PIXELS * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }

    create_settings_menu(ui_root);

    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_touch_event_cb, LV_EVENT_ALL, NULL);
}
