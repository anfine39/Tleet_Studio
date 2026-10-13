#!/usr/bin/env python3
"""Skin preparation for TLEET // VEIL.

Turns the approved mockup (assets/reference/VEIL.png) into the runtime assets
  assets/processed/VEIL/plate.png        faceplate
  assets/processed/VEIL/<sprite>.png     lit buttons and lamp (RGBA)
using the geometry in plugins/VEIL/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - knob pointers (all thirteen knobs; DRY/WET is drawn with two);
  - every number printed on a knob scale (the dots stay): the editor draws
    the parameter's real value at the angle where the knob really has it;
  - the values in the readout windows under the knobs;
  - the lit buttons (CHARACTER, SIDECHAIN, METER) and the STEREO LINK lamp:
    the plate shows every button dark, the editor draws the lit button of the
    real state. Sprites are made for every button, also those the artwork
    shows dark, from the lit button's own light and the button's own caption;
  - both VU needles;
  - the spectrum: curve, fill, grid and frequency labels (the analyser draws
    them at its real frequency axis).

Usage: python3 tools/skinprep/prep_veil.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import angular_fill, feather_composite, inpaint_with_grain, lamp_off, match_illumination, rect_mask
from prep_weight import erase_label

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "VEIL.png")
OUT = os.path.join(ROOT, "assets", "processed", "VEIL")
UI_MAP = os.path.join(ROOT, "plugins", "VEIL", "UI_MAP.json")


def pad(rect, p):
    x, y, w, h = rect
    return [x - p, y - p, w + 2 * p, h + 2 * p]


def red_mask(img, s_min=45, v_min=60):
    """The pointers', rims' and texts' light is red / pink (hue near 0)."""
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    return ((h <= 10) | (h >= 160)) & (s > s_min) & (v > v_min)


# ---------------------------------------------------------------- knobs

def remove_pointer_at(img, plate, c, angle, yy, xx):
    """A lit pointer at `angle` (degrees, 0 = up, clockwise), its glow and its
    notch on the skirt: replaced by the knob at the same radius some degrees
    away (the face is turned brushed metal, the skirt a dark ring, both
    rotationally even), with the slow brightness from the surroundings."""
    cx, cy = c["centre"]
    t = np.radians(angle)
    ux, uy = np.sin(t), -np.cos(t)
    px, py = xx - cx, yy - cy
    along = px * ux + py * uy
    across = np.abs(px * uy - py * ux)
    r = np.hypot(px, py)
    rim = c["rimRadius"]
    hw = c["pointer"]["width"] * 1.3 + 2.5
    line = (along > -2) & (along < rim + 1) & (across < hw)
    ang = np.degrees(np.arctan2(px, -py))
    off = np.abs((ang - angle + 180.0) % 360.0 - 180.0)
    near = (along > -4) & (along < rim + 3) & (across < hw + 9) & (r < rim + 2) & ((off < 16.0) | (r < 9))
    glow = red_mask(img, 35, 45) & near
    hot = (cv2.cvtColor(img, cv2.COLOR_BGR2HSV)[..., 2] > 170) & near & (across < hw + 4)
    mask = ((line | glow | hot | (r <= 6)) & (r < rim + 2)).astype(np.uint8) * 255
    mask = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1)

    # source: the turn whose source area holds the least light of its own
    best = None
    lum = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    lit = (red_mask(plate, 35, 45) | (lum > 150)).astype(np.uint8) * 255
    for turn in (28.0, -28.0, 40.0, -40.0, 55.0, -55.0):
        m = cv2.getRotationMatrix2D((float(cx), float(cy)), turn, 1.0)
        src_lit = cv2.warpAffine(lit, m, (lit.shape[1], lit.shape[0]))
        score = float((src_lit[mask > 0] > 0).sum())
        if best is None or score < best[0]:
            best = (score, turn)
    turn = best[1]
    m = cv2.getRotationMatrix2D((float(cx), float(cy)), turn, 1.0)
    rotated = cv2.warpAffine(plate, m, (plate.shape[1], plate.shape[0]), flags=cv2.INTER_LINEAR,
                             borderMode=cv2.BORDER_REFLECT).astype(np.float32)
    # face: the light on the turned metal changes with the angle, so its level
    # comes along the circle from both sides; only fine grain is copied
    smooth = angular_fill(plate, mask, (float(cx), float(cy))).astype(np.float32)
    grain = rotated - cv2.GaussianBlur(rotated, (0, 0), 1.2)
    # near the hub a turn moves only a few pixels: the copy would still hold the pointer
    grain *= np.clip((r - 10.0) / 6.0, 0.0, 1.0)[..., None]
    face = smooth + grain
    # skirt: the dark ring, whole from the turned copy, level from the surroundings
    skirt = match_illumination(plate, rotated.astype(np.uint8), mask, sigma=3.0).astype(np.float32)
    on_face = (r < c["capRadius"] - 1)[..., None]
    filled = np.clip(np.where(on_face, face, skirt), 0, 255).astype(np.uint8)
    out = feather_composite(plate, filled, mask, 1.0)
    # the pointer's red light on the rest of the knob: to the metal's neutral
    knob = r < rim + 1
    hsv = cv2.cvtColor(out, cv2.COLOR_BGR2HSV).astype(np.float32)
    red = ((hsv[..., 0] <= 10) | (hsv[..., 0] >= 160)) & knob
    hsv[..., 1] = np.where(red, np.minimum(hsv[..., 1], 28.0), hsv[..., 1])
    return cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR)


