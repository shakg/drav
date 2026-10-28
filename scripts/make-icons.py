#!/usr/bin/env python3
"""Rasterizes the app icon (geometry mirrors resources/icons/drav.svg) into PNG, ICO and ICNS. Needs Pillow."""
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "resources", "icons")
S = 4  # supersampling
N = 1024 * S


def bezier(p0, p1, p2, p3, n=3000):
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        yield tuple((u**3 * p0[k] + 3 * u * u * t * p1[k] + 3 * u * t * t * p2[k] + t**3 * p3[k]) * S for k in range(2))


img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
grad = Image.new("RGBA", (N, N))
gd = ImageDraw.Draw(grad)
a, b = (0x7B, 0x61, 0xFF), (0x0D, 0x99, 0xFF)
for i in range(0, 2 * N, 4):
    t = i / (2 * N)
    gd.line([(i, 0), (0, i)], fill=tuple(int(a[k] + (b[k] - a[k]) * t) for k in range(3)) + (255,), width=6)
mask = Image.new("L", (N, N), 0)
ImageDraw.Draw(mask).rounded_rectangle([64 * S, 64 * S, 960 * S, 960 * S], radius=200 * S, fill=255)
img.paste(grad, (0, 0), mask)

handles = Image.new("RGBA", (N, N), (0, 0, 0, 0))
hd = ImageDraw.Draw(handles)
hd.line([(256 * S, 704 * S), (384 * S, 224 * S)], fill=(255, 255, 255, 153), width=20 * S)
hd.line([(768 * S, 704 * S), (640 * S, 224 * S)], fill=(255, 255, 255, 153), width=20 * S)
img = Image.alpha_composite(img, handles)
d = ImageDraw.Draw(img)
r = 32 * S
for p in bezier((256, 704), (352, 288), (672, 288), (768, 704)):
    d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill="white")
for cx in (384, 640):
    r = 36 * S
    d.ellipse([cx * S - r, 224 * S - r, cx * S + r, 224 * S + r], fill="white")
for x in (200, 712):
    d.rounded_rectangle([x * S, 648 * S, (x + 112) * S, 760 * S], radius=16 * S, fill="white")

big = img.resize((1024, 1024), Image.LANCZOS)
big.resize((512, 512), Image.LANCZOS).save(os.path.join(OUT, "drav.png"))
big.save(os.path.join(OUT, "drav.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
big.save(os.path.join(OUT, "drav.icns"))
