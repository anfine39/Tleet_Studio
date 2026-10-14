#!/usr/bin/env python3
"""Skin preparation for TLEET // TAPE.

Turns the approved mockup (assets/reference/TAPE.png) into the runtime assets
  assets/processed/TAPE/plate.png       faceplate
  assets/processed/TAPE/<sprite>.png    lit buttons, LED segments, VU caps, reels (RGBA)
  assets/processed/TAPE/sprites.json    where each sprite goes (reference pixels)
using the geometry in plugins/TAPE/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - all thirteen knob pointers and their glow, every scale dot (the printed
    dots are unevenly spaced), every number printed on a knob scale, and the
    values in the readout windows;
  - the lit TAPE TYPE, TAPE SPEED, CALIBRATION and NOISE buttons and the lit
    AUTO GAIN button;
  - both needles of both VU meters (the pivot caps become sprites drawn over
    the live needles);
  - the lit SATURATION segments (lit white and lit red segment sprites);
  - the TAPE CONDITION curve, fill and grid, and its frequency labels.

The reels: the spec asks for reel animation, and the artwork's reels are
clipped by the window and not symmetric, so a turning reel is rebuilt as a
three-window NAB reel from each reel's own picture:
  - the light that falls on the reel (the lamp at the head block, the dark
    lower left) is measured on the bare metal and divided out, so it stays
    in place while the reel turns: <reel>_light.png holds it (RGB = light x
    128) and, in alpha, where the reel can be seen (below the window's top
    edge, behind the two tension levers);
  - the reel itself (<reel>.png) is one window's half-sector, mirrored and
    repeated every 120 degrees, with the hub, the hub ring, the label on
    the left reel and the TLEET print on the right one kept from the
    artwork; the hub is re-centred on the flange.

Usage: python3 tools/skinprep/prep_tape.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import feather_composite, match_illumination, rect_mask
from prep_echo import erase_captions, erase_dot, pad
from prep_steel import flat_fill, glass_fill, white_alpha, hole
from prep_veil import resize_to, sprite_from
from prep_void import button_group, caption_alpha, deglow, pointer_out, printed_mask

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "TAPE.png")
OUT = os.path.join(ROOT, "assets", "processed", "TAPE")
UI_MAP = os.path.join(ROOT, "plugins", "TAPE", "UI_MAP.json")

HALO = 14


# ---------------------------------------------------------------- knobs

def printed_dots(img, c, threshold=40.0):
    """The printed scale dots: peaks of fine detail along the dot circle
    (the dim end dots included)."""
    cx, cy = c["scaleCentre"]
    R = c["dotRadius"]
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    hp = g - cv2.GaussianBlur(g, (0, 0), 4)
    a = np.radians(np.arange(-180.0, 180.0, 0.5))
    rs = np.arange(R - 3.0, R + 3.1, 1.0)
    xs = (cx + rs[:, None] * np.sin(a)[None, :]).astype(np.float32)
    ys = (cy - rs[:, None] * np.cos(a)[None, :]).astype(np.float32)
    v = cv2.remap(hp, xs, ys, cv2.INTER_LINEAR)
    best = v.max(axis=0)
    ridx = v.argmax(axis=0)
    n = len(a)
    pts = []
    for i in range(n):
        win = [best[(i + k) % n] for k in range(-8, 9)]
        if best[i] > threshold and best[i] == max(win):
            pts.append((float(xs[ridx[i], i]), float(ys[ridx[i], i])))
    return pts


# ---------------------------------------------------------------- AUTO GAIN

def auto_gain(img, plate, c):
    """AUTO GAIN is lit in the artwork: dark on the plate (the dark NOISE ON
    body fitted to it, its caption in the donor's ink, its light around
    removed); lit sprite = the artwork's lit button."""
    g = HALO
    x, y, w, h = c["body"]
    a = white_alpha(img[y:y + h, x:x + w], margin=11)  # the lit frame is not caption
    dx, dy, dw, dh = c["darkDonor"]
    donor = img[dy:dy + dh, dx:dx + dw]
    da = caption_alpha(donor, 0)
    dark_ink = np.median(donor[da > 0.8].astype(np.float32), axis=0)
    blank = cv2.inpaint(donor, hole(da), 5, cv2.INPAINT_TELEA).astype(np.float32)
    fitted = resize_to(blank, w, h)
    aa = cv2.GaussianBlur(a, (0, 0), 0.5)[..., None]
    fitted = fitted * (1 - aa) + dark_ink * aa
    ring = img[y - g:y + h + g, x - g:x + w + g].copy()
    pasted = plate.copy()
    pasted[y:y + h, x:x + w] = np.clip(fitted, 0, 255).astype(np.uint8)
    plate = feather_composite(plate, pasted, rect_mask(plate.shape, [(x, y, w - 1, h - 1)]), 0.8)
    plate = deglow(plate, (x, y, w, h), g, inside=False)
    lit = plate.copy()
    lit[y - g:y + h + g, x - g:x + w + g] = ring
    return plate, lit


