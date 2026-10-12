#!/usr/bin/env python3
"""Skin preparation for TLEET // DOUBLE.

Turns the approved mockup (assets/reference/DOUBLE.png) into runtime assets:
  assets/processed/DOUBLE/plate.png           faceplate with every value-dependent part removed
  assets/processed/DOUBLE/<group>_lit_<i>.png  lit button sprites (RGBA), one per state
using the geometry in plugins/DOUBLE/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - knob pointers (the HPF knob even had two);
  - the value readouts under the knobs ("12 cents", "18 ms", ...): they did
    not match the pointers, the editor prints the real values;
  - the lit VOICES "2", MODE "WIDE" and MONO SAFE buttons: the plate shows
    every button released; the lit look of each button is rebuilt from the
    lit one of its group with that button's own label;
  - the waveform and the dotted pan axis of the voice display;
  - the curve, grid and frequency labels of the filter display (the labels
    did not sit at their frequencies, and "20 20K" was garbled).

Usage: python3 tools/skinprep/prep_double.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import angular_fill, feather_composite, inpaint_with_grain, rotate_fill, shift, text_alpha

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "DOUBLE.png")
OUT = os.path.join(ROOT, "assets", "processed", "DOUBLE")
UI_MAP = os.path.join(ROOT, "plugins", "DOUBLE", "UI_MAP.json")


def rect_mask(shape, rects, grow=0):
    m = np.zeros(shape[:2], np.uint8)
    for x, y, w, h in rects:
        cv2.rectangle(m, (int(round(x)) - grow, int(round(y)) - grow),
                      (int(round(x + w)) + grow - 1, int(round(y + h)) + grow - 1), 255, -1)
    return m


def remove_pointers(img, plate, c, yy, xx):
    cx, cy = c["centre"]
    cap = c["capRadius"]
    r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    inside = r <= cap - 2
    # white-hot cores: every elongated radial blob on the cap (HPF has two)
    hot = ((hsv[..., 2] > 170) & inside).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(hot)
    core = np.zeros(img.shape[:2], np.uint8)
    for i in range(1, n):
        ys, xs = np.nonzero(lab == i)
        rr = r[ys, xs]
        if st[i, cv2.CC_STAT_AREA] >= 25 and rr.max() - rr.min() >= 15:
            core[lab == i] = 255
    # pink glow around the cores
    around = cv2.dilate(core, np.ones((3, 3), np.uint8), iterations=7) > 0
    glow = (hsv[..., 1] > 40) & (hsv[..., 2] > 70) & around & inside
    mask = cv2.dilate((glow | (core > 0)).astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=2)
    mask[r <= 11] = 255  # pointer root on the hub
    mask[r > cap - 1] = 0
    smooth = angular_fill(plate, mask, (float(cx), float(cy))).astype(np.float32)
    rotated = rotate_fill(plate, mask, (float(cx), float(cy)), 70.0, cv2.INTER_NEAREST).astype(np.float32)
    grain = rotated - cv2.GaussianBlur(rotated, (0, 0), 1.5)
    m = cv2.getRotationMatrix2D((float(cx), float(cy)), 70.0, 1.0)
    source_was_pointer = cv2.warpAffine(mask, m, (mask.shape[1], mask.shape[0])) > 0
    grain[source_was_pointer | (r <= 13)] = 0
    filled = np.clip(smooth + grain, 0, 255).astype(np.uint8)
    return feather_composite(plate, filled, mask, 1.0)


def remove_text(plate, rect, inset=3, offset=18.0, grow=2, grain_from=None):
    x, y, w, h = [int(round(v)) for v in rect]
    region = plate[y + inset:y + h - inset, x + inset:x + w - inset]
    a = text_alpha(region, offset=offset)
    m = np.zeros(plate.shape[:2], np.uint8)
    m[y + inset:y + h - inset, x + inset:x + w - inset] = (a > 0.05).astype(np.uint8) * 255
    m = cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=grow)
    m[: y + 1, :] = 0
    m[y + h - 1:, :] = 0
    m[:, : x + 1] = 0
    m[:, x + w - 1:] = 0
    src = grain_from if grain_from is not None else (x + 2, y + 2, w - 4, 6)
    return inpaint_with_grain(plate, m, src), m


MARGIN = 12  # lit glow spills this far around a bezel


def face_alpha(img, face, inset=5, offset=14.0):
    x, y, w, h = [int(round(v)) for v in face]
    a = np.zeros((h, w), np.float32)
    a[inset:h - inset, inset:w - inset] = text_alpha(img[y + inset:y + h - inset, x + inset:x + w - inset], offset=offset)
    # drop dust specks: a label stroke is never a tiny isolated blob
    n, lab, st, _ = cv2.connectedComponentsWithStats((a > 0.02).astype(np.uint8))
    for i in range(1, n):
        if st[i, cv2.CC_STAT_AREA] < 12:
            a[lab == i] = 0
    return a


def mean_colour(region, alpha, lo=0.8):
    sel = alpha >= lo
    return region[sel].reshape(-1, 3).mean(axis=0) if sel.any() else np.array([200, 200, 200], np.float32)


def blank_face(img, face, alpha, grain_rows=4):
    """The face without its label: inpaint the strokes, keep the face texture."""
    x, y, w, h = [int(round(v)) for v in face]
    m = np.zeros(img.shape[:2], np.uint8)
    m[y:y + h, x:x + w] = cv2.dilate((alpha > 0.04).astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=2)
    return inpaint_with_grain(img, m, (x + 4, y + 3, w - 8, grain_rows), radius=4)


def put_text(base, face, alpha, colour, glow=None, glow_sigma=3.5):
    """Composite a label (soft alpha) in `colour`, optionally with an additive glow."""
    x, y, w, h = [int(round(v)) for v in face]
    out = base.astype(np.float32)
    reg = out[y:y + h, x:x + w]
    a = alpha[..., None]
    reg[:] = reg * (1 - a) + np.asarray(colour, np.float32) * a
    if glow is not None:
        g = cv2.GaussianBlur(alpha, (0, 0), glow_sigma)[..., None]
        reg[:] = reg + g * np.asarray(glow, np.float32)
    return np.clip(out, 0, 255).astype(np.uint8)


def process_group(img, plate, faces, bezels, lit_index, unlit_index, name, out_dir):
    """Rebuild a row/column of illuminated push buttons.

    The artwork has one lit button per group. The plate gets that button
    released (the released template moved there, with its own label), and
    every button gets a lit sprite (the lit template moved there, with its
    own label lit)."""
    n = len(faces)
    alphas = []
    for i in range(n):
        lit = i == lit_index
        alphas.append(face_alpha(img, faces[i], offset=60.0 if lit else 14.0))
    unlit_colour = mean_colour(img[int(round(faces[unlit_index][1])):, int(round(faces[unlit_index][0])):][:alphas[unlit_index].shape[0], :alphas[unlit_index].shape[1]], alphas[unlit_index])
    lf = faces[lit_index]
    lit_region = img[int(round(lf[1])):int(round(lf[1])) + alphas[lit_index].shape[0], int(round(lf[0])):int(round(lf[0])) + alphas[lit_index].shape[1]]
    lit_colour = mean_colour(lit_region, alphas[lit_index], 0.85)

    # templates without labels
    # blanking masks reach further out than the label shapes, so no glow fringe of the old label stays
    unlit_blank = blank_face(img, faces[unlit_index], face_alpha(img, faces[unlit_index], offset=8.0))
    lit_blank = blank_face(img, faces[lit_index], face_alpha(img, faces[lit_index], offset=8.0))
    fx, fy, fw, fh = [int(round(v)) for v in faces[lit_index]]
    inner = lit_blank[fy + 4:fy + fh - 4, fx + 4:fx + fw - 4]
    inner[:] = cv2.medianBlur(inner, 5)  # no stray specks from the grain under every lit label

    def area(b, grow):
        x, y, w, h = b
        return (x - grow, y - grow, w + 2 * grow, h + 2 * grow)

    # plate: the lit button becomes released
    dx = faces[lit_index][0] - faces[unlit_index][0]
    dy = faces[lit_index][1] - faces[unlit_index][1]
    released = shift(unlit_blank, dx, dy)
    # the lit label's core is thinner than a released stroke (its glow is cut off): widen it a little
    # the lit label's own alpha includes its glow; its hot core has the released stroke width
    released = put_text(released, faces[lit_index], face_alpha(img, faces[lit_index], offset=95.0), unlit_colour)
    region = rect_mask(img.shape, [area(bezels[lit_index], MARGIN - 4)])
    plate = feather_composite(plate, released, region, 3.0)

    # lit sprites
    sprites = []
    for i in range(n):
        ddx = faces[i][0] - faces[lit_index][0]
        ddy = faces[i][1] - faces[lit_index][1]
        lit = shift(lit_blank, ddx, ddy)
        lit = put_text(lit, faces[i], alphas[i], lit_colour, glow=(70, 60, 150))
        x, y, w, h = [int(round(v)) for v in area(bezels[i], MARGIN)]
        rgb = lit[y:y + h, x:x + w].astype(np.float32)
        under = plate[y:y + h, x:x + w].astype(np.float32)
        # alpha: opaque on the button, the glow fades out over the panel
        alpha = np.zeros((h, w), np.float32)
        bx, by, bw, bh = [int(round(v)) for v in bezels[i]]
        alpha[by - y:by - y + bh, bx - x:bx - x + bw] = 1.0
        diff = np.clip((cv2.cvtColor(rgb.astype(np.uint8), cv2.COLOR_BGR2GRAY).astype(np.float32)
                        - cv2.cvtColor(under.astype(np.uint8), cv2.COLOR_BGR2GRAY).astype(np.float32)) / 30.0, 0, 1)
        alpha = np.maximum(alpha, diff)
        alpha = cv2.GaussianBlur(alpha, (0, 0), 1.0)
        sprite = np.dstack([rgb, alpha * 255]).astype(np.uint8)
        cv2.imwrite(os.path.join(out_dir, "%s_lit_%d.png" % (name, i)), sprite)
        sprites.append((x, y, w, h))
    return plate, sprites


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
    os.makedirs(OUT, exist_ok=True)

    # ---- knob pointers and value readouts -----------------------------------
    for c in ui["controls"]:
        if c["type"] != "ROTARY":
            continue
        plate = remove_pointers(img, plate, c, yy, xx)
        # the readout window is plain dark glass: regenerate its whole inside
        # (the text had a soft glow that a text-only mask would leave behind)
        x, y, w, h = c["readout"]
        inner = rect_mask(img.shape, [(x + 3, y + 3, w - 6, h - 6)])
        plate = inpaint_with_grain(plate, inner, (x + 2, y + 1, w - 4, 3), radius=3)

    # ---- illuminated buttons ------------------------------------------------
    sprite_boxes = {}
    for gid in ("voices", "mode"):
        c = controls[gid]
        plate, boxes = process_group(img, plate, c["faces"], c["bezels"], c["litInArtwork"], 0, gid, OUT)
        sprite_boxes[gid] = boxes
    ms, al = controls["monoSafe"], controls["autoLevel"]
    plate, boxes = process_group(img, plate, [ms["face"], al["face"]], [ms["bezel"], al["bezel"]], 0, 1, "toggle", OUT)
    sprite_boxes["toggle"] = boxes
    print("sprite boxes", json.dumps(sprite_boxes))

    # ---- voice display: waveform, its haze, axis and pan dots go; L/R and the centre star stay
    # The screen is rebuilt: its smooth glass brightness is taken from the
    # strips above and below the old waveform, grain is added, and the static
    # marks are laid back on top from the artwork.
    vd = controls["voiceDisplay"]
    sx, sy, sw, sh = [int(v) for v in vd["screen"]]
    x0, x1, y0, y1 = sx + 3, sx + sw - 3, sy + 3, sy + sh - 3
    src = img.astype(np.float32)
    top = np.median(src[y0 + 2:y0 + 40, x0:x1], axis=0)
    bot = np.median(src[y1 - 14:y1 - 2, x0:x1], axis=0)
    top = cv2.GaussianBlur(top[None], (0, 0), 12)[0]
    bot = cv2.GaussianBlur(bot[None], (0, 0), 12)[0]
    t = np.linspace(0.0, 1.0, y1 - y0)[:, None, None]
    field = top[None] * (1 - t) + bot[None] * t
    strip = src[y0 + 2:y0 + 40, x0 + 60:x0 + 460]
    hp = strip - cv2.GaussianBlur(strip, (0, 0), 3)
    rng = np.random.default_rng(5)
    grain = np.zeros_like(field)
    for by in range(0, grain.shape[0], 16):
        for bx in range(0, grain.shape[1], 16):
            oy = int(rng.integers(0, hp.shape[0] - 16)); ox = int(rng.integers(0, hp.shape[1] - 16))
            hh = min(16, grain.shape[0] - by); ww = min(16, grain.shape[1] - bx)
            grain[by:by + hh, bx:bx + ww] = hp[oy:oy + hh, ox:ox + ww]
    glass = np.clip(field + grain * 0.6, 0, 255)
    # static marks back on top
    keep = rect_mask(img.shape, [tuple(r) for r in vd["keepStatic"].values()] + [(752, 384, 33, 26)])[y0:y1, x0:x1] > 0
    lum_src = cv2.cvtColor(img[y0:y1, x0:x1], cv2.COLOR_BGR2GRAY).astype(np.float32)
    lum_field = cv2.cvtColor(np.clip(field, 0, 255).astype(np.uint8), cv2.COLOR_BGR2GRAY).astype(np.float32)
    a = np.clip((lum_src - lum_field - 4.0) / 25.0, 0, 1) * keep
    a = cv2.GaussianBlur(a, (0, 0), 0.6)[..., None]
    region = glass * (1 - a) + src[y0:y1, x0:x1] * a
    plate[y0:y1, x0:x1] = feather_composite(plate[y0:y1, x0:x1], region.astype(np.uint8),
                                            np.pad(np.full((y1 - y0 - 4, x1 - x0 - 4), 255, np.uint8), 2), 1.0)

    # ---- filter display: curve, fill, grid and axis labels go (all drawn live)
    fd = controls["filterDisplay"]
    fx, fy, fw, fh = [int(v) for v in fd["screen"]]
    x0, x1, y0, y1 = fx + 2, fx + fw - 2, fy + 2, fy + fh - 2
    inside = img[y0:y1, x0:x1].astype(np.float32)
    lum = cv2.cvtColor(img[y0:y1, x0:x1], cv2.COLOR_BGR2GRAY)
    dark = inside[lum <= np.percentile(lum, 35)].mean(axis=0)
    rng = np.random.default_rng(7)
    grain = np.zeros_like(inside)
    for by in range(0, grain.shape[0], 16):
        for bx in range(0, grain.shape[1], 16):
            oy = int(rng.integers(0, hp.shape[0] - 16)); ox = int(rng.integers(0, hp.shape[1] - 16))
            hh = min(16, grain.shape[0] - by); ww = min(16, grain.shape[1] - bx)
            grain[by:by + hh, bx:bx + ww] = hp[oy:oy + hh, ox:ox + ww]
    glass = np.clip(dark[None, None, :] + grain * 0.6, 0, 255).astype(np.uint8)
    plate[y0:y1, x0:x1] = feather_composite(plate[y0:y1, x0:x1], glass,
                                            np.pad(np.full((y1 - y0 - 2, x1 - x0 - 2), 255, np.uint8), 1), 0.7)
    lx, ly, lw, lh = fd["labelRow"]
    plate, _ = remove_text(plate, (lx, ly, lw, lh), inset=1, offset=12.0, grow=2, grain_from=(455, 836, 35, 20))

    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    print("wrote", OUT)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        for c in ui["controls"]:
            boxes = c.get("hitboxes", [c["hitbox"]] if "hitbox" in c else [])
            for x, y, w, h in boxes:
                cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (0, 255, 0), 1)
            if "readout" in c:
                x, y, w, h = c["readout"]
                cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (255, 255, 0), 1)
            if "centre" in c:
                cx, cy = c["centre"]
                cv2.drawMarker(dbg, (int(round(cx)), int(round(cy))), (0, 0, 255), cv2.MARKER_CROSS, 12, 1)
                p_ = c["pointer"]
                for deg in c["travelDegrees"]:
                    a = np.radians(deg)
                    cv2.line(dbg, (int(cx + np.sin(a) * p_["inner"]), int(cy - np.cos(a) * p_["inner"])),
                             (int(cx + np.sin(a) * p_["outer"]), int(cy - np.cos(a) * p_["outer"])), (255, 0, 255), 1)
            for key in ("screen", "plot"):
                if key in c:
                    x, y, w, h = c[key]
                    cv2.rectangle(dbg, (int(x), int(y)), (int(x + w), int(y + h)), (0, 200, 255), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
