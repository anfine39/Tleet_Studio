#!/usr/bin/env python3
"""Skin preparation for TLEET // GLUE.

Turns the approved mockup (assets/reference/GLUE.png) into the runtime plate
  assets/processed/GLUE/plate.png
using the geometry in plugins/GLUE/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - knob pointers (all eleven knobs): filled along the circle around the cap
    centre, grain from a rotated copy;
  - every number printed on a knob scale: the editor draws the parameter's
    real value at the angle where the knob really has it;
  - the scale dots of the stepped knobs (Attack, Release, Ratio): the
    printed dots did not sit on real steps, so the editor draws one dot per step;
  - the lit lamp on the Character arc (it was lit at TLEET);
  - the GR LED bar with its glow (twelve segments were lit): the plate keeps
    the window, the rail, the ticks and the printed dB numbers; the editor
    draws every segment from the actual gain reduction.

Usage: python3 tools/skinprep/prep_glue.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import angular_fill, feather_composite, match_illumination, rotate_fill

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "GLUE.png")
OUT = os.path.join(ROOT, "assets", "processed", "GLUE")
UI_MAP = os.path.join(ROOT, "plugins", "GLUE", "UI_MAP.json")


def rect_mask(shape, rects, grow=0):
    m = np.zeros(shape[:2], np.uint8)
    for x, y, w, h in rects:
        cv2.rectangle(m, (int(x) - grow, int(y) - grow), (int(x + w) + grow, int(y + h) + grow), 255, -1)
    return m


def rib_period(plate, mask, c):
    """Angular period (degrees) of the ribbed skirt, by autocorrelation of
    the skirt ring outside the masked pointer; None if no clear period."""
    cx, cy = c["centre"]
    g = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    r = c["capRadius"] + 7.0
    th = np.radians(np.arange(0, 360, 0.25))
    px = (cx + r * np.sin(th)).astype(np.float32)
    py = (cy - r * np.cos(th)).astype(np.float32)
    v = cv2.remap(g, px.reshape(-1, 1), py.reshape(-1, 1), cv2.INTER_LINEAR).ravel()
    ok = cv2.remap(mask.astype(np.float32), px.reshape(-1, 1), py.reshape(-1, 1), cv2.INTER_NEAREST).ravel() == 0
    v = v - v[ok].mean()
    lags = np.arange(int(4 / 0.25), int(40 / 0.25))
    corr = np.full(len(lags), -1.0)
    for i, lag in enumerate(lags):
        both = ok & np.roll(ok, -lag)
        if both.sum() >= 200:
            a, b = v[both], np.roll(v, -lag)[both]
            corr[i] = float((a * b).mean() / (a.std() * b.std() + 1e-6))
    # first clear peak after the correlation has dropped (smooth signals
    # correlate at every small lag)
    dropped = False
    for i in range(4, len(lags) - 4):
        if corr[i] < 0.0:
            dropped = True
        if dropped and corr[i] > 0.3 and corr[i] == corr[i - 4:i + 5].max():
            return float(lags[i] * 0.25)
    return None


def source_turn(plate, mask, c, r, a):
    """Smallest rib-aligned rotation that moves the whole mask off itself.
    The knobs are drawn in perspective (side wall visible at the bottom), so
    a small turn keeps the source at nearly the same height on the knob; of
    the two directions, the one whose source sits at the closer height wins."""
    sel = (mask > 0) & (r > c["capRadius"] - 4)
    angles = a[sel]
    centre_angle = np.degrees(np.arctan2(np.sin(np.radians(angles)).mean(), np.cos(np.radians(angles)).mean()))
    spread = np.abs(((angles - centre_angle + 180) % 360) - 180).max()
    need = 2 * spread + 6
    period = rib_period(plate, mask, c)
    turn = need if period is None else period * np.ceil(need / period)
    best = None
    for sign in (1, -1):
        src = centre_angle - sign * turn  # where the copied pixels come from
        score = abs(np.cos(np.radians(src)) - np.cos(np.radians(centre_angle)))
        if best is None or score < best[0]:
            best = (score, sign * turn)
    return float(best[1])


def remove_pointer(img, plate, c, yy, xx):
    """The lit pointer and its glow, filled along the circle around the cap."""
    cx, cy = c["centre"]
    reach = max(c["capRadius"] * 1.45, c["pointer"]["outer"] + 14)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    near = r <= reach
    hot = ((hsv[..., 0] >= 5) & (hsv[..., 0] <= 32) & (hsv[..., 1] > 90) & (hsv[..., 2] > 150) & near).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(hot)
    biggest = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))
    core = (lab == biggest).astype(np.uint8) * 255
    around = cv2.dilate(core, np.ones((3, 3), np.uint8), iterations=12) > 0
    glow = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 34) & (hsv[..., 1] > 60) & (hsv[..., 2] > 45) & around
    # the pointer continues as a lit notch on the skirt: extend along its direction
    ys, xs = np.nonzero(core)
    far = int(np.argmax((xs - cx) ** 2 + (ys - cy) ** 2))
    ang = np.arctan2(xs[far] - cx, -(ys[far] - cy))
    ext = np.zeros(img.shape[:2], np.uint8)
    r0 = c["pointer"]["outer"] - 6
    r1 = reach
    cv2.line(ext, (int(round(cx + np.sin(ang) * r0)), int(round(cy - np.cos(ang) * r0))),
             (int(round(cx + np.sin(ang) * r1)), int(round(cy - np.cos(ang) * r1))), 255, 9)
    warm = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 34) & (hsv[..., 1] > 50) & (hsv[..., 2] > 70)
    bright = hsv[..., 2] > 150
    glow |= (ext > 0) & (warm | bright)
    mask = cv2.dilate((glow | (core > 0)).astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=3)
    mask[r <= 8] = 255
    mask[~near] = 0
    ang_deg = np.degrees(np.arctan2(xx - cx, -(yy - cy)))
    turn = source_turn(plate, mask, c, r, ang_deg)
    smooth = angular_fill(plate, mask, (float(cx), float(cy))).astype(np.float32)
    rotated = rotate_fill(plate, mask, (float(cx), float(cy)), turn, cv2.INTER_LINEAR).astype(np.float32)
    # ribs and brushing come whole from the rib-aligned rotated copy; only the
    # slow brightness change around the cap comes from the angular fill
    m = cv2.getRotationMatrix2D((float(cx), float(cy)), turn, 1.0)
    source_was_pointer = cv2.warpAffine(mask, m, (mask.shape[1], mask.shape[0])) > 0
    # skirt: ribs whole from the rotated copy, brightness from the angular fill
    detail = rotated - cv2.GaussianBlur(rotated, (0, 0), 4.0)
    detail[source_was_pointer] = 0
    skirt = cv2.GaussianBlur(smooth, (0, 0), 4.0) + detail
    # face: brushed cone, brightness follows the angle; fine grain only
    grain = rotated - cv2.GaussianBlur(rotated, (0, 0), 1.2)
    grain[source_was_pointer | (r <= 12)] = 0
    face = smooth + grain
    on_face = (r < c["capRadius"] - 2)[..., None]
    filled = np.clip(np.where(on_face, face, skirt), 0, 255).astype(np.uint8)
    return feather_composite(plate, filled, mask, 1.0), mask


def erase_rect_beside(plate, rect, centre, sigma=4.0):
    """Printed text: panel texture from beside the text (away from the knob),
    smooth brightness from the surroundings."""
    x, y, w, h = rect
    mask = rect_mask(plate.shape, [(x, y, w, h)])
    # candidate sources beside and above/below the text; the one with the least
    # fine detail wins, so section dividers and scale dots are not copied in
    gray = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    detail = np.abs(gray - cv2.GaussianBlur(gray, (0, 0), 2.0))
    best = None
    sx = 1 if x + w / 2 > centre[0] else -1
    sy = 1 if y + h / 2 > centre[1] else -1
    for dx, dy in ((sx * (w + 6), 0), (sx * (w + 14), 0), (0, sy * (h + 4)), (sx * (w + 6), sy * (h + 4))):
        x0, y0 = x + dx, y + dy
        if x0 < 0 or y0 < 0 or x0 + w >= plate.shape[1] or y0 + h >= plate.shape[0]:
            continue
        score = float(detail[y0:y0 + h, x0:x0 + w].mean())
        if best is None or score < best[0]:
            best = (score, dx, dy)
    _, dx, dy = best
    source = np.roll(np.roll(plate, -dy, axis=0), -dx, axis=1)
    filled = match_illumination(plate, source, mask, sigma=sigma)
    return feather_composite(plate, filled, cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1), 1.2)


def clean_patch(plate, centre, avoid, size=24, search=70):
    """The quietest size x size patch of panel near `centre` (least fine
    detail, nothing from `avoid`), for film grain."""
    g = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    detail = np.abs(g - cv2.GaussianBlur(g, (0, 0), 2.0))
    cx, cy = int(centre[0]), int(centre[1])
    best = None
    for y in range(cy - search, cy + search - size, 4):
        for x in range(cx - search, cx + search - size, 4):
            if x < 0 or y < 0 or x + size >= plate.shape[1] or y + size >= plate.shape[0]:
                continue
            if avoid[y:y + size, x:x + size].any():
                continue
            score = float(detail[y:y + size, x:x + size].mean()) + 0.02 * float(g[y:y + size, x:x + size].std())
            if best is None or score < best[0]:
                best = (score, (x, y, size, size))
    return best[1]


def erase_dot(plate, centre, avoid, radius=11):
    """A printed scale dot and its soft halo: a clean patch of the same
    panel, brought to the brightness of the panel around the dot."""
    px, py = centre
    ix, iy = int(round(px)), int(round(py))
    mask = np.zeros(plate.shape[:2], np.uint8)
    cv2.circle(mask, (ix, iy), radius, 255, -1)
    ring = np.zeros_like(mask)
    cv2.circle(ring, (ix, iy), radius + 6, 255, -1)
    ring[mask > 0] = 0
    sel = (ring > 0) & ~avoid
    if sel.sum() < 20:
        sel = ring > 0
    level = np.median(plate[sel].astype(np.float32), axis=0)
    x, y, w, h = clean_patch(plate, centre, avoid | (mask > 0), size=2 * radius + 4)
    patch = plate[y:y + h, x:x + w].astype(np.float32)
    patch = patch * (level / np.maximum(np.median(patch.reshape(-1, 3), axis=0), 1.0))
    pasted = plate.copy()
    half = w // 2
    pasted[iy - half:iy - half + h, ix - half:ix - half + w] = np.clip(patch, 0, 255).astype(np.uint8)
    return feather_composite(plate, pasted, mask, 2.0)


def rebuild_meter(img, plate, meter):
    """GR bar: the window keeps its rail, ticks and numbers; segments and glow go.

    The unlit right part of the artwork (no glow) is the template: its segment
    strip is replaced by the dark floor mirrored upwards, its ticks are taken
    out, and the result is tiled over the whole bar. Ticks are then put back
    at every printed scale position."""
    scale = {float(k): float(v) for k, v in meter["scale"].items()}
    labelled = (0, 2, 4, 6, 8, 10, 12, 16, 20)
    majors = [v for k, v in scale.items() if k in labelled]
    minors = [v for k, v in scale.items() if k not in labelled]
    minors.append(scale[16] + (scale[20] - scale[16]) / 2)  # unlabelled 18 dB tick
    y0, y1 = 329, 382          # band rebuilt: below the numbers, above the window floor
    rail_y0, rail_y1 = 343, 345
    clean_x0, clean_x1 = 730, 1016

    band = img[y0:y1, clean_x0:clean_x1].copy()
    # segment strip -> floor mirrored upwards about the strip's lower edge
    strip_top, strip_bottom = rail_y1 + 1, 363
    for k in range(strip_bottom - strip_top + 1):
        band[strip_bottom - k - y0] = band[min(y1 - 1, strip_bottom + 1 + k) - y0]
    # ticks out of the template
    ticks = np.zeros(band.shape[:2], np.uint8)
    for x in majors + minors:
        if clean_x0 - 3 <= x < clean_x1 + 3:
            xi = int(round(x)) - clean_x0
            ticks[0:rail_y0 - y0, max(0, xi - 3):xi + 4] = 255
    band = cv2.inpaint(band, ticks, 3, cv2.INPAINT_TELEA)

    # tick sprites (with their own alpha) from the unlit part of the artwork
    def sprite(x):
        xi = int(round(x))
        patch = img[y0:rail_y1 + 1, xi - 3:xi + 4].astype(np.float32)
        lum = patch.mean(axis=2)
        bg = np.median(lum[:, [0, -1]])
        alpha = np.clip((lum - bg - 12.0) / 50.0, 0.0, 1.0)
        return patch, alpha[..., None]
    tick_major = sprite(scale[10])
    tick_minor = sprite(scale[11])

    x_start, x_end = 398, clean_x1
    out = plate.copy()
    w = band.shape[1]
    x = x_end
    while x > x_start:
        take = min(w, x - x_start)
        out[y0:y1, x - take:x] = band[:, w - take:]
        x -= take
    for xs, (patch, alpha) in [(majors, tick_major), (minors, tick_minor)]:
        for x in xs:
            xi = int(round(x))
            region = out[y0:rail_y1 + 1, xi - 3:xi + 4].astype(np.float32)
            out[y0:rail_y1 + 1, xi - 3:xi + 4] = np.clip(region * (1 - alpha) + patch * alpha, 0, 255).astype(np.uint8)

    mask = np.zeros(plate.shape[:2], np.uint8)
    mask[y0 + 2:y1 - 3, x_start + 3:x_end] = 255
    return feather_composite(plate, out, mask, 1.5)


def remove_arc_lamp(img, plate, c, yy, xx):
    """The lamp on the Character arc was lit at TLEET; the arc is restored
    along its circle and the TLEET dot put back from the CLEAN dot."""
    cx, cy = c["centre"]
    lamp = c["arcLamp"]
    r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    a = np.degrees(np.arctan2(xx - cx, -(yy - cy)))
    r0, r1 = lamp["sectorRadii"]
    mask = ((np.abs(a) <= lamp["sectorDegrees"]) & (r >= r0) & (r <= r1)).astype(np.uint8) * 255
    filled = angular_fill(plate, mask, (float(cx), float(cy)), max_deg=60.0)
    out = feather_composite(plate, filled, mask, 1.2)
    return out, mask


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

    # Character arc lamp first: its glow overlaps the pointer's
    plate, _ = remove_arc_lamp(img, plate, controls["character"], yy, xx)

    for c in knobs:
        plate, _ = remove_pointer(img, plate, c, yy, xx)

    for c in knobs:
        if c.get("eraseDots"):
            # keep grain sources off the knob, the labels and the other dots
            avoid = np.zeros(img.shape[:2], bool)
            cx, cy = c["centre"]
            avoid |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["capRadius"] + 22) ** 2
            for x, y, w, h in c.get("erase", []):
                avoid[max(0, y - 4):y + h + 4, max(0, x - 4):x + w + 4] = True
            for d in c["eraseDots"]:
                avoid |= ((xx - d[0]) ** 2 + (yy - d[1]) ** 2) <= 12 ** 2
            for d in c["eraseDots"]:
                plate = erase_dot(plate, d, avoid)
        for rect in c.get("erase", []):
            plate = erase_rect_beside(plate, rect, c["scaleCentre"])

    # scale dots tinted by the lit pointer: copies of an untinted dot
    for c in knobs:
        for dst, src in c.get("repaintDots", []):
            sx, sy, dx, dy = [int(round(v)) for v in (src[0], src[1], dst[0], dst[1])]
            patch = img[sy - 9:sy + 10, sx - 9:sx + 10]
            disc = np.zeros(img.shape[:2], np.uint8)
            cv2.circle(disc, (dx, dy), 8, 255, -1)
            pasted = plate.copy()
            pasted[dy - 9:dy + 10, dx - 9:dx + 10] = patch
            plate = feather_composite(plate, pasted, disc, 1.5)

    # the TLEET dot above the arc: a copy of the CLEAN dot
    ch = controls["character"]
    src = (622.9, 877.0)
    dst = (691.3, 818.9)
    sx, sy, dx, dy = int(round(src[0])), int(round(src[1])), int(round(dst[0])), int(round(dst[1]))
    patch = img[sy - 6:sy + 7, sx - 6:sx + 7]
    disc = np.zeros(img.shape[:2], np.uint8)
    cv2.circle(disc, (dx, dy), 5, 255, -1)
    pasted = plate.copy()
    pasted[dy - 6:dy + 7, dx - 6:dx + 7] = patch
    plate = feather_composite(plate, pasted, disc, 1.0)

    plate = rebuild_meter(img, plate, controls["grMeter"])

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    print("wrote", OUT)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        for c in ui["controls"]:
            if "hitbox" in c:
                x, y, w, h = c["hitbox"]
                cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (0, 255, 0), 1)
            if "centre" in c:
                cx, cy = c["centre"]
                cv2.drawMarker(dbg, (int(round(cx)), int(round(cy))), (0, 0, 255), cv2.MARKER_CROSS, 14, 1)
                p = c["pointer"]
                for deg in c["travelDegrees"]:
                    a = np.radians(deg)
                    cv2.line(dbg, (int(cx + np.sin(a) * p["inner"]), int(cy - np.cos(a) * p["inner"])),
                             (int(cx + np.sin(a) * p["outer"]), int(cy - np.cos(a) * p["outer"])), (255, 0, 255), 1)
            if "scaleCentre" in c:
                sx, sy = c["scaleCentre"]
                cv2.circle(dbg, (int(round(sx)), int(round(sy))), int(round(c["dotRadius"])), (255, 255, 0), 1)
        m = controls["grMeter"]
        for k, x in m["scale"].items():
            cv2.line(dbg, (int(x), m["segmentRow"][0]), (int(x), m["segmentRow"][1]), (0, 255, 255), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