# ---------------------------------------------------------------- VU

def remove_needles(img, plate, vu):
    """Both baked needles (a dark one and a faint one, each with a light
    edge). The light step the artwork has at each needle is ramped out
    first (lift_needle_step); then every needle pixel is taken from the same
    radius around the scale's centre a few degrees to either side, the
    lighter of the two (ink and the pivot cap are dark, the paper light), so
    the face's radial shading and the scale arc stay continuous. Above the
    arc's inner edge only the needle's tip is taken out, never the printed
    ticks (+-1.6 px around each tick's ray) or the arc itself."""
    px, py = vu["pivot"]
    yy, xx = np.mgrid[0:plate.shape[0], 0:plate.shape[1]].astype(np.float32)
    r = np.hypot(xx - px, yy - py)
    ang = np.degrees(np.arctan2(xx - px, py - yy))
    cx, cy = vu["cap"]["centre"]
    arc = vu["arcRadius"]
    off_cap = np.hypot(xx - cx, yy - cy) > vu["cap"]["radius"] + 0.5
    near_tick = np.zeros(plate.shape[:2], bool)
    for deg in vu["scaleDegrees"].values():
        near_tick |= np.abs((ang - float(deg)) * np.radians(1.0) * r) < 1.6
    stub_zone = (r >= arc - 6.0) & (r <= arc + 14.0) & ~near_tick & (np.abs(r - arc) > 1.2)
    fx, fy, fw, fh = vu["face"]
    face = rect_mask(plate.shape, [(fx, fy, fw, fh)]) > 0
    for a, b in vu["bakedNeedles"]:
        plate = lift_needle_step(img, plate, vu, a, b)

    def lighter_of(turn):
        srcs = []
        for t in (turn, -turn):
            m = cv2.getRotationMatrix2D((float(px), float(py)), t, 1.0)
            srcs.append(cv2.warpAffine(plate, m, (plate.shape[1], plate.shape[0]), flags=cv2.INTER_LINEAR,
                                       borderMode=cv2.BORDER_REFLECT).astype(np.float32))
        pick = (srcs[0].sum(axis=2) >= srcs[1].sum(axis=2))[..., None]
        return np.where(pick, srcs[0], srcs[1])

    for (x0, y0), (x1, y1) in vu["bakedNeedles"]:
        line = np.zeros(plate.shape[:2], np.uint8)
        cv2.line(line, (int(round(x0)), int(round(y0))), (int(round(x1)), int(round(y1 + 4))), 255, 7)
        tip = np.zeros_like(line)
        cv2.line(tip, (int(round(x0)), int(round(y0))), (int(round(x1)), int(round(y1))), 255, 5)
        main = (line > 0) & (r < arc - 6.0) & off_cap & face
        top = (tip > 0) & stub_zone & face
        out = plate.astype(np.float32)
        out[main] = lighter_of(4.0)[main]
        out[top] = lighter_of(3.0)[top]
        mask = ((main | top) * 255).astype(np.uint8)
        plate = feather_composite(plate, np.clip(out, 0, 255).astype(np.uint8), mask, 0.7)
    return plate


