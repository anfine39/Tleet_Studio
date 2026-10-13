#!/usr/bin/env python3
"""Skin preparation for TLEET // WEIGHT.

Turns the approved mockup (assets/reference/WEIGHT.png) into the runtime assets
  assets/processed/WEIGHT/plate.png        faceplate
  assets/processed/WEIGHT/<sprite>.png     lit lamps (RGBA), cut from the mockup
using the geometry in plugins/WEIGHT/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - knob pointers (all nine knobs);
  - every number printed on a knob scale (the dots of continuous knobs stay):
    the editor draws the parameter's real value at the angle where the knob
    really has it;
  - all scale dots of the stepped knobs (attack, release, ratio): the printed
    dots do not sit at the real step angles; the editor draws one per step;
  - the lit lamps (THRUST, DETECTOR, TONE, GR MODE, EXTERNAL SC): the plate
    shows every lamp dark, the editor draws the lit lamp of the real state;
  - both LED bars with their glow: the plate keeps the windows, rails, ticks
    and printed numbers; the editor draws every segment from the actual
    per-channel gain reduction.

Usage: python3 tools/skinprep/prep_weight.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import erase_dot, feather_composite, lamp_off, match_illumination, rect_mask, remove_pointer_mirrored

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "WEIGHT.png")
OUT = os.path.join(ROOT, "assets", "processed", "WEIGHT")
UI_MAP = os.path.join(ROOT, "plugins", "WEIGHT", "UI_MAP.json")


def pad(rect, p):
    x, y, w, h = rect
    return [x - p, y - p, w + 2 * p, h + 2 * p]


def erase_label(plate, rect, centre, avoid, sigma=4.0):
    """Printed text: panel texture from beside, above or below the text (away
    from the knob), never from a place with a scale dot, a knob or other
    print in it; smooth brightness from the surroundings."""
    x, y, w, h = rect
    mask = rect_mask(plate.shape, [(x, y, w, h)])
    gray = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    detail = np.abs(gray - cv2.GaussianBlur(gray, (0, 0), 2.0))
    sx = 1 if x + w / 2 > centre[0] else -1
    sy = 1 if y + h / 2 > centre[1] else -1
    best = None
    for dx, dy in ((sx * (w + 6), 0), (sx * (w + 14), 0), (-sx * (w + 6), 0), (0, -sy * (h + 4)),
                   (0, sy * (h + 4)), (sx * (w + 6), -sy * (h + 4)), (sx * (w + 24), 0)):
        x0, y0 = x + dx, y + dy
        if x0 < 0 or y0 < 0 or x0 + w >= plate.shape[1] or y0 + h >= plate.shape[0]:
            continue
        blocked = avoid[y0:y0 + h, x0:x0 + w].any()
        score = float(detail[y0:y0 + h, x0:x0 + w].mean()) + (1e3 if blocked else 0.0)
        if best is None or score < best[0]:
            best = (score, dx, dy)
    _, dx, dy = best
    source = np.roll(np.roll(plate, -dy, axis=0), -dx, axis=1)
    # the brightness around the text, without the dots and print next to it
    near = np.zeros(plate.shape[:2], np.uint8)
    near[max(0, y - 14):y + h + 14, max(0, x - 14):x + w + 14] = 1
    others = ((avoid & (near > 0)) & (mask == 0)).astype(np.uint8) * 255
    clean = cv2.inpaint(plate, cv2.dilate(others, np.ones((3, 3), np.uint8)), 5, cv2.INPAINT_TELEA) if others.any() else plate
    filled = match_illumination(clean, source, mask, sigma=sigma)
    return feather_composite(plate, filled, cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1), 1.2)


def rect_alpha(shape, rect, feather):
    m = np.zeros(shape[:2], np.uint8)
    x, y, w, h = rect
    m[y:y + h, x:x + w] = 255
    return m


def unlit_in_tray(plate, group):
    """A lamp lit in a tray of separate buttons: the tray floor and rails,
    row by row, from the gaps beside it; on top, a copy of a dark sibling
    button (with its rim of shadow) at the lit button's place."""
    u = group["unlit"]
    x, y, w, h = u["cell"]
    cols = np.concatenate([np.arange(a, b) for a, b in u["floor"]])
    floor = np.median(plate[y:y + h, cols].astype(np.float32), axis=1)  # (h, 3)
    out = plate.copy()
    out[y:y + h, x:x + w] = np.clip(floor[:, None, :], 0, 255).astype(np.uint8)

    lit = next(b for b in group["buttons"] if b["rect"][0] >= x and b["rect"][0] + b["rect"][2] <= x + w)
    src = group["buttons"][u["copyFrom"]]["rect"]
    sx, sy, sw, sh = pad(src, 3)
    dx = int(round(lit["rect"][0] + lit["rect"][2] / 2 - (src[0] + src[2] / 2)))
    dy = int(round(lit["rect"][1] + lit["rect"][3] / 2 - (src[1] + src[3] / 2)))
    btn_mask = np.zeros(plate.shape[:2], np.uint8)
    btn_mask[sy + dy:sy + dy + sh, sx + dx:sx + dx + sw] = 255
    pasted = out.copy()
    pasted[sy + dy:sy + dy + sh, sx + dx:sx + dx + sw] = plate[sy:sy + sh, sx:sx + sw]
    out = feather_composite(out, pasted, btn_mask, 1.0)

    cell = rect_alpha(plate.shape, u["cell"], 0)
    return feather_composite(plate, out, cell, 1.5)


