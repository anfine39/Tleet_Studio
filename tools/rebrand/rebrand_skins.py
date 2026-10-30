#!/usr/bin/env python3
"""РИЛАЙ rebrand pass over the processed skins.

Runs after the per-plugin tools/skinprep/prep_<name>.py scripts and rewrites
assets/processed/<NAME>/*.png in place:
  1. header lockup on plate.png: the TLEET star and the word TLEET are
     replaced by the РИЛАЙ emblem and wordmark (assets/brand), and the
     "// NAME" lettering is moved right to make room;
  2. other printed TLEET marks (maker plates, VU faces, labels) are painted
     over and re-lettered from tools/rebrand/text_patches.json;
  3. every PNG (plate and sprites) goes through palette.recolour, the same
     transform tools/rebrand/recolour_source.py applies to colours in code.

The pass is not idempotent: run it once on freshly prepared skins
(prep_<name>.py, then this script). --only NAME limits it to one plugin,
--preview DIR writes copies instead of overwriting.

Requires: numpy, opencv-python-headless, pillow.
"""
import argparse
import glob
import json
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from palette import recolour  # noqa: E402

BRAND = os.path.join(ROOT, "assets", "brand")
FONT = os.path.join(BRAND, "fonts", "Oswald-Variable.ttf")
WORD_CAP = 0.81                 # cap height / height of assets/brand/wordmark.png
WORD_INK = (222, 227, 238)      # chalk white of the wordmark (BGR)


def oswald(size, weight=b"SemiBold"):
    f = ImageFont.truetype(FONT, size)
    f.set_variation_by_name(weight)
    return f


