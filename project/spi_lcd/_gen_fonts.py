import os, math
from PIL import Image, ImageDraw, ImageFont

MAIN = r"C:\Users\ufo28\Desktop\vik_project\thermal_imaging_camera\project\spi_lcd\main"
FONTS = r"C:\Windows\Fonts"

# ---- language / strings (single source of truth) ----
# 顺序：中英俄日韩
LANG_ORDER = ["ZH", "EN", "RU", "JA", "KO"]

STR_KEYS = [
    "LANG_TITLE", "MODE_CONFIG", "COLOR_MODE", "POWER_OFF",
    "BACK", "OK", "POWER", "SELECT_FMT", "CALIBRATING",
    "LANGUAGE", "CAPTURE", "GALLERY", "DELETE", "DELETE_ALL",
    "SAVED", "NO_PHOTOS", "CLOSE", "PHOTO", "FULL",
    "BASIC_SETTINGS", "ADVANCED_SETTINGS", "OTHER", "COLOR_STYLE",
    "TEMP_LABEL", "CHART", "ROTATE", "MIRROR", "TEMP_SCALE",
    "SCREEN", "FILTER", "EMISSIVITY", "GAMMA", "CALIBRATION",
    "VISIT_WEBPAGE", "FACTORY_RESET", "ABOUT", "REVERSE",
    "BRIGHTNESS", "AUTO_SLEEP", "OFFSET", "RESET_OFFSET",
    "COLDEST", "HOTTEST", "FIXTURE",
    "MAX", "MIN", "FIX", "CENTER", "DT", "MODE",
    "CHART_SOURCE", "WAITING_SAMPLES",
    "VERTICAL", "HORIZONTAL", "RESET_NOW", "CHECK_CONNECTION",
]
STRINGS = {
    "LANG_TITLE":  {"ZH": "选择语言", "EN": "Select Language", "RU": "Выберите язык", "JA": "言語を選択", "KO": "언어 선택"},
    "MODE_CONFIG": {"ZH": "模式配置", "EN": "Mode Config",     "RU": "Настройки",       "JA": "モード設定",   "KO": "모드 설정"},
    "COLOR_MODE":  {"ZH": "色彩模式", "EN": "Color Mode",     "RU": "Цветовой режим", "JA": "カラーモード", "KO": "컬러 모드"},
    "POWER_OFF":   {"ZH": "关机",     "EN": "Power Off",      "RU": "Выключение",     "JA": "電源オフ",     "KO": "전원 끄기"},
    "BACK":        {"ZH": "返回",     "EN": "Back",           "RU": "Назад",          "JA": "戻る",         "KO": "뒤로"},
    "OK":          {"ZH": "确定",     "EN": "OK",             "RU": "OK",             "JA": "決定",         "KO": "확인"},
    "POWER":       {"ZH": "电源",     "EN": "Power",          "RU": "Питание",        "JA": "電源",         "KO": "전원"},
    "SELECT_FMT":  {"ZH": "选择: %s", "EN": "Select: %s",     "RU": "Выбор: %s",      "JA": "選択: %s",     "KO": "선택: %s"},
    "CALIBRATING": {"ZH": "传感器校准中...", "EN": "Calibrating Sensor...", "RU": "Калибровка...", "JA": "センサー較正中...", "KO": "센서 보정 중..."},
    "LANGUAGE":    {"ZH": "语言",     "EN": "Language",       "RU": "Язык",           "JA": "言語",         "KO": "언어"},
    "CAPTURE":     {"ZH": "拍照",     "EN": "Capture",        "RU": "Снимок",         "JA": "撮影",         "KO": "촬영"},
    "GALLERY":     {"ZH": "相册",     "EN": "Gallery",        "RU": "Галерея",        "JA": "アルバム",     "KO": "앨범"},
    "DELETE":      {"ZH": "删除",     "EN": "Delete",         "RU": "Удалить",        "JA": "削除",         "KO": "삭제"},
    "DELETE_ALL":  {"ZH": "全部删除", "EN": "Delete All",     "RU": "Удалить всё",    "JA": "すべて削除",   "KO": "전체 삭제"},
    "SAVED":       {"ZH": "已保存",   "EN": "Saved",          "RU": "Сохранено",      "JA": "保存済み",     "KO": "저장됨"},
    "NO_PHOTOS":   {"ZH": "暂无照片", "EN": "No Photos",      "RU": "Нет фото",       "JA": "写真なし",     "KO": "사진 없음"},
    "CLOSE":       {"ZH": "关闭",     "EN": "Close",          "RU": "Закрыть",        "JA": "閉じる",       "KO": "닫기"},
    "PHOTO":       {"ZH": "照片",     "EN": "Photo",          "RU": "Фото",           "JA": "写真",         "KO": "사진"},
    "FULL":        {"ZH": "存储已满", "EN": "Storage Full",   "RU": "Нет места",      "JA": "容量不足",     "KO": "저장 공간 없음"},
    "BASIC_SETTINGS":    {"ZH": "基本设置", "EN": "Basic settings",    "RU": "Основные",            "JA": "基本設定",       "KO": "기본 설정"},
    "ADVANCED_SETTINGS": {"ZH": "高级设置", "EN": "Advanced settings", "RU": "Дополнительно",      "JA": "詳細設定",       "KO": "고급 설정"},
    "OTHER":             {"ZH": "其他",     "EN": "Other",             "RU": "Прочее",             "JA": "その他",         "KO": "기타"},
    "COLOR_STYLE":       {"ZH": "色彩样式", "EN": "Color style",       "RU": "Цветовой стиль",     "JA": "カラースタイル", "KO": "색상 스타일"},
    "TEMP_LABEL":        {"ZH": "温度标签", "EN": "Temperature label", "RU": "Метки температуры",  "JA": "温度ラベル",     "KO": "온도 라벨"},
    "CHART":             {"ZH": "曲线",     "EN": "Chart",             "RU": "График",             "JA": "グラフ",         "KO": "차트"},
    "ROTATE":            {"ZH": "旋转",     "EN": "Rotate",            "RU": "Поворот",            "JA": "回転",           "KO": "회전"},
    "MIRROR":            {"ZH": "镜像",     "EN": "Mirror",            "RU": "Зеркало",            "JA": "ミラー",         "KO": "미러"},
    "TEMP_SCALE":        {"ZH": "温度单位", "EN": "Temperature scale", "RU": "Шкала температуры",  "JA": "温度単位",       "KO": "온도 단위"},
    "SCREEN":            {"ZH": "屏幕",     "EN": "Screen",            "RU": "Экран",              "JA": "画面",           "KO": "화면"},
    "FILTER":            {"ZH": "滤波",     "EN": "Filter",            "RU": "Фильтр",             "JA": "フィルター",     "KO": "필터"},
    "EMISSIVITY":        {"ZH": "发射率",   "EN": "Emissivity",        "RU": "Излучательность",    "JA": "放射率",         "KO": "방사율"},
    "GAMMA":             {"ZH": "伽马",     "EN": "Gamma",             "RU": "Гамма",              "JA": "ガンマ",         "KO": "감마"},
    "CALIBRATION":       {"ZH": "校准",     "EN": "Calibration",       "RU": "Калибровка",         "JA": "キャリブレーション", "KO": "보정"},
    "VISIT_WEBPAGE":     {"ZH": "访问网页", "EN": "Visit webpage",     "RU": "Веб-страница",       "JA": "ウェブページ",   "KO": "웹페이지"},
    "FACTORY_RESET":     {"ZH": "恢复出厂", "EN": "Factory reset",     "RU": "Сброс настроек",     "JA": "工場出荷時リセット", "KO": "공장 초기화"},
    "ABOUT":             {"ZH": "关于",     "EN": "About",             "RU": "О программе",        "JA": "情報",           "KO": "정보"},
    "REVERSE":           {"ZH": "反色",     "EN": "Reverse",           "RU": "Инвертировать",      "JA": "反転",           "KO": "반전"},
    "BRIGHTNESS":        {"ZH": "亮度",     "EN": "Brightness",        "RU": "Яркость",            "JA": "明るさ",         "KO": "밝기"},
    "AUTO_SLEEP":        {"ZH": "自动休眠", "EN": "Auto sleep",        "RU": "Авто-сон",           "JA": "自動スリープ",   "KO": "자동 절전"},
    "OFFSET":            {"ZH": "偏移",     "EN": "Offset",            "RU": "Смещение",           "JA": "オフセット",     "KO": "오프셋"},
    "RESET_OFFSET":      {"ZH": "重置偏移", "EN": "Reset offset",      "RU": "Сброс смещения",     "JA": "オフセットをリセット", "KO": "오프셋 초기화"},
    "COLDEST":           {"ZH": "最低温",   "EN": "Coldest",           "RU": "Самая низкая",       "JA": "最低",           "KO": "최저"},
    "HOTTEST":           {"ZH": "最高温",   "EN": "Hottest",           "RU": "Самая высокая",      "JA": "最高",           "KO": "최고"},
    "FIXTURE":           {"ZH": "定点",     "EN": "Fixture",           "RU": "Точка",              "JA": "定点",           "KO": "고정점"},
    "MAX":               {"ZH": "最高",     "EN": "MAX",               "RU": "МАКС",               "JA": "最高",           "KO": "최고"},
    "MIN":               {"ZH": "最低",     "EN": "MIN",               "RU": "МИН",                "JA": "最低",           "KO": "최저"},
    "FIX":               {"ZH": "定点",     "EN": "FIX",               "RU": "ФИКС",               "JA": "定点",           "KO": "고정"},
    "CENTER":            {"ZH": "中心",     "EN": "CEN",               "RU": "ЦЕНТР",              "JA": "中心",           "KO": "중심"},
    "DT":                {"ZH": "温差",     "EN": "DT",                "RU": "ΔТ",                 "JA": "温度差",         "KO": "온도차"},
    "MODE":              {"ZH": "模式",     "EN": "MODE",              "RU": "РЕЖИМ",              "JA": "モード",         "KO": "모드"},
    "CHART_SOURCE":      {"ZH": "曲线来源", "EN": "Chart source",      "RU": "Источник графика",   "JA": "グラフソース",   "KO": "차트 소스"},
    "WAITING_SAMPLES":   {"ZH": "等待采样中", "EN": "Waiting for samples", "RU": "Ожидание данных", "JA": "サンプル待ち",  "KO": "샘플 대기 중"},
    "VERTICAL":          {"ZH": "垂直",     "EN": "Vertical",            "RU": "Вертикально",        "JA": "垂直",           "KO": "세로"},
    "HORIZONTAL":        {"ZH": "水平",     "EN": "Horizontal",          "RU": "Горизонтально",      "JA": "水平",           "KO": "가로"},
    "RESET_NOW":         {"ZH": "立即重置", "EN": "Reset now",           "RU": "Сбросить",           "JA": "今すぐリセット", "KO": "지금 초기화"},
    "CHECK_CONNECTION":  {"ZH": "检查连接", "EN": "Check connection",    "RU": "Проверить связь",    "JA": "接続確認",       "KO": "연결 확인"},
}
LANG_NAMES = {"ZH": "中文", "EN": "English", "RU": "Русский", "JA": "日本語", "KO": "한국어"}

