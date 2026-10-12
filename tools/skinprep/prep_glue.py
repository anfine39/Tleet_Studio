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

from skinlib import angular_fill, erase_dot, erase_rect_beside, feather_composite, remove_pointer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "GLUE.png")
OUT = os.path.join(ROOT, "assets", "processed", "GLUE")
UI_MAP = os.path.join(ROOT, "plugins", "GLUE", "UI_MAP.json")


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