def unlit_in_housing(plate, group):
    """FEED FORWARD: the lamp sits in a wide button face; the whole face gets
    the rows of the plain FEED BACK face (dark), with a little of its grain."""
    u = group["unlit"]
    x, y, w, h = u["face"]
    fx, fy, fw, fh = u["faceFrom"]
    src = plate[fy:fy + fh, fx:fx + fw].astype(np.float32)
    rows = np.median(src, axis=1)  # (fh, 3)
    rows = cv2.resize(rows[None, :, :], (h, 1), interpolation=cv2.INTER_LINEAR)[0]
    grain = src - cv2.GaussianBlur(src, (0, 0), 1.5)
    grain = np.tile(grain, (int(np.ceil(h / fh)) + 1, int(np.ceil(w / fw)) + 1, 1))[:h, :w]
    out = plate.copy()
    out[y:y + h, x:x + w] = np.clip(rows[:, None, :] + grain, 0, 255).astype(np.uint8)
    mask = rect_alpha(plate.shape, u["face"], 0)
    return feather_composite(plate, out, mask, 1.2)


def unwarm(plate, rect, margin=(20, 18)):
    """The amber spill of a lit lamp on the rims, the tray and the panel
    around it: warm, saturated pixels near the lamp go to the dim neutral
    tone of an unlit rim. Cream print and the blue panel are not warm and
    saturated enough to be touched."""
    x, y, w, h = rect
    mx, my = margin
    x0, y0, x1, y1 = x - mx, y - my, x + w + mx, y + h + my
    reg = plate[y0:y1, x0:x1].astype(np.float32)
    hsv = cv2.cvtColor(plate[y0:y1, x0:x1], cv2.COLOR_BGR2HSV).astype(np.float32)
    hue_ok = ((hsv[..., 0] >= 4) & (hsv[..., 0] <= 30)).astype(np.float32)
    warmth = hue_ok * np.clip((hsv[..., 1] - 70.0) / 90.0, 0.0, 1.0)
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    cx, cy = x + w / 2.0, y + h / 2.0
    sx, sy = w / 2.0 + mx * 0.6, h / 2.0 + my * 0.6
    near = np.exp(-0.5 * (((xx - cx) / sx) ** 2 + ((yy - cy) / sy) ** 2))
    wgt = (warmth * np.clip(near * 1.6, 0.0, 1.0))[..., None]
    lum = reg.mean(axis=2, keepdims=True)
    neutral = lum * 0.72 * np.array([0.92, 1.0, 1.08], np.float32)  # BGR: a touch cooler
    out = plate.copy()
    out[y0:y1, x0:x1] = np.clip(reg * (1 - wgt) + neutral * wgt, 0, 255).astype(np.uint8)
    return out


