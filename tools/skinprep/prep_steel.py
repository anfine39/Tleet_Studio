#!/usr/bin/env python3
"""Skin preparation for TLEET // STEEL.

Turns the approved mockup (assets/reference/STEEL.png) into the runtime assets
  assets/processed/STEEL/plate.png       faceplate
  assets/processed/STEEL/<sprite>.png    lit buttons (RGBA)
  assets/processed/STEEL/sprites.json    where each sprite goes (reference pixels)
using the geometry in plugins/STEEL/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - all seven knob pointers and their glow, every scale dot (the small
    knobs' printed dots are unevenly spaced) and every number printed on a
    knob scale, and the values in the readout windows;
  - the lit MODE, RELEASE, OVERSAMPLING and SUB FILTER buttons and the lit
    TRUE PEAK and SOFT CLIP buttons (DELTA and LEVEL MATCH get lit images
    made from the lit TRUE PEAK button);
  - the history display's waveform, GR trace, grid, dB scales and time
    labels; the lit parts of the IN / OUT bars;
  - the five loudness values;
  - the bottom spectrum's curve, grid and frequency labels.

Usage: python3 tools/skinprep/prep_steel.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import feather_composite, rect_mask
from prep_echo import erase_captions, erase_dot, pad
from prep_veil import resize_to, sprite_from
from prep_void import GLOW, button_group, caption_alpha, deglow, pointer_out, printed_mask, scale_dots, tile_unlit

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "STEEL.png")
OUT = os.path.join(ROOT, "assets", "processed", "STEEL")
UI_MAP = os.path.join(ROOT, "plugins", "STEEL", "UI_MAP.json")

HALO = 14


def white_alpha(face, margin=6):
    """The white-hot strokes of a lit caption (low saturation, bright)."""
    hsv = cv2.cvtColor(face, cv2.COLOR_BGR2HSV).astype(np.float32)
    white = np.clip((150.0 - hsv[..., 1]) / 80.0, 0.0, 1.0) * np.clip((hsv[..., 2] - 150.0) / 60.0, 0.0, 1.0)
    white[:margin, :] = 0; white[-margin:, :] = 0; white[:, :margin] = 0; white[:, -margin:] = 0
    return white


def hole(a):
    m = (cv2.GaussianBlur(a, (0, 0), 1.0) > 0.05).astype(np.uint8) * 255
    return cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=2)


def toggles(img, plate, controls):
    """TRUE PEAK and SOFT CLIP are lit in the artwork: dark on the plate (a
    dark donor button fitted to the body, the caption in the donor's ink,
    the light around removed), lit sprite = the artwork's lit button.
    DELTA and LEVEL MATCH are dark in the artwork: their lit image is the
    lit TRUE PEAK body (caption removed) fitted to them, their glyph in the
    lit ink with a rose glow, and TRUE PEAK's halo."""
    g = HALO
    tp = controls["truePeak"]
    x, y, w, h = tp["body"]
    tp_alpha = white_alpha(img[y:y + h, x:x + w])
    lit_ink = np.median(img[y:y + h, x:x + w][tp_alpha > 0.8].astype(np.float32), axis=0)
    lit_blank = cv2.inpaint(img[y:y + h, x:x + w], hole(tp_alpha), 5, cv2.INPAINT_TELEA).astype(np.float32)
    tp_ring = img[y - g:y + h + g, x - g:x + w + g].astype(np.float32)

    lit_images = {}
    halo = None
    for cid in ("truePeak", "softClip"):
        c = controls[cid]
        x, y, w, h = c["body"]
        a = white_alpha(img[y:y + h, x:x + w])
        dx, dy, dw, dh = c["darkDonor"]
        donor = img[dy:dy + dh, dx:dx + dw]
        da = caption_alpha(donor, 0)
        dark_ink = np.median(donor[da > 0.8].astype(np.float32), axis=0)
        blank = cv2.inpaint(donor, hole(da), 5, cv2.INPAINT_TELEA).astype(np.float32)
        fitted = resize_to(blank, w, h)
        aa = cv2.GaussianBlur(a, (0, 0), 0.5)[..., None]
        fitted = fitted * (1 - aa) + dark_ink * aa
        ring = img[y - g:y + h + g, x - g:x + w + g].astype(np.float32)
        pasted = plate.copy()
        pasted[y:y + h, x:x + w] = np.clip(fitted, 0, 255).astype(np.uint8)
        plate = feather_composite(plate, pasted, rect_mask(plate.shape, [(x, y, w - 1, h - 1)]), 0.8)
        plate = deglow(plate, (x, y, w, h), g, inside=False)
        lit = plate.copy()
        lit[y - g:y + h + g, x - g:x + w + g] = ring.astype(np.uint8)
        lit_images[cid] = lit
        if cid == "truePeak":
            dark_ring = plate[y - g:y + h + g, x - g:x + w + g].astype(np.float32)
            halo = np.clip(tp_ring - dark_ring, 0, None)
            halo[g:g + h, g:g + w] = 0.0

    for cid in ("delta", "levelMatch"):
        c = controls[cid]
        x, y, w, h = c["body"]
        a = caption_alpha(img[y:y + h, x:x + w], 0)
        a = np.clip(cv2.GaussianBlur(a, (0, 0), 0.5) * 1.3, 0.0, 1.0)
        body = resize_to(lit_blank, w, h)
        glow = cv2.GaussianBlur(a, (0, 0), 2.5)
        body = body + GLOW * 0.5 * glow[..., None]
        body = body * (1 - a[..., None]) + lit_ink * a[..., None]
        lit = plate.astype(np.float32).copy()
        lit[y:y + h, x:x + w] = body
        hl = resize_to(halo, w + 2 * g, h + 2 * g)
        hl[g:g + h, g:g + w] = 0.0
        lit[y - g:y + h + g, x - g:x + w + g] += hl
        lit_images[cid] = np.clip(lit, 0, 255).astype(np.uint8)
    return plate, lit_images


