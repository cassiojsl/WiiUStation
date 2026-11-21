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


def hole_r(r):
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).rounded_rectangle((HX0, 0, HX1 - 1, H - 1), r, fill=255)
    return np.asarray(m, np.float32) / 255.0


def save_rgba(name, rgb_img, hole, overlay=None):
    """rgb_img: HxWx3 art; hole: 1 = transparent; overlay: (HxWx3, HxW alpha)
    drawn inside the hole (glass glare, tube shading)."""
    a = (1 - hole) * 255.0
    col = np.clip(rgb_img, 0, 255)
    if overlay is not None:
        oc, oa = overlay
        inside = hole > 0
        col = np.where(inside[..., None], oc, col)
        a = np.where(inside, np.clip(oa, 0, 255) * hole + a, a)
    im = Image.fromarray(np.dstack([col.astype(np.uint8), np.clip(a, 0, 255).astype(np.uint8)]), "RGBA")
    im.save(os.path.join(OUT, name + ".png"), optimize=True)
    print("wrote", name)


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



# ================================================================ tube TVs
def tube_overlay(r):
    """Shading of a curved picture tube, drawn over the game: darker towards
    the edges and corners, and a soft reflection of the room on the glass."""
    cx, cy = (HX0 + HX1) / 2, H / 2
    nx = np.abs(xx - cx) / ((HX1 - HX0) / 2)
    ny = np.abs(yy - cy) / (H / 2)
    edge = np.clip((nx ** 6 + ny ** 6) ** 0.5, 0, 1)
    dark = edge ** 3 * 170
    # diagonal glare, top left
    g = np.clip(1 - np.hypot((xx - (HX0 + 250)) / 420, (yy - 150) / 260), 0, 1) ** 2 * 34
    col = np.zeros((H, W, 3), np.float32)
    col = col * 0 + np.where((g > dark)[..., None], 255, 0)
    alpha = np.maximum(dark, g)
    return col, alpha


def tube_tv():
    r = 70
    hole = hole_r(r)
    n = noise(3, 2, 11) * 8
    base = lerp(rgb("#26272b"), rgb("#0d0d0f"), yy / H) + n[..., None]
    # deep moulded funnel around the tube
    d = dist
    funnel = np.clip(1 - d / 60, 0, 1)
    base = base * (1 - funnel[..., None] * 0.75)
    base = base + (np.exp(-np.abs(d - 60) / 2.5) * 40)[..., None]   # bright lip
    base = vignette(base, 0.35)
    col, alpha = tube_overlay(r)

    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(img)
    cx = (HX1 + W) // 2
    # front panel on the right: channel buttons and a volume slider
    dr.rounded_rectangle((cx - 50, 140, cx + 50, 420), 10, fill=(18, 18, 20))
    for i in range(6):
        y = 160 + i * 42
        dr.rounded_rectangle((cx - 36, y, cx + 36, y + 28), 6, fill=(54, 55, 60))
        dr.rounded_rectangle((cx - 36, y, cx + 36, y + 6), 3, fill=(78, 80, 86))
    dr.rounded_rectangle((cx - 6, 460, cx + 6, 640), 6, fill=(10, 10, 12))
    dr.rounded_rectangle((cx - 22, 520, cx + 22, 548), 6, fill=(90, 92, 98))
    lx = HX0 // 2
    for i in range(22):
        y = 120 + i * 20
        dr.rounded_rectangle((lx - 44, y, lx + 44, y + 8), 4, fill=(6, 6, 7))
    dr.ellipse((lx - 8, 610, lx + 8, 626), fill=(230, 40, 40))
    base = np.asarray(img, np.float32)
    save_rgba("tubetv", base, hole, (col, alpha))


