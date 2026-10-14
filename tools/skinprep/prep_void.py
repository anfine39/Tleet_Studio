#!/usr/bin/env python3
"""Skin preparation for TLEET // VOID.

Turns the approved mockup (assets/reference/VOID.png) into the runtime assets
  assets/processed/VOID/plate.png       faceplate
  assets/processed/VOID/<sprite>.png    lit lamps, buttons and the DROWN ring (RGBA)
  assets/processed/VOID/sprites.json    where each sprite goes (reference pixels)
using the geometry in plugins/VOID/UI_MAP.json.

Removed from the artwork, because the runtime draws them from the real state:
  - all fourteen knob pointers and their glow; DROWN's lit arc (the plate
    shows the whole track dark, the editor lights it up to the value);
  - every scale dot and every number printed on a knob scale, and the
    values in the readout windows;
  - the lit ALGORITHM lamp, its caption light and the two lines that frame
    the selected row (the editor draws them on the selected row); the lit
    COLOR and FILTER MODE buttons; the lit FREEZE button;
  - the spectrum display's frequency labels and ticks (printed up to 12 px
    off a log axis), the lit parts of the EARLY / LATE bars and of the IN /
    OUT meters;
  - the filter graph's curve, grid and labels (also off a log axis);
  - the preset name.

Usage: python3 tools/skinprep/prep_void.py [--debug out_dir]
Requires: numpy, opencv-python-headless.
"""

import argparse
import json
import os

import cv2
import numpy as np

from skinlib import angular_fill, feather_composite, inpaint_with_grain, rect_mask, rotate_fill, text_alpha
from prep_echo import erase_captions, erase_dot, pad
from prep_veil import resize_to, sprite_from

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "assets", "reference", "VOID.png")
OUT = os.path.join(ROOT, "assets", "processed", "VOID")
UI_MAP = os.path.join(ROOT, "plugins", "VOID", "UI_MAP.json")

# the artwork's lit pink: a near-white core in a rose glow (BGR)
GLOW = np.array([175.0, 120.0, 235.0], np.float32)


# ---------------------------------------------------------------- pink light

def pink_weight(region):
    """How much of a pixel is the rose light of a lamp, button or pointer:
    rose hue (magenta to red), saturated."""
    hsv = cv2.cvtColor(region, cv2.COLOR_BGR2HSV).astype(np.float32)
    rose = ((hsv[..., 0] >= 140) | (hsv[..., 0] <= 12)).astype(np.float32)
    return rose * np.clip((hsv[..., 1] - 30.0) / 60.0, 0.0, 1.0) * np.clip((hsv[..., 2] - 25.0) / 40.0, 0.0, 1.0)


def deglow(plate, rect, margin, inside=True):
    """The rose light a lit element throws on the dark panel around it. The
    panel is a near-neutral warm grey; the light raises red and blue far
    more than green, so a lit pixel goes back to the panel grey at the level
    its green channel gives."""
    x, y, w, h = rect
    x0, y0 = max(0, x - margin), max(0, y - margin)
    x1, y1 = min(plate.shape[1], x + w + margin), min(plate.shape[0], y + h + margin)
    reg = plate[y0:y1, x0:x1].astype(np.float32)
    wgt = pink_weight(plate[y0:y1, x0:x1])
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    dx = np.maximum(0, np.maximum(x - xx, xx - (x + w)))
    dy = np.maximum(0, np.maximum(y - yy, yy - (y + h)))
    near = np.clip(1.0 - np.hypot(dx, dy) / max(1, margin), 0.0, 1.0)
    if not inside:
        near[(dx == 0) & (dy == 0)] = 0.0
    wgt = (wgt * near)[..., None]
    grey = reg[..., 1:2] * np.array([0.92, 1.0, 1.12], np.float32)
    out = plate.copy()
    out[y0:y1, x0:x1] = np.clip(reg * (1 - wgt) + grey * wgt, 0, 255).astype(np.uint8)
    return out