# ---------------------------------------------------------------- readouts

def empty_readout(plate, rect):
    """The readout glass without its baked value: per row, the glass level
    beside the text; grain from the glass itself."""
    x, y, w, h = rect
    inner = (x + 3, y + 3, w - 6, h - 6)
    ix, iy, iw, ih = inner
    reg = plate[iy:iy + ih, ix:ix + iw].astype(np.float32)
    lum = reg.mean(axis=2)
    out = reg.copy()
    rng = np.random.default_rng(x * 7 + y)
    for j in range(ih):
        row = reg[j]
        l = lum[j]
        keep = l <= np.percentile(l, 45) + 3.0
        level = np.median(row[keep], axis=0)
        spread = max(0.6, float(np.std(l[keep])) * 0.8)
        out[j] = level + rng.normal(0.0, spread, (iw, 1))
    out = cv2.GaussianBlur(out, (0, 0), 0.5)
    # the glass level changes smoothly from top to bottom: smooth the rows
    rows = cv2.GaussianBlur(out.mean(axis=1, keepdims=True), (0, 0), 1.2)
    grain = out - out.mean(axis=1, keepdims=True)
    out = rows + grain
    res = plate.copy()
    res[iy:iy + ih, ix:ix + iw] = np.clip(out, 0, 255).astype(np.uint8)
    mask = rect_mask(plate.shape, [inner])
    return feather_composite(plate, res, cv2.erode(mask, np.ones((3, 3), np.uint8)), 0.8)


# ---------------------------------------------------------------- buttons

def text_alpha_on(face, lit):
    """Soft alpha of a button caption: light strokes on a dark face."""
    lum = cv2.cvtColor(face, cv2.COLOR_BGR2GRAY).astype(np.float32)
    bg = cv2.medianBlur(face, 21)
    bgl = cv2.cvtColor(bg, cv2.COLOR_BGR2GRAY).astype(np.float32)
    top = float(np.percentile(lum, 99.5))
    lo = 40.0 if lit else 25.0  # a lit caption has a red halo: keep only the strokes
    a = (lum - bgl - lo) / max(1.0, top - float(np.median(bgl)) - lo)
    return np.clip(a, 0.0, 1.0)