def deglow_housing(img, plate, rect, ref_rect):
    """The glow of a lit lamp on the bevels around it: slow light level taken
    down to that of the same bevel around a dark button."""
    x, y, w, h = rect
    rx, ry, rw, rh = ref_rect
    base = cv2.GaussianBlur(plate[y:y + h, x:x + w].astype(np.float32), (0, 0), 3.0)
    ref = cv2.GaussianBlur(plate[ry:ry + rh, rx:rx + rw].astype(np.float32), (0, 0), 3.0)
    ref_rows = np.median(ref, axis=1, keepdims=True)
    glow = np.clip(base - ref_rows, 0, None)
    out = plate.copy()
    out[y:y + h, x:x + w] = np.clip(plate[y:y + h, x:x + w].astype(np.float32) - glow, 0, 255).astype(np.uint8)
    return out


def sprite_from(img, plate, rect):
    """The lit original where it is brighter than the dark plate, with an
    alpha that brings back exactly the lit look over the plate."""
    x, y, w, h = rect
    lit = img[y:y + h, x:x + w].astype(np.float32)
    dark = plate[y:y + h, x:x + w].astype(np.float32)
    diff = (lit - dark).max(axis=2)
    alpha = np.clip((diff - 4.0) / 36.0, 0.0, 1.0)
    alpha = cv2.GaussianBlur(alpha, (0, 0), 0.8)
    # at the rect's border the sprite must vanish
    edge = np.ones_like(alpha)
    for k in range(3):
        edge[k, :] = edge[-1 - k, :] = edge[:, k] = edge[:, -1 - k] = min(edge[k, 0], (k + 0.5) / 3.0)
    alpha *= edge
    return np.dstack([img[y:y + h, x:x + w], np.clip(alpha * 255.0, 0, 255).astype(np.uint8)])


def polar_maps(meter, shape):
    """Distance below the rail (d, px) and angle (deg) of every pixel, around
    the meter's arc centre."""
    cx, cy = meter["arc"]["centre"]
    R = meter["arc"]["railRadius"]
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]].astype(np.float32)
    r = np.hypot(xx - cx, yy - cy)
    a = np.degrees(np.arctan2(xx - cx, -(yy - cy)))
    return R - r, a


