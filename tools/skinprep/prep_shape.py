#!/usr/bin/env python3
"""Skin preparation for TLEET // SHAPE.

Turns the approved mockup (assets/reference/SHAPE.png) into the runtime assets
  assets/processed/SHAPE/plate.png      faceplate
  assets/processed/SHAPE/eqin_lamp.png  the lit EQ IN lamp (RGBA)
using the geometry in plugins/SHAPE/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - all twelve knob pointers and their glow;
  - every number printed on a scale: the editor draws the parameter's real
    value at the angle where the knob really has it (the HIGH FREQUENCY
    selector was printed 3 4 5 10 13 12 16, the BANDWIDTH scale 1 2 3 4 6 4 7);
  - every printed scale dot: the selectors' dots were off-centre, the others
    unevenly spaced (up to 6 px off the real values), so the editor draws one
    dot per real position (selectors) or per step of a tenth of the travel;
  - the lit EQ IN lamp (dark on the plate, a sprite for the lit state).
Kept: CLEAN /
ORIGINAL / BURN with their arc and marks, SHARP / BROAD, units, titles.

Usage: python3 tools/skinprep/prep_shape.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import (angular_fill, clean_patch, erase_dot, feather_composite, lamp_off, match_illumination,
                     orange_mask, rect_mask, remove_pointer)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "SHAPE.png")
OUT = os.path.join(ROOT, "assets", "processed", "SHAPE")
UI_MAP = os.path.join(ROOT, "plugins", "SHAPE", "UI_MAP.json")


def pad(rect, p):
    x, y, w, h = rect
    return [x - p, y - p, w + 2 * p, h + 2 * p]


def pointer_out(img, plate, c, yy, xx):
    """The lit pointer, its glow and its notch on the skirt. The face is
    filled around the cap (remove_pointer). On the skirt only the pointer's
    own light is replaced (warm pixels within a narrow angle of the pointer),
    from the mirror image about the knob's vertical axis (the skirt is drawn
    in perspective, so a turned copy would not fit its outline) or, for a
    pointer near that axis, from a small turn; brightness from around the
    gap. Nothing outside the rim is touched."""
    cx, cy = c["centre"]
    face, mask = remove_pointer(img, plate, c, yy, xx, reach=c["rimRadius"] + 1)
    r = np.hypot(xx - cx, yy - cy)
    a = np.degrees(np.arctan2(xx - cx, -(yy - cy)))
    out = plate.astype(np.float32)
    on_face = (r < c["capRadius"] - 1)
    face_mask = ((mask > 0) & on_face).astype(np.uint8) * 255
    out = feather_composite(out.astype(np.uint8), face, face_mask, 0.8).astype(np.float32)

    hot = (orange_mask(img) > 0) & (r <= c["rimRadius"] + 1)
    core = hot & (r < c["capRadius"])
    if core.sum() < 10:
        core = hot
    ang = np.degrees(np.arctan2(np.sin(np.radians(a[core])).mean(), np.cos(np.radians(a[core])).mean()))
    dang = np.abs(((a - ang + 180) % 360) - 180)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    warm = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 34) & (hsv[..., 1] > 70) & (hsv[..., 2] > 70)
    width = np.degrees(np.arctan2(9.0, max(c["capRadius"], 1.0)))
    skirt = warm & (dang < width) & (r >= c["capRadius"] - 3) & (r <= c["rimRadius"] + 1)
    skirt = cv2.dilate(skirt.astype(np.uint8) * 255, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)), iterations=1)
    skirt[r > c["rimRadius"] + 1] = 0
    if skirt.any():
        if abs(ang) > 2 * width + 4:
            mx, my = (2.0 * cx - xx).astype(np.float32), yy.astype(np.float32)
            src = cv2.remap(plate, mx, my, cv2.INTER_LINEAR).astype(np.float32)
        else:
            turn = (2 * width + 6) * (-1 if ang >= 0 else 1)
            m = cv2.getRotationMatrix2D((float(cx), float(cy)), turn, 1.0)
            src = cv2.warpAffine(plate, m, (plate.shape[1], plate.shape[0]), flags=cv2.INTER_LINEAR).astype(np.float32)
        smooth = angular_fill(plate, skirt, (float(cx), float(cy))).astype(np.float32)
        gain = cv2.GaussianBlur(smooth, (0, 0), 3.0) / np.maximum(cv2.GaussianBlur(src, (0, 0), 3.0), 1.0)
        patch = np.clip(src * np.clip(gain, 0.6, 1.6), 0, 255).astype(np.uint8)
        out = feather_composite(out.astype(np.uint8), patch, skirt, 1.0).astype(np.float32)
    return np.clip(out, 0, 255).astype(np.uint8)


def erase_text(plate, rect, avoid, sigma=4.0):
    """A printed number: the quietest patch of panel nearby (no print, no
    knob, no dot in it, margin included), brought to the brightness around
    the number."""
    x, y, w, h = rect
    mask = rect_mask(plate.shape, [(x, y, w, h)])
    m = 4  # the composite's feather reaches past the rect
    size = max(w, h) + 2 * m
    corner = (x + w / 2 - size / 2, y + h / 2 - size / 2)
    try:
        px, py, _, _ = clean_patch(plate, corner, avoid | (mask > 0), size=size)
    except TypeError:  # nothing clean close by: look further
        px, py, _, _ = clean_patch(plate, corner, avoid | (mask > 0), size=size, search=160)
    dx, dy = px + m - x, py + m - y
    source = np.roll(np.roll(plate, -dy, axis=0), -dx, axis=1)
    # brightness matched over the whole reach of the feather, so a patch
    # from further away cannot leave its own brightness as an outline
    filled = match_illumination(plate, source, cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=3), sigma=sigma)
    return feather_composite(plate, filled, cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1), 1.2)


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

    knobs = [c for c in ui["controls"] if c["type"].startswith("ROTARY")]
    for c in knobs:
        plate = pointer_out(img, plate, c, yy, xx)

    # everything printed (text, dots, knobs) that a fill must not copy from
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    printed = np.abs(g - cv2.GaussianBlur(g, (0, 0), 6.0)) > 22
    printed = cv2.dilate(printed.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    for c in knobs:
        cx, cy = c["centre"]
        printed |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["rimRadius"] + 4) ** 2

    # grain and rust specks alone would block every patch: for the clean
    # patch under a dot only print bigger than the grain counts
    n, lab, st, _ = cv2.connectedComponentsWithStats(printed.astype(np.uint8))
    printed_bold = (st[:, cv2.CC_STAT_AREA] >= 90)[lab] & (lab > 0)

    for c in knobs:
        for p in c.get("eraseDots", []):
            avoid = printed_bold.copy()
            avoid[int(p[1]) - 8:int(p[1]) + 9, int(p[0]) - 8:int(p[0]) + 9] = False
            plate = erase_dot(plate, p, avoid, radius=8)
        for rect in c.get("erase", []):
            plate = erase_text(plate, pad(rect, 3), printed_bold)

    plate, lamp_sprite = lamp_off(img, plate, controls["eqIn"])

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    cv2.imwrite(os.path.join(OUT, "eqin_lamp.png"), lamp_sprite, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    print("wrote", OUT)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        for c in ui["controls"]:
            x, y, w, h = c["hitbox"]
            cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (0, 255, 0), 1)
            if c["type"].startswith("ROTARY"):
                cx, cy = c["centre"]
                cv2.drawMarker(dbg, (int(round(cx)), int(round(cy))), (0, 0, 255), cv2.MARKER_CROSS, 10, 1)
                p = c["pointer"]
                for deg in c["travelDegrees"]:
                    a = np.radians(deg)
                    cv2.line(dbg, (int(cx + np.sin(a) * p["inner"]), int(cy - np.cos(a) * p["inner"])),
                             (int(cx + np.sin(a) * p["outer"]), int(cy - np.cos(a) * p["outer"])), (255, 0, 255), 1)
                cv2.circle(dbg, (int(round(cx)), int(round(cy))), int(round(c["dotRadius"])), (255, 255, 0), 1)
            if c["type"] == "LAMP_SWITCH":
                x, y, w, h = c["lamp"]["rect"]
                cv2.rectangle(dbg, (x, y), (x + w, y + h), (255, 128, 0), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