def blank_face(body_img, margin):
    """A button body with its caption removed: inside the face, each row
    takes the face level of that row (the caption occupies the middle)."""
    b = body_img.astype(np.float32)
    h, w = b.shape[:2]
    m = margin
    out = b.copy()
    face = b[m:h - m, m:w - m]
    lum = face.mean(axis=2)
    for j in range(face.shape[0]):
        l = lum[j]
        keep = l <= np.percentile(l, 50) + 4.0
        out[m + j, m:w - m] = np.median(face[j][keep], axis=0)
    detail = b - cv2.GaussianBlur(b, (0, 0), 1.5)
    tmask = np.zeros((h, w), np.float32)
    tmask[m:h - m, m:w - m] = 1.0
    textish = cv2.dilate((lum > np.percentile(lum, 70) + 8).astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    keep_detail = np.ones((h, w), np.float32)
    keep_detail[m:h - m, m:w - m][textish] = 0.0
    out = out + detail * keep_detail[..., None] * tmask[..., None] * 0.6
    out[tmask == 0] = b[tmask == 0]
    return out


def resize_to(img, w, h):
    return cv2.resize(img, (int(w), int(h)), interpolation=cv2.INTER_AREA if img.shape[1] > w else cv2.INTER_CUBIC)


def unred(plate, rect, margin=18):
    """The red light a lit button spills on the tray and rims around it:
    red, saturated pixels near the button go to a dim neutral of the same
    brightness."""
    x, y, w, h = rect
    x0, y0, x1, y1 = max(0, x - margin), max(0, y - margin), x + w + margin, y + h + margin
    reg = plate[y0:y1, x0:x1].astype(np.float32)
    hsv = cv2.cvtColor(plate[y0:y1, x0:x1], cv2.COLOR_BGR2HSV).astype(np.float32)
    redness = (((hsv[..., 0] <= 10) | (hsv[..., 0] >= 160)).astype(np.float32)
               * np.clip((hsv[..., 1] - 50.0) / 80.0, 0.0, 1.0))
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    dx = np.maximum(0, np.maximum(x - xx, xx - (x + w)))
    dy = np.maximum(0, np.maximum(y - yy, yy - (y + h)))
    near = np.clip(1.0 - np.hypot(dx, dy) / margin, 0.0, 1.0)
    wgt = (redness * near)[..., None]
    lum = reg.mean(axis=2, keepdims=True)
    neutral = lum * 0.6 * np.array([1.0, 1.0, 1.04], np.float32)
    out = plate.copy()
    out[y0:y1, x0:x1] = np.clip(reg * (1 - wgt) + neutral * wgt, 0, 255).astype(np.uint8)
    return out


def button_group(img, plate, group, glow_margin=16):
    """Every button of a group dark on the plate, and a lit image of every
    button. The artwork has one lit button L; the others are dark.
      dark L:  a dark sibling's body (caption removed), fitted to L's body,
               with L's caption laid in the dark caption colour;
      lit b:   L's lit body (caption removed) fitted to b's body, the light
               L throws around itself, and b's caption in the lit colour
               with its halo.
    Returns the plate and {label: lit image (BGR, full size)}."""
    buttons = group["buttons"]
    li = group["litInArtwork"]
    di = group["copyFrom"]
    L = buttons[li]
    D = buttons[di]
    lx, ly, lw, lh = L["body"]
    dx_, dy_, dw, dh = D["body"]
    mL = max(5, int(round(min(lw, lh) * 0.14)))
    mD = max(5, int(round(min(dw, dh) * 0.14)))

    lit_body = img[ly:ly + lh, lx:lx + lw]
    dark_body = img[dy_:dy_ + dh, dx_:dx_ + dw]

    captions = {}
    colours = {}
    for b in buttons:
        x, y, w, h = b["body"]
        lit = b is L
        a = text_alpha_on(img[y:y + h, x:x + w], lit)
        mm = mL if lit else mD
        edge = np.zeros_like(a)
        edge[mm:h - mm, mm:w - mm] = 1.0
        a *= edge
        captions[b["label"]] = a
        sel = a > 0.8
        colours[b["label"]] = np.median(img[y:y + h, x:x + w][sel].astype(np.float32), axis=0) if sel.any() else None
    dark_ink = colours[D["label"]]
    lit_ink = colours[L["label"]]

    # the light L throws around itself: L's surroundings minus a dark sibling's
    g = glow_margin
    lit_ring = img[ly - g:ly + lh + g, lx - g:lx + lw + g].astype(np.float32)
    plate = plate.copy()

    # 1. L dark on the plate
    dark_blank = blank_face(dark_body, mD)
    fitted = resize_to(dark_blank, lw, lh)
    a = captions[L["label"]][..., None]
    fitted = fitted * (1 - a) + dark_ink[None, None, :] * a
    pasted = plate.copy()
    pasted[ly:ly + lh, lx:lx + lw] = np.clip(fitted, 0, 255).astype(np.uint8)
    mask = rect_mask(plate.shape, [(lx, ly, lw - 1, lh - 1)])
    plate = feather_composite(plate, pasted, mask, 1.0)
    plate = unred(plate, (lx, ly, lw, lh), margin=g)
    dark_ring = plate[ly - g:ly + lh + g, lx - g:lx + lw + g].astype(np.float32)
    halo = np.clip(lit_ring - dark_ring, 0, None)
    halo[g:g + lh, g:g + lw] = 0.0  # the body is replaced whole

    # 2. lit image of every button
    lit_blank = blank_face(lit_body, mL)
    lit_images = {}
    for b in buttons:
        x, y, w, h = b["body"]
        img_b = plate.astype(np.float32).copy()
        body = resize_to(lit_blank, w, h)
        a = captions[b["label"]]
        if b is not L:
            # a dark caption is thinner than a lit one: thicken a little
            a = np.clip(cv2.GaussianBlur(a, (0, 0), 0.6) * 1.6, 0.0, 1.0)
        halo_t = cv2.GaussianBlur(a, (0, 0), 2.2)
        red = np.array([60.0, 60.0, 230.0], np.float32)  # BGR
        body = body + red[None, None, :] * (halo_t * (0.55 if b is L else 0.9))[..., None]
        body = body * (1 - a[..., None]) + lit_ink[None, None, :] * a[..., None]
        img_b[y:y + h, x:x + w] = body
        hl = resize_to(halo, w + 2 * g, h + 2 * g)
        hl[g:g + h, g:g + w] = 0.0
        img_b[y - g:y + h + g, x - g:x + w + g] += hl
        lit_images[b["label"]] = np.clip(img_b, 0, 255).astype(np.uint8)
    return plate, lit_images


def sprite_from(lit_img, plate, rect):
    """The lit image where it differs from the dark plate, with an alpha that
    brings back exactly the lit look over the plate."""
    x, y, w, h = rect
    lit = lit_img[y:y + h, x:x + w].astype(np.float32)
    dark = plate[y:y + h, x:x + w].astype(np.float32)
    diff = np.abs(lit - dark).max(axis=2)
    alpha = np.clip((diff - 2.0) / 30.0, 0.0, 1.0)
    alpha = cv2.GaussianBlur(alpha, (0, 0), 0.7)
    edge = np.ones_like(alpha)
    for k in range(3):
        v = (k + 0.5) / 3.0
        edge[k, :] = np.minimum(edge[k, :], v)
        edge[-1 - k, :] = np.minimum(edge[-1 - k, :], v)
        edge[:, k] = np.minimum(edge[:, k], v)
        edge[:, -1 - k] = np.minimum(edge[:, -1 - k], v)
    alpha *= edge
    # colour such that plate*(1-a) + colour*a == lit
    a3 = np.maximum(alpha[..., None], 1e-3)
    colour = np.where(alpha[..., None] > 1e-3, (lit - dark * (1 - a3)) / a3, lit)
    return np.dstack([np.clip(colour, 0, 255).astype(np.uint8), np.clip(alpha * 255.0, 0, 255).astype(np.uint8)])


def lamp_off_round(img, plate, lamp):
    """STEREO LINK: the lit orange lens and its light on the black bezel go
    dark; the cream panel around it is left alone (it is warm too, so the
    lamp is limited to its round bezel)."""
    x, y, w, h = lamp["rect"]
    cx, cy = lamp["centre"]
    crop = img[y:y + h, x:x + w]
    yy, xx = np.mgrid[y:y + h, x:x + w].astype(np.float32)
    r = np.hypot(xx - cx, yy - cy)
    bezel = np.clip((lamp["bezelRadius"] + 1.0 - r) / 2.0, 0.0, 1.0)
    hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV).astype(np.float32)
    lit = np.clip((hsv[..., 1] - 90.0) / 60.0, 0.0, 1.0) * np.clip((hsv[..., 2] - 40.0) / 60.0, 0.0, 1.0)
    lit = np.maximum(lit, (hsv[..., 2] > 200).astype(np.float32))
    lit = cv2.GaussianBlur(lit, (0, 0), 1.0) * bezel
    # unlit: the same shapes, desaturated and dark like smoked glass
    dark_hsv = hsv.copy()
    dark_hsv[..., 1] *= 0.25
    dark_hsv[..., 2] = 14.0 + 0.22 * dark_hsv[..., 2]
    dark = cv2.cvtColor(np.clip(dark_hsv, 0, 255).astype(np.uint8), cv2.COLOR_HSV2BGR).astype(np.float32)
    a = lit[..., None]
    region = plate[y:y + h, x:x + w].astype(np.float32)
    plate = plate.copy()
    plate[y:y + h, x:x + w] = np.clip(region * (1 - a) + dark * a, 0, 255).astype(np.uint8)
    sprite = np.dstack([crop, np.clip(lit * 255.0, 0, 255).astype(np.uint8)])
    return plate, sprite


