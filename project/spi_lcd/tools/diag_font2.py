# -*- coding: utf-8 -*-
"""验证 app_font_ru 的 fix 逻辑：模拟"按行对齐→跨行连续"重排，检查还原是否无损。"""
import re

path = r"main/app_font_ru.c"
txt = open(path, encoding="utf-8", errors="ignore").read()

bm = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{2}", txt.split("glyph_bitmap[]")[1].split("};")[0])]

dsc_txt = txt.split("glyph_dsc[] = {")[1].split("};")[0]
dscs = []
for m in re.finditer(r"bitmap_index = (\d+), adv_w = (\d+), box_w = (\d+), box_h = (\d+), ofs_x = (-?\d+), ofs_y = (-?\d+)", dsc_txt):
    dscs.append(tuple(map(int, m.groups())))
print("glyph_dsc count:", len(dscs))

# script cmap: 取 cmaps 文本里第2个 range_start
cmaps_txt = txt.split("cmaps[] = {")[1].split("};")[0]
starts = [int(v) for v in re.findall(r"range_start = (\d+)", cmaps_txt)]
print("range_starts:", starts)
rstart = starts[1]

ul_txt = txt.split("unicode_list_1[]")[1].split("};")[0]
ul = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", ul_txt)]

def glyph_id(ch):
    cp = ord(ch)
    if 32 <= cp < 127:
        return cp - 32 + 1
    rcp = cp - rstart
    if rcp in ul:
        return 96 + ul.index(rcp)
    return None

def row_aligned_pixels(gid):
    bi, aw, bw, bh, ox, oy = dscs[gid]
    row_bytes = (bw + 1) >> 1
    px = []
    for y in range(bh):
        for x in range(bw):
            b = bm[bi + y * row_bytes + (x >> 1)]
            px.append((b >> 4) if (x & 1) == 0 else (b & 0x0F))
    return bw, bh, px

def fix_rearrange(gid):
    """模拟 app_font_get_bitmap_fix_odd_width"""
    bi, aw, bw, bh, ox, oy = dscs[gid]
    row_bytes = (bw + 1) >> 1
    nibbles = []
    for y in range(bh):
        for x in range(bw):
            b = bm[bi + y * row_bytes + (x >> 1)]
            nibbles.append((b >> 4) if (x & 1) == 0 else (b & 0x0F))
    # 打包成跨行连续字节
    out = bytearray()
    for i in range(0, len(nibbles), 2):
        hi = nibbles[i]
        lo = nibbles[i + 1] if i + 1 < len(nibbles) else 0
        out.append((hi << 4) | lo)
    return bw, bh, nibbles, out

def lvgl_read_packed(bw, bh, packed):
    """LVGL 跨行连续读法还原像素"""
    width_bit = bw * 4
    px = []
    for y in range(bh):
        for x in range(bw):
            bit_ofs = y * width_bit + x * 4
            byte = packed[bit_ofs >> 3]
            shift = 8 - (bit_ofs & 7) - 4
            px.append((byte >> shift) & 0x0F)
    return px

for ch in "Сох":
    gid = glyph_id(ch)
    if gid is None or gid >= len(dscs):
        print(f"{ch}: MISSING gid={gid}")
        continue
    bw, bh, raw_px = row_aligned_pixels(gid)
    bw2, bh2, nibbles, packed = fix_rearrange(gid)
    lv_px = lvgl_read_packed(bw, bh, packed)
    same = raw_px == lv_px
    nonz = sum(1 for v in raw_px if v)
    print(f"{ch} U+{ord(ch):04X} gid={gid} box={bw}x{bh} nonzero_px={nonz} fix_lossless={same} packed_bytes={len(packed)}")