def classic_tv():
    r = 46
    hole = hole_r(r)
    small = np.random.default_rng(21).random((8, 160)).astype(np.float32)
    n = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32) / 255
    t = (np.sin(xx * 0.07 + n * 8) * 0.5 + 0.5) * 0.7 + noise(2, 2, 12) * 0.3
    base = lerp(rgb("#3d200c"), rgb("#7a4a22"), t)
    base = vignette(base, 0.3)
    # chrome trim around the screen
    d = dist
    trim = (d < 16)
    base[trim] = lerp(rgb("#8c8f96"), rgb("#e8eaee"), (yy[trim] / H))
    base[(d >= 16) & (d < 19)] = rgb("#1b1008")
    col, alpha = tube_overlay(r)
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(img)
    # speaker cloth on the left
    lx0, lx1 = 22, HX0 - 34
    dr.rounded_rectangle((lx0, 60, lx1, 660), 12, fill=(196, 178, 140))
    for y in range(64, 656, 6):
        dr.line((lx0 + 4, y, lx1 - 4, y), fill=(170, 150, 112))
    for x in range(lx0 + 4, lx1 - 4, 6):
        dr.line((x, 64, x, 656), fill=(180, 160, 122))
    # silver control panel on the right with two dials
    rx0, rx1 = HX1 + 34, W - 22
    dr.rounded_rectangle((rx0, 60, rx1, 660), 12, fill=(176, 178, 184))
    dr.rounded_rectangle((rx0 + 4, 64, rx1 - 4, 656), 10, fill=(206, 208, 214))
    cx = (rx0 + rx1) // 2
    import math
    for y, nt in ((210, 12), (420, 0)):
        for k in range(nt):
            a = math.radians(-120 + k * 240 / (nt - 1))
            x1, y1 = cx + math.sin(a) * 44, y - math.cos(a) * 44
            x2, y2 = cx + math.sin(a) * 52, y - math.cos(a) * 52
            dr.line((x1, y1, x2, y2), fill=(60, 60, 66), width=3)
        dr.ellipse((cx - 38, y - 38, cx + 38, y + 38), fill=(40, 40, 44))
        dr.ellipse((cx - 34, y - 34, cx + 34, y + 34), fill=(232, 233, 236))
        dr.ellipse((cx - 26, y - 26, cx + 26, y + 26), fill=(196, 198, 204))
        dr.rectangle((cx - 4, y - 32, cx + 4, y - 6), fill=(30, 30, 34))
    for i in range(3):
        y = 540 + i * 36
        dr.rounded_rectangle((cx - 30, y, cx + 30, y + 22), 6, fill=(60, 60, 66))
    save_rgba("classictv", np.asarray(img, np.float32), hole, (col, alpha))


# ================================================================ pixel art
PS = 4                      # one art pixel = 4 screen pixels
LW, LH = W // PS, H // PS   # 320x180
LX0, LX1 = HX0 // PS, HX1 // PS


def pixel_finish(name, im):
    """im: 320x180 RGB pixel art. Adds a pixel frame round the hole, scales
    up without smoothing, cuts the hole."""
    dr = ImageDraw.Draw(im)
    dr.rectangle((LX0 - 3, 0, LX0 - 1, LH), fill=(20, 16, 30))
    dr.rectangle((LX1, 0, LX1 + 2, LH), fill=(20, 16, 30))
    dr.line((LX0 - 4, 0, LX0 - 4, LH), fill=(255, 255, 255))
    dr.line((LX1 + 3, 0, LX1 + 3, LH), fill=(255, 255, 255))
    big = im.resize((W, H), Image.NEAREST)
    a = np.full((H, W), 255, np.uint8)
    a[:, HX0:HX1] = 0
    out = Image.fromarray(np.dstack([np.asarray(big), a]), "RGBA")
    out.save(os.path.join(OUT, name + ".png"), optimize=True)
    print("wrote", name)


def blob(dr, x, y, parts, fill):
    for dx, dy, w, h in parts:
        dr.rectangle((x + dx, y + dy, x + dx + w - 1, y + dy + h - 1), fill=fill)


CLOUD = [(2, 0, 8, 2), (0, 2, 14, 3), (3, -2, 4, 2), (9, 1, 4, 2)]


def pixel_sky():
    r = np.random.default_rng(31)
    im = Image.new("RGB", (LW, LH))
    dr = ImageDraw.Draw(im)
    bands = [(92, 148, 252), (104, 160, 255), (120, 176, 255), (140, 192, 255), (164, 208, 255)]
    for i, c in enumerate(bands):
        dr.rectangle((0, i * 26, LW, i * 26 + 25), fill=c)
        if i:   # dithered band edges
            for x in range(0, LW, 2):
                dr.point((x + (i % 2), i * 26), fill=bands[i - 1])
    for x0, x1 in ((0, LX0 - 4), (LX1 + 4, LW)):
        for _ in range(3):
            cx = int(r.integers(x0, max(x0 + 1, x1 - 14)))
            cy = int(r.integers(8, 90))
            blob(dr, cx + 1, cy + 1, CLOUD, (180, 200, 240))
            blob(dr, cx, cy, CLOUD, (255, 255, 255))
        # hills
        for k in range(2):
            hx = x0 + (x1 - x0) * (k + 0.5) / 2
            for y in range(120, LH):
                w = int(((y - 118) ** 0.5) * 9)
                dr.line((hx - w, y, hx + w, y), fill=(0, 148, 60) if k else (24, 172, 72))
        # grass strip and dirt
        dr.rectangle((x0, 160, x1, 163), fill=(80, 208, 64))
        dr.rectangle((x0, 164, x1, LH), fill=(176, 104, 48))
        for x in range(x0, x1, 8):
            dr.rectangle((x, 164, x + 3, 165), fill=(140, 76, 32))
            dr.rectangle((x + 4, 172, x + 7, 173), fill=(140, 76, 32))
        for x in range(x0 + 3, x1, 9):
            dr.point((x, 159), fill=(80, 208, 64))
        for _ in range(2):
            fx = int(r.integers(x0 + 2, x1 - 2))
            dr.point((fx, 158), fill=(255, 220, 60))
            dr.point((fx, 159), fill=(30, 120, 40))
    pixel_finish("pixelsky", im)