# ---------------------------------------------------------------- VU

def remove_needle(img, plate, vu):
    """The baked needle (dark line with a soft shadow and a thin tip over the
    scale): every pixel is taken from the same radius around the scale's
    centre a few degrees away, from the side of the 0 mark it lies on, so
    the arc, the red band and the face stay continuous; the face's slow
    shading is matched from the surroundings."""
    (x0, y0), (x1, y1) = vu["bakedNeedle"]
    (tx0, ty0), (tx1, ty1) = vu["bakedTip"]
    mask = np.zeros(plate.shape[:2], np.uint8)
    cv2.line(mask, (int(round(x0)), int(round(y0 + 6))), (int(round(x1)), int(round(y1))), 255, 13)
    cv2.line(mask, (int(round(tx0)), int(round(ty0 + 2))), (int(round(tx1 + 1.5)), int(round(ty1 - 7))), 255, 6)
    fx, fy, fw, fh = vu["face"]
    mask = cv2.bitwise_and(mask, rect_mask(plate.shape, [(fx, fy, fw, fh)]))
    px, py = vu["pivot"]
    zero = float(vu["scaleDegrees"]["0"])
    yy, xx = np.mgrid[0:plate.shape[0], 0:plate.shape[1]].astype(np.float32)
    ang = np.degrees(np.arctan2(xx - px, -(yy - py)))
    out = plate.copy()
    for turn, side in ((-3.2, ang < zero), (3.2, ang >= zero)):
        # the pixel at angle a takes the pixel at a + turn (image rotates by -turn)
        m = cv2.getRotationMatrix2D((float(px), float(py)), turn, 1.0)
        rotated = cv2.warpAffine(plate, m, (plate.shape[1], plate.shape[0]), flags=cv2.INTER_LINEAR,
                                 borderMode=cv2.BORDER_REFLECT)
        sel = (mask > 0) & side
        out[sel] = rotated[sel]
    filled = match_illumination(plate, out, mask, sigma=4.0)
    plate = feather_composite(plate, filled, mask, 1.0)
    return lift_needle_shadow(plate, vu, (x0, y0), (x1, y1))