def printed_mask(img, extra_discs=()):
    """Everything printed or bright that a fill must not copy from."""
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    printed = np.abs(g - cv2.GaussianBlur(g, (0, 0), 6.0)) > 22
    printed = cv2.dilate(printed.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    for (cx, cy, r) in extra_discs:
        printed |= ((xx - cx) ** 2 + (yy - cy) ** 2) <= r * r
    n, lab, st, _ = cv2.connectedComponentsWithStats(printed.astype(np.uint8))
    return (st[:, cv2.CC_STAT_AREA] >= 90)[lab] & (lab > 0)


# ---------------------------------------------------------------- knobs

def pointer_angle(img, c):
    """The artwork pointer's angle: the ray from the cap centre that is
    brightest between the pointer's inner and outer radius."""
    cx, cy = c["centre"]
    p = c["pointer"]
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    best = None
    r = np.linspace(p["inner"] + 3, p["outer"] - 3, 24)
    for deg in np.arange(-180.0, 180.0, 0.5):
        t = np.radians(deg)
        xs = (cx + r * np.sin(t)).astype(np.float32)
        ys = (cy - r * np.cos(t)).astype(np.float32)
        v = cv2.remap(g, xs.reshape(-1, 1), ys.reshape(-1, 1), cv2.INTER_LINEAR).ravel()
        score = float(np.percentile(v, 30))
        if best is None or score > best[0]:
            best = (score, deg)
    return best[1]


def pointer_out(img, plate, c, yy, xx):
    """The lit pointer and its glow: a band along the pointer, filled along
    the circles around the cap (smooth brightness) with the brushed grain of
    a turned copy of the cap."""
    cx, cy = c["centre"]
    p = c["pointer"]
    ang = pointer_angle(img, c)
    t = np.radians(ang)
    ux, uy = np.sin(t), -np.cos(t)
    px, py = xx - cx, yy - cy
    along = px * ux + py * uy
    across = np.abs(px * uy - py * ux)
    r = np.hypot(px, py)
    r0, r1 = p["inner"] - 5, min(p["outer"] + 6, c["rimRadius"] + 2)
    band = (along >= r0) & (along <= r1) & (across <= p["width"] / 2 + 5)
    # the glow reaches a little further where it is lit
    glow = (pink_weight(img) > 0.15) | (cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) > 120)
    wide = (along >= r0) & (along <= r1) & (across <= p["width"] / 2 + 9) & glow
    mask = ((band | wide) & (r <= c["rimRadius"] + 2)).astype(np.uint8) * 255
    mask[r <= 6] = 255
    mask = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1)
    mask[r > c["rimRadius"] + 3] = 0
    angles = np.degrees(np.arctan2(px[mask > 0], -py[mask > 0]))
    spread = np.abs(((angles - ang + 180) % 360) - 180)
    spread = float(np.percentile(spread[r[mask > 0] > 12], 98)) if (r[mask > 0] > 12).any() else 10.0
    turn = 2.0 * spread + 8.0
    smooth = angular_fill(plate, mask, (float(cx), float(cy))).astype(np.float32)
    rotated = rotate_fill(plate, mask, (float(cx), float(cy)), turn, cv2.INTER_LINEAR).astype(np.float32)
    m = cv2.getRotationMatrix2D((float(cx), float(cy)), turn, 1.0)
    src_was_pointer = cv2.warpAffine(mask, m, (mask.shape[1], mask.shape[0])) > 0
    grain = rotated - cv2.GaussianBlur(rotated, (0, 0), 1.5)
    grain[src_was_pointer | (r <= 10)] = 0
    filled = np.clip(smooth + grain, 0, 255).astype(np.uint8)
    return feather_composite(plate, filled, mask, 1.0)


def scale_dots(img, c):
    """Printed scale dots on a knob's dot circle (small bright blobs)."""
    cx, cy = c["scaleCentre"]
    R = c["dotRadius"]
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    hp = g - cv2.GaussianBlur(g, (0, 0), 4)
    yy, xx = np.mgrid[0:g.shape[0], 0:g.shape[1]]
    d = np.hypot(xx - cx, yy - cy)
    m = ((hp > 18) & (np.abs(d - R) < 5)).astype(np.uint8)
    n, lab, st, cen = cv2.connectedComponentsWithStats(m)
    pts = []
    for i in range(1, n):
        if 2 <= st[i, 4] <= 120 and st[i, 2] <= 12 and st[i, 3] <= 12:
            pts.append((float(cen[i][0]), float(cen[i][1])))
    return pts