def lift_needle_step(img, plate, vu, a, b):
    """The artwork lights the face inside the two needles' V more than
    outside it, with a hard step at each needle. Along the needle, the step
    is measured on the artwork (both sides, 4..9 px from the line) and
    turned into a ramp 80 px wide (the dark side lifted, the light side
    lowered, each by half of it at the line), so the face shades smoothly
    through where the needle was."""
    (x0, y0), (x1, y1) = a, b
    d = np.array([x1 - x0, y1 - y0], np.float32)
    L = float(np.hypot(*d))
    u = d / L
    nrm = np.array([-u[1], u[0]], np.float32)
    fx, fy, fw, fh = vu["face"]
    yy, xx = np.mgrid[fy:fy + fh, fx:fx + fw].astype(np.float32)
    along = (xx - x0) * u[0] + (yy - y0) * u[1]
    across = (xx - x0) * nrm[0] + (yy - y0) * nrm[1]
    art = cv2.GaussianBlur(img[fy:fy + fh, fx:fx + fw].astype(np.float32), (0, 0), 1.2)
    reg = plate[fy:fy + fh, fx:fx + fw].astype(np.float32)
    bins = np.linspace(0.0, L, 9)
    centres, ratios, sides = [], [], []
    for lo, hi in zip(bins[:-1], bins[1:]):
        span = (along >= lo) & (along < hi)
        neg = np.median(art[span & (across >= -9) & (across <= -4)], axis=0)
        pos = np.median(art[span & (across >= 4) & (across <= 9)], axis=0)
        centres.append(0.5 * (lo + hi))
        side = 1.0 if neg.sum() > pos.sum() else -1.0  # +1: the dark side is +across
        bright, dark = (neg, pos) if side > 0 else (pos, neg)
        ratios.append(np.clip(bright / np.maximum(dark, 1.0), 1.0, 1.8))
        sides.append(side)
    side = float(np.sign(np.sum(sides)))
    ratios = np.array(ratios)
    r = np.stack([np.interp(along, centres, ratios[:, ch]) for ch in range(3)], axis=-1)
    # the step at the needle becomes a ramp across +-W, half of it on each
    # side (the needle itself is filled afterwards)
    W = 40.0
    x = across * side
    t = np.clip((x + W) / (2 * W), 0.0, 1.0)[..., None]
    expo = np.where((x < 0)[..., None], -t, 1.0 - t)
    ends = np.clip((along + 4.0) / 8.0, 0.0, 1.0) * np.clip((L + 6.0 - along) / 8.0, 0.0, 1.0)
    # only below the scale arc (the step is the needle's, not the scale's)
    px, py = vu["pivot"]
    below = np.clip((vu["arcRadius"] - 8.0 - np.hypot(xx - px, yy - py)) / 4.0, 0.0, 1.0)
    gain = np.power(r, expo * (ends * below)[..., None])
    out = plate.copy()
    out[fy:fy + fh, fx:fx + fw] = np.clip(reg * gain, 0, 255).astype(np.uint8)
    return out


def vu_cap(plate, vu):
    """The pivot cap over the live needles: the dome, cut at the face's
    lower edge."""
    cx, cy = vu["cap"]["centre"]
    r = vu["cap"]["radius"] + 1.0
    fx, fy, fw, fh = vu["face"]
    x0, y0 = int(np.floor(cx - r - 1)), int(np.floor(cy - r - 1))
    rect = [x0, y0, int(np.ceil(2 * r + 3)), fy + fh - y0]
    x, y, w, h = rect
    yy, xx = np.mgrid[y:y + h, x:x + w].astype(np.float32)
    a = np.clip(r - np.hypot(xx + 0.5 - cx, yy + 0.5 - cy) + 0.5, 0.0, 1.0)
    return np.dstack([plate[y:y + h, x:x + w], np.clip(a * 255, 0, 255).astype(np.uint8)]), rect


# ---------------------------------------------------------------- SATURATION

def seg_x(c, i):
    return c["first"] + i * c["pitch"]