def chalk_text(text, cap, stretch=1.08, seed=3):
    """Set text in the hand-drawn chalk style of the РИЛАЙ wordmark: bold
    condensed letters, wobbly edges, dry-brush speckle. Returns BGRA."""
    k = 4                                          # supersampling
    probe = oswald(200, b"Bold")
    hb = probe.getbbox("H")
    size = int(round(200.0 * cap * k / (hb[3] - hb[1])))
    font = oswald(size, b"Bold")
    w = int(font.getlength(text) + size)
    pil = Image.new("L", (w, size * 2), 0)
    ImageDraw.Draw(pil).text((size // 4, size // 2), text, font=font, fill=255)
    m = np.array(pil).astype(np.float32) / 255.0
    ys, xs = np.nonzero(m > 0.01)
    m = m[ys.min() - 8:ys.max() + 9, xs.min() - 8:xs.max() + 9]
    m = cv2.resize(m, (int(m.shape[1] * stretch), m.shape[0]), interpolation=cv2.INTER_LINEAR)

    rng = np.random.default_rng(seed)
    h, w = m.shape
    amp = 0.022 * cap * k                          # edge wobble
    def field(sigma):
        f = cv2.GaussianBlur(rng.normal(0, 1, (h, w)).astype(np.float32), (0, 0), sigma)
        return f / (np.abs(f).max() + 1e-6)
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    dx, dy = field(0.06 * cap * k) * amp, field(0.06 * cap * k) * amp
    m = cv2.remap(m, gx + dx, gy + dy, cv2.INTER_LINEAR, borderValue=0)
    rough = field(0.012 * cap * k) * 0.5 + field(0.004 * cap * k) * 0.5
    m = np.clip((m - 0.5) * 3.0 + 0.5 + rough * 0.35, 0, 1)
    speck = rng.random((h, w)).astype(np.float32)
    speck = cv2.GaussianBlur(speck, (0, 0), 0.6 * k)
    m *= np.clip(1.0 - (speck < np.percentile(speck, 6)) * 0.85, 0, 1)

    m = cv2.resize(m, (max(1, w // k), max(1, h // k)), interpolation=cv2.INTER_AREA)
    ys, xs = np.nonzero(m > 0.03)
    m = m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    out = np.zeros(m.shape + (4,), np.uint8)
    out[..., :3] = WORD_INK
    out[..., 3] = np.clip(m * 255.0, 0, 255).astype(np.uint8)
    return out
CHALK = (236, 232, 224)


def load_rgba(name):
    return cv2.imread(os.path.join(BRAND, name), cv2.IMREAD_UNCHANGED)


def paste_rgba(dst, sprite, x, y, h, gain=0.94):
    """Scale an RGBA sprite to height h and alpha-blend it onto dst (BGR) at x, y."""
    w = max(1, int(round(sprite.shape[1] * h / sprite.shape[0])))
    s = cv2.resize(sprite, (w, h), interpolation=cv2.INTER_AREA).astype(np.float32)
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(dst.shape[1], x + w), min(dst.shape[0], y + h)
    s = s[y0 - y:y1 - y, x0 - x:x1 - x]
    a = s[..., 3:4] / 255.0
    roi = dst[y0:y1, x0:x1].astype(np.float32)
    dst[y0:y1, x0:x1] = np.clip(roi * (1 - a) + s[..., :3] * gain * a, 0, 255).astype(np.uint8)
    return w


def text_mask(img, box, delta=26):
    """Pixels inside box that are clearly brighter than the local background."""
    x, y, w, h = box
    L = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)[..., 0]
    bg = cv2.medianBlur(L, 41).astype(np.int16)
    m = np.zeros(L.shape, np.uint8)
    sub = (L.astype(np.int16) - bg) > delta
    m[y:y + h, x:x + w] = sub[y:y + h, x:x + w].astype(np.uint8) * 255
    return m


def erase(img, mask, grow=4):
    """Inpaint mask and put back grain sampled from the untouched surroundings."""
    m = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=grow)
    filled = cv2.inpaint(img, m, 9, cv2.INPAINT_TELEA).astype(np.float32)
    rng = np.random.default_rng(7)
    ring = cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=10) & ~m
    hp = img.astype(np.float32) - cv2.GaussianBlur(img.astype(np.float32), (0, 0), 2.0)
    sd = float(hp[ring > 0].std()) if (ring > 0).any() else 4.0
    grain = rng.normal(0.0, sd, img.shape[:2]).astype(np.float32)
    grain = cv2.GaussianBlur(grain, (0, 0), 0.7)[..., None] * 1.3
    out = filled + grain
    a = cv2.GaussianBlur(m.astype(np.float32) / 255.0, (0, 0), 1.2)[..., None]
    return np.clip(img * (1 - a) + out * a, 0, 255).astype(np.uint8)


def components(img, region):
    x, y, w, h = region
    roi = img[y:y + h, x:x + w]
    L = cv2.cvtColor(roi, cv2.COLOR_BGR2LAB)[..., 0].astype(np.float32)
    bg = cv2.medianBlur(L.astype(np.uint8), 41).astype(np.float32)
    m = ((L - bg) > 28).astype(np.uint8)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((5, 3), np.uint8))
    _, _, st, _ = cv2.connectedComponentsWithStats(m)
    return sorted((int(a) + x, int(b) + y, int(c), int(d)) for a, b, c, d, _ in st[1:])


def header(img, cfg):
    """Swap the star + TLEET for emblem + РИЛАЙ and shift "// NAME" right."""
    H, W = img.shape[:2]
    comps = components(img, (0, 0, int(W * 0.62), int(H * 0.17)))
    comps = [c for c in comps if 35 < c[3] < 160 and c[2] < 200]
    star = max((c for c in comps if c[2] > 60 and c[0] < 170), key=lambda c: c[2] * c[3])
    letters = [c for c in comps if c[0] > star[0] + star[2] and abs(c[1] - star[1] - star[3] // 2) < 120]
    # the lettering shares one baseline; drop stray parts (screws, art) off it
    base = int(np.median([c[1] + c[3] for c in letters[:6]]))
    letters = [c for c in letters if abs(c[1] + c[3] - base) < 8]
    tleet, rest = letters[:5], letters[5:]
    rest = rest[:len(rest)]  # "//" (one or two parts) + the plugin name
    cap = int(np.median([c[3] for c in tleet]))
    # the name ends at the first wide gap; anything further right is artwork
    kept = []
    for c in rest:
        if kept and c[0] - max(k[0] + k[2] for k in kept) > 0.8 * cap:
            break
        kept.append(c)
    rest = kept
    top = base - cap
    tx0 = tleet[0][0]
    tx1 = tleet[-1][0] + tleet[-1][2]
    rx0 = min(c[0] for c in rest)
    rx1 = max(c[0] + c[2] for c in rest)
    gap = rx0 - tx1

    pad = cfg.get("pad", 6)
    rest_box = (rx0 - pad, top - pad - 8, rx1 - rx0 + 2 * pad + cfg.get("pad_right", 0), cap + 2 * pad + 16 + cfg.get("pad_below", 0))
    rest_mask = text_mask(img, rest_box, cfg.get("delta", 26))

    mask = np.zeros(img.shape[:2], np.uint8)
    for (x, y, w, h) in [star] + tleet:
        mask |= text_mask(img, (x - pad, y - pad, w + 2 * pad, h + 2 * pad + cfg.get("pad_below", 0)), cfg.get("delta", 26))
    mask |= rest_mask
    out = erase(img, mask)

    eh = int(round(max(star[3], cap) * cfg.get("emblem_scale", 1.18)))
    ecx, ecy = star[0] + star[2] // 2, top + cap // 2
    emb = load_rgba("emblem.png")
    ew = int(round(emb.shape[1] * eh / emb.shape[0]))
    ex = min(ecx - ew // 2, tx0 - ew - int(0.3 * cap))
    paste_rgba(out, emb, ex, ecy - eh // 2, eh)

    wh = int(round(cap * cfg.get("word_scale", 0.92)))
    wx = max(tx0, ex + ew + int(0.3 * cap))
    ww = paste_rgba(out, load_rgba("wordmark.png"), wx, base - wh, wh)
    # "// NAME" in the same chalk lettering, same cap height and baseline
    name = cfg.get("title", "// " + cfg.get("name", ""))
    spr = chalk_text(name, int(round(wh * WORD_CAP)))
    nx = wx + ww + int(round(0.42 * wh))
    paste_rgba(out, spr, nx, base - spr.shape[0], spr.shape[0], gain=1.0)
    shift = nx - rx0
    return out, {"cap": cap, "shift": shift, "word_w": ww}


def letter(img, p):
    """Paint over a printed mark and set new text in the same spot, cap
    height, ink colour and (scaled) letter spacing."""
    x, y, w, h = p["box"]
    dark = p.get("ink") == "dark"
    L = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)[..., 0].astype(np.int16)
    bg = cv2.medianBlur(L.astype(np.uint8), 41).astype(np.int16)
    diff = (bg - L) if dark else (L - bg)
    m = np.zeros(L.shape, np.uint8)
    m[y:y + h, x:x + w] = (diff[y:y + h, x:x + w] > p.get("delta", 22)).astype(np.uint8) * 255
    # drop grain specks, then keep the strongest band of text rows so a
    # neighbouring line clipped by the box does not count
    _, lab, st, _ = cv2.connectedComponentsWithStats(m)
    keep = np.isin(lab, [i for i in range(1, len(st)) if st[i, 4] >= 6 and st[i, 3] >= 4])
    m = (keep * 255).astype(np.uint8)
    prof = m.sum(1).astype(np.float32)
    rows = prof > 0.2 * prof.max()
    peak = int(np.argmax(prof))
    r0 = peak
    while r0 > 0 and rows[r0 - 1]:
        r0 -= 1
    r1 = peak
    while r1 < len(rows) - 1 and rows[r1 + 1]:
        r1 += 1
    band_h = r1 - r0 + 1
    _, lab, st, _ = cv2.connectedComponentsWithStats(m)
    good = [i for i in range(1, len(st))
            if r0 - 2 <= st[i, 1] + st[i, 3] / 2 <= r1 + 2 and st[i, 3] >= 0.4 * band_h]
    m = (np.isin(lab, good) * 255).astype(np.uint8)
    ys, xs = np.nonzero(m)
    bx0, bx1, by0, by1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    if os.environ.get('REBRAND_DEBUG'):
        print('  patch', p['box'], '->', (int(bx0), int(by0), int(bx1 - bx0), int(by1 - by0)))
    ink = img[m > 0].reshape(-1, 3).astype(np.float32)
    lum = ink.mean(axis=1)
    ink_col = np.median(ink[lum <= np.percentile(lum, 25)] if dark else ink[lum >= np.percentile(lum, 75)], axis=0)
    if dark:
        out = cv2.inpaint(img, cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=2), 5, cv2.INPAINT_TELEA)
    else:
        out = erase(img, m, p.get("grow", 2))

    cap = p.get("cap", by1 - by0)
    probe = oswald(100)
    hb = probe.getbbox("H")
    size = max(6, int(round(100.0 * cap / (hb[3] - hb[1]))))
    font = oswald(size)
    text = p["text"]
    target_w = (bx1 - bx0) * p.get("width", 1.0)      # same footprint as the old mark
    plain = sum(font.getlength(ch) for ch in text)
    spacing = max(0.0, (target_w - plain) / max(1, len(text) - 1))

    pil = Image.new("L", (int(target_w + size * 4), size * 2), 0)
    d = ImageDraw.Draw(pil)
    cx = float(size)
    for ch in text:
        d.text((cx, size // 2), ch, font=font, fill=255)
        cx += font.getlength(ch) + spacing
    arr = np.array(pil)
    ys, xs = np.nonzero(arr)
    arr = arr[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    if p.get("angle"):
        # text printed at a slant (e.g. on a reel): rotate the setting to match
        hh, ww = arr.shape
        rot = cv2.getRotationMatrix2D((ww / 2, hh / 2), -p["angle"], 1.0)
        cos, sin = abs(rot[0, 0]), abs(rot[0, 1])
        nw, nh = int(ww * cos + hh * sin) + 2, int(ww * sin + hh * cos) + 2
        rot[0, 2] += nw / 2 - ww / 2
        rot[1, 2] += nh / 2 - hh / 2
        arr = cv2.warpAffine(arr, rot, (nw, nh), flags=cv2.INTER_LINEAR)
        ys, xs = np.nonzero(arr > 8)
        arr = arr[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    th, tw = arr.shape
    if p.get("align") == "centre":
        ox = (bx0 + bx1) // 2 - tw // 2
    else:
        ox = bx0
    oy = (by0 + by1) // 2 - th // 2
    a = arr.astype(np.float32)[..., None] / 255.0 * p.get("opacity", 0.95)
    roi = out[oy:oy + th, ox:ox + tw].astype(np.float32)
    out[oy:oy + th, ox:ox + tw] = np.clip(roi * (1 - a) + ink_col * a, 0, 255).astype(np.uint8)
    return out


def recolour_bgr(img):
    if img.ndim == 3 and img.shape[2] == 4:
        rgb = recolour(img[..., 2::-1].astype(np.float32) / 255.0)
        return np.dstack([np.round(rgb[..., ::-1] * 255).astype(np.uint8), img[..., 3]])
    rgb = recolour(img[..., ::-1].astype(np.float32) / 255.0)
    return np.round(rgb[..., ::-1] * 255).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--preview", default=None)
    args = ap.parse_args()
    patches = json.load(open(os.path.join(HERE, "text_patches.json")))

    for d in sorted(glob.glob(os.path.join(ROOT, "assets", "processed", "*"))):
        name = os.path.basename(d)
        if args.only and name != args.only:
            continue
        cfg = patches.get(name, {})
        for f in sorted(glob.glob(os.path.join(d, "*.png"))):
            img = cv2.imread(f, cv2.IMREAD_UNCHANGED)
            if os.path.basename(f) == "plate.png":
                img, info = header(img, dict(cfg.get("header", {}), name=name))
                print(name, info)
                for p in cfg.get("patches", []):
                    img = letter(img, p)
            for p in cfg.get("sprites", {}).get(os.path.basename(f), []):
                alpha = img[..., 3:] if img.shape[2] == 4 else None
                img = letter(np.ascontiguousarray(img[..., :3]), p)
                if alpha is not None:
                    img = np.dstack([img, alpha])
            img = recolour_bgr(img)
            if args.preview:
                os.makedirs(os.path.join(args.preview, name), exist_ok=True)
                cv2.imwrite(os.path.join(args.preview, name, os.path.basename(f)), img)
            else:
                cv2.imwrite(f, img)


if __name__ == "__main__":
    main()
