# -*- coding: utf-8 -*-
"""诊断 app_font_ru.c：解析 cmap/glyph_dsc/glyph_bitmap，检查俄语字符串每个字符。"""
import re

path = r"main/app_font_ru.c"
txt = open(path, encoding="utf-8", errors="ignore").read()

# glyph_bitmap 字节
bm = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{2}", txt.split("glyph_bitmap[]")[1].split("};")[0])]
print("glyph_bitmap bytes:", len(bm), "nonzero:", sum(1 for b in bm if b))

# glyph_dsc
dsc_txt = txt.split("glyph_dsc[] = {")[1].split("};")[0]
dscs = []
for m in re.finditer(r"bitmap_index = (\d+), adv_w = (\d+), box_w = (\d+), box_h = (\d+), ofs_x = (-?\d+), ofs_y = (-?\d+)", dsc_txt):
    dscs.append(tuple(map(int, m.groups())))
print("glyph_dsc count:", len(dscs))

# cmap 信息
range_start = int(re.search(r"range_start = (\d+)", txt.split("cmaps[]")[1]).group(1))
# unicode_list_1
ul = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", txt.split("unicode_list_1[]")[1].split("};")[0])]
print("script range_start:", range_start, "unicode_list:", ul)

def glyph_id(ch):
    cp = ord(ch)
    if 32 <= cp < 127:
        return cp - 32 + 1  # ascii cmap: range_start=32, glyph_id_start=1
    rcp = cp - range_start
    if rcp in ul:
        return 96 + ul.index(rcp)
    return None

for ch in "Сохранено Русский":
    gid = glyph_id(ch)
    if gid is None or gid >= len(dscs):
        print(f"  U+{ord(ch):04X} '{ch}' -> glyph_id={gid} MISSING")
        continue
    bi, aw, bw, bh, ox, oy = dscs[gid]
    nbytes = (bw * bh + 1) // 2
    seg = bm[bi:bi + nbytes]
    nonzero = sum(1 for b in seg if b)
    print(f"  U+{ord(ch):04X} '{ch}' -> gid={gid} box {bw}x{bh} bitmap@ {bi} bytes={nbytes} nonzero={nonzero}")