def saturation_meter(img, plate, c):
    """Every segment dark: the dark run at the right (segments 19..23 with
    their housing) repeated leftwards at the segment pitch, sub-pixel exact.
    Lit sprites: a lit white and a lit red segment of the artwork as the
    light they add over the dark segment (colour and alpha such that
    dark * (1 - a) + colour * a == lit)."""
    hx, hy, hw, hh = c["housing"]
    u0, u1 = c["unlitArt"]
    pitch = c["pitch"]
    n = u1 - u0 + 1
    src_x0 = seg_x(c, u0) - pitch * 0.5
    span = n * pitch
    out = plate.astype(np.float32).copy()
    region = plate.astype(np.float32)
    lo = hx + 3
    cols = np.arange(lo, int(np.ceil(src_x0)) + 1)
    ks = np.ceil((src_x0 - cols) / span).astype(int)  # which repeat covers each column
    for k in np.unique(ks):
        m = np.float32([[1, 0, -k * span], [0, 1, 0]])
        moved = cv2.warpAffine(region, m, (plate.shape[1], plate.shape[0]), flags=cv2.INTER_CUBIC,
                               borderMode=cv2.BORDER_REFLECT)
        sel = cols[ks == k]
        out[hy:hy + hh, sel] = moved[hy:hy + hh, sel]
    dark = np.clip(out, 0, 255).astype(np.uint8)
    blend = rect_mask(plate.shape, [(hx + 3, hy, int(src_x0) - hx - 3, hh)])
    plate = feather_composite(plate, dark, blend, 0.7)
    # the rose light the lit segments threw on the housing and panel
    plate = deglow(plate, (hx, hy, int(src_x0) - hx, hh), 10, inside=False)

    sprites = {}
    for name, i in (("satWhite", c["litWhiteArt"]), ("satRed", c["litRedArt"])):
        x0 = seg_x(c, i)
        # the sprite is a whole number of pixels around the segment; the
        # segment's left edge sits `frac` into its first column
        x = int(np.floor(x0)) - 3
        w = int(c["segment"][0]) + 7
        y = int(c["top"]) - 3
        h = int(c["segment"][1]) + 6
        lit = img[y:y + h, x:x + w].astype(np.float32)
        dk = plate[y:y + h, x:x + w].astype(np.float32)
        a = np.max((lit - dk) / np.maximum(255.0 - dk, 1.0), axis=2)
        a = np.clip(a, 0.0, 1.0)
        a3 = np.maximum(a[..., None], 1e-3)
        colour = np.where(a[..., None] > 1e-3, (lit - dk * (1 - a3)) / a3, lit)
        sprites[name] = (np.dstack([np.clip(colour, 0, 255).astype(np.uint8), np.clip(a * 255.0, 0, 255).astype(np.uint8)]),
                         [x, y, w, h], x0 - x)
    return plate, sprites


# ---------------------------------------------------------------- REELS

HUB_IN, HUB_OUT = 56.0, 64.0


def reel_light(img, rc):
    """The light on the reel, per channel, relative to its median on the
    bare metal: a wide, normalised blur of the metal alone (windows, label,
    print, levers and anything above the window left out)."""
    H, W = img.shape[:2]
    im = img.astype(np.float32)
    fx, fy = rc["flange"]
    R = rc["radius"]
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    r = np.hypot(xx - fx, yy - fy)
    lum = im.mean(axis=2)
    med = cv2.medianBlur(lum.astype(np.uint8), 31).astype(np.float32)
    m = (r > HUB_OUT + 2) & (r < R - 4) & (yy > rc["windowTop"] + 2) & (lum > 0.75 * med) & (lum > 60)
    ang = np.degrees(np.arctan2(xx - fx, -(yy - fy))) % 360
    d = rc["decal"]
    m &= ~((ang >= d["angles"][0]) & (ang <= d["angles"][1]) & (r >= d["radii"][0]) & (r <= d["radii"][1]))
    for ox, oy, ow, oh in rc["occluders"]:
        m[oy - 3:oy + oh + 3, ox - 3:ox + ow + 3] = False
    m = m.astype(np.float32)

    def blur(s):
        den = cv2.GaussianBlur(m, (0, 0), s)
        L = np.dstack([cv2.GaussianBlur(im[..., ch] * m, (0, 0), s) for ch in range(3)]) / np.maximum(den, 1e-4)[..., None]
        return L, den

    near, den = blur(22)
    far, _ = blur(60)
    wgt = np.clip(den / 0.15, 0, 1)[..., None]
    L = near * wgt + far * (1 - wgt)
    ref = np.array([np.median(L[..., ch][m > 0]) for ch in range(3)], np.float32)
    return L / ref