# ---------------------------------------------------------------- ALGORITHM lamps

def algorithm_lamps(img, plate, c, avoid):
    """Every lamp dark, every caption in the dark ink; a lit image per row:
    the artwork's lamp light, the row's caption in the lit ink with its
    glow, and the two lines that frame the selected row."""
    lx = c["lampX"]
    rows = c["rows"]
    ax, ay = c["artLamp"]
    capx, capy, capw, caph = c["artCaption"]
    # the lit row's light on the panel, then the lamp, caption and lines out
    plate = deglow(plate, [int(ax) - 8, int(ay) - 8, 16, 16], 24)
    plate = deglow(plate, [capx, capy, capw, caph], 10)
    room_alpha = text_alpha(img[capy - 2:capy + caph + 2, capx - 2:capx + capw + 2], offset=18.0)
    rects = [[int(ax) - 11, int(ay) - 11, 22, 22], [capx - 1, capy - 1, capw + 2, caph + 2]] + c["lines"]
    clean = erase_captions(plate, [pad(r, 2) for r in rects], avoid)
    light = np.clip(img.astype(np.float32) - clean.astype(np.float32), 0, None)

    # dark lamp (from the PLATE row) where the ROOM lamp belongs
    k = 10
    sy = int(round(rows[1]))
    dark = clean[sy - k:sy + k + 1, int(lx) - k:int(lx) + k + 1].copy()
    x0, y0 = int(round(lx)), int(round(rows[0]))
    pasted = clean.copy()
    pasted[y0 - k:y0 + k + 1, x0 - k:x0 + k + 1] = dark
    m = np.zeros(plate.shape[:2], np.uint8)
    cv2.circle(m, (x0, y0), 9, 255, -1)
    plate = feather_composite(clean, pasted, m, 1.5)

    # ROOM caption in the dark ink of the other captions, on the row's line
    ink_rect = c["captions"][1]
    x, y, w, h = ink_rect
    a1 = text_alpha(img[y:y + h, x:x + w], offset=18.0)
    dark_ink = np.median(img[y:y + h, x:x + w][a1 > 0.8].astype(np.float32), axis=0)
    tx, ty = c["captions"][0][0] - 2, c["captions"][0][1] - 2
    hh, ww = room_alpha.shape
    reg = plate[ty:ty + hh, tx:tx + ww].astype(np.float32)
    a = room_alpha[..., None]
    plate[ty:ty + hh, tx:tx + ww] = np.clip(reg * (1 - a) + dark_ink * a, 0, 255).astype(np.uint8)

    # lit ink: the lit caption's strokes
    lit_ink = np.median(img[capy:capy + caph, capx:capx + capw][room_alpha[2:-2, 2:-2] > 0.8].astype(np.float32), axis=0)

    sprites = {}
    lamp_light = np.zeros_like(light)
    lamp_light[int(ay) - 18:int(ay) + 19, int(ax) - 18:int(ax) + 19] = light[int(ay) - 18:int(ay) + 19, int(ax) - 18:int(ax) + 19]
    line_light = np.zeros_like(light)
    for x, y, w, h in c["lines"]:
        line_light[y - 2:y + h + 2, x - 2:x + w + 2] = light[y - 2:y + h + 2, x - 2:x + w + 2]
    for i, ry in enumerate(rows):
        lit = plate.astype(np.float32).copy()
        dx, dy = lx - ax, ry - ay
        M = np.float32([[1, 0, dx], [0, 1, dy]])
        lit += cv2.warpAffine(lamp_light, M, (lit.shape[1], lit.shape[0]))
        lit += cv2.warpAffine(line_light, np.float32([[1, 0, 0], [0, 1, ry - ay]]), (lit.shape[1], lit.shape[0]))
        x, y, w, h = c["captions"][i]
        a = text_alpha(plate[y - 2:y + h + 2, x - 2:x + w + 2], offset=18.0)
        glow = cv2.GaussianBlur(a, (0, 0), 2.5)[..., None]
        reg = lit[y - 2:y + h + 2, x - 2:x + w + 2]
        reg = reg + GLOW * 0.35 * glow
        reg = reg * (1 - a[..., None]) + lit_ink * a[..., None]
        lit[y - 2:y + h + 2, x - 2:x + w + 2] = reg
        lit = np.clip(lit, 0, 255).astype(np.uint8)
        top = int(round(ry)) - 22
        rect = [int(lx) - 26, top, 330 - int(lx) + 26 - 10, 44]
        sprites["algorithm_" + str(i)] = (sprite_from(lit, plate, rect), rect)
    return plate, sprites


