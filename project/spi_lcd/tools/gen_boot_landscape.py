# -*- coding: utf-8 -*-
"""Crop the logo from the supplied 280x240 artwork and generate an RGB565 icon."""
import re
import sys
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageChops

SOURCE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("main/new_boot-280x240.c")
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("main/power_on_start_img.c")
PADDING = 8
ICON_MAX_SIZE = (96, 96)

source_text = SOURCE.read_text(encoding="utf-8")
map_match = re.search(
    r"uint8_t\s+boot-280x240_map\[\]\s*=\s*\{(.*?)\};", source_text, re.S
)
if map_match is None:
    raise ValueError(f"PNG byte array not found in {SOURCE}")
png_data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", map_match.group(1)))
size_match = re.search(r"\.data_size\s*=\s*(\d+)", source_text)
if size_match is None or len(png_data) != int(size_match.group(1)):
    raise ValueError(f"PNG data size mismatch in {SOURCE}")

color_swap = False
for config in (Path("sdkconfig"), Path("sdkconfig.defaults")):
    if not config.exists():
        continue
    for line in config.read_text(encoding="utf-8").splitlines():
        setting = line.strip()
        if setting.startswith("CONFIG_LV_COLOR_16_SWAP="):
            color_swap = setting.endswith("=y")
            break
        if setting == "# CONFIG_LV_COLOR_16_SWAP is not set":
            color_swap = False
            break
    else:
        continue
    break

with Image.open(BytesIO(png_data)) as source:
    if source.size != (280, 240):
        raise ValueError(f"expected 280x240 source artwork, got {source.width}x{source.height}")
    rgba = source.convert("RGBA")
    image = Image.alpha_composite(
        Image.new("RGBA", rgba.size, (0, 0, 0, 255)), rgba
    ).convert("RGB")
    background = image.getpixel((0, 0))
    bounds = ImageChops.difference(
        image, Image.new("RGB", image.size, background)
    ).getbbox()
    if bounds is None:
        raise ValueError("source artwork contains no logo")

    left = max(0, bounds[0] - PADDING)
    top = max(0, bounds[1] - PADDING)
    right = min(image.width, bounds[2] + PADDING)
    bottom = min(image.height, bounds[3] + PADDING)
    image = image.crop((left, top, right, bottom))
    crop_rgb = bytearray(image.tobytes())
    source_background = bytes(background)
    for i in range(0, len(crop_rgb), 3):
        if crop_rgb[i:i + 3] == source_background:
            crop_rgb[i:i + 3] = b"\x00\x00\x00"
    image = Image.frombytes("RGB", image.size, bytes(crop_rgb))
    image.thumbnail(ICON_MAX_SIZE, Image.Resampling.LANCZOS)

width, height = image.size
rgb = image.tobytes()


def rgb565(i):
    value = ((rgb[i] >> 3) << 11) | ((rgb[i + 1] >> 2) << 5) | (rgb[i + 2] >> 3)
    if color_swap:
        return ((value & 0xFF) << 8) | (value >> 8)
    return value


pixels = [rgb565(i) for i in range(0, len(rgb), 3)]

lines = [
    "/* Auto-generated cropped and proportionally scaled boot logo (RGB565). */",
    '#include "lvgl.h"',
    "",
    "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
    "#define LV_ATTRIBUTE_MEM_ALIGN",
    "#endif",
    "",
    "static LV_ATTRIBUTE_MEM_ALIGN const uint16_t power_on_start_img_map[] = {",
]

for i in range(0, len(pixels), 12):
    lines.append("    " + ", ".join(f"0x{value:04x}" for value in pixels[i:i + 12]) + ",")

lines.extend([
    "};",
    "",
    "const lv_img_dsc_t power_on_start_img = {",
    "    .header.cf = LV_IMG_CF_TRUE_COLOR,",
    "    .header.always_zero = 0,",
    "    .header.reserved = 0,",
    f"    .header.w = {width},",
    f"    .header.h = {height},",
    f"    .data_size = {len(pixels) * 2},",
    "    .data = (const uint8_t *)power_on_start_img_map,",
    "};",
    "",
])

OUTPUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
print(
    f"wrote {OUTPUT}: centered logo {width}x{height}, {len(pixels)} RGB565 pixels "
    f"(LV_COLOR_16_SWAP={'y' if color_swap else 'n'})"
)