# fonts to generate: lang -> (ttf path, index, size)
FONT_SPECS = {
    "ZH": (os.path.join(FONTS, "simsun.ttc"), 0, 16),
    "JA": (os.path.join(FONTS, "msgothic.ttc"), 0, 16),
    "KO": (os.path.join(FONTS, "malgun.ttf"), 0, 16),
    "RU": (os.path.join(FONTS, "arial.ttf"), 0, 16),
}

def collect_chars(lang):
    chars = set()
    chars.add(" ")  # always include space
    for k in STR_KEYS:
        for ch in STRINGS[k][lang]:
            chars.add(ch)
    for ch in LANG_NAMES[lang]:
        chars.add(ch)
    return chars

def render_glyph(font, ch):
    ascent, descent = font.getmetrics()
    adv = font.getlength(ch)
    W = int(math.ceil(adv)) + 8
    H = ascent + descent + 4
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    # 不使用描边：描边会让墨迹超出字形步进，相邻字母互相压叠（看起来像 "Bacck"）
    d.text((4, 2), ch, font=font, fill=255)
    bbox = img.getbbox()
    if bbox is None:
        return {"adv_w": int(round(adv * 16)), "box_w": 0, "box_h": 0,
                "ofs_x": 0, "ofs_y": 0, "bitmap": b""}
    min_x, min_y, max_x, max_y = bbox
    box_w = max_x - min_x
    box_h = max_y - min_y
    ofs_x = min_x - 4
    ofs_y = 2 + ascent - max_y
    # 步进必须能容纳整块墨迹，否则相邻字符会重叠
    adv_px = max(round(adv), ofs_x + box_w)
    adv_w = int(adv_px * 16)
    region = img.crop((min_x, min_y, max_x, max_y))
    px = region.tobytes()  # mode "L"，逐行排列，每像素 1 字节
    out = bytearray()
    # LVGL 读取字形位图时行跨度是 box_w * bpp 位（连续位流，行间不补齐到字节）。
    # 4bpp 下若 box_w 为奇数，行跨度就不是 8 的整数倍，按字节对齐打包会错位，
    # 表现为文字出现横线/乱码。这里把 box_w 统一补成偶数，右侧补一列全透明像素。
    row_w = box_w + (box_w & 1)
    for y in range(box_h):
        row = []
        base = y * box_w
        for x in range(box_w):
            row.append(px[base + x] >> 4)
        if row_w != box_w:
            row.append(0)
        for i in range(0, row_w, 2):
            out.append((row[i] << 4) | row[i + 1])
    return {"adv_w": adv_w, "box_w": row_w, "box_h": box_h, "ofs_x": ofs_x,
            "ofs_y": ofs_y, "bitmap": bytes(out)}

