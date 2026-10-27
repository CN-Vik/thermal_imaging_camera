# -*- coding: utf-8 -*-
"""
把竖屏 240x280 的开机图 power_on_start_img.c 顺时针旋转 90°，
重写为横屏 280x240 版本（与 LVGL sw_rotate ROT_90 的映射一致：
New(x, y) = Old(y, 279 - x)）。

用法: python tools/rotate_boot_img.py [main/power_on_start_img.c]
"""
import re
import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else "main/power_on_start_img.c"

with open(PATH, "r", encoding="utf-8", errors="ignore") as f:
    txt = f.read()

vals = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{4}", txt)]
m = re.search(r"\.header\.w = (\d+).*?\.header\.h = (\d+)", txt, re.S)
w, h = int(m.group(1)), int(m.group(2))
assert (w, h) == (240, 280), f"unexpected source size {w}x{h}"
assert len(vals) == w * h, f"count {len(vals)} != {w*h}"

# 顺时针旋转 90°: New(x, y) = Old(y, 279 - x), 新尺寸 280x240
nw, nh = h, w
rot = [0] * (nw * nh)
for x in range(nw):
    for y in range(nh):
        rot[y * nw + x] = vals[y * w + (h - 1 - x)]
vals = rot
w, h = nw, nh

# 重新生成 C 文件（12 个值一行，与原有格式一致）
lines = []
lines.append("/* Auto-generated boot splash (RGB565, 280x240 landscape, white bg). */")
lines.append("#include \"lvgl.h\"")
lines.append("")
lines.append("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
lines.append("#define LV_ATTRIBUTE_MEM_ALIGN")
lines.append("#endif")
lines.append("")
lines.append("static LV_ATTRIBUTE_MEM_ALIGN const uint16_t power_on_start_img_map[] = {")
PER_LINE = 12
for i in range(0, len(vals), PER_LINE):
    chunk = vals[i:i + PER_LINE]
    lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + ",")
lines.append("};")
lines.append("")
lines.append("const lv_img_dsc_t power_on_start_img = {")
lines.append("    .header.cf = LV_IMG_CF_TRUE_COLOR,")
lines.append("    .header.always_zero = 0,")
lines.append("    .header.reserved = 0,")
lines.append("    .header.w = 280,")
lines.append("    .header.h = 240,")
lines.append("    .data_size = %d," % (len(vals) * 2))
lines.append("    .data = (const uint8_t *)power_on_start_img_map,")
lines.append("};")
lines.append("")

with open(PATH, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines))

print("rewritten %s as %dx%d (%d values)" % (PATH, w, h, len(vals)))

# 同时输出 PNG 预览，方便人工确认
import zlib
import struct

def png_chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

rgb = bytearray()
for v in vals:
    rgb.append(((v >> 11) & 0x1F) * 255 // 31)
    rgb.append(((v >> 5) & 0x3F) * 255 // 63)
    rgb.append((v & 0x1F) * 255 // 31)
raw = bytearray()
for y in range(h):
    raw.append(0)
    raw.extend(rgb[y * w * 3:(y + 1) * w * 3])
png = (b"\x89PNG\r\n\x1a\n"
       + png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
       + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
       + png_chunk(b"IEND", b""))
out = "tools/boot_landscape.png"
with open(out, "wb") as f:
    f.write(png)
print("preview:", out)
