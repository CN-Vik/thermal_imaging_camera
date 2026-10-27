import re, struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else r"main/power_on_start_img.c"
out = sys.argv[2] if len(sys.argv) > 2 else r"tools/boot_portrait.png"

with open(path, "r", encoding="utf-8", errors="ignore") as f:
    txt = f.read()

vals = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]{4}", txt)]
print("total values:", len(vals))

# header w/h
m = re.search(r"\.header\.w = (\d+).*?\.header\.h = (\d+)", txt, re.S)
w, h = int(m.group(1)), int(m.group(2))
print("w,h =", w, h)

assert len(vals) == w * h, "count mismatch %d != %d" % (len(vals), w * h)

rgb = bytearray()
for v in vals:
    rgb.append(((v >> 11) & 0x1F) * 255 // 31)
    rgb.append(((v >> 5) & 0x3F) * 255 // 63)
    rgb.append((v & 0x1F) * 255 // 31)

# use zlib/png manual writer to avoid PIL dependency
import zlib
def png_chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

raw = bytearray()
for y in range(h):
    raw.append(0)
    raw.extend(rgb[y * w * 3:(y + 1) * w * 3])
png = (b"\x89PNG\r\n\x1a\n"
       + png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
       + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
       + png_chunk(b"IEND", b""))
with open(out, "wb") as f:
    f.write(png)
print("wrote", out)
