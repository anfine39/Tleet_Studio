#!/usr/bin/env python3
"""Skin preparation for TLEET // ECHO.

Turns the approved mockup (assets/reference/ECHO.png) into the runtime assets
  assets/processed/ECHO/plate.png       faceplate
  assets/processed/ECHO/<sprite>.png    lit buttons, lamps and switch states (RGBA)
  assets/processed/ECHO/sprites.json    where each sprite goes (reference pixels)
using the geometry in plugins/ECHO/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - all eighteen knob pointers and their glow;
  - every number printed on a knob scale and every scale dot: the editor
    draws the dots evenly over the real travel and the numbers at the angles
    where the parameter really has them (the MODE ring was printed
    "1-2  3  1-2", REPEAT RATE "200  200");
  - the MODE lamps: four were lit at once in the artwork; the plate shows
    every position dark, the editor lights the selected one. The ninth
    printed position (ECHO+REVERB) has no mode behind it and is removed
    with its caption (REVERB VOLUME adds the spring in every mode);
  - the lit buttons (NOTE DIVISION, FEEL, CHARACTER): the plate shows every
    button dark, the editor draws the lit button of the real state;
  - the TIME MODE / HOST BPM slide switches and the FILTER MODE switch: a
    sprite per position;
  - the TAP lamp, the BPM digits, the lit part of the TAPE CONDITION bar;
  - both VU needles. The OUT meter's scale was printed "-2" where the IN
    meter (and every VU) has "-3": the IN meter's "-3" is set there.

Usage: python3 tools/skinprep/prep_echo.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import clean_patch, feather_composite, inpaint_with_grain, lamp_off, rect_mask
from prep_shape import pointer_out
from prep_veil import empty_readout, remove_needle, resize_to, sprite_from

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "ECHO.png")
OUT = os.path.join(ROOT, "assets", "processed", "ECHO")
UI_MAP = os.path.join(ROOT, "plugins", "ECHO", "UI_MAP.json")


def pad(rect, p):
    x, y, w, h = rect
    return [x - p, y - p, w + 2 * p, h + 2 * p]


def split(rect, most=22):
    """A wide caption in pieces of at most `most` pixels: a clean patch
    as wide as a whole word is rarely found on the stained panel."""
    x, y, w, h = rect
    n = int(np.ceil(w / most))
    edges = np.linspace(x, x + w, n + 1).round().astype(int)
    return [[int(edges[i]), y, int(edges[i + 1] - edges[i]), h] for i in range(n)]


def erase_captions(plate, rects, avoid, sigma=4.0):
    """Printed captions: the smooth brightness inpainted from around all of
    them at once (so one caption is never filled from the light of its
    neighbour), the film grain from the quietest patches of panel nearby
    (one per piece of a wide caption, nothing printed in them)."""
    mask = rect_mask(plate.shape, rects)
    grow = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=3)
    low = cv2.GaussianBlur(cv2.inpaint(plate, grow, 7, cv2.INPAINT_TELEA).astype(np.float32), (0, 0), sigma)
    f = plate.astype(np.float32)
    detail = f - cv2.GaussianBlur(f, (0, 0), sigma)
    grain = np.zeros_like(f)
    m = 3
    pieces = [p for x, y, w, h in rects for p in split([x - m, y - m, w + 2 * m, h + 2 * m])]
    for px_, py_, pw, ph in pieces:
        size = max(pw, ph)
        corner = (px_ + pw / 2 - size / 2, py_ + ph / 2 - size / 2)
        try:
            sx, sy, _, _ = clean_patch(plate, corner, avoid | (grow > 0), size=size)
        except TypeError:
            sx, sy, _, _ = clean_patch(plate, corner, avoid | (grow > 0), size=size, search=200)
        grain[py_:py_ + ph, px_:px_ + pw] = detail[sy:sy + ph, sx:sx + pw]
    out = f.copy()
    sel = grow > 0
    out[sel] = (low + grain)[sel]
    filled = np.clip(out, 0, 255).astype(np.uint8)
    return feather_composite(plate, filled, cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1), 1.2)


def polar(c, r, deg):
    t = np.radians(deg)
    return (c[0] + r * np.sin(t), c[1] - r * np.cos(t))


def erase_dot(plate, centre, avoid, radius=11):
    """A printed dot or lamp and its halo: a clean patch of the same panel,
    brought to the brightness around it (skinlib.erase_dot, looking further
    for a clean patch when there is none close by)."""
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
    size = 2 * radius + 4
    try:
        x, y, w, h = clean_patch(plate, centre, avoid | (mask > 0), size=size)
    except TypeError:
        x, y, w, h = clean_patch(plate, centre, avoid | (mask > 0), size=size, search=200)
    patch = plate[y:y + h, x:x + w].astype(np.float32)
    patch = patch * (level / np.maximum(np.median(patch.reshape(-1, 3), axis=0), 1.0))
    pasted = plate.copy()
    half = w // 2
    pasted[iy - half:iy - half + h, ix - half:ix - half + w] = np.clip(patch, 0, 255).astype(np.uint8)
    return feather_composite(plate, pasted, mask, 2.0)


# ---------------------------------------------------------------- amber light

def deglow(plate, rect, margin, inside=True):
    """The amber light a lit lamp or button throws on the dark tray and the
    rims around it. The tray is near neutral and the light is amber (blue
    nearly untouched), so a lit pixel goes back to the tray's own warm grey
    at the level its blue channel gives. Neutral highlights (bevels, print)
    keep their blue, so they stay."""
    x, y, w, h = rect
    x0, y0 = max(0, x - margin), max(0, y - margin)
    x1, y1 = min(plate.shape[1], x + w + margin), min(plate.shape[0], y + h + margin)
    reg = plate[y0:y1, x0:x1].astype(np.float32)
    hsv = cv2.cvtColor(plate[y0:y1, x0:x1], cv2.COLOR_BGR2HSV).astype(np.float32)
    amber = ((hsv[..., 0] >= 4) & (hsv[..., 0] <= 32)).astype(np.float32)
    wgt = amber * np.clip((hsv[..., 1] - 80.0) / 70.0, 0.0, 1.0)
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    dx = np.maximum(0, np.maximum(x - xx, xx - (x + w)))
    dy = np.maximum(0, np.maximum(y - yy, yy - (y + h)))
    near = np.clip(1.0 - np.hypot(dx, dy) / max(1, margin), 0.0, 1.0)
    if not inside:
        near[(dx == 0) & (dy == 0)] = 0.0
    wgt = (wgt * near)[..., None]
    b = reg[..., 0:1]
    tray = b * np.array([1.0, 1.13, 1.08], np.float32)
    out = plate.copy()
    out[y0:y1, x0:x1] = np.clip(reg * (1 - wgt) + tray * wgt, 0, 255).astype(np.uint8)
    return out


# ---------------------------------------------------------------- MODE lamps

def mode_lamps(img, plate, c):
    """Every MODE position dark; the lit lamp as a sprite (drawn by the
    editor at the selected position)."""
    lp = c["lamps"]
    cx, cy = c["centre"]
    # dark lamp of the artwork, laid on every lit position
    dx, dy = lp["darkFrom"]
    k = 9
    dark = img[int(round(dy)) - k:int(round(dy)) + k + 1, int(round(dx)) - k:int(round(dx)) + k + 1]
    lit_positions = [polar((cx, cy), lp["radius"], a) for a in lp["litAngles"]]
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    avoid = np.abs(g - cv2.GaussianBlur(g, (0, 0), 6.0)) > 22
    avoid = cv2.dilate(avoid.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    avoid |= np.hypot(xx - cx, yy - cy) <= c["rimRadius"] + 4
    n, lab, st, _ = cv2.connectedComponentsWithStats(avoid.astype(np.uint8))
    avoid = (st[:, cv2.CC_STAT_AREA] >= 90)[lab] & (lab > 0)
    for p in lit_positions:
        # the lamp's own light first: on the ring it reaches past the lamp
        plate = deglow(plate, [int(p[0]) - 6, int(p[1]) - 6, 12, 12], 26)
    # the printed lamps sat up to 2.4 degrees off a 30-degree grid: those
    # off it are moved onto it (the lit ones go dark at the same time)
    for p in [tuple(q) for q in lp["moveFrom"]] + [tuple(lp["eraseLamp"])]:
        a = avoid.copy()
        a[int(p[1]) - 16:int(p[1]) + 17, int(p[0]) - 16:int(p[0]) + 17] = False
        plate = erase_dot(plate, p, a, radius=12)
    for p in lp["eraseDots"]:
        a = avoid.copy()
        a[int(p[1]) - 9:int(p[1]) + 10, int(p[0]) - 9:int(p[0]) + 10] = False
        plate = erase_dot(plate, p, a, radius=7)
    # a dark lamp on each moved position: the artwork's dark lamp with its
    # own surroundings
    for p in lp["moveTo"]:
        x, y = int(round(p[0])), int(round(p[1]))
        pasted = plate.copy()
        pasted[y - k:y + k + 1, x - k:x + k + 1] = dark
        m = np.zeros(plate.shape[:2], np.uint8)
        cv2.circle(m, (x, y), 8, 255, -1)
        plate = feather_composite(plate, pasted, m, 2.0)
    return plate


def mode_lamp_sprite(img, plate, c):
    """The lit lamp at `spriteFrom` over the dark lamp the plate now has
    there, limited to a disc so nothing of the ring around comes along."""
    lp = c["lamps"]
    sx, sy = lp["spriteFrom"]
    s = lp["spriteSize"]
    x, y = int(round(sx - s / 2)), int(round(sy - s / 2))
    sp = sprite_from(img, plate, [x, y, s, s])
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    r = np.hypot(xx + x - sx, yy + y - sy)
    disc = np.clip((11.0 - r) / 3.0, 0.0, 1.0)
    sp[..., 3] = (sp[..., 3].astype(np.float32) * disc).astype(np.uint8)
    return sp, [x, y, s, s], [sx - x, sy - y]


# ---------------------------------------------------------------- buttons

def caption_alpha(face, light_on_dark, threshold=28.0):
    """Soft alpha of a button caption, light strokes on a dark face or dark
    strokes on a lit face."""
    lum = cv2.cvtColor(face, cv2.COLOR_BGR2GRAY).astype(np.float32)
    bg = cv2.cvtColor(cv2.medianBlur(face, 21), cv2.COLOR_BGR2GRAY).astype(np.float32)
    if light_on_dark:
        top = float(np.percentile(lum, 99.5))
        a = (lum - bg - threshold) / max(1.0, top - float(np.median(bg)) - threshold)
    else:
        low = float(np.percentile(lum, 0.5))
        a = (bg - lum - threshold) / max(1.0, float(np.median(bg)) - low - threshold)
    return np.clip(a, 0.0, 1.0)


def blank(body, margin, light_on_dark):
    """A button body with its caption removed: inside the face, each row
    takes the face level of that row."""
    b = body.astype(np.float32)
    h, w = b.shape[:2]
    m = margin
    out = b.copy()
    face = b[m:h - m, m:w - m]
    lum = face.mean(axis=2)
    for j in range(face.shape[0]):
        l = lum[j]
        keep = (l <= np.percentile(l, 50) + 4.0) if light_on_dark else (l >= np.percentile(l, 50) - 4.0)
        out[m + j, m:w - m] = np.median(face[j][keep], axis=0)
    out[m:h - m, m:w - m] = cv2.GaussianBlur(out[m:h - m, m:w - m], (0, 0), 0.8)
    return out


def button_group(img, plate, group, glow_margin=14):
    """Every button of a group dark on the plate, and a lit image of every
    button. The artwork has one lit button L; the others are dark.
      dark L:  a dark sibling's body (caption removed), fitted to L's body,
               with L's caption in the dark caption colour; L's light on the
               tray around it removed;
      lit b:   L's lit body (caption removed) fitted to b's body, the light L
               throws around itself, and b's caption in L's lit ink (with
               L's glow around the strokes when the lit caption glows).
    Returns the plate and {label: lit image (BGR, full size)}."""
    buttons = group["buttons"]
    L = buttons[group["litInArtwork"]]
    D = buttons[group["copyFrom"]]
    lx, ly, lw, lh = L["body"]
    dx_, dy_, dw, dh = D["body"]
    mL = max(4, int(round(min(lw, lh) * 0.14)))
    mD = max(4, int(round(min(dw, dh) * 0.14)))
    lit_body = img[ly:ly + lh, lx:lx + lw]
    dark_body = img[dy_:dy_ + dh, dx_:dx_ + dw]

    # the lit face: bright with dark print (NOTE DIVISION) or dark with
    # glowing print (FEEL, CHARACTER)
    inner = lit_body[mL:lh - mL, mL:lw - mL]
    lit_face_bright = float(np.median(cv2.cvtColor(inner, cv2.COLOR_BGR2GRAY))) > 110

    captions, inks = {}, {}
    for b in buttons:
        x, y, w, h = b["body"]
        lit = b is L
        light = not (lit and lit_face_bright)
        a = caption_alpha(img[y:y + h, x:x + w], light)
        mm = mL if lit else mD
        edge = np.zeros_like(a)
        edge[mm:h - mm, mm:w - mm] = 1.0
        a *= edge
        captions[b["label"]] = a
    if not lit_face_bright:
        # a glowing lit caption: its strokes are the white-hot core inside an
        # amber glow. Keep the whitest pixels, as many as the dark captions
        # have ink per letter, so the dark copy has the same weight.
        x, y, w, h = L["body"]
        hsv = cv2.cvtColor(img[y:y + h, x:x + w], cv2.COLOR_BGR2HSV).astype(np.float32)
        white = (255.0 - hsv[..., 1]) * np.clip((hsv[..., 2] - 120.0) / 60.0, 0.0, 1.0)
        edge = np.zeros_like(white)
        edge[mL:h - mL, mL:w - mL] = 1.0
        white *= edge
        per_letter = np.mean([(captions[b["label"]] > 0.5).sum() / max(1, len(b["label"].replace(" ", "")))
                              for b in buttons if b is not L])
        # the lit font is drawn a little lighter than the dark one
        want = 1.35 * per_letter * len(L["label"].replace(" ", ""))
        level = np.sort(white.ravel())[::-1][int(want)]
        captions[L["label"]] = np.clip((white - level) / 40.0 + 0.5, 0.0, 1.0) * (edge > 0)
    for b in buttons:
        x, y, w, h = b["body"]
        sel = captions[b["label"]] > 0.8
        inks[b["label"]] = np.median(img[y:y + h, x:x + w][sel].astype(np.float32), axis=0)
    dark_ink = inks[D["label"]]
    lit_ink = inks[L["label"]]

    g = glow_margin
    lit_ring = img[ly - g:ly + lh + g, lx - g:lx + lw + g].astype(np.float32)
    plate = plate.copy()

    # 1. L dark on the plate
    fitted = resize_to(blank(dark_body, mD, True), lw, lh)
    a = captions[L["label"]]
    if lit_face_bright:
        # dark print on a lit face reads fainter in the alpha: full strength
        a = np.clip(a * 1.3, 0.0, 1.0)
    a = a[..., None]
    fitted = fitted * (1 - a) + dark_ink[None, None, :] * a
    pasted = plate.copy()
    pasted[ly:ly + lh, lx:lx + lw] = np.clip(fitted, 0, 255).astype(np.uint8)
    plate = feather_composite(plate, pasted, rect_mask(plate.shape, [(lx, ly, lw - 1, lh - 1)]), 0.8)
    plate = deglow(plate, (lx, ly, lw, lh), g)
    dark_ring = plate[ly - g:ly + lh + g, lx - g:lx + lw + g].astype(np.float32)
    halo = np.clip(lit_ring - dark_ring, 0, None)
    halo[g:g + lh, g:g + lw] = 0.0

    # 2. lit image of every button
    if lit_face_bright:
        lit_blank = blank(lit_body, mL, False)
    else:
        # a dark face lit by its glowing caption: the caption and its glow
        # inpainted from the face around them
        hole = (cv2.GaussianBlur(captions[L["label"]], (0, 0), 1.0) > 0.05).astype(np.uint8) * 255
        hole = cv2.dilate(hole, np.ones((3, 3), np.uint8), iterations=3)
        lit_blank = cv2.inpaint(lit_body, hole, 6, cv2.INPAINT_TELEA).astype(np.float32)
    lit_images = {}
    for b in buttons:
        x, y, w, h = b["body"]
        img_b = plate.astype(np.float32).copy()
        if b is L:
            img_b[ly - g:ly + lh + g, lx - g:lx + lw + g] = lit_ring
            lit_images[b["label"]] = np.clip(img_b, 0, 255).astype(np.uint8)
            continue
        body = resize_to(lit_blank, w, h)
        a = captions[b["label"]]
        if not lit_face_bright:
            # glowing print: the strokes are wider, with an amber glow
            a = np.clip(cv2.GaussianBlur(a, (0, 0), 0.6) * 1.5, 0.0, 1.0)
            glow = cv2.GaussianBlur(a, (0, 0), 2.5)
            body = body + np.array([10.0, 80.0, 220.0], np.float32)[None, None, :] * (glow * 0.9)[..., None]
        body = body * (1 - a[..., None]) + lit_ink[None, None, :] * a[..., None]
        img_b[y:y + h, x:x + w] = body
        hl = resize_to(halo, w + 2 * g, h + 2 * g)
        hl[g:g + h, g:g + w] = 0.0
        img_b[y - g:y + h + g, x - g:x + w + g] += hl
        lit_images[b["label"]] = np.clip(img_b, 0, 255).astype(np.uint8)
    return plate, lit_images


# ---------------------------------------------------------------- switches

def edge_alpha(h, w, feather):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy))
    return np.clip((d + 0.5) / feather, 0.0, 1.0)


def slide_switch(img, plate, c):
    """Both positions of a slide switch as opaque sprites: the artwork's
    position, and its mirror image about the housing's vertical axis (the
    lever and its lit window move to the other side). The plate keeps the
    housing; its light on the panel outside is removed."""
    x, y, w, h = c["housing"]
    crop = img[y:y + h, x:x + w]
    a = (edge_alpha(h, w, 4.0) * 255).astype(np.uint8)
    art = np.dstack([crop, a])
    other = np.dstack([crop[:, ::-1], a])
    states = [None, None]
    states[c["artState"]] = art
    states[1 - c["artState"]] = other
    plate = deglow(plate, [x + 4, y + 4, w - 8, h - 8], 12, inside=False)
    return plate, states


def vertical_switch(img, plate, c):
    """FILTER MODE: three cells, the artwork lit in the middle. The plate
    shows every cell dark (the middle one from the dark top cell); the lit
    cell is a sprite moved to the selected cell."""
    sx, sy, sw, sh = c["darkCellFrom"]
    lx, ly, lw, lh = c["litCell"]
    pitch = c["cellPitch"]
    dy = int(round(pitch))
    src = plate[sy:sy + sh, sx:sx + sw].copy()
    pasted = plate.copy()
    pasted[sy + dy:sy + dy + sh, sx:sx + sw] = src
    plate = feather_composite(plate, pasted, rect_mask(plate.shape, [(sx + 1, sy + dy + 1, sw - 3, sh - 3)]), 1.0)
    hx, hy, hw, hh = c["housing"]
    plate = deglow(plate, [hx, hy, hw, hh], 10)
    sp = sprite_from(img, plate, [lx - 4, ly - 4, lw + 8, lh + 8])
    return plate, sp, [lx - 4, ly - 4, lw + 8, lh + 8]


# ---------------------------------------------------------------- tape bar

def tape_bar(img, plate, c):
    """The bar unlit over its whole length: the unlit segments on the right
    copied over the lit ones (same segment pitch), the light around removed."""
    sx, sy, sw, sh = c["copySrc"]
    dx, dy = c["copyDst"]
    pasted = plate.copy()
    pasted[dy:dy + sh, dx:dx + sw] = plate[sy:sy + sh, sx:sx + sw]
    m = rect_mask(plate.shape, [(dx, dy + 3, sw - 1, sh - 6)])
    plate = feather_composite(plate, pasted, m, 0.8)
    bx, by, bw, bh = c["bar"]
    return deglow(plate, [bx, by, 130, bh], 18)


# ---------------------------------------------------------------- VU

def fix_vu_label(img, plate, c):
    """OUT meter: the misprinted "-2" out, the IN meter's "-3" in, centred
    where the "-2" was."""
    fl = c["fixLabel"]

    def ink(src, rect, threshold):
        x, y, w, h = rect
        lum = cv2.cvtColor(src[y:y + h, x:x + w], cv2.COLOR_BGR2GRAY).astype(np.float32)
        bg = cv2.cvtColor(cv2.medianBlur(src[y - 6:y + h + 6, x - 6:x + w + 6], 11),
                          cv2.COLOR_BGR2GRAY).astype(np.float32)[6:-6, 6:-6]
        return bg - lum, (bg - lum) > threshold

    x, y, w, h = fl["erase"]
    _, dark = ink(plate, fl["erase"], 12)
    ys, xs = np.nonzero(dark)
    old_centre = (x + (xs.min() + xs.max()) / 2.0, y + (ys.min() + ys.max()) / 2.0)
    mask = np.zeros(plate.shape[:2], np.uint8)
    mask[y:y + h, x:x + w] = cv2.dilate(dark.astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=2)
    # grain from the quietest patch of the face nearby (no scale, no print)
    g = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    busy = cv2.dilate((np.abs(g - cv2.GaussianBlur(g, (0, 0), 3.0)) > 24).astype(np.uint8), np.ones((3, 3), np.uint8)) > 0
    grain = clean_patch(plate, (x, y), busy | (mask > 0), size=12, search=60)
    plate = inpaint_with_grain(plate, mask, grain, radius=4, sigma=1.0)

    gx, gy, gw, gh = fl["glyphFrom"]
    depth, sel = ink(img, fl["glyphFrom"], 12)
    a = np.clip((depth - 10.0) / 70.0, 0.0, 1.0)
    ys, xs = np.nonzero(sel)
    glyph_centre = ((xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0)
    colour = np.median(img[gy:gy + gh, gx:gx + gw][a > 0.8].astype(np.float32), axis=0)
    # sub-pixel placement of the glyph's alpha
    tx, ty = int(np.floor(old_centre[0] - glyph_centre[0])), int(np.floor(old_centre[1] - glyph_centre[1]))
    fx, fy = old_centre[0] - glyph_centre[0] - tx, old_centre[1] - glyph_centre[1] - ty
    a = np.pad(a, 2)
    a = cv2.warpAffine(a, np.float32([[1, 0, fx], [0, 1, fy]]), (a.shape[1], a.shape[0]), flags=cv2.INTER_LINEAR)
    tx, ty = tx - 2, ty - 2
    hh, ww = a.shape
    reg = plate[ty:ty + hh, tx:tx + ww].astype(np.float32)
    out = plate.copy()
    out[ty:ty + hh, tx:tx + ww] = np.clip(reg * (1 - a[..., None]) + colour[None, None, :] * a[..., None], 0, 255).astype(np.uint8)
    return out


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

    knobs = [c for c in ui["controls"] if c["type"].startswith("ROTARY")]
    for c in knobs:
        plate = pointer_out(img, plate, c, yy, xx)

    # everything printed (text, dots, knobs) that a fill must not copy from;
    # grain and specks alone would block every patch, so only print bigger
    # than the grain counts
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    printed = np.abs(g - cv2.GaussianBlur(g, (0, 0), 6.0)) > 22
    printed = cv2.dilate(printed.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    for c in knobs:
        cx, cy = c["centre"]
        printed |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= (c["rimRadius"] + 4) ** 2
    n, lab, st, _ = cv2.connectedComponentsWithStats(printed.astype(np.uint8))
    printed_bold = (st[:, cv2.CC_STAT_AREA] >= 90)[lab] & (lab > 0)

    # MODE lamps first: their light reaches the captions around the ring
    mode = controls["mode"]
    plate = mode_lamps(img, plate, mode)

    for c in knobs:
        for p in c.get("eraseDots", []):
            avoid = printed_bold.copy()
            avoid[int(p[1]) - 8:int(p[1]) + 9, int(p[0]) - 8:int(p[0]) + 9] = False
            plate = erase_dot(plate, p, avoid, radius=7)
        if c.get("erase"):
            plate = erase_captions(plate, [pad(r, 4) for r in c["erase"]], printed_bold)


    sprites = {}
    for gid in ("noteDivision", "feel", "character"):
        grp = controls[gid]
        plate, lit_images = button_group(img, plate, grp)
        for b in grp["buttons"]:
            name = gid + "_" + b["label"].replace("/", "").lower()
            sprites[name] = ("lit", lit_images[b["label"]], pad(b["body"], 14))

    for sid in ("timeMode", "bpmSource"):
        plate, states = slide_switch(img, plate, controls[sid])
        for i, s in enumerate(states):
            sprites[sid + "_" + str(i)] = ("rgba", s, controls[sid]["housing"])

    plate, fm, fm_rect = vertical_switch(img, plate, controls["filterMode"])
    sprites["filterMode_lit"] = ("rgba", fm, fm_rect)

    tap = controls["tap"]
    plate, tap_sprite = lamp_off(img, plate, tap)
    sprites["tap_lamp"] = ("rgba", tap_sprite, tap["lamp"]["rect"])

    plate = empty_readout(plate, controls["bpmDisplay"]["glass"])
    plate = tape_bar(img, plate, controls["tapeCondition"])

    for vid in ("vuIn", "vuOut"):
        plate = remove_needle(img, plate, controls[vid])
    plate = fix_vu_label(img, plate, controls["vuOut"])

    # the lamp sprite needs the finished plate under it
    ml, ml_rect, ml_centre = mode_lamp_sprite(img, plate, mode)
    sprites["mode_lamp"] = ("rgba", ml, ml_rect)

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    rects = {}
    for name, (kind, im_, rect) in sprites.items():
        sp = sprite_from(im_, plate, rect) if kind == "lit" else im_
        cv2.imwrite(os.path.join(OUT, name + ".png"), sp, [cv2.IMWRITE_PNG_COMPRESSION, 9])
        rects[name] = [int(v) for v in rect]
    # where each sprite goes (reference pixels); the MODE lamp also gives
    # its centre inside the sprite, to centre it on a position
    meta = {"sprites": rects, "modeLampCentre": [round(float(v), 2) for v in ml_centre]}
    with open(os.path.join(OUT, "sprites.json"), "w") as f:
        json.dump(meta, f, indent=1)
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
            if c["type"].startswith("ROTARY"):
                cx, cy = c["centre"]
                cv2.drawMarker(dbg, (int(round(cx)), int(round(cy))), (0, 0, 255), cv2.MARKER_CROSS, 10, 1)
                p = c["pointer"]
                for deg in c["travelDegrees"]:
                    a = np.radians(deg)
                    cv2.line(dbg, (int(cx + np.sin(a) * p["inner"]), int(cy - np.cos(a) * p["inner"])),
                             (int(cx + np.sin(a) * p["outer"]), int(cy - np.cos(a) * p["outer"])), (255, 0, 255), 1)
                cv2.circle(dbg, (int(round(cx)), int(round(cy))), int(round(c["dotRadius"])), (255, 255, 0), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
