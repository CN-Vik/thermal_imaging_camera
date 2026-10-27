# -*- coding: utf-8 -*-
"""
从原始竖屏开机图(240x280)中提取 logo 内容区域，放大 2 倍后居中到横屏 280x240 白底，
生成新的 power_on_start_img.c。避免任何旋转导致的方向/居中问题。
"""
import re
import sys

ORIG = sys.argv[1] if len(sys.argv) > 1 else "tools/boot_orig_240x280.c"
OUT  = sys.argv[2] if len(sys.argv) > 2 else "main/power_on_start_img.c"
SCALE = int(sys.argv[3]) if len(sys.argv) > 3 else 2

NW, NH = 280, 240
WHITE = 0xFFFF

with open(ORIG, "r", encoding="utf-8", errors="ignore") as f:
    txt = f.read()
vals = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{4}", txt)]
m = re.search(r"\.header\.w = (\d+).*?\.header\.h = (\d+)", txt, re.S)
w, h = int(m.group(1)), int(m.group(2))
assert len(vals) == w * h, (len(vals), w, h)

nonwhite = [(x, y) for y in range(h) for x in range(w) if vals[y * w + x] != WHITE]
xs = [p[0] for p in nonwhite]
ys = [p[1] for p in nonwhite]
x0, x1 = min(xs), max(xs)
y0, y1 = min(ys), max(ys)
bw, bh = x1 - x0 + 1, y1 - y0 + 1
print(f"logo bbox {bw}x{bh} at ({x0},{y0})")

sw, sh = bw * SCALE, bh * SCALE
dx = (NW - sw) // 2
dy = (NH - sh) // 2
print(f"scaled {SCALE}x -> {sw}x{sh}, placed at ({dx},{dy})")

out = [WHITE] * (NW * NH)
for oy in range(sh):
    for ox in range(sw):
        sx = x0 + ox // SCALE
        sy = y0 + oy // SCALE
        out[(dy + oy) * NW + (dx + ox)] = vals[sy * w + sx]

# 生成 C 文件（12 个值一行）
lines = []
lines.append("/* Auto-generated boot splash (RGB565, 280x240 landscape, white bg, logo 2x centered). */")
lines.append("#include \"lvgl.h\"")
lines.append("")
lines.append("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
lines.append("#define LV_ATTRIBUTE_MEM_ALIGN")
lines.append("#endif")
lines.append("")
lines.append("static LV_ATTRIBUTE_MEM_ALIGN const uint16_t power_on_start_img_map[] = {")
PER = 12
for i in range(0, len(out), PER):
    lines.append("    " + ", ".join("0x%04x" % v for v in out[i:i + PER]) + ",")
lines.append("};")
lines.append("")
lines.append("const lv_img_dsc_t power_on_start_img = {")
lines.append("    .header.cf = LV_IMG_CF_TRUE_COLOR,")
lines.append("    .header.always_zero = 0,")
lines.append("    .header.reserved = 0,")
lines.append("    .header.w = %d," % NW)
lines.append("    .header.h = %d," % NH)
lines.append("    .data_size = %d," % (len(out) * 2))
lines.append("    .data = (const uint8_t *)power_on_start_img_map,")
lines.append("};")
lines.append("")
with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines))
print("wrote", OUT)
