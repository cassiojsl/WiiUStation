#!/usr/bin/env python3
"""Generate WiiUStation's built-in bezels (content/bezels/*.png).

Every bezel is 1280x720 with a transparent 960x720 hole at x=160, where the
4:3 game picture goes. The app scales the bezel so the hole always matches the
picture, whatever the screen mode. All art is drawn here from scratch.
"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 1280, 720
HX0, HX1 = 160, 1120          # hole
R = 22                        # rounded hole corners
rng = np.random.default_rng(7)
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "..", "content", "bezels")
os.makedirs(OUT, exist_ok=True)

yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)


def hole_mask():
    """1 inside the hole (transparent), with rounded corners."""
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).rounded_rectangle((HX0, -R, HX1 - 1, H - 1 + R), R, fill=255)
    # the hole touches the top and bottom edges, so round only its sides
    m2 = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m2).rounded_rectangle((HX0, 0, HX1 - 1, H - 1), R, fill=255)
    return np.asarray(m2, np.float32) / 255.0


HOLE = hole_mask()
# distance (px) from the hole edge, for rims and inner shadows
dist = np.minimum(np.abs(xx - HX0), np.abs(xx - HX1))


def noise(scale, octaves=4, seed=0):
    r = np.random.default_rng(seed)
    acc = np.zeros((H, W), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        s = max(1, int(scale / (2 ** o)))
        small = r.random((H // s + 2, W // s + 2)).astype(np.float32)
        img = Image.fromarray((small * 255).astype(np.uint8)).resize(
            ((W // s + 2) * s, (H // s + 2) * s), Image.BICUBIC)
        acc += np.asarray(img, np.float32)[:H, :W] / 255.0 * amp
        tot += amp
        amp *= 0.5
    return acc / tot


def rgb(c):
    return np.array([int(c[i:i + 2], 16) for i in (1, 3, 5)], np.float32)


def lerp(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return a * (1 - t) + b * t


def rim(img, color, width=6, glow=0.0):
    """A bevelled rim right around the hole."""
    d = dist
    t = np.clip(1 - d / width, 0, 1)
    img = lerp(img, np.broadcast_to(rgb(color), img.shape), t * 0.9)
    if glow:
        g = np.exp(-d / glow)[..., None] * 0.55
        img = img + rgb(color) * g
    return img


def inner_shadow(img, depth=26, strength=0.55):
    s = np.clip(1 - dist / depth, 0, 1) ** 2
    return img * (1 - s[..., None] * strength)


def vignette(img, k=0.35):
    cx = np.abs(xx - W / 2) / (W / 2)
    cy = np.abs(yy - H / 2) / (H / 2)
    v = 1 - k * (cx ** 2 * 0.6 + cy ** 2)
    return img * v[..., None]


def save(name, img, extra=None):
    img = np.clip(img, 0, 255).astype(np.uint8)
    a = ((1 - HOLE) * 255).astype(np.uint8)
    im = Image.fromarray(np.dstack([img, a]), "RGBA")
    if extra:
        extra(im)
        # keep the hole clean even if the extra drawing touched it
        arr = np.asarray(im).copy()
        arr[..., 3] = np.minimum(arr[..., 3], a)
        im = Image.fromarray(arr, "RGBA")
    im.save(os.path.join(OUT, name + ".png"), optimize=True)
    print("wrote", name)


def side_draw(im, fn):
    """Call fn(draw, x0, x1, mirror) for both side panels."""
    d = ImageDraw.Draw(im)
    fn(d, 0, HX0, False)
    fn(d, HX1, W, True)


# ---------------------------------------------------------------- TV
def tv():
    n = noise(3, 2, 1) * 10
    base = lerp(rgb("#2a2b30"), rgb("#16171a"), yy / H) + n[..., None]
    base = vignette(base, 0.3)
    # a moulded step around the screen
    step = (dist < 34).astype(np.float32)
    base = lerp(base, base * 0.6, step * 0.8)
    base = rim(base, "#4a4c55", 5)
    base = inner_shadow(base, 30, 0.6)

    def deco(d, x0, x1, right):
        cx = (x0 + x1) // 2
        if right:
            # speaker grille
            for i in range(16):
                y = 110 + i * 14
                d.rounded_rectangle((cx - 46, y, cx + 46, y + 6), 3, fill=(10, 10, 12, 255))
            # two knobs
            for k, y in enumerate((470, 580)):
                d.ellipse((cx - 32, y - 32, cx + 32, y + 32), fill=(12, 12, 14, 255))
                d.ellipse((cx - 28, y - 28, cx + 28, y + 28), fill=(58, 60, 66, 255))
                d.ellipse((cx - 22, y - 22, cx + 22, y + 22), fill=(44, 46, 51, 255))
                d.rectangle((cx - 3, y - 26, cx + 3, y - 8), fill=(200, 200, 205, 255))
        else:
            for i in range(16):
                y = 110 + i * 14
                d.rounded_rectangle((cx - 46, y, cx + 46, y + 6), 3, fill=(10, 10, 12, 255))
            # power LED and button
            d.ellipse((cx - 7, 560, cx + 7, 574), fill=(40, 220, 90, 255))
            d.rounded_rectangle((cx - 30, 600, cx + 30, 624), 8, fill=(12, 12, 14, 255))
            d.rounded_rectangle((cx - 27, 603, cx + 27, 621), 7, fill=(70, 72, 78, 255))
    save("tv", base, lambda im: side_draw(im, deco))


# ---------------------------------------------------------------- grey console
def grey():
    n = noise(2, 2, 2) * 6
    base = lerp(rgb("#d6d5d1"), rgb("#b9b8b3"), yy / H) + n[..., None]
    # horizontal ridges on the sides
    ridge = ((yy.astype(int) % 18) < 3) & (dist > 40) & (dist < 120)
    base[ridge] *= 0.86
    ridge2 = ((yy.astype(int) % 18) == 3) & (dist > 40) & (dist < 120)
    base[ridge2] = np.minimum(base[ridge2] * 1.08, 255)
    base = rim(base, "#8e8d89", 4)
    base = inner_shadow(base, 22, 0.45)
    base = vignette(base, 0.18)

    def deco(d, x0, x1, right):
        cx = (x0 + x1) // 2
        if not right:
            d.ellipse((cx - 22, 600, cx + 22, 644), fill=(150, 149, 145, 255))
            d.ellipse((cx - 18, 604, cx + 18, 640), fill=(200, 199, 195, 255))
            d.ellipse((cx - 5, 66, cx + 5, 76), fill=(70, 210, 80, 255))
        else:
            d.ellipse((cx - 22, 600, cx + 22, 644), fill=(150, 149, 145, 255))
            d.ellipse((cx - 18, 604, cx + 18, 640), fill=(200, 199, 195, 255))
            d.ellipse((cx - 5, 66, cx + 5, 76), fill=(230, 140, 40, 255))
    save("grey", base, lambda im: side_draw(im, deco))


# ---------------------------------------------------------------- carbon
def carbon():
    c = 12
    u = (xx.astype(int) // c + yy.astype(int) // c) % 2
    fx = (xx % c) / c
    fy = (yy % c) / c
    weave = np.where(u == 0, np.sin(fx * np.pi), np.sin(fy * np.pi))
    v = 16 + weave * 22
    base = np.repeat(v[..., None], 3, 2) * np.array([1.0, 1.02, 1.1])
    base = vignette(base, 0.5)
    base = inner_shadow(base, 30, 0.6)
    base = rim(base, "#3da5ff", 3, glow=14)
    save("carbon", base)


# ---------------------------------------------------------------- synthwave
def synthwave():
    sky = lerp(rgb("#0b0420"), rgb("#3b0d4f"), (yy / H) ** 1.4)
    base = sky.copy()
    # stars
    s = rng.random((H, W)) > 0.9975
    base[s & (yy < 430)] = 255
    # perspective grid on the floor
    hor = 430.0
    floor = yy > hor
    z = 1.0 / np.maximum(yy - hor, 1)
    hl = (np.abs(((1 / z) * 0.25) % 1 - 0.5) > 0)  # placeholder
    rows = np.abs(np.sin(np.log(np.maximum(yy - hor, 1)) * 9)) < 0.08
    cols = np.abs(np.sin((xx - W / 2) * z * 0.15)) < 0.06
    grid = floor & (rows | cols)
    base = np.where(floor[..., None], lerp(rgb("#200830"), rgb("#05010c"), (yy - hor) / 290), base)
    base[grid] = rgb("#ff3fb4")
    # horizon glow
    g = np.exp(-np.abs(yy - hor) / 18)[..., None]
    base = base + rgb("#ff4f9a") * g * 0.6
    base = inner_shadow(base, 26, 0.5)
    base = rim(base, "#ff4fd8", 3, glow=12)
    save("synthwave", base)


# ---------------------------------------------------------------- wood
def wood():
    # planks of vertical grain: noise stretched along y, then rings
    small = np.random.default_rng(3).random((8, 160)).astype(np.float32)
    n = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC),
                   np.float32) / 255.0
    rings = np.sin(xx * 0.09 + n * 9 + np.sin(yy * 0.004) * 2) * 0.5 + 0.5
    fine = noise(2, 2, 4)
    streak = np.asarray(Image.fromarray((np.random.default_rng(9).random((H // 40 + 1, W)) * 255)
                        .astype(np.uint8)).resize((W, H), Image.BILINEAR), np.float32) / 255.0
    t = rings * 0.55 + streak * 0.3 + fine * 0.15
    base = lerp(rgb("#4e2c14"), rgb("#9a6634"), t)
    plank = (xx.astype(int) % 54) < 2
    base[plank] *= 0.55
    base = vignette(base, 0.3)
    # black plastic screen frame inside the wood
    frame = dist < 30
    base[frame] = (base[frame] * 0.0 + rgb("#141416"))
    base = rim(base, "#2e2e33", 4)
    edge = (np.abs(dist - 30) < 2)
    base[edge] = rgb("#3a2312")
    base = inner_shadow(base, 30, 0.5)
    save("wood", base)


# ---------------------------------------------------------------- white (Wii U)
def white():
    base = lerp(rgb("#f4f6f8"), rgb("#dde3ea"), yy / H)
    hl = np.exp(-((yy - 140) / 160) ** 2) * 10
    base = base + hl[..., None]
    base = rim(base, "#2a9df4", 3, glow=10)
    base = inner_shadow(base, 18, 0.2)
    save("white", base)


# ---------------------------------------------------------------- space
def space():
    neb1 = noise(120, 5, 5)
    neb2 = noise(90, 5, 6)
    base = np.zeros((H, W, 3), np.float32) + rgb("#03040c")
    base += rgb("#3a1f8a") * (np.clip(neb1 - 0.45, 0, 1) * 2.2)[..., None]
    base += rgb("#0f6c9c") * (np.clip(neb2 - 0.5, 0, 1) * 2.4)[..., None]
    st = rng.random((H, W))
    base[st > 0.997] = 220
    base[st > 0.9993] = 255
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    # a few soft bright stars
    glow = Image.new("RGB", (W, H), 0)
    d = ImageDraw.Draw(glow)
    for _ in range(14):
        x = int(rng.choice([rng.integers(10, 150), rng.integers(1130, 1270)]))
        y = int(rng.integers(10, 710))
        d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(255, 255, 255))
    glow = glow.filter(ImageFilter.GaussianBlur(3))
    base = np.asarray(img, np.float32) + np.asarray(glow, np.float32) * 1.5
    base = inner_shadow(base, 30, 0.6)
    base = rim(base, "#6fb8ff", 2, glow=8)
    save("space", base)


for f in (tv, grey, carbon, synthwave, wood, white, space):
    f()