def lift_needle_shadow(plate, vu, a, b):
    """The baked needle cast a soft shadow on the face to its right. Across
    the needle line, the face's level is measured as a function of the
    distance from the line; the dip against the straight trend of both
    sides is divided out."""
    (x0, y0), (x1, y1) = a, b
    d = np.array([x1 - x0, y1 - y0], np.float32)
    L = float(np.hypot(*d))
    u = d / L
    nrm = np.array([-u[1], u[0]], np.float32)  # points to the needle's right
    if nrm[0] < 0:
        nrm = -nrm
    fx, fy, fw, fh = vu["face"]
    yy, xx = np.mgrid[fy:fy + fh, fx:fx + fw].astype(np.float32)
    along = (xx - x0) * u[0] + (yy - y0) * u[1]
    across = (xx - x0) * nrm[0] + (yy - y0) * nrm[1]
    reg = plate[fy:fy + fh, fx:fx + fw].astype(np.float32)
    smooth = cv2.GaussianBlur(reg, (0, 0), 2.0)
    span = (along > 6) & (along < L - 4)
    dists = np.arange(-40, 41)
    prof = np.zeros((len(dists), 3), np.float32)
    for i, k in enumerate(dists):
        sel = span & (np.abs(across - k) < 0.5)
        prof[i] = np.median(smooth[sel], axis=0)
    # the face is lit less on the needle's right: a step at the line that
    # fades out over ~40 px. Lift the right side by the step, fading likewise.
    left = np.median(prof[(dists >= -9) & (dists <= -3)], axis=0)
    right = np.median(prof[(dists >= 3) & (dists <= 9)], axis=0)
    step = np.clip(left / np.maximum(right, 1.0), 1.0, 1.3)
    fade = np.clip(1.0 - (dists - 2.0) / 38.0, 0.0, 1.0)
    fade[dists < -2] = 0.0
    fade = np.minimum(fade, np.clip((dists + 2.0) / 4.0, 0.0, 1.0))
    ratio = 1.0 + (step[None, :] - 1.0) * fade[:, None]
    gain = np.ones_like(reg)
    k = np.clip(np.round(across).astype(int) + 40, 0, len(dists) - 1)
    inband = span | ((along >= -10) & (along <= L + 30))
    for c in range(3):
        gc = ratio[k, c]
        gain[..., c] = np.where(inband, gc, 1.0)
    out = plate.copy()
    out[fy:fy + fh, fx:fx + fw] = np.clip(reg * gain, 0, 255).astype(np.uint8)
    return out


