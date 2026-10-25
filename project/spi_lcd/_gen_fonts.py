import os, math
from PIL import Image, ImageDraw, ImageFont

MAIN = r"C:\Users\ufo28\Desktop\vik_project\thermal_imaging_camera\project\spi_lcd\main"
FONTS = r"C:\Windows\Fonts"

# ---- language / strings (single source of truth) ----
LANG_ORDER = ["ZH", "EN", "JA", "KO", "RU"]

STR_KEYS = [
    "LANG_TITLE", "MODE_CONFIG", "COLOR_MODE", "POWER_OFF",
    "BACK", "OK", "POWER", "SELECT_FMT", "CALIBRATING",
]
STRINGS = {
    "LANG_TITLE":  {"ZH": "选择语言", "EN": "Select Language", "JA": "言語を選択", "KO": "언어 선택", "RU": "Выберите язык"},
    "MODE_CONFIG": {"ZH": "模式配置", "EN": "Mode Config",     "JA": "モード設定",   "KO": "모드 설정", "RU": "Настройки"},
    "COLOR_MODE":  {"ZH": "色彩模式", "EN": "Color Mode",     "JA": "カラーモード", "KO": "컬러 모드", "RU": "Цветовой режим"},
    "POWER_OFF":   {"ZH": "关机",     "EN": "Power Off",      "JA": "電源オフ",     "KO": "전원 끄기", "RU": "Выключение"},
    "BACK":        {"ZH": "返回",     "EN": "Back",           "JA": "戻る",         "KO": "뒤로",       "RU": "Назад"},
    "OK":          {"ZH": "确定",     "EN": "OK",             "JA": "決定",         "KO": "확인",       "RU": "OK"},
    "POWER":       {"ZH": "电源",     "EN": "Power",          "JA": "電源",         "KO": "전원",       "RU": "Питание"},
    "SELECT_FMT":  {"ZH": "选择: %s", "EN": "Select: %s",     "JA": "選択: %s",     "KO": "선택: %s",   "RU": "Выбор: %s"},
    "CALIBRATING": {"ZH": "传感器校准中...", "EN": "Calibrating Sensor...", "JA": "センサー較正中...", "KO": "센서 보정 중...", "RU": "Калибровка..."},
}
LANG_NAMES = {"ZH": "中文", "EN": "English", "JA": "日本語", "KO": "한국어", "RU": "Русский"}

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
    adv_w = int(round(adv * 16))
    W = int(math.ceil(adv)) + 8
    H = ascent + descent + 4
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    d.text((4, 2), ch, font=font, fill=255, stroke_width=1, stroke_fill=255)
    bbox = img.getbbox()
    if bbox is None:
        return {"adv_w": adv_w, "box_w": 0, "box_h": 0, "ofs_x": 0, "ofs_y": 0, "bitmap": b""}
    min_x, min_y, max_x, max_y = bbox
    box_w = max_x - min_x
    box_h = max_y - min_y
    ofs_x = min_x - 4
    ofs_y = 2 + ascent - max_y
    region = img.crop((min_x, min_y, max_x, max_y))
    px = region.getdata()
    out = bytearray()
    for y in range(box_h):
        row = []
        for x in range(box_w):
            row.append(px[y * box_w + x] >> 4)
        for i in range(0, box_w, 2):
            hi = row[i]
            lo = row[i + 1] if i + 1 < box_w else 0
            out.append((hi << 4) | lo)
    return {"adv_w": adv_w, "box_w": box_w, "box_h": box_h, "ofs_x": ofs_x, "ofs_y": ofs_y, "bitmap": bytes(out)}

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
    H.append("    LANG_ZH = 0,")
    H.append("    LANG_EN,")
    H.append("    LANG_JA,")
    H.append("    LANG_KO,")
    H.append("    LANG_RU,")
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
