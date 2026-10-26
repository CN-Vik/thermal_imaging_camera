#include "lang.h"

extern const lv_font_t app_font_zh;
extern const lv_font_t app_font_ja;
extern const lv_font_t app_font_ko;
extern const lv_font_t app_font_ru;

static const char * const s_lang_names[LANG_COUNT] = {
    "中文",
    "English",
    "Русский",
    "日本語",
    "한국어",
};

static const char * const s_str_table[STR_COUNT][LANG_COUNT] = {
    { "选择语言", "Select Language", "Выберите язык", "言語を選択", "언어 선택" },
    { "模式配置", "Mode Config", "Настройки", "モード設定", "모드 설정" },
    { "色彩模式", "Color Mode", "Цветовой режим", "カラーモード", "컬러 모드" },
    { "关机", "Power Off", "Выключение", "電源オフ", "전원 끄기" },
    { "返回", "Back", "Назад", "戻る", "뒤로" },
    { "确定", "OK", "OK", "決定", "확인" },
    { "电源", "Power", "Питание", "電源", "전원" },
    { "选择: %s", "Select: %s", "Выбор: %s", "選択: %s", "선택: %s" },
    { "传感器校准中...", "Calibrating Sensor...", "Калибровка...", "センサー較正中...", "센서 보정 중..." },
    { "语言", "Language", "Язык", "言語", "언어" },
    { "拍照", "Capture", "Снимок", "撮影", "촬영" },
    { "相册", "Gallery", "Галерея", "アルバム", "앨범" },
    { "删除", "Delete", "Удалить", "削除", "삭제" },
    { "全部删除", "Delete All", "Удалить всё", "すべて削除", "전체 삭제" },
    { "已保存", "Saved", "Сохранено", "保存済み", "저장됨" },
    { "暂无照片", "No Photos", "Нет фото", "写真なし", "사진 없음" },
    { "关闭", "Close", "Закрыть", "閉じる", "닫기" },
    { "照片", "Photo", "Фото", "写真", "사진" },
    { "存储已满", "Storage Full", "Нет места", "容量不足", "저장 공간 없음" },
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
