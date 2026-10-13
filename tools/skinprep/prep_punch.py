#!/usr/bin/env python3
"""Skin preparation for TLEET // PUNCH.

Turns the approved mockup (assets/reference/PUNCH.png) into the runtime assets
  assets/processed/PUNCH/plate.png      faceplate
  assets/processed/PUNCH/lamp_lit.png   the lit OverEasy lamp (RGBA), cut from the mockup
using the geometry in plugins/PUNCH/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - knob pointers (all five knobs);
  - every number printed on a knob scale (the scale dots stay): the editor
    draws the parameter's real value at the angle where the knob really has it;
  - the lit OverEasy lamp (the plate gets the unlit lamp; the editor draws
    lamp_lit.png only while OverEasy is on);
  - the GR LED bar with its glow (eleven segments were lit): the plate keeps
    the window, the rail, the ticks and the printed dB numbers; the editor
    draws every segment from the actual gain reduction.

Usage: python3 tools/skinprep/prep_punch.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import erase_rect_beside, feather_composite, lamp_off, remove_pointer_mirrored

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "PUNCH.png")
OUT = os.path.join(ROOT, "assets", "processed", "PUNCH")
UI_MAP = os.path.join(ROOT, "plugins", "PUNCH", "UI_MAP.json")


def deglow_window(img, plate, meter):
    """The lit bar lights up the whole meter window; take that glow out so the
    window looks like the unlit part everywhere (the editor draws the glow of
    the bar it lights)."""
    x, y, w, h = meter["window"]
    win = plate[y:y + h, x:x + w].astype(np.float32)
    # slow light level, without the thin printed text
    base = cv2.medianBlur(plate[y:y + h, x:x + w], 21).astype(np.float32)
    base = cv2.GaussianBlur(base, (0, 0), 6.0)
    # per row, the level of the unlit right part of the window
    ref_x0, ref_x1 = 930 - x, 1060 - x
    ref = np.median(base[:, ref_x0:ref_x1], axis=1, keepdims=True)
    glow = np.clip(base - ref, 0, None)
    # only the lit side; fade out towards the unlit part
    ramp = np.clip((860.0 - (np.arange(w) + x)) / 80.0, 0.0, 1.0)[None, :, None]
    out = plate.copy()
    out[y:y + h, x:x + w] = np.clip(win - glow * ramp, 0, 255).astype(np.uint8)
    return out


def rebuild_meter(img, plate, meter):
    """Segments out, ticks and rail kept: the unlit right part of the bar is
    the template, its segment strip replaced by the window floor mirrored
    upwards, its ticks taken out; it is tiled along the bar and the ticks put
    back at every printed scale position."""
    scale = {float(k): float(v) for k, v in meter["scale"].items()}
    majors = [scale[float(k)] for k in meter["majorTicks"]]
    minors = [v for k, v in scale.items() if k not in [float(m) for m in meter["majorTicks"]]]
    y0, y1 = 412, 478          # below the numbers, down to the window floor
    tick_y0, rail_y1 = 413, 437
    strip_top, strip_bottom = 438, 458
    clean_x0, clean_x1 = 776, 1072

    band = plate[y0:y1, clean_x0:clean_x1].copy()
    for k in range(strip_bottom - strip_top + 1):
        band[strip_bottom - k - y0] = band[min(y1 - 1, strip_bottom + 1 + k) - y0]
    # above the rail: the dark window without the ticks (one profile per row,
    # plus the fine grain of the template away from the ticks)
    above = slice(0, 433 - y0)
    flat = np.median(band[above], axis=1, keepdims=True).astype(np.float32)
    grain = band[above].astype(np.float32) - cv2.GaussianBlur(band[above].astype(np.float32), (0, 0), 1.5)
    near_tick = np.zeros(band.shape[1], bool)
    for x in majors + minors:
        xi = int(round(x)) - clean_x0
        near_tick[max(0, xi - 6):max(0, xi + 7)] = True
    grain[:, near_tick] = 0
    band[above] = np.clip(flat + grain, 0, 255).astype(np.uint8)
    # the rail is a plain horizontal line: one profile for its whole length,
    # so no tick roots are carried along the bar
    for yr in range(433 - y0, rail_y1 + 2 - y0):
        band[yr] = np.median(band[yr], axis=0)

    def sprite(x):
        xi = int(round(x))
        patch = img[tick_y0:rail_y1 + 1, xi - 3:xi + 4].astype(np.float32)
        lum = patch.mean(axis=2)
        bg = np.median(lum[:, [0, -1]])
        alpha = np.clip((lum - bg - 12.0) / 50.0, 0.0, 1.0)
        return patch, alpha[..., None]
    tick_major = sprite(scale[16])
    tick_minor = sprite(scale[14])

    x_start, x_end = 368, clean_x1
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
            region = out[tick_y0:rail_y1 + 1, xi - 3:xi + 4].astype(np.float32)
            out[tick_y0:rail_y1 + 1, xi - 3:xi + 4] = np.clip(region * (1 - alpha) + patch * alpha, 0, 255).astype(np.uint8)

    mask = np.zeros(plate.shape[:2], np.uint8)
    mask[y0 + 2:y1 - 3, x_start + 3:x_end] = 255
    return feather_composite(plate, out, mask, 1.5)


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

    knobs = [c for c in ui["controls"] if c["type"] == "ROTARY"]
    for c in knobs:
        plate = remove_pointer_mirrored(img, plate, c, yy, xx)
    for c in knobs:
        for rect in c.get("erase", []):
            plate = erase_rect_beside(plate, rect, c["scaleCentre"])

    plate, lamp = lamp_off(img, plate, controls["overEasy"])
    plate = deglow_window(img, plate, controls["grMeter"])
    plate = rebuild_meter(img, plate, controls["grMeter"])

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    cv2.imwrite(os.path.join(OUT, "lamp_lit.png"), lamp, [cv2.IMWRITE_PNG_COMPRESSION, 9])
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
            cv2.line(dbg, (int(round(x)), m["segmentRow"][0]), (int(round(x)), m["segmentRow"][1]), (0, 255, 255), 1)
        x, y, w, h = controls["overEasy"]["lamp"]["rect"]
        cv2.rectangle(dbg, (x, y), (x + w, y + h), (255, 128, 0), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