def gen_font(lang):
    ttf, idx, size = FONT_SPECS[lang]
    font = ImageFont.truetype(ttf, size, index=idx)
    ascent, descent = font.getmetrics()

    ascii_chars = [chr(c) for c in range(0x20, 0x7F)]  # 0x20..0x7E
    script_chars = sorted(ch for ch in collect_chars(lang) if not (0x20 <= ord(ch) < 0x7F))

    # build glyph list: glyph_dsc[0] reserved, then ASCII, then script
    glyphs = []  # list of dicts
    glyphs.append({"adv_w": 0, "box_w": 0, "box_h": 0, "ofs_x": 0, "ofs_y": 0, "bitmap": b""})  # id 0 reserved
    glyph_chars = ["\0"]
    for ch in ascii_chars:
        glyphs.append(render_glyph(font, ch)); glyph_chars.append(ch)
    for ch in script_chars:
        g = render_glyph(font, ch)
        if g["box_w"] == 0:
            print(f"  WARN {lang}: empty glyph U+{ord(ch):04X} '{ch}'")
        glyphs.append(g); glyph_chars.append(ch)

    # bitmap array
    bitmap = bytearray()
    gdsc_lines = []
    for i, g in enumerate(glyphs):
        bi = len(bitmap)
        bitmap += g["bitmap"]
        gdsc_lines.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},"
            % (bi, g["adv_w"], g["box_w"], g["box_h"], g["ofs_x"], g["ofs_y"])
        )

    ascii_start_id = 1
    script_start_id = 1 + len(ascii_chars)  # = 96

    # script cmap
    script_cps = [ord(ch) for ch in script_chars]
    script_unicode_list = []
    script_cmap = None
    if script_cps:
        rstart = min(script_cps)
        rlen = max(script_cps) - rstart + 1
        script_unicode_list = [cp - rstart for cp in script_cps]
        script_cmap = (rstart, rlen, script_start_id, script_unicode_list)

    name = "app_font_" + lang.lower()
    guard = name.upper()

    L = []
    L.append("/* Auto-generated LVGL 8.3 font: %s (%s) */" % (name, lang))
    L.append("#include \"lvgl.h\"")
    L.append("")
    L.append("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
    L.append("#define LV_ATTRIBUTE_MEM_ALIGN")
    L.append("#endif")
    L.append("")
    L.append("static LV_ATTRIBUTE_MEM_ALIGN const uint8_t glyph_bitmap[] = {")
    for i in range(0, len(bitmap), 16):
        L.append("    " + ", ".join("0x%02x" % b for b in bitmap[i:i+16]) + ",")
    L.append("};")
    L.append("")
    L.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    for line in gdsc_lines:
        L.append(line)
    L.append("};")
    L.append("")
    if script_cmap:
        L.append("static const uint16_t unicode_list_1[] = {")
        for i in range(0, len(script_unicode_list), 8):
            L.append("    " + ", ".join("0x%x" % v for v in script_unicode_list[i:i+8]) + ",")
        L.append("};")
        L.append("")
    L.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    L.append("    {")
    L.append("        .range_start = 32, .range_length = %d, .glyph_id_start = %d," % (len(ascii_chars), ascii_start_id))
    L.append("        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY")
    L.append("    },")
    if script_cmap:
        L.append("    {")
        L.append("        .range_start = %d, .range_length = %d, .glyph_id_start = %d," % (script_cmap[0], script_cmap[1], script_cmap[2]))
        L.append("        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = %d, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY" % len(script_unicode_list))
        L.append("    },")
    L.append("};")
    L.append("")
    L.append("static lv_font_fmt_txt_dsc_t font_dsc = {")
    L.append("    .glyph_bitmap = glyph_bitmap,")
    L.append("    .glyph_dsc = glyph_dsc,")
    L.append("    .cmaps = cmaps,")
    L.append("    .kern_dsc = NULL,")
    L.append("    .kern_scale = 16,")
    L.append("    .cmap_num = %d," % (2 if script_cmap else 1))
    L.append("    .bpp = 4,")
    L.append("    .kern_classes = 0,")
    L.append("    .bitmap_format = 0,")
    L.append("    .cache = NULL,")
    L.append("};")
    L.append("")
    L.append("const lv_font_t %s = {" % name)
    L.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    L.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    L.append("    .line_height = %d," % (ascent + descent))
    L.append("    .base_line = %d," % descent)
    L.append("    .subpx = LV_FONT_SUBPX_NONE,")
    L.append("    .underline_position = -1,")
    L.append("    .underline_thickness = 1,")
    L.append("    .dsc = &font_dsc")
    L.append("};")
    L.append("")

    path = os.path.join(MAIN, name + ".c")
    open(path, "w", encoding="utf-8").write("\n".join(L))
    return name, ascent + descent, descent, len(glyphs), len(bitmap)

def gen_lang_header_and_data():
    # lang.h
    H = []
    H.append("#pragma once")
    H.append("#include <stdbool.h>")
    H.append("#include \"esp_err.h\"")
    H.append("#include \"lvgl.h\"")
    H.append("")
    H.append("#ifdef __cplusplus")
    H.append("extern \"C\" {")
    H.append("#endif")
    H.append("")
    H.append("typedef enum {")
    for i, l in enumerate(LANG_ORDER):
        H.append("    LANG_%s%s," % (l, " = 0" if i == 0 else ""))
    H.append("    LANG_COUNT")
    H.append("} app_lang_t;")
    H.append("")
    H.append("typedef enum {")
    for i, k in enumerate(STR_KEYS):
        H.append("    STR_%s%s," % (k, " = 0" if i == 0 else ""))
    H.append("    STR_COUNT")
    H.append("} lang_str_id_t;")
    H.append("")
    H.append("const char *lang_name(app_lang_t l);")
    H.append("const char *lang_str(lang_str_id_t id);")
    H.append("const lv_font_t *lang_font(void);")
    H.append("const lv_font_t *lang_font_for(app_lang_t l);")
    H.append("")
    H.append("esp_err_t lang_init(void);")
    H.append("app_lang_t lang_get(void);")
    H.append("void lang_set(app_lang_t l);")
    H.append("bool lang_configured(void);")
    H.append("")
    H.append("#ifdef __cplusplus")
    H.append("}")
    H.append("#endif")
    H.append("")
    open(os.path.join(MAIN, "lang.h"), "w", encoding="utf-8").write("\n".join(H))

    # lang_data.c
    D = []
    D.append("#include \"lang.h\"")
    D.append("")
    D.append("extern const lv_font_t app_font_zh;")
    D.append("extern const lv_font_t app_font_ja;")
    D.append("extern const lv_font_t app_font_ko;")
    D.append("extern const lv_font_t app_font_ru;")
    D.append("")
    D.append("static const char * const s_lang_names[LANG_COUNT] = {")
    for l in LANG_ORDER:
        D.append("    \"%s\"," % LANG_NAMES[l])
    D.append("};")
    D.append("")
    D.append("static const char * const s_str_table[STR_COUNT][LANG_COUNT] = {")
    for k in STR_KEYS:
        row = ", ".join('"%s"' % STRINGS[k][l] for l in LANG_ORDER)
        D.append("    { %s }," % row)
    D.append("};")
    D.append("")
    D.append("const char *lang_name(app_lang_t l) {")
    D.append("    if (l >= LANG_COUNT) l = LANG_EN;")
    D.append("    return s_lang_names[l];")
    D.append("}")
    D.append("")
    D.append("const char *lang_str(lang_str_id_t id) {")
    D.append("    app_lang_t l = lang_get();")
    D.append("    if (id >= STR_COUNT) id = 0;")
    D.append("    return s_str_table[id][l];")
    D.append("}")
    D.append("")
    D.append("const lv_font_t *lang_font_for(app_lang_t l) {")
    D.append("    switch (l) {")
    D.append("    case LANG_ZH: return &app_font_zh;")
    D.append("    case LANG_JA: return &app_font_ja;")
    D.append("    case LANG_KO: return &app_font_ko;")
    D.append("    case LANG_RU: return &app_font_ru;")
    D.append("    default: return &lv_font_montserrat_14;")
    D.append("    }")
    D.append("}")
    D.append("")
    D.append("const lv_font_t *lang_font(void) {")
    D.append("    return lang_font_for(lang_get());")
    D.append("}")
    D.append("")
    open(os.path.join(MAIN, "lang_data.c"), "w", encoding="utf-8").write("\n".join(D))
    print("wrote lang.h, lang_data.c")

if __name__ == "__main__":
    for l in ["ZH", "JA", "KO", "RU"]:
        n, lh, bl, ng, nb = gen_font(l)
        print("font %s: line_height=%d base_line=%d glyphs=%d bitmap_bytes=%d" % (n, lh, bl, ng, nb))
    gen_lang_header_and_data()
    print("DONE")
