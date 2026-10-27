# -*- coding: utf-8 -*-
"""分析开机图：找出非白像素的包围盒(logo 内容区域)，并统计宽高比。"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else r"tools/boot_orig_240x280.c"

with open(path, "r", encoding="utf-8", errors="ignore") as f:
    txt = f.read()

vals = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{4}", txt)]
m = re.search(r"\.header\.w = (\d+).*?\.header\.h = (\d+)", txt, re.S)
w, h = int(m.group(1)), int(m.group(2))
assert len(vals) == w * h, (len(vals), w, h)

# 白色 RGB565 = 0xFFFF
nonwhite = [(x, y) for y in range(h) for x in range(w) if vals[y * w + x] != 0xFFFF]
print("size:", w, "x", h, " nonwhite pixels:", len(nonwhite))

if not nonwhite:
    print("ALL WHITE")
    sys.exit(0)

xs = [p[0] for p in nonwhite]
ys = [p[1] for p in nonwhite]
x0, x1 = min(xs), max(xs)
y0, y1 = min(ys), max(ys)
bw, bh = x1 - x0 + 1, y1 - y0 + 1
print(f"bbox: x[{x0}..{x1}] y[{y0}..{y1}]  size {bw}x{bh}")
print(f"center: ({ (x0+x1)//2 }, { (y0+y1)//2 })  screen center: ({w//2}, {h//2})")
print(f"aspect w/h = {bw/bh:.2f}")
