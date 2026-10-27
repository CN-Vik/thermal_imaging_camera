# -*- coding: utf-8 -*-
"""
把 app_font_*.c 的 glyph_bitmap 从"按行字节对齐"重排为 LVGL 需要的"跨行连续"格式，
并重新计算 bitmap_index；同时把 get_glyph_bitmap 改回标准 lv_font_get_bitmap_fmt_txt，
去掉 app_font_get_bitmap_fix_odd_width 依赖。
"""
import re
import sys

def process(path):
    txt = open(path, encoding="utf-8", errors="ignore").read()

    # 1. 旧位图字节
    bm_seg = txt.split("glyph_bitmap[] = {")[1].split("};")[0]
    old_bm = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{2}", bm_seg)]
    print(f"{path}: old bitmap {len(old_bm)} bytes")

    # 2. 旧 glyph_dsc
    dsc_seg = txt.split("glyph_dsc[] = {")[1].split("};")[0]
    dscs = []
    for m in re.finditer(r"\.?bitmap_index = (\d+), \.?adv_w = (\d+), \.?box_w = (\d+), \.?box_h = (\d+), \.?ofs_x = (-?\d+), \.?ofs_y = (-?\d+)", dsc_seg):
        dscs.append(tuple(map(int, m.groups())))
    print(f"  glyph_dsc {len(dscs)} entries")
    assert len(dscs) > 0, "glyph_dsc parse failed!"

    # 3. 重排
    new_bm = bytearray()
    new_dsc_lines = []
    for (bi, aw, bw, bh, ox, oy) in dscs:
        if bw == 0 or bh == 0:
            new_bi = 0  # 空 glyph 仍指向 0（与旧格式一致）
        else:
            row_bytes = (bw + 1) >> 1
            pixels = []
            for y in range(bh):
                for x in range(bw):
                    b = old_bm[bi + y * row_bytes + (x >> 1)]
                    pixels.append((b >> 4) if (x & 1) == 0 else (b & 0x0F))
            gbytes = bytearray()
            for i in range(0, len(pixels), 2):
                hi = pixels[i]
                lo = pixels[i + 1] if i + 1 < len(pixels) else 0
                gbytes.append((hi << 4) | lo)
            new_bi = len(new_bm)
            new_bm += gbytes
        new_dsc_lines.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},"
            % (new_bi, aw, bw, bh, ox, oy)
        )
    print(f"  new bitmap {len(new_bm)} bytes")

    # 4. 写回：替换 glyph_bitmap 数组
    PER = 16
    bm_lines = []
    for i in range(0, len(new_bm), PER):
        bm_lines.append("    " + ", ".join("0x%02x" % b for b in new_bm[i:i + PER]) + ",")
    txt = re.sub(r"static LV_ATTRIBUTE_MEM_ALIGN const uint8_t glyph_bitmap\[\] = \{.*?\};",
                 "static LV_ATTRIBUTE_MEM_ALIGN const uint8_t glyph_bitmap[] = {\n" + "\n".join(bm_lines) + "\n};",
                 txt, count=1, flags=re.S)

    # 5. 替换 glyph_dsc 数组
    txt = re.sub(r"static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = \{.*?\};",
                 "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n" + "\n".join(new_dsc_lines) + "\n};",
                 txt, count=1, flags=re.S)

    # 6. get_glyph_bitmap 改回标准函数，去掉 extern 声明
    txt = txt.replace(".get_glyph_bitmap = app_font_get_bitmap_fix_odd_width,",
                      ".get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    txt = re.sub(r"extern const uint8_t \* app_font_get_bitmap_fix_odd_width\(const lv_font_t \* font, uint32_t unicode_letter\);\n\n", "", txt)

    open(path, "w", encoding="utf-8", newline="\n").write(txt)
    print(f"  done: {path}\n")

for p in ["main/app_font_zh.c", "main/app_font_ja.c", "main/app_font_ko.c", "main/app_font_ru.c"]:
    process(p)
print("ALL DONE")