def reel(img, rc):
    H, W = img.shape[:2]
    im = img.astype(np.float32)
    light = reel_light(img, rc)
    alb = im / np.maximum(light, 0.05)
    fx, fy = rc["flange"]
    hx, hy = rc["hub"]
    R = rc["radius"]
    # the levers in front of the flange: the flange 12 degrees away instead
    m = cv2.getRotationMatrix2D((fx, fy), 12.0, 1.0)
    rot = cv2.warpAffine(alb, m, (W, H), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)
    om = np.zeros((H, W), np.float32)
    for ox, oy, ow, oh in rc["occluders"]:
        om[oy - 4:oy + oh + 4, ox - 4:ox + ow + 4] = 1
    om = cv2.GaussianBlur(om, (0, 0), 1.5)[..., None]
    alb = alb * (1 - om) + rot * om

    n = int(np.ceil(R)) + 2
    size = 2 * n
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    dx, dy = xx + 0.5 - n, yy + 0.5 - n
    r = np.hypot(dx, dy)
    t = np.degrees(np.arctan2(dx, -dy)) % 360

    def art(rr, tt):
        k = np.clip((rr - HUB_IN) / (HUB_OUT - HUB_IN), 0, 1)
        cx = hx + (fx - hx) * k
        cy = hy + (fy - hy) * k
        a = np.radians(tt)
        return (cx + rr * np.sin(a)).astype(np.float32), (cy - rr * np.cos(a)).astype(np.float32)

    def sample(src, x, y):
        return cv2.remap(src, x, y, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)

    # three windows: the half-sector before the template window, mirrored
    # (and mirrored back before it reaches the previous window)
    rel = ((t - rc["phase"] + 60.0) % 120.0) - 60.0
    q = np.abs(rel)
    q = np.where(q > 52.0, 104.0 - q, q)
    outer = sample(alb, *art(r, rc["window"] - q))
    orig = sample(alb, *art(r, t))
    d = rc["decal"]

    def soft(v, lo, hi, f):
        return np.clip((v - lo) / f, 0, 1) * np.clip((hi - v) / f, 0, 1)

    keep = np.maximum(soft(t, d["angles"][0], d["angles"][1], 3.0) * soft(r, d["radii"][0], d["radii"][1], 3.0),
                      np.clip((HUB_OUT - r) / (HUB_OUT - HUB_IN), 0, 1))[..., None]
    body = outer * (1 - keep) + orig * keep
    alpha = np.clip(R - r + 0.5, 0, 1)
    sprite = np.dstack([np.clip(body, 0, 255).astype(np.uint8), np.clip(alpha * 255, 0, 255).astype(np.uint8)])

    # the fixed light and where the reel shows, in the same frame
    gx, gy = (xx + 0.5 - n + fx).astype(np.float32), (yy + 0.5 - n + fy).astype(np.float32)
    L = cv2.remap(light, gx - 0.5, gy - 0.5, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    vis = np.clip(gy - rc["windowTop"], 0, 1)
    for ox, oy, ow, oh in rc["occluders"]:
        inside = np.clip(np.minimum.reduce([gx - ox, ox + ow - gx, gy - oy, oy + oh - gy]) + 0.5, 0, 1)
        vis = vis * (1 - inside)
    lightmap = np.dstack([np.clip(L * 128.0, 0, 255).astype(np.uint8), np.clip(vis * 255, 0, 255).astype(np.uint8)])
    rect = [fx - n, fy - n, size, size]
    return sprite, lightmap, rect


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--debug", default=None)
    args = ap.parse_args()

    ui = json.load(open(UI_MAP))
    controls = {c["id"]: c for c in ui["controls"]}
    img = cv2.imread(REF, cv2.IMREAD_COLOR)
    assert img.shape[1] == ui["reference"]["width"] and img.shape[0] == ui["reference"]["height"]
    plate = img.copy()
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]].astype(np.float32)

    knobs = [c for c in ui["controls"] if c["type"] == "ROTARY"]
    for c in knobs:
        plate = pointer_out(img, plate, c, yy, xx)

    discs = [(c["centre"][0], c["centre"][1], c["rimRadius"] + 4) for c in knobs]
    avoid = printed_mask(img, discs)

    for c in knobs:
        for p in printed_dots(img, c):
            av = avoid.copy()
            av[int(p[1]) - 8:int(p[1]) + 9, int(p[0]) - 8:int(p[0]) + 9] = False
            for _ in range(2):  # the feathered edge of one pass leaves a faint ghost of a bright dot
                plate = erase_dot(plate, p, av, radius=6 if c["capRadius"] > 40 else 5)
        plate = erase_captions(plate, c.get("erase", []), avoid)
        # the value and its glow: the window's own glass tone, row by row
        plate = glass_fill(plate, pad(c["readout"], -3), 31)

    sprites = {}  # name -> (rgba, rect)
    for gid in ("tapeType", "speed", "calibration", "noise"):
        grp = controls[gid]
        plate, lit = button_group(img, plate, grp, 0, g=HALO, frame=7)
        for b in grp["buttons"]:
            name = gid + "_" + b["label"].lower().replace(".", "_").replace("+", "p").replace("-", "m")
            r = pad(b["body"], HALO)
            sprites[name] = (lit[b["label"]], r)

    ag = controls["autoGain"]
    plate, lit = auto_gain(img, plate, ag)
    sprites["autoGain_lit"] = (lit, pad(ag["body"], HALO))

    rgba = {}
    for vid in ("vuRecord", "vuRepro"):
        plate = remove_needles(img, plate, controls[vid])
    for vid in ("vuRecord", "vuRepro"):
        im, rect = vu_cap(plate, controls[vid])
        rgba[vid + "_cap"] = (im, rect)

    plate, segs = saturation_meter(img, plate, controls["saturationMeter"])
    seg_offsets = {}
    for name, (im, rect, off) in segs.items():
        rgba[name] = (im, rect)
        seg_offsets[name] = round(float(off), 3)

    tc = controls["tapeCondition"]
    plate = flat_fill(plate, tc["erase"][0], 13)
    plate = erase_captions(plate, tc["eraseLabels"], avoid)

    reels = {}
    for rid in ("reelLeft", "reelRight"):
        sprite, lightmap, rect = reel(img, controls[rid])
        reels[rid] = (sprite, lightmap, rect)

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    rects = {}
    for name, (lit_img, rect) in sprites.items():
        cv2.imwrite(os.path.join(OUT, name + ".png"), sprite_from(lit_img, plate, rect), [cv2.IMWRITE_PNG_COMPRESSION, 9])
        rects[name] = [int(v) for v in rect]
    for name, (im, rect) in rgba.items():
        cv2.imwrite(os.path.join(OUT, name + ".png"), im, [cv2.IMWRITE_PNG_COMPRESSION, 9])
        rects[name] = [int(v) for v in rect]
    for rid, (sprite, lightmap, rect) in reels.items():
        cv2.imwrite(os.path.join(OUT, rid + ".png"), sprite, [cv2.IMWRITE_PNG_COMPRESSION, 9])
        cv2.imwrite(os.path.join(OUT, rid + "_light.png"), lightmap, [cv2.IMWRITE_PNG_COMPRESSION, 9])
        rects[rid] = [round(float(v), 2) for v in rect]
        rects[rid + "_light"] = rects[rid]
    with open(os.path.join(OUT, "sprites.json"), "w") as f:
        json.dump({"sprites": rects, "segmentOffset": seg_offsets}, f, indent=1)
    print("sprites", json.dumps(rects))
    print("wrote", OUT)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        for c in ui["controls"]:
            boxes = [c["hitbox"]] if "hitbox" in c else []
            boxes += [b["hitbox"] for b in c.get("buttons", [])]
            for x, y, w, h in boxes:
                cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (0, 255, 0), 1)
            if c["type"] == "ROTARY":
                cx, cy = c["centre"]
                cv2.drawMarker(dbg, (int(round(cx)), int(round(cy))), (0, 0, 255), cv2.MARKER_CROSS, 10, 1)
                p = c["pointer"]
                for deg in c["travelDegrees"]:
                    a = np.radians(deg)
                    cv2.line(dbg, (int(cx + np.sin(a) * p["inner"]), int(cy - np.cos(a) * p["inner"])),
                             (int(cx + np.sin(a) * p["outer"]), int(cy - np.cos(a) * p["outer"])), (255, 0, 255), 1)
                cv2.circle(dbg, (int(round(cx)), int(round(cy))), int(round(c["dotRadius"])), (255, 255, 0), 1)
                x, y, w, h = c["readout"]
                cv2.rectangle(dbg, (x, y), (x + w, y + h), (0, 200, 255), 1)
            if c["type"] == "VU":
                px, py = c["pivot"]
                for v, deg in c["scaleDegrees"].items():
                    a = np.radians(deg)
                    cv2.line(dbg, (int(px + np.sin(a) * (c["arcRadius"] - 30)), int(py - np.cos(a) * (c["arcRadius"] - 30))),
                             (int(px + np.sin(a) * c["needleLength"]), int(py - np.cos(a) * c["needleLength"])), (255, 0, 255), 1)
            if c["type"] == "LED_BAR":
                for i in range(c["count"]):
                    x = seg_x(c, i)
                    cv2.rectangle(dbg, (int(round(x)), c["top"]), (int(round(x + c["segment"][0])), c["top"] + c["segment"][1]),
                                  (0, 0, 255) if i >= c["redFrom"] else (255, 255, 0), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