# ---------------------------------------------------------------- buttons

def caption_alpha(face, col0):
    """Light caption on a dark face, right of the button's lamp."""
    lum = cv2.cvtColor(face, cv2.COLOR_BGR2GRAY).astype(np.float32)
    bg = cv2.cvtColor(cv2.medianBlur(face, 15), cv2.COLOR_BGR2GRAY).astype(np.float32)
    top = float(np.percentile(lum[:, col0:], 99.5))
    a = (lum - bg - 22.0) / max(1.0, top - float(np.median(bg)) - 22.0)
    a = np.clip(a, 0.0, 1.0)
    a[:, :col0] = 0.0
    m = 4
    a[:m, :] = 0; a[-m:, :] = 0; a[:, -m:] = 0
    return a


def button_group(img, plate, grp, col0, g=14, frame=0):
    """Every button of a group dark on the plate, and a lit image of every
    button. The artwork has one lit button L (lit face, lit lamp, glowing
    caption); the others are dark.
      dark L:  the dark sibling D (caption inpainted, its dark lamp kept),
               fitted to L's body, with L's caption in D's ink; L's light on
               the panel around it removed;
      lit b:   L's lit body (caption inpainted, lit lamp kept) fitted to b's
               body, the light L throws around itself, and b's caption in
               L's lit ink with a rose glow.
    `frame`: width of a lit outline inside L's body edge (STEEL), kept out
    of L's caption."""
    buttons = grp["buttons"]
    L = buttons[grp["litInArtwork"]]
    D = buttons[grp["copyFrom"]]
    alphas = {}
    for b in buttons:
        x, y, w, h = b["body"]
        alphas[b["label"]] = caption_alpha(img[y:y + h, x:x + w], col0)
    # the lit caption: its white-hot core, as heavy as the dark captions' ink
    lx, ly, lw, lh = L["body"]
    hsv = cv2.cvtColor(img[ly:ly + lh, lx:lx + lw], cv2.COLOR_BGR2HSV).astype(np.float32)
    white = (255.0 - hsv[..., 1]) * np.clip((hsv[..., 2] - 120.0) / 60.0, 0.0, 1.0)
    white[:, :col0] = 0
    if frame:
        white[:frame, :] = 0; white[-frame:, :] = 0; white[:, :frame] = 0; white[:, -frame:] = 0
    per_letter = np.mean([(alphas[b["label"]] > 0.5).sum() / len(b["label"]) for b in buttons if b is not L])
    want = int(1.2 * per_letter * len(L["label"]))
    level = np.sort(white.ravel())[::-1][want]
    alphas[L["label"]] = np.clip((white - level) / 40.0 + 0.5, 0.0, 1.0)

    inks = {}
    for b in buttons:
        x, y, w, h = b["body"]
        inks[b["label"]] = np.median(img[y:y + h, x:x + w][alphas[b["label"]] > 0.8].astype(np.float32), axis=0)
    dark_ink, lit_ink = inks[D["label"]], inks[L["label"]]

    def hole(a):
        m = (cv2.GaussianBlur(a, (0, 0), 1.0) > 0.05).astype(np.uint8) * 255
        return cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=2)

    dx_, dy_, dw, dh = D["body"]
    dark_blank = cv2.inpaint(img[dy_:dy_ + dh, dx_:dx_ + dw], hole(alphas[D["label"]]), 5, cv2.INPAINT_TELEA).astype(np.float32)
    lit_blank = cv2.inpaint(img[ly:ly + lh, lx:lx + lw], hole(alphas[L["label"]]), 5, cv2.INPAINT_TELEA).astype(np.float32)
    lit_ring = img[ly - g:ly + lh + g, lx - g:lx + lw + g].astype(np.float32)

    plate = plate.copy()
    fitted = resize_to(dark_blank, lw, lh)
    a = cv2.GaussianBlur(alphas[L["label"]], (0, 0), 0.5)[..., None]
    fitted = fitted * (1 - a) + dark_ink * a
    pasted = plate.copy()
    pasted[ly:ly + lh, lx:lx + lw] = np.clip(fitted, 0, 255).astype(np.uint8)
    plate = feather_composite(plate, pasted, rect_mask(plate.shape, [(lx, ly, lw - 1, lh - 1)]), 0.8)
    plate = deglow(plate, (lx, ly, lw, lh), g, inside=False)
    dark_ring = plate[ly - g:ly + lh + g, lx - g:lx + lw + g].astype(np.float32)
    halo = np.clip(lit_ring - dark_ring, 0, None)
    halo[g:g + lh, g:g + lw] = 0.0

    lit_images = {}
    for b in buttons:
        x, y, w, h = b["body"]
        lit = plate.astype(np.float32).copy()
        if b is L:
            lit[ly - g:ly + lh + g, lx - g:lx + lw + g] = lit_ring
        else:
            body = resize_to(lit_blank, w, h)
            a = alphas[b["label"]]
            a = np.clip(cv2.GaussianBlur(a, (0, 0), 0.5) * 1.3, 0.0, 1.0)
            glow = cv2.GaussianBlur(a, (0, 0), 2.5)
            body = body + GLOW * 0.5 * glow[..., None]
            body = body * (1 - a[..., None]) + lit_ink * a[..., None]
            lit[y:y + h, x:x + w] = body
            hl = resize_to(halo, w + 2 * g, h + 2 * g)
            hl[g:g + h, g:g + w] = 0.0
            lit[y - g:y + h + g, x - g:x + w + g] += hl
        lit_images[b["label"]] = np.clip(lit, 0, 255).astype(np.uint8)
    return plate, lit_images


