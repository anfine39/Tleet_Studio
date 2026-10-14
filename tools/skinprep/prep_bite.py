#!/usr/bin/env python3
"""Skin preparation for TLEET // BITE.

Turns the approved mockup (assets/reference/BITE.png) into runtime assets:
  assets/processed/BITE/plate.png      faceplate with every moving or value-dependent part removed
  assets/processed/BITE/ratio_lit.png  one lit ratio button (RGBA), drawn on the selected button
using the geometry in plugins/BITE/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - knob pointers: filled from the same cap rotated around its centre;
  - Attack/Release numbers (.1 ... 10 ms / s): they did not match the real
    times, so the editor draws the real values instead (UI_SPEC_CONFLICTS.md);
  - ratio lamps: every lamp was lit and ALL was pressed; the plate gets dark
    lamps and an unpressed ALL button (copied from the button beside it);
  - VU needle: face filled from the same radius around the pivot.

Usage: python3 tools/skinprep/prep_bite.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import angular_fill, feather_composite, match_illumination, match_illumination_far, rotate_fill

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "BITE.png")
OUT = os.path.join(ROOT, "assets", "processed", "BITE")
UI_MAP = os.path.join(ROOT, "plugins", "BITE", "UI_MAP.json")


def rect_mask(shape, rects, grow=0):
    m = np.zeros(shape[:2], np.uint8)
    for x, y, w, h in rects:
        cv2.rectangle(m, (int(x) - grow, int(y) - grow), (int(x + w) + grow, int(y + h) + grow), 255, -1)
    return m


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

    # ---- knob pointers -------------------------------------------------
    for c in ui["controls"]:
        if c["type"] not in ("ROTARY", "ROTARY_STEPPED"):
            continue
        cx, cy = c["centre"]
        # The brushed caps have bronze highlights, so take only the strongly
        # saturated pointer: the largest such blob on the cap, plus its glow.
        hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
        near = ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["capRadius"] * 1.35) ** 2
        strict = ((hsv[..., 0] >= 5) & (hsv[..., 0] <= 30) & (hsv[..., 1] > 150) & (hsv[..., 2] > 180) & near).astype(np.uint8)
        n, lab, st, _ = cv2.connectedComponentsWithStats(strict)
        biggest = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))
        core = (lab == biggest).astype(np.uint8) * 255
        # plus the orange glow around it (saturated, close to the pointer)
        around = cv2.dilate(core, np.ones((3, 3), np.uint8), iterations=8) > 0
        glow = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 30) & (hsv[..., 1] > 130) & (hsv[..., 2] > 60) & around
        mask = cv2.dilate((glow | (core > 0)).astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=3)
        # the pointer's dim inner end sits on the hub
        r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
        mask[(r <= 12)] = 255
        mask[~near] = 0
        # Brightness of a conical brushed cap follows the angle: interpolate it
        # along the circle, then put back the grain from a rotated copy.
        smooth = angular_fill(plate, mask, (float(cx), float(cy))).astype(np.float32)
        rotated = rotate_fill(plate, mask, (float(cx), float(cy)), 70.0, cv2.INTER_NEAREST).astype(np.float32)
        grain = rotated - cv2.GaussianBlur(rotated, (0, 0), 1.5)
        # no grain where the rotated copy still came from the pointer (near the hub)
        m = cv2.getRotationMatrix2D((float(cx), float(cy)), 70.0, 1.0)
        source_was_pointer = cv2.warpAffine(mask, m, (mask.shape[1], mask.shape[0])) > 0
        grain[source_was_pointer | (r <= 14)] = 0
        filled = np.clip(smooth + grain, 0, 255).astype(np.uint8)
        plate = feather_composite(plate, filled, mask, 1.0)

    # ---- printed Attack/Release numbers -----------------------------------
    # Filled with panel texture from beside the text, away from the knob; the
    # smooth brightness comes from the surroundings.
    for cid in ("attack", "release"):
        c = controls[cid]
        cx = c["centre"][0]
        for x, y, w, h in c["erase"]:
            mask = rect_mask(img.shape, [(x, y, w, h)])
            dx = int(w + 6) if x + w / 2 > cx else -int(w + 6)
            source = np.roll(plate, -dx, axis=1)
            filled = match_illumination(plate, source, mask, sigma=4.0)
            plate = feather_composite(plate, filled, cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1), 1.2)

    # ---- ratio buttons ----------------------------------------------------
    rb = controls["ratio"]
    os.makedirs(OUT, exist_ok=True)

    # lit button sprite from the pressed ALL button, soft edges
    lx, ly, lw, lh = rb["litCell"]
    sprite_alpha = np.zeros((lh, lw), np.uint8)
    cv2.rectangle(sprite_alpha, (3, 3), (lw - 4, lh - 4), 255, -1)
    sprite_alpha = cv2.GaussianBlur(sprite_alpha, (0, 0), 2.0)
    cv2.imwrite(os.path.join(OUT, "ratio_lit.png"), np.dstack([img[ly:ly + lh, lx:lx + lw], sprite_alpha]))

    # unpressed ALL button: copy the "20" button one cell to the right
    sx, sy, sw, sh = rb["offCellSource"]
    cell = np.zeros(img.shape[:2], np.uint8)
    cv2.rectangle(cell, (lx + 2, ly + 2), (lx + lw - 3, ly + lh - 3), 255, -1)
    shifted = np.roll(plate, lx - sx, axis=1)
    plate = feather_composite(plate, shifted, cell, 1.5)

    # dark lamps: the same glass, unlit
    leds = rect_mask(img.shape, [tuple(l) for l in rb["leds"]])
    hsv = cv2.cvtColor(plate, cv2.COLOR_BGR2HSV).astype(np.float32)
    hsv[..., 1] *= 0.75
    hsv[..., 2] = 18.0 + hsv[..., 2] * 0.22
    dark = cv2.cvtColor(np.clip(hsv, 0, 255).astype(np.uint8), cv2.COLOR_HSV2BGR)
    plate = feather_composite(plate, dark, leds, 1.0)

    # ---- VU needle --------------------------------------------------------
    vu = controls["vu"]
    (ax, ay), (bx, by) = vu["bakedNeedle"]
    fx0, fy0, fx1, fy1 = vu["face"]
    face = np.zeros(img.shape[:2], np.uint8)
    face[fy0:fy1 - 14, fx0:fx1] = 255
    split_y = vu["bandBottomY"]  # below: plain face; above: red band
    t = (split_y - ay) / (by - ay)
    mx, my = ax + (bx - ax) * t, ay + (by - ay) * t

    # face part: the light needle has a wide glow
    needle = np.zeros(img.shape[:2], np.uint8)
    # the needle also shades the face on its right, so the mask leans right
    nx, ny = (by - ay), -(bx - ax)
    norm = (nx * nx + ny * ny) ** 0.5
    ox, oy = nx / norm * 8, ny / norm * 8
    cv2.line(needle, (int(ax + ox), int(ay + oy) + 6), (int(round(mx + ox)), int(round(my + oy))), 255, 31)
    needle[: int(split_y) - 3, :] = 0
    needle = cv2.bitwise_and(needle, face)
    pivot = (float(vu["pivot"][0]), float(vu["pivot"][1]))
    # face texture from beside the needle, moved along the band's slope so the
    # band's lower rim continues (the bottom rim is handled below)
    sdx, sdy = vu["bandTickStep"]
    filled = np.roll(np.roll(plate, -int(round(48 * sdy / sdx)), axis=0), -48, axis=1)
    below_band = face.copy()
    below_band[: int(split_y) - 3, :] = 0
    filled, area = match_illumination_far(plate, filled, needle, limit=below_band)
    plate = feather_composite(plate, filled, area, 2.0)

    # bottom rim under the needle base: horizontal, so a plain sideways copy
    rim = np.zeros_like(needle)
    cv2.rectangle(rim, (int(ax) - 26, fy1 - 18), (int(ax) + 26, fy1 + 4), 255, -1)
    side = np.roll(img, -48, axis=1).astype(np.float32)
    x0r, x1r = int(ax) - 26, int(ax) + 26
    cols = np.r_[x0r - 46:x0r - 34, x1r + 34:x1r + 46]  # clear of the needle's glow
    for y in range(fy1 - 18, fy1 + 5):  # match each row's brightness to the face around it
        gain = img[y, cols].astype(np.float32).mean(axis=0) / np.maximum(side[y, cols].mean(axis=0), 1.0)
        side[y] *= gain
    plate = feather_composite(plate, np.clip(side, 0, 255).astype(np.uint8), rim, 3.0)

    # band part: copied from one tick further along the band, which also
    # restores the tick under the needle tip
    tip = np.zeros_like(needle)
    cv2.line(tip, (int(round(mx)), int(round(my))), (int(bx), int(by) - 2), 255, 11)
    cv2.line(tip, (int(round(mx)), int(round(my))), (int(round(mx + (bx - mx) * 0.4)), int(round(my + (by - my) * 0.4))), 255, 15)
    tip[int(split_y) + 1:, :] = 0
    dx, dy = vu["bandTickStep"]
    shifted = np.roll(np.roll(plate, -int(dy), axis=0), -int(dx), axis=1)
    plate = feather_composite(plate, shifted, tip, 1.0)

    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    print("wrote", OUT)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        s = 1
        for c in ui["controls"]:
            for hb in c.get("hitboxes", [c["hitbox"]] if "hitbox" in c else []):
                x, y, w, h = hb
                cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (0, 255, 0), 1)
            if "centre" in c:
                cx, cy = c["centre"]
                cv2.drawMarker(dbg, (int(round(cx)), int(round(cy))), (0, 0, 255), cv2.MARKER_CROSS, 14, 1)
                p = c["pointer"]
                for deg in c["travelDegrees"]:
                    a = np.radians(deg)
                    cv2.line(dbg, (int(cx + np.sin(a) * p["inner"] * s), int(cy - np.cos(a) * p["inner"] * s)),
                             (int(cx + np.sin(a) * p["outer"] * s), int(cy - np.cos(a) * p["outer"] * s)), (255, 0, 255), 1)
        px, py = vu["pivot"]
        for v, deg in vu["scaleDegrees"].items():
            a = np.radians(deg)
            cv2.line(dbg, (int(px + np.sin(a) * (vu["needleLength"] - 40)), int(py - np.cos(a) * (vu["needleLength"] - 40))),
                     (int(px + np.sin(a) * vu["needleLength"]), int(py - np.cos(a) * vu["needleLength"])), (255, 255, 0), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