def pixel_city():
    r = np.random.default_rng(41)
    im = Image.new("RGB", (LW, LH))
    dr = ImageDraw.Draw(im)
    bands = [(10, 8, 40), (18, 14, 60), (30, 20, 80), (50, 28, 96), (80, 36, 104)]
    for i, c in enumerate(bands):
        dr.rectangle((0, i * 24, LW, i * 24 + 23), fill=c)
        if i:
            for x in range(0, LW, 2):
                dr.point((x + (i % 2), i * 24), fill=bands[i - 1])
    dr.rectangle((0, 120, LW, LH), fill=bands[-1])
    for _ in range(90):
        x, y = int(r.integers(0, LW)), int(r.integers(0, 100))
        dr.point((x, y), fill=(255, 255, 220) if r.random() > 0.3 else (150, 150, 200))
    # moon on the left
    dr.ellipse((8, 14, 26, 32), fill=(250, 240, 200))
    dr.ellipse((14, 12, 32, 30), fill=bands[0])
    for x0, x1 in ((0, LX0 - 4), (LX1 + 4, LW)):
        x = x0 - 2
        while x < x1:
            bw = int(r.integers(7, 14))
            top = int(r.integers(70, 130))
            dr.rectangle((x, top, x + bw, LH), fill=(14, 10, 28))
            dr.line((x + bw, top, x + bw, LH), fill=(36, 26, 60))
            for wy in range(top + 3, LH - 3, 4):
                for wx in range(x + 2, x + bw - 1, 3):
                    if r.random() > 0.55:
                        dr.point((wx, wy), fill=(255, 210, 90) if r.random() > 0.2 else (120, 220, 255))
            if r.random() > 0.6:
                dr.line((x + bw // 2, top - 6, x + bw // 2, top), fill=(14, 10, 28))
                dr.point((x + bw // 2, top - 7), fill=(255, 60, 60))
            x += bw + 1
    pixel_finish("pixelcity", im)


def pixel_castle():
    r = np.random.default_rng(51)
    im = Image.new("RGB", (LW, LH), (40, 36, 48))
    dr = ImageDraw.Draw(im)
    for row, y in enumerate(range(0, LH, 6)):
        off = 0 if row % 2 else 6
        for x in range(-12 + off, LW, 12):
            shade = int(r.integers(-8, 9))
            c = (92 + shade, 88 + shade, 104 + shade)
            dr.rectangle((x, y, x + 10, y + 4), fill=c)
            dr.line((x, y, x + 10, y), fill=(120 + shade, 116 + shade, 132 + shade))
    # torches with a warm glow
    glow = Image.new("RGB", (LW, LH))
    gd = ImageDraw.Draw(glow)
    for cx in (LX0 // 2, (LX1 + LW) // 2):
        for rad, c in ((26, (60, 30, 0)), (17, (90, 48, 0)), (9, (120, 70, 10))):
            gd.ellipse((cx - rad, 70 - rad, cx + rad, 70 + rad), fill=c)
    im = Image.fromarray(np.clip(np.asarray(im, np.int32) + np.asarray(glow, np.int32), 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    for cx in (LX0 // 2, (LX1 + LW) // 2):
        dr.rectangle((cx - 1, 72, cx + 1, 84), fill=(90, 56, 30))
        dr.rectangle((cx - 3, 70, cx + 3, 72), fill=(60, 60, 70))
        blob(dr, cx - 2, 62, [(1, 0, 3, 2), (0, 2, 5, 4), (1, 6, 3, 2)], (255, 150, 30))
        blob(dr, cx - 1, 64, [(0, 1, 3, 4)], (255, 230, 90))
        # banners
        by = 110
        dr.rectangle((cx - 9, by, cx + 9, by + 36), fill=(150, 20, 40))
        dr.polygon([(cx - 9, by + 36), (cx, by + 30), (cx + 9, by + 36), (cx + 9, by + 42), (cx, by + 36), (cx - 9, by + 42)], fill=(150, 20, 40))
        dr.rectangle((cx - 9, by, cx + 9, by + 1), fill=(230, 190, 60))
        dr.rectangle((cx - 3, by + 12, cx + 3, by + 18), fill=(230, 190, 60))
        dr.rectangle((cx - 1, by + 10, cx + 1, by + 20), fill=(230, 190, 60))
    pixel_finish("pixelcastle", im)


for f in (tv, grey, carbon, synthwave, wood, white, space,
          tube_tv, classic_tv, pixel_sky, pixel_city, pixel_castle):
    f()