# ---------------------------------------------------------------- DROWN

def drown(img, plate, c):
    """The arc dark over its whole travel: the lit right half replaced by the
    mirror image of the dark left half. The lit ring is a sprite (the
    artwork's light per radius, the same all round); the editor shows it
    from the start of the travel to the value."""
    cx, cy = c["centre"]
    arc = c["arc"]
    H, W = plate.shape[:2]
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    r = np.hypot(xx - cx, yy - cy)
    a = np.degrees(np.arctan2(xx - cx, -(yy - cy)))
    r0, r1 = arc["inner"] - 4, arc["outer"] + arc["glow"] + 8
    right = (r >= r0) & (r <= r1) & (a > -3.0) & (a < 150.0)
    mirrored = cv2.remap(plate, (2.0 * cx - xx).astype(np.float32), yy, cv2.INTER_LINEAR)
    m = right.astype(np.uint8) * 255
    dark = feather_composite(plate, mirrored, m, 1.5)
    # the start of the lit arc (just left of the top) has no dark mirror:
    # take the ring 25 degrees further left (the ring is the same all round)
    src = np.radians(a - 25.0)
    rot = cv2.remap(dark, (cx + r * np.sin(src)).astype(np.float32), (cy - r * np.cos(src)).astype(np.float32), cv2.INTER_LINEAR)
    top = ((r >= r0) & (r <= r1) & (a > -10.0) & (a < 4.0)).astype(np.uint8) * 255
    dark = feather_composite(dark, rot, top, 1.5)

    # the light per radius: lit minus dark, averaged over the lit part of the arc
    lo, hi = c["litFromArt"]
    rs = np.arange(int(arc["inner"] - 8), int(arc["outer"] + arc["glow"] + 4))
    prof = []
    for rr in rs:
        sel = (np.abs(r - rr) < 0.5) & (a > lo) & (a < hi)
        lit = img[sel].astype(np.float32)
        dk = dark[sel].astype(np.float32)
        prof.append(np.clip(np.median(lit, axis=0) - np.median(dk, axis=0), 0, None) if sel.any() else np.zeros(3))
    prof = np.array(prof)
    dark_level = np.median(dark[(r > arc["inner"]) & (r < arc["outer"]) & (a > -120) & (a < -20)].astype(np.float32), axis=0)
    half = int(arc["outer"] + arc["glow"] + 6)
    size = 2 * half + 1
    gy, gx = np.mgrid[0:size, 0:size].astype(np.float32)
    rr = np.hypot(gx - half, gy - half)
    light = np.stack([np.interp(rr, rs, prof[:, k], left=0, right=0) for k in range(3)], axis=-1)
    lum = light.max(axis=2)
    alpha = np.clip(lum / np.maximum(255.0 - dark_level.max(), 1.0), 0.0, 1.0)
    colour = np.clip(dark_level + light / np.maximum(alpha[..., None], 1e-3), 0, 255)
    sprite = np.dstack([colour.astype(np.uint8), (alpha * 255).astype(np.uint8)])
    rect = [int(round(cx)) - half, int(round(cy)) - half, size, size]
    return dark, sprite, rect


