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
  - the printed dots of the three frequency selectors and of BANDWIDTH: they
    were off-centre or unevenly spaced, so the editor draws one dot per real
    position (selectors) or per step of 1 (BANDWIDTH);
  - the lit EQ IN lamp (dark on the plate, a sprite for the lit state).
Kept: the dots of the other knobs (they sit at the real values), CLEAN /
ORIGINAL / BURN with their arc and marks, SHARP / BROAD, units, titles.

Usage: python3 tools/skinprep/prep_shape.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import angular_fill, erase_dot, feather_composite, lamp_off, orange_mask, remove_pointer
from prep_weight import erase_label

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

    for c in knobs:
        for p in c.get("eraseDots", []):
            avoid = printed.copy()
            avoid[int(p[1]) - 8:int(p[1]) + 9, int(p[0]) - 8:int(p[0]) + 9] = False
            plate = erase_dot(plate, p, avoid, radius=8)
        for rect in c.get("erase", []):
            r = pad(rect, 3)
            avoid = printed.copy()
            avoid[r[1]:r[1] + r[3], r[0]:r[0] + r[2]] = False
            plate = erase_label(plate, r, c["scaleCentre"], avoid)

    # the printed dots that stay: back from the artwork, whole (a label
    # erased right next to a dot must not nibble at it)
    for c in knobs:
        if c.get("eraseDots") or "keepDots" not in c:
            continue
        for p in c["keepDots"]:
            # the dot itself: the dark blob under its centre (not a digit next to it)
            px, py = int(round(p[0])), int(round(p[1]))
            box = g[py - 9:py + 10, px - 9:px + 10]
            ink = (cv2.GaussianBlur(g, (0, 0), 6.0)[py - 9:py + 10, px - 9:px + 10] - box > 25).astype(np.uint8)
            n, lab = cv2.connectedComponents(ink)
            centre_label = lab[9, 9] if ink[9, 9] else (np.bincount(lab[7:12, 7:12].ravel())[1:].argmax() + 1 if ink[7:12, 7:12].any() else 0)
            if centre_label == 0:
                continue
            m = np.zeros(plate.shape[:2], np.uint8)
            m[py - 9:py + 10, px - 9:px + 10] = (lab == centre_label).astype(np.uint8) * 255
            m = cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
            plate = feather_composite(plate, img, m, 0.8)

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