def rows(rect, most=30):
    """A tall strip of scale labels in pieces at most `most` pixels high."""
    x, y, w, h = rect
    n = int(np.ceil(h / most))
    edges = np.linspace(y, y + h, n + 1).round().astype(int)
    return [[x, int(edges[i]), w, int(edges[i + 1] - edges[i])] for i in range(n)]


def glass_fill(plate, rect, seed, sigma=1.6):
    """Printing on a display's glass: each row's own glass tone (the darker
    half of the row, so no label is copied) with a fine grain."""
    x, y, w, h = rect
    reg = plate[y:y + h, x:x + w].astype(np.float32)
    s = reg.sum(axis=2)
    tone = np.array([np.median(reg[r][s[r] <= np.percentile(s[r], 40)], axis=0) for r in range(h)])
    tone = cv2.GaussianBlur(tone[:, None, :], (1, 0), sigmaX=0.1, sigmaY=3.0)[:, 0, :]
    filled = plate.astype(np.float32).copy()
    filled[y:y + h, x:x + w] = tone[:, None, :] + np.random.default_rng(seed).normal(0.0, sigma, (h, w))[..., None]
    return feather_composite(plate, np.clip(filled, 0, 255).astype(np.uint8), rect_mask(plate.shape, [rect]), 0.8)


def flat_fill(plate, rect, seed, sigma=1.6):
    """A display's glass: its own dark tone (the darkest part, so no curve
    or waveform is copied) with a fine grain."""
    x, y, w, h = rect
    inner = plate[y:y + h, x:x + w].reshape(-1, 3).astype(np.float32)
    s = inner.sum(axis=1)
    tone = np.median(inner[s < np.percentile(s, 25)], axis=0)
    filled = np.empty(plate.shape, np.float32)
    filled[:] = tone
    filled += np.random.default_rng(seed).normal(0.0, sigma, filled.shape[:2])[..., None]
    return feather_composite(plate, np.clip(filled, 0, 255).astype(np.uint8), rect_mask(plate.shape, [rect]), 0.8)


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
        for p in scale_dots(img, c):
            av = avoid.copy()
            av[int(p[1]) - 7:int(p[1]) + 8, int(p[0]) - 7:int(p[0]) + 8] = False
            plate = erase_dot(plate, p, av, radius=5 if c["capRadius"] > 40 else 4)
        rects = [pad(r, 2) for r in c.get("erase", [])] + [c["eraseReadout"]]
        plate = erase_captions(plate, rects, avoid)

    sprites = {}
    for gid in ("mode", "releaseMode", "oversampling", "subFilter"):
        grp = controls[gid]
        plate, lit = button_group(img, plate, grp, 0, g=HALO, frame=7)
        for b in grp["buttons"]:
            sprites[gid + "_" + b["label"].lower()] = (lit[b["label"]], pad(b["body"], HALO), "lit")

    plate, lit = toggles(img, plate, controls)
    for cid, im in lit.items():
        sprites[cid + "_lit"] = (im, pad(controls[cid]["body"], HALO), "lit")

    # IN / OUT bars unlit (each bar's own unlit top, repeated)
    mt = controls["meters"]
    for side in ("in", "out"):
        for (bx, by, bw, bh), top in zip(mt[side], mt["litTopArt"][side]):
            plate = tile_unlit(plate, [bx - 1, by, bw + 2, bh], top, by + bh + 1, "y")
            plate = deglow(plate, [bx, top, bw, by + bh - top], 22)

    # history display: waveform, GR trace and grid out; dB scales and time labels out
    hs = controls["history"]
    for i, r in enumerate(hs["erase"]):
        plate = flat_fill(plate, r, 7 + i)
    for i, r in enumerate(hs["eraseScale"] + hs["eraseTime"]):
        plate = glass_fill(plate, pad(r, 1), 20 + i)

    # loudness values
    lp = controls["loudness"]
    plate = erase_captions(plate, [p for v in lp["values"] for p in rows(v["rect"], 22)], avoid)

    # bottom spectrum
    sp = controls["spectrum"]
    plate = flat_fill(plate, sp["erase"][0], 11)
    plate = erase_captions(plate, sp["eraseLabels"], avoid)

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    rects = {}
    for name, (lit_img, rect, _) in sprites.items():
        cv2.imwrite(os.path.join(OUT, name + ".png"), sprite_from(lit_img, plate, rect), [cv2.IMWRITE_PNG_COMPRESSION, 9])
        rects[name] = [int(v) for v in rect]
    with open(os.path.join(OUT, "sprites.json"), "w") as f:
        json.dump({"sprites": rects}, f, indent=1)
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
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
