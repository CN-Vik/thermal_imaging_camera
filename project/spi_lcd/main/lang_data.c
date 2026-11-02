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
    { "基本设置", "Basic settings", "Основные", "基本設定", "기본 설정" },
    { "高级设置", "Advanced settings", "Дополнительно", "詳細設定", "고급 설정" },
    { "其他", "Other", "Прочее", "その他", "기타" },
    { "色彩样式", "Color style", "Цветовой стиль", "カラースタイル", "색상 스타일" },
    { "温度标签", "Temperature label", "Метки температуры", "温度ラベル", "온도 라벨" },
    { "曲线", "Chart", "График", "グラフ", "차트" },
    { "旋转", "Rotate", "Поворот", "回転", "회전" },
    { "镜像", "Mirror", "Зеркало", "ミラー", "미러" },
    { "温度单位", "Temperature scale", "Шкала температуры", "温度単位", "온도 단위" },
    { "屏幕", "Screen", "Экран", "画面", "화면" },
    { "滤波", "Filter", "Фильтр", "フィルター", "필터" },
    { "发射率", "Emissivity", "Излучательность", "放射率", "방사율" },
    { "伽马", "Gamma", "Гамма", "ガンマ", "감마" },
    { "校准", "Calibration", "Калибровка", "キャリブレーション", "보정" },
    { "访问网页", "Visit webpage", "Веб-страница", "ウェブページ", "웹페이지" },
    { "恢复出厂", "Factory reset", "Сброс настроек", "工場出荷時リセット", "공장 초기화" },
    { "关于", "About", "О программе", "情報", "정보" },
    { "反色", "Reverse", "Инвертировать", "反転", "반전" },
    { "亮度", "Brightness", "Яркость", "明るさ", "밝기" },
    { "自动休眠", "Auto sleep", "Авто-сон", "自動スリープ", "자동 절전" },
    { "偏移", "Offset", "Смещение", "オフセット", "오프셋" },
    { "重置偏移", "Reset offset", "Сброс смещения", "オフセットをリセット", "오프셋 초기화" },
    { "最低温", "Coldest", "Самая низкая", "最低", "최저" },
    { "最高温", "Hottest", "Самая высокая", "最高", "최고" },
    { "定点", "Fixture", "Точка", "定点", "고정점" },
    { "最高", "MAX", "МАКС", "最高", "최고" },
    { "最低", "MIN", "МИН", "最低", "최저" },
    { "定点", "FIX", "ФИКС", "定点", "고정" },
    { "中心", "CEN", "ЦЕНТР", "中心", "중심" },
    { "温差", "DT", "ΔТ", "温度差", "온도차" },
    { "模式", "MODE", "РЕЖИМ", "モード", "모드" },
    { "曲线来源", "Chart source", "Источник графика", "グラフソース", "차트 소스" },
    { "等待采样中", "Waiting for samples", "Ожидание данных", "サンプル待ち", "샘플 대기 중" },
    { "垂直", "Vertical", "Вертикально", "垂直", "세로" },
    { "水平", "Horizontal", "Горизонтально", "水平", "가로" },
    { "立即重置", "Reset now", "Сбросить", "今すぐリセット", "지금 초기화" },
    { "检查连接", "Check connection", "Проверить связь", "接続確認", "연결 확인" },
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
