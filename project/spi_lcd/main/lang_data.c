#include "lang.h"

extern const lv_font_t app_font_zh;
extern const lv_font_t app_font_ja;
extern const lv_font_t app_font_ko;
extern const lv_font_t app_font_ru;

static const char * const s_lang_names[LANG_COUNT] = {
    "中文",
    "English",
    "日本語",
    "한국어",
    "Русский",
};

static const char * const s_str_table[STR_COUNT][LANG_COUNT] = {
    { "选择语言", "Select Language", "言語を選択", "언어 선택", "Выберите язык" },
    { "模式配置", "Mode Config", "モード設定", "모드 설정", "Настройки" },
    { "色彩模式", "Color Mode", "カラーモード", "컬러 모드", "Цветовой режим" },
    { "关机", "Power Off", "電源オフ", "전원 끄기", "Выключение" },
    { "返回", "Back", "戻る", "뒤로", "Назад" },
    { "确定", "OK", "決定", "확인", "OK" },
    { "电源", "Power", "電源", "전원", "Питание" },
    { "选择: %s", "Select: %s", "選択: %s", "선택: %s", "Выбор: %s" },
    { "传感器校准中...", "Calibrating Sensor...", "センサー較正中...", "센서 보정 중...", "Калибровка..." },
};

const char *lang_name(app_lang_t l) {
    if (l >= LANG_COUNT) l = LANG_EN;
    return s_lang_names[l];
}

const char *lang_str(lang_str_id_t id) {
    app_lang_t l = lang_get();
    if (id >= STR_COUNT) id = 0;
    return s_str_table[id][l];
}

const lv_font_t *lang_font_for(app_lang_t l) {
    switch (l) {
    case LANG_ZH: return &app_font_zh;
    case LANG_JA: return &app_font_ja;
    case LANG_KO: return &app_font_ko;
    case LANG_RU: return &app_font_ru;
    default: return &lv_font_montserrat_14;
    }
}

const lv_font_t *lang_font(void) {
    return lang_font_for(lang_get());
}