# ---------------------------------------------------------------- FREEZE

def freeze(img, plate, c):
    """FREEZE off on the plate: the lit face's glow out (to the panel's
    neutral dark) and the snowflake in the dark ink of unlit captions; the
    lit face as a sprite."""
    x, y, w, h = c["face"]
    face = img[y:y + h, x:x + w]
    lum = cv2.cvtColor(face, cv2.COLOR_BGR2GRAY).astype(np.float32)
    flake = np.clip((lum - 200.0) / 40.0, 0.0, 1.0)
    hole = cv2.dilate((flake > 0.05).astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=2)
    bg = cv2.inpaint(face, hole, 5, cv2.INPAINT_TELEA).astype(np.float32)
    # the face's own shading kept, its light taken down to an unlit face
    base = cv2.GaussianBlur(bg, (0, 0), 6.0)
    detail = bg - base
    target = np.array([30.0, 30.0, 33.0], np.float32)
    level = base.mean(axis=2, keepdims=True)
    shade = (level - level.mean()) * 0.15
    off = target + shade + detail * 0.6
    ink = np.array([120.0, 118.0, 124.0], np.float32)
    a = cv2.GaussianBlur(flake, (0, 0), 0.4)[..., None]
    off = off * (1 - a) + ink * a
    out = plate.copy()
    out[y:y + h, x:x + w] = np.clip(off, 0, 255).astype(np.uint8)
    bx, by, bw, bh = c["body"]
    out = deglow(out, [bx, by, bw, bh], 10)
    rect = [bx - 6, by - 6, bw + 12, bh + 12]
    return out, sprite_from(img, out, rect), rect


# ---------------------------------------------------------------- display, meters