def clear_bar(img, plate, meter):
    """All segments out, the rail and ticks kept. Inside the bar's band each
    pixel takes the colour that the unlit end of the window has at the same
    distance below the rail; then the glow of the lit part is taken out of
    the window (the editor draws the glow of the bar it lights)."""
    d, a = polar_maps(meter, plate.shape)
    wx, wy, ww, wh = meter["window"]
    inside = np.zeros(plate.shape[:2], bool)
    inside[wy:wy + wh, wx:wx + ww] = True
    a0, a1 = meter["clearDegrees"]
    deg = meter["scaleDegrees"]
    zero = float(deg["0"])
    # reference: the right end of the bar (never lit on the artwork)
    ref_sel = inside & (a > zero + 0.4) & (a < a1 - 0.3)
    out = plate.astype(np.float32)

    # 1. the band of the segments: the colour of the unlit end at the same depth
    d0, d1 = meter["arc"]["segmentDepth"]
    # down to well below the bar: the artwork's lit segments light the floor under them
    below = d1 + 16.0
    band = inside & (d > 1.6) & (d < below + 6.0) & (a > a0) & (a < a1)
    floor = np.median(out[ref_sel & (d > d1 + 2.0) & (d < below)], axis=0)
    gap = np.median(out[ref_sel & (d > 1.6) & (d < d0 - 1.0)], axis=0)
    fill = np.where((d < d0 - 1.0)[..., None], gap, floor)
    bins = np.round(d * 2).astype(int)
    for b in range(3, int((d0 - 1.0) * 2) + 1):
        sel = ref_sel & (bins == b)
        if sel.sum() >= 10:
            fill[bins == b] = np.median(out[sel], axis=0)
    rng = np.random.default_rng(7)
    grain = cv2.GaussianBlur(rng.normal(0.0, 1.6, plate.shape).astype(np.float32), (0, 0), 0.6)
    # the floor's own fine texture, from the unlit end of the bar
    tex = out - cv2.GaussianBlur(out, (0, 0), 2.0)
    tex_ref = tex[ref_sel & (d > d1 + 2.0) & (d < below)]
    pick = rng.integers(0, len(tex_ref), size=int(band.sum()))
    fill_band = fill[band] + 0.6 * grain[band] + 0.8 * tex_ref[pick]
    # fade into the untouched floor over the last 6 px
    t = np.clip((below + 6.0 - d[band]) / 6.0, 0.0, 1.0)
    t *= np.clip((d[band] - 1.6) / 1.2, 0.0, 1.0)  # soft edge under the rail
    t *= np.clip(np.minimum(a[band] - a0, a1 - a[band]) / 0.1, 0.0, 1.0)  # and at both ends
    t = t[:, None]
    out[band] = fill_band * t + out[band] * (1.0 - t)

    # 2. glow out of the window, measured on the plate without the segments,
    # ramped off towards the part the artwork leaves unlit
    filled = np.clip(out, 0, 255).astype(np.uint8)
    win = filled[wy:wy + wh, wx:wx + ww]
    base = cv2.GaussianBlur(cv2.medianBlur(win, 15).astype(np.float32), (0, 0), 4.0)
    dw = d[wy:wy + wh, wx:wx + ww]
    aw = a[wy:wy + wh, wx:wx + ww]
    refw = ref_sel[wy:wy + wh, wx:wx + ww]
    bins = np.round(dw).astype(int)
    table = {}
    for b in range(bins.min(), bins.max() + 1):
        sel = refw & (bins == b)
        if sel.sum() >= 20:
            table[b] = np.median(base[sel], axis=0)
    keys = np.array(sorted(table))
    vals = np.array([table[k] for k in keys])
    ref_level = np.zeros_like(base)
    for ch in range(3):
        ref_level[..., ch] = np.interp(dw, keys, vals[:, ch])
    glow = np.clip(base - ref_level, 0, None)
    lit_end = float(deg["1"])  # the artwork's bar is lit up to -3; its glow fades by -1
    ramp = np.clip((lit_end - aw) / 1.0, 0.0, 1.0)[..., None]
    out[wy:wy + wh, wx:wx + ww] -= glow * ramp

    res = np.clip(out, 0, 255).astype(np.uint8)
    win_mask = np.zeros(plate.shape[:2], np.uint8)
    win_mask[wy:wy + wh, wx:wx + ww] = 255
    return feather_composite(plate, res, win_mask, 1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--debug", default=None)
    args = ap.parse_args()

    ui = json.load(open(UI_MAP))
    controls = {c["id"]: c for c in ui["controls"]}
    img = cv2.imread(REF, cv2.IMREAD_COLOR)
    assert img.shape[1] == ui["reference"]["width"] and img.shape[0] == ui["reference"]["height"]
    plate = img.copy()
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]

    knobs = [c for c in ui["controls"] if c["type"] in ("ROTARY", "ROTARY_STEPPED")]
    for c in knobs:
        # a pointer near 12 o'clock would mirror onto itself: tilted mirror axis
        plate = remove_pointer_mirrored(img, plate, c, yy, xx, c.get("mirrorAxisDegrees", 0.0))
    # any lit speck left on a knob (the glow of a pointer's end on the skirt)
    hsv = cv2.cvtColor(plate, cv2.COLOR_BGR2HSV)
    speck = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 30) & (hsv[..., 1] > 110) & (hsv[..., 2] > 70)
    on_knob = np.zeros(img.shape[:2], bool)
    for c in knobs:
        cx, cy = c["centre"]
        on_knob |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["rimRadius"] + 1) ** 2
    speck = cv2.dilate((speck & on_knob).astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=1)
    if speck.any():
        plate = cv2.inpaint(plate, speck, 3, cv2.INPAINT_TELEA)

    for c in knobs:
        if c.get("eraseDots"):
            avoid = np.zeros(img.shape[:2], bool)
            cx, cy = c["centre"]
            avoid |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["rimRadius"] + 4) ** 2
            for x, y, w, h in c.get("erase", []):
                avoid[max(0, y - 4):y + h + 4, max(0, x - 4):x + w + 4] = True
            for d in c["eraseDots"]:
                avoid |= ((xx - d[0]) ** 2 + (yy - d[1]) ** 2) <= 9 ** 2
            for d in c["eraseDots"]:
                plate = erase_dot(plate, d, avoid, radius=7)
    # everything a label's fill must not copy: knobs, scale dots, other print
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    printed = (g - cv2.GaussianBlur(g, (0, 0), 6.0)) > 25
    printed = cv2.dilate(printed.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    for c in knobs:
        cx, cy = c["centre"]
        printed |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["rimRadius"] + 4) ** 2
    for c in knobs:
        for rect in c.get("erase", []):
            r = pad(rect, c.get("erasePad", 2))
            avoid = printed.copy()
            avoid[r[1]:r[1] + r[3], r[0]:r[0] + r[2]] = False
            plate = erase_label(plate, r, c["scaleCentre"], avoid)

    # lamps: dark on the plate, lit sprites from the artwork
    sprites = {}
    lit_plate_src = {}
    for gid in ("thrust", "tone", "grMode"):
        plate = unlit_in_tray(plate, controls[gid])
    plate = unlit_in_housing(plate, controls["detector"])
    for gid in ("thrust", "tone", "grMode"):
        plate = unwarm(plate, controls[gid]["unlit"]["cell"])
    plate = unwarm(plate, controls["detector"]["unlit"]["face"], margin=(14, 14))
    plate, lamp = lamp_off(img, plate, controls["scExternal"])

    for name, s in ui["sprites"].items():
        if name == "lamp_ext":
            sprites[name] = lamp
        else:
            sprites[name] = sprite_from(img, plate, s["rect"])

    for mid in ("grMeterL", "grMeterR"):
        plate = clear_bar(img, plate, controls[mid])

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    for name, sp in sprites.items():
        cv2.imwrite(os.path.join(OUT, name + ".png"), sp, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    print("wrote", OUT)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        for c in ui["controls"]:
            boxes = [c["hitbox"]] if "hitbox" in c else [b["hitbox"] for b in c.get("buttons", [])]
            for x, y, w, h in boxes:
                cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (0, 255, 0), 1)
            if "centre" in c and "pointer" in c:
                cx, cy = c["centre"]
                cv2.drawMarker(dbg, (int(round(cx)), int(round(cy))), (0, 0, 255), cv2.MARKER_CROSS, 10, 1)
                p = c["pointer"]
                for deg in c["travelDegrees"]:
                    a = np.radians(deg)
                    cv2.line(dbg, (int(cx + np.sin(a) * p["inner"]), int(cy - np.cos(a) * p["inner"])),
                             (int(cx + np.sin(a) * p["outer"]), int(cy - np.cos(a) * p["outer"])), (255, 0, 255), 1)
                sx, sy = c["scaleCentre"]
                cv2.circle(dbg, (int(round(sx)), int(round(sy))), int(round(c["dotRadius"])), (255, 255, 0), 1)
            for b in c.get("buttons", []):
                x, y, w, h = b["rect"]
                cv2.rectangle(dbg, (x, y), (x + w, y + h), (255, 128, 0), 1)
            if c["type"] == "ARC_LED_METER":
                (acx, acy), R = c["arc"]["centre"], c["arc"]["railRadius"]
                for dd in c["arc"]["segmentDepth"]:
                    a0, a1 = c["clearDegrees"]
                    pts = [(acx + (R - dd) * np.sin(np.radians(t)), acy - (R - dd) * np.cos(np.radians(t)))
                           for t in np.linspace(a0, a1, 200)]
                    cv2.polylines(dbg, [np.round(np.array(pts)).astype(np.int32)], False, (0, 255, 255), 1)
                for k, t in c["scaleDegrees"].items():
                    t = np.radians(t)
                    p0 = (acx + (R - c["arc"]["segmentDepth"][0]) * np.sin(t), acy - (R - c["arc"]["segmentDepth"][0]) * np.cos(t))
                    p1 = (acx + (R - c["arc"]["segmentDepth"][1]) * np.sin(t), acy - (R - c["arc"]["segmentDepth"][1]) * np.cos(t))
                    cv2.line(dbg, tuple(int(round(v)) for v in p0), tuple(int(round(v)) for v in p1), (0, 0, 255), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