# ---------------------------------------------------------------- spectrum

def clear_screen(img, plate, sp):
    """The screen glass without curve, fill or grid: each row's glass level
    from the clean, dark part of that row; fine grain from the bezel-side
    glass. The frequency labels under the screen are erased too."""
    x, y, w, h = sp["screen"]
    reg = plate[y:y + h, x:x + w].astype(np.float32)
    lum = reg.mean(axis=2)
    levels = np.zeros((h, 3), np.float32)
    for j in range(h):
        l = lum[j]
        keep = l <= np.percentile(l, 20) + 1.0
        levels[j] = np.median(reg[j][keep], axis=0)
    levels = cv2.GaussianBlur(levels[:, None, :], (0, 0), 7.0)[:, 0, :]  # grid rows must not print
    # slow left-right vignette, from the darkest pixels of each column block
    cols = np.zeros((w,), np.float32)
    for i in range(w):
        l = lum[:, i]
        cols[i] = np.percentile(l, 15)
    cols = cv2.GaussianBlur(cols[None, :], (0, 0), 25.0)[0]
    cols = cols / max(1e-3, float(np.median(cols)))
    rng = np.random.default_rng(11)
    grain = cv2.GaussianBlur(rng.normal(0.0, 2.2, (h, w, 3)).astype(np.float32), (0, 0), 0.55)
    specks = (rng.random((h, w)) > 0.996).astype(np.float32)
    specks = cv2.GaussianBlur(specks, (0, 0), 0.6)[..., None] * 25.0
    out = levels[:, None, :] * np.clip(cols, 0.7, 1.3)[None, :, None] + grain + specks
    res = plate.copy()
    res[y:y + h, x:x + w] = np.clip(out, 0, 255).astype(np.uint8)
    mask = rect_mask(plate.shape, [(x + 1, y + 1, w - 3, h - 3)])
    plate = feather_composite(plate, res, mask, 0.7)

    lx, ly, lw, lh = sp["labelStrip"]
    labels = plate[ly:ly + lh, lx:lx + lw]
    g = cv2.cvtColor(labels, cv2.COLOR_BGR2GRAY).astype(np.float32)
    text = (g - cv2.GaussianBlur(g, (0, 0), 5.0)) > 14
    text = cv2.dilate(text.astype(np.uint8) * 255, np.ones((5, 5), np.uint8), iterations=1)
    full = np.zeros(plate.shape[:2], np.uint8)
    full[ly:ly + lh, lx:lx + lw] = text
    # panel texture from a clean gap between two labels, brightness from around them
    gx, gy, gw, gh = sp["labelGrain"]
    return inpaint_with_grain(plate, full, (gx, gy, gw, gh), radius=4)


