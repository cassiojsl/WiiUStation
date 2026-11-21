#!/usr/bin/env python3
"""Make the app's images from the logo in meta/source/ (black background):
content/logo.png (menu header and opening, transparent), meta/icon.png,
meta/splash_tv.png, meta/splash_drc.png."""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
wide = Image.open(os.path.join(ROOT, "meta/source/logo_wide.png")).convert("RGB")
square = Image.open(os.path.join(ROOT, "meta/source/logo_square.png")).convert("RGB")

def black_to_alpha(im, floor=10):
    """black background -> transparent, keeping the soft edges: the
    brightest channel becomes the alpha and the colour is un-blended"""
    a = np.asarray(im, np.float32)
    m = a.max(axis=2)
    alpha = np.clip((m - floor) / (255.0 - floor), 0, 1)
    rgb = np.where(alpha[..., None] > 0, a / np.maximum(alpha[..., None], 1e-3), 0)
    out = np.dstack([np.clip(rgb, 0, 255), alpha * 255]).astype(np.uint8)
    img = Image.fromarray(out, "RGBA")
    return img.crop(img.getbbox())

def bbox_on_black(im, floor=24):
    a = np.asarray(im).max(axis=2) > floor
    ys, xs = np.where(a)
    return (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)

# menu header / opening: wide logo, transparent, 2x the drawn height (80 px)
logo = black_to_alpha(wide)
h = 184
logo = logo.resize((round(logo.width * h / logo.height), h), Image.LANCZOS)
logo.save(os.path.join(ROOT, "content/logo.png"), optimize=True)

# icon: the emblem of the square logo (the name would be unreadable at 128 px)
x0, y0, x1, y1 = bbox_on_black(square)
text_top = int(square.height * 0.80)                 # the name starts below this
emblem = square.crop(bbox_on_black(square.crop((0, 0, square.width, text_top))))
side = max(emblem.size)
canvas = Image.new("RGB", (int(side * 1.08), int(side * 1.08)), (0, 0, 0))
canvas.paste(emblem, ((canvas.width - emblem.width) // 2, (canvas.height - emblem.height) // 2))
canvas.resize((128, 128), Image.LANCZOS).save(os.path.join(ROOT, "meta/icon.png"), optimize=True)

# splash screens: the square logo, centred on black
sq = square.crop((x0, y0, x1, y1))
for name, (W, H), fill in (("splash_tv.png", (1280, 720), 0.78), ("splash_drc.png", (854, 480), 0.78)):
    s = H * fill / sq.height
    im = sq.resize((round(sq.width * s), round(sq.height * s)), Image.LANCZOS)
    bg = Image.new("RGB", (W, H), (0, 0, 0))
    bg.paste(im, ((W - im.width) // 2, (H - im.height) // 2))
    bg.save(os.path.join(ROOT, "meta", name), optimize=True)
print("logo", logo.size)
