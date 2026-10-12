#!/usr/bin/env python3
"""Skin preparation for TLEET // ASH.

Turns the approved mockup (assets/reference/ASH.png) into runtime assets:
  assets/processed/ASH/plate.png   faceplate with every moving part removed
  assets/processed/ASH/lever.png   toggle lever sprite (RGBA), drawn rotated at runtime
and checks that the geometry in plugins/ASH/UI_MAP.json matches the artwork.

Moving parts are removed so that nothing value-dependent stays baked in:
  - knob pointers: filled from the same knob rotated around its centre
    (the caps are concentric/brushed, so a rotated copy is seamless);
  - toggle lever: the round nut behind it is filled the same way, the paper
    below it with OpenCV inpainting;
  - VU needle: inpainted on the meter face.

Usage: python3 tools/skinprep/prep_ash.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import math
import os

import cv2
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "ASH.png")
OUT = os.path.join(ROOT, "assets", "processed", "ASH")
UI_MAP = os.path.join(ROOT, "plugins", "ASH", "UI_MAP.json")


NEEDLE_FILL_DEG = float(os.environ.get("NEEDLE_FILL_DEG", "6"))


def rotate_fill(img, mask, centre, degrees, interpolation=cv2.INTER_LINEAR):
    """Replace masked pixels with pixels at the same radius, `degrees` away."""
    h, w = mask.shape
    m = cv2.getRotationMatrix2D(centre, degrees, 1.0)
    rotated = cv2.warpAffine(img, m, (w, h), flags=interpolation, borderMode=cv2.BORDER_REFLECT)
    out = img.copy()
    out[mask > 0] = rotated[mask > 0]
    return out


def match_illumination(original, filled, mask, sigma=3.0):
    """Keep only the grain of `filled` inside `mask` and take the smooth
    brightness from an inpainting of the surroundings, so a patch taken from
    a brighter or darker part of the face does not show as a streak."""
    grow = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=2)
    smooth = cv2.inpaint(original, grow, 7, cv2.INPAINT_TELEA).astype(np.float32)
    low = cv2.GaussianBlur(smooth, (0, 0), sigma)
    f = filled.astype(np.float32)
    grain = f - cv2.GaussianBlur(f, (0, 0), sigma)
    out = f.copy()
    out[grow > 0] = (low + grain)[grow > 0]
    return np.clip(out, 0, 255).astype(np.uint8)


def feather_composite(base, patch, mask, feather=2):
    alpha = cv2.GaussianBlur(mask.astype(np.float32) / 255.0, (0, 0), feather)[..., None]
    return (base * (1 - alpha) + patch * alpha).astype(np.uint8)


def orange_mask(img):
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    return ((hsv[..., 0] >= 6) & (hsv[..., 0] <= 34) & (hsv[..., 1] > 90) & (hsv[..., 2] > 150)).astype(np.uint8) * 255


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--debug", default=None)
    args = ap.parse_args()

    ui = json.load(open(UI_MAP))
    img = cv2.imread(REF, cv2.IMREAD_COLOR)
    assert img.shape[1] == ui["reference"]["width"] and img.shape[0] == ui["reference"]["height"]
    plate = img.copy()
    orange = orange_mask(img)

    # ---- knob pointers -------------------------------------------------
    for c in ui["controls"]:
        if c["type"] != "ROTARY":
            continue
        cx, cy = c["centre"]
        r_body = c["capRadius"]
        yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
        inside = ((xx - cx) ** 2 + (yy - cy) ** 2) <= (r_body * 0.97) ** 2
        m = (orange > 0) & inside
        m = cv2.dilate(m.astype(np.uint8) * 255, np.ones((5, 5), np.uint8), iterations=2)
        m = (m > 0) & inside
        mask = m.astype(np.uint8) * 255
        filled = rotate_fill(plate, mask, (float(cx), float(cy)), 70.0)
        plate = feather_composite(plate, filled, mask, 1.5)

    # ---- toggle lever ----------------------------------------------------
    tg = next(c for c in ui["controls"] if c["type"] == "TOGGLE")
    px, py = tg["pivot"]
    x0, y0, x1, y1 = tg["leverBox"]
    lever_mask = np.zeros(img.shape[:2], np.uint8)
    # shaft + ball as a rounded capsule
    cv2.rectangle(lever_mask, (x0 + 5, int(py)), (x1 - 5, y1 - 14), 255, -1)
    cv2.circle(lever_mask, (int(round(px)), y1 - 16), (x1 - x0) // 2, 255, -1)
    sprite_alpha = cv2.GaussianBlur(lever_mask, (0, 0), 1.0)
    sprite = np.dstack([img, sprite_alpha])[y0:y1, x0:x1]
    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "lever.png"), sprite)

    # remove the lever:
    #   hole (r < holeRadius): mirror of the glowing slot above the pivot,
    #   nut rings: same radius from a rotated copy (rings are concentric),
    #   paper below the nut: copied from paper beside it.
    grow = cv2.dilate(lever_mask, np.ones((3, 3), np.uint8), iterations=6)
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    rr = np.sqrt((xx - px) ** 2 + (yy - py) ** 2)
    hole_r = tg["holeRadius"]
    hole = ((grow > 0) & (rr <= hole_r)).astype(np.uint8) * 255
    nut = ((grow > 0) & (rr > hole_r) & (rr <= tg["nutRadius"])).astype(np.uint8) * 255
    paper = ((grow > 0) & (rr > tg["nutRadius"])).astype(np.uint8) * 255

    mirrored = plate.copy()
    ys, xs = np.nonzero(hole)
    src_y = np.clip((2 * py - ys).round().astype(int), 0, img.shape[0] - 1)
    mirrored[ys, xs] = plate[src_y, xs]
    plate = feather_composite(plate, mirrored, hole, 1.0)

    filled = rotate_fill(plate, nut, (float(px), float(py)), 90.0)
    plate = feather_composite(plate, filled, nut, 1.0)

    paper = cv2.dilate(paper, np.ones((3, 3), np.uint8), iterations=4)  # include the lever shadow
    paper[rr <= tg["nutRadius"]] = 0
    shifted = np.roll(plate, -(tg["nutRadius"] + 30), axis=1).astype(np.float32)
    # match the local brightness (the nut casts a soft shadow on the paper)
    border = (cv2.dilate(paper, np.ones((3, 3), np.uint8), iterations=5) > 0) & (paper == 0) & (rr > tg["nutRadius"])
    gain = plate[border].mean(axis=0) / np.maximum(shifted[paper > 0].mean(axis=0), 1.0)
    shifted = np.clip(shifted * gain, 0, 255).astype(np.uint8)
    plate = feather_composite(plate, shifted, paper, 2.0)

    # ---- VU needle ---------------------------------------------------------
    vu = next(c for c in ui["controls"] if c["type"] == "VU")
    needle = np.zeros(img.shape[:2], np.uint8)
    (ax, ay), (bx, by) = vu["bakedNeedle"]
    fx0, fy0, fx1, fy1 = vu["face"]
    face = np.zeros_like(needle)
    face[fy0:fy1 - 6, fx0:fx1] = 255
    split_y = vu["bandBottomY"]  # below: plain face; above: red band and scale arc

    # Face part. The needle carries a soft shadow and highlight, so the mask
    # is wide. The face glow is concentric with the pivot, so texture from the
    # same radius a few degrees away fits; its smooth brightness is then taken
    # from the surroundings so no streak remains.
    t = (split_y - ay) / (by - ay)
    mx, my = ax + (bx - ax) * t, ay + (by - ay) * t
    cv2.line(needle, (int(ax), int(ay)), (int(round(mx)), int(round(my))), 255, 17)
    needle[: int(split_y), :] = 0
    needle = cv2.bitwise_and(needle, face)
    pivot = (float(vu["pivot"][0]), float(vu["pivot"][1]))
    filled = rotate_fill(plate, needle, pivot, 6.0, cv2.INTER_NEAREST)  # nearest keeps the grain sharp
    filled = match_illumination(plate, filled, needle)
    plate = feather_composite(plate, filled, cv2.dilate(needle, np.ones((3, 3), np.uint8), iterations=2), 1.5)

    # Base of the needle, where it meets the pivot cover and the bottom rim:
    # both are symmetric about the pivot, so mirror the other side in.
    base = np.zeros_like(needle)
    cv2.line(base, (int(ax), int(ay) + 2), (int(round(ax + (bx - ax) * 0.2)), int(round(ay + (by - ay) * 0.2))), 255, 13)
    base[: int(ay) - 24, :] = 0
    base[int(ay) + 1:, :] = 0
    ys, xs = np.nonzero(base)
    mirrored = plate.copy()
    mirrored[ys, xs] = plate[ys, np.clip((2 * pivot[0] - xs).round().astype(int), 0, img.shape[1] - 1)]
    plate = feather_composite(plate, mirrored, base, 1.0)

    # Band part: the band is not concentric with the pivot, so copy it from
    # one minor-tick period further along the band, which also restores the
    # minor tick the needle tip covered.
    tip = np.zeros_like(needle)
    cv2.line(tip, (int(round(mx)), int(round(my)) + 4), (int(bx), int(by)), 255, 9)
    tip[int(split_y) + 4:, :] = 0
    dx, dy = vu["bandTickStep"]
    shifted = np.roll(np.roll(plate, -int(dy), axis=0), -int(dx), axis=1)
    plate = feather_composite(plate, shifted, tip, 1.0)

    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        for c in ui["controls"]:
            if "hitbox" in c:
                x, y, w, h = c["hitbox"]
                cv2.rectangle(dbg, (x, y), (x + w, y + h), (0, 255, 0), 1)
                cv2.putText(dbg, c["id"], (x + 2, y + 14), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 0), 1)
            if "centre" in c:
                cv2.circle(dbg, tuple(int(v) for v in c["centre"]), 3, (0, 0, 255), -1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)

    print("wrote", OUT)


if __name__ == "__main__":
    main()