# ---------------------------------------------------------------- main

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
        for a in c["artPointers"]:
            plate = remove_pointer_at(img, plate, c, a, yy, xx)

    # scale numbers: texture from beside, never from a dot, a knob or other print
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    printed = np.abs(g - cv2.GaussianBlur(g, (0, 0), 6.0)) > 22
    printed = cv2.dilate(printed.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    for c in knobs:
        cx, cy = c["centre"]
        printed |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["rimRadius"] + 4) ** 2
        if "readout" in c:
            x, y, w, h = c["readout"]
            printed[y - 2:y + h + 2, x - 2:x + w + 2] = True
    for c in knobs:
        for rect in c.get("erase", []):
            r = pad(rect, 2)
            avoid = printed.copy()
            avoid[r[1]:r[1] + r[3], r[0]:r[0] + r[2]] = False
            plate = erase_label(plate, r, c["scaleCentre"], avoid)
        if "readout" in c:
            plate = empty_readout(plate, c["readout"])

    # buttons: all dark on the plate, a lit sprite for each
    sprites = {}
    for gid in ("character", "scExternal", "meterMode"):
        grp = controls[gid]
        plate, lit_images = button_group(img, plate, grp)
        for b in grp["buttons"]:
            x, y, w, h = b["body"]
            rect = pad([x, y, w, h], 16)
            sprites[gid + "_" + b["label"].replace("/", "").lower()] = (lit_images[b["label"]], rect)

    # STEREO LINK lamp and DUAL MONO
    link = controls["stereoLink"]
    plate, lamp_sprite = lamp_off_round(img, plate, link["lamp"])
    # DUAL MONO lit: the METER group's lit light fitted to its body
    dm = {"buttons": [{"label": "DUAL MONO", "body": link["dualMono"]["body"]},
                      {"label": "GR", "body": controls["meterMode"]["buttons"][0]["body"]},
                      {"label": "VU", "body": controls["meterMode"]["buttons"][1]["body"]}],
          "litInArtwork": 1, "copyFrom": 2}
    # the GR button is dark on the plate now; take its lit look from the artwork
    lit_img_src = img.copy()
    _, lit_images = button_group(lit_img_src, plate, dm)
    x, y, w, h = link["dualMono"]["body"]
    sprites["link_dualmono"] = (lit_images["DUAL MONO"], pad([x, y, w, h], 16))

    for vid in ("vuL", "vuR"):
        plate = remove_needle(img, plate, controls[vid])

    plate = clear_screen(img, plate, controls["spectrum"])

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    rects = {}
    for name, (lit_img, rect) in sprites.items():
        sp = sprite_from(lit_img, plate, rect)
        cv2.imwrite(os.path.join(OUT, name + ".png"), sp, [cv2.IMWRITE_PNG_COMPRESSION, 9])
        rects[name] = rect
    cv2.imwrite(os.path.join(OUT, "link_lamp.png"), lamp_sprite, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    rects["link_lamp"] = link["lamp"]["rect"]
    print("sprites", json.dumps(rects))
    print("wrote", OUT)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = plate.copy()
        for c in ui["controls"]:
            boxes = [c["hitbox"]] if "hitbox" in c else [b["hitbox"] for b in c.get("buttons", [])]
            if c["type"] == "LINK_SWITCH":
                boxes = [c["lamp"]["hitbox"], c["dualMono"]["hitbox"]]
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
                if "readout" in c:
                    x, y, w, h = c["readout"]
                    cv2.rectangle(dbg, (x, y), (x + w, y + h), (0, 200, 255), 1)
            for b in c.get("buttons", []):
                x, y, w, h = b["body"]
                cv2.rectangle(dbg, (x, y), (x + w, y + h), (255, 128, 0), 1)
            if c["type"] == "VU":
                px, py = c["pivot"]
                for k, t in c["scaleDegrees"].items():
                    t = np.radians(t)
                    r0, r1 = c["arcRadius"] - 25, c["arcRadius"] + 8
                    cv2.line(dbg, (int(px + r0 * np.sin(t)), int(py - r0 * np.cos(t))),
                             (int(px + r1 * np.sin(t)), int(py - r1 * np.cos(t))), (0, 0, 255), 1)
                x, y, w, h = c["face"]
                cv2.rectangle(dbg, (x, y), (x + w, y + h), (255, 0, 255), 1)
            if c["type"] == "SPECTRUM":
                x, y, w, h = c["screen"]
                cv2.rectangle(dbg, (x, y), (x + w, y + h), (255, 0, 255), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