def tile_unlit(plate, bar, lit_from, lit_to, axis):
    """A bar's lit part replaced by its own unlit part, repeated."""
    x, y, w, h = bar
    out = plate.copy()
    if axis == "y":
        src = plate[y:lit_from, x:x + w].copy()
        n = src.shape[0]
        for yy in range(lit_from, lit_to):
            out[yy, x:x + w] = src[(yy - lit_from) % n]
    else:
        src = plate[y:y + h, lit_to:x + w].copy()
        n = src.shape[1]
        for xx in range(lit_from, lit_to):
            out[y:y + h, xx] = src[:, (xx - lit_from) % n]
    return out


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

    discs = [(c["centre"][0], c["centre"][1], c["rimRadius"] + 4) for c in knobs]
    avoid = printed_mask(img, discs)

    for c in knobs:
        for p in scale_dots(img, c):
            av = avoid.copy()
            av[int(p[1]) - 7:int(p[1]) + 8, int(p[0]) - 7:int(p[0]) + 8] = False
            plate = erase_dot(plate, p, av, radius=5)
        if c.get("erase"):
            plate = erase_captions(plate, [pad(r, 3) for r in c["erase"]], avoid)

    sprites = {}
    plate, alg = algorithm_lamps(img, plate, controls["algorithmLamps"], avoid)
    sprites.update(alg)

    for gid, col0 in (("color", 30), ("filterMode", 22)):
        grp = controls[gid]
        plate, lit = button_group(img, plate, grp, col0)
        for b in grp["buttons"]:
            rect = pad(b["body"], 14)
            sprites[gid + "_" + b["label"].lower()] = (lit[b["label"]], rect, "lit")

    plate, ring, ring_rect = drown(img, plate, controls["drown"])
    sprites["drown_ring"] = (ring, ring_rect)

    plate, fr, fr_rect = freeze(img, plate, controls["freeze"])
    sprites["freeze_lit"] = (fr, fr_rect)

    # spectrum axis: labels and ticks out (the editor draws a log axis)
    sp = controls["spectrum"]
    # (the baseline stays as a plain line: each row becomes its own median
    # tone plus grain, so the ticks and the labels go and the line remains)
    x, y, w, h = sp["eraseAxis"]
    rng = np.random.default_rng(5)
    strip = plate[y:y + h, x:x + w].astype(np.float32)
    med = np.median(strip, axis=1, keepdims=True)
    strip = np.repeat(med, w, axis=1) + rng.normal(0.0, 1.3, (h, w))[..., None]
    plate = plate.copy()
    plate[y:y + h, x:x + w] = np.clip(strip, 0, 255).astype(np.uint8)

    # EARLY / LATE bars and the IN / OUT meters unlit
    el = controls["earlyLate"]
    for key in ("early", "late"):
        tx, ty, tw, th = el[key]["track"]
        lxx, _, lw, _ = el[key]["lit"]
        plate = tile_unlit(plate, [tx, ty - 1, tw, th + 2], lxx, lxx + lw + 1, "x")
        plate = deglow(plate, [tx, ty, lw, th], 8)
    mt = controls["meters"]
    for bx, by, bw, bh in mt["bars"]:
        plate = tile_unlit(plate, [bx - 1, by, bw + 1, bh], mt["litTop"], by + bh + 1, "y")
        plate = deglow(plate, [bx, mt["litTop"], bw, by + bh - mt["litTop"]], 8)

    # filter graph: curve, fill and grid out; labels out
    fg = controls["filterGraph"]
    x, y, w, h = fg["erase"][0]
    m = rect_mask(plate.shape, [(x, y, w, h)])
    # flat glass tone (the dark of the interior, the curve's fill left out)
    inner = plate[y:y + h, x:x + w].reshape(-1, 3).astype(np.float32)
    tone = np.median(inner[inner.sum(axis=1) < np.percentile(inner.sum(axis=1), 40)], axis=0)
    filled = np.empty(plate.shape, np.float32)
    filled[:] = tone
    rng = np.random.default_rng(3)
    filled += rng.normal(0.0, 1.6, filled.shape[:2])[..., None]
    plate = feather_composite(plate, np.clip(filled, 0, 255).astype(np.uint8), m, 0.8)
    plate = erase_captions(plate, [pad(r, 1) for r in fg["eraseLabels"]], avoid)

    # preset name
    plate = erase_captions(plate, [pad(r, 2) for r in controls["presetBar"]["eraseName"]], avoid)

    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, "plate.png"), plate, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    rects = {}
    for name, entry in sprites.items():
        if len(entry) == 3:
            lit, rect, _ = entry
            sp_img = sprite_from(lit, plate, rect)
        else:
            sp_img, rect = entry
        cv2.imwrite(os.path.join(OUT, name + ".png"), sp_img, [cv2.IMWRITE_PNG_COMPRESSION, 9])
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
            boxes += c.get("hitboxes", [])
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
                if "readout" in c:
                    x, y, w, h = c["readout"]
                    cv2.rectangle(dbg, (x, y), (x + w, y + h), (0, 200, 255), 1)
        cv2.imwrite(os.path.join(args.debug, "UI_OVERLAY_DEBUG.png"), dbg)


if __name__ == "__main__":
    main()
