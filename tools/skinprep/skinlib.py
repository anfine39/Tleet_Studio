"""Shared helpers for the TLEET skin preparation scripts."""

import cv2
import numpy as np


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


def angular_fill(img, mask, centre, step_deg=0.5, max_deg=120.0):
    """Fill masked pixels by interpolating along the circle through them,
    between the nearest unmasked pixels on either side. Suits radial and
    conical textures (brushed caps, ribbed skirts) where brightness changes
    smoothly with angle and a rotated copy would be too bright or too dark."""
    cx, cy = centre
    ys, xs = np.nonzero(mask)
    out = img.copy()
    if len(xs) == 0:
        return out
    r = np.hypot(xs - cx, ys - cy)
    th = np.arctan2(ys - cy, xs - cx)
    h, w = mask.shape

    def find(direction):
        found = np.full(len(xs), np.nan)
        for k in range(1, int(max_deg / step_deg) + 1):
            todo = np.isnan(found)
            if not todo.any():
                break
            a = th[todo] + direction * np.radians(k * step_deg)
            px = np.clip(np.round(cx + r[todo] * np.cos(a)).astype(int), 0, w - 1)
            py = np.clip(np.round(cy + r[todo] * np.sin(a)).astype(int), 0, h - 1)
            ok = mask[py, px] == 0
            idx = np.nonzero(todo)[0][ok]
            found[idx] = a[ok]
        return found

    a0, a1 = find(-1.0), find(1.0)
    good = ~np.isnan(a0) & ~np.isnan(a1)
    span = np.where(good, a1 - a0, 1.0)
    t = np.where(good, (th - a0) / span, 0.5)
    src = img.astype(np.float32)

    def sample(a):
        mx = (cx + r * np.cos(a)).astype(np.float32)
        my = (cy + r * np.sin(a)).astype(np.float32)
        return cv2.remap(src, mx.reshape(-1, 1), my.reshape(-1, 1), cv2.INTER_LINEAR).reshape(-1, 3)

    v = sample(np.nan_to_num(a0)) * (1 - t)[:, None] + sample(np.nan_to_num(a1)) * t[:, None]
    out[ys[good], xs[good]] = np.clip(v[good], 0, 255).astype(np.uint8)
    rest = np.zeros_like(mask)
    rest[ys[~good], xs[~good]] = 255
    if rest.any():
        out = cv2.inpaint(out, rest, 3, cv2.INPAINT_TELEA)
    return out


def match_illumination_far(original, filled, mask, gap=5, sigma=4.0, limit=None):
    """Like match_illumination, but the smooth brightness is inpainted from a
    boundary a few pixels away from the hole, so a glow or shadow hugging the
    removed object does not leak into the fill."""
    near = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=gap)
    if limit is not None:
        near = cv2.bitwise_and(near, limit)
    low = cv2.GaussianBlur(cv2.inpaint(original, near, 9, cv2.INPAINT_TELEA).astype(np.float32), (0, 0), sigma)
    f = filled.astype(np.float32)
    detail = f - cv2.GaussianBlur(f, (0, 0), sigma)
    out = original.astype(np.float32)
    sel = near > 0
    out[sel] = (low + detail)[sel]
    return np.clip(out, 0, 255).astype(np.uint8), near


def inpaint_with_grain(img, mask, grain_src, radius=5, sigma=1.2, seed=1):
    """Fill `mask` by inpainting (smooth brightness) and add back film grain
    taken at random from `grain_src` (x, y, w, h), a clean patch of the same
    surface, so the fill is not a flat smear."""
    filled = cv2.inpaint(img, mask, radius, cv2.INPAINT_TELEA).astype(np.float32)
    x, y, w, h = [int(v) for v in grain_src]
    patch = img[y:y + h, x:x + w].astype(np.float32)
    hp = patch - cv2.GaussianBlur(patch, (0, 0), sigma * 3)
    rng = np.random.default_rng(seed)
    ys, xs = np.nonzero(mask)
    if len(ys) == 0:
        return img.copy()
    # tile the high-pass patch with a random offset per 16x16 block
    grain = np.zeros_like(filled)
    for by in range(ys.min() // 16 * 16, ys.max() + 1, 16):
        for bx in range(xs.min() // 16 * 16, xs.max() + 1, 16):
            ox = int(rng.integers(0, max(1, w - 16)))
            oy = int(rng.integers(0, max(1, h - 16)))
            bh = min(16, grain.shape[0] - by, h - oy)
            bw = min(16, grain.shape[1] - bx, w - ox)
            if bh > 0 and bw > 0:
                grain[by:by + bh, bx:bx + bw] = hp[oy:oy + bh, ox:ox + bw]
    out = img.astype(np.float32)
    m = mask > 0
    out[m] = filled[m] + grain[m]
    return np.clip(out, 0, 255).astype(np.uint8)


def text_alpha(face_img, offset=10.0, top_percentile=98.0):
    """Soft alpha of light text on a darker face: 0 on the face, 1 on the
    strokes, anti-aliased edges kept."""
    lum = cv2.cvtColor(face_img, cv2.COLOR_BGR2GRAY).astype(np.float32)
    bg = float(np.median(lum))
    top = float(np.percentile(lum, top_percentile))
    a = (lum - bg - offset) / max(1.0, top - bg - offset)
    return np.clip(a, 0.0, 1.0)


def shift(img, dx, dy):
    """Translate an image (sub-pixel), edges replicated."""
    m = np.float32([[1, 0, dx], [0, 1, dy]])
    return cv2.warpAffine(img, m, (img.shape[1], img.shape[0]), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)


# ---- knob pointers, printed text and scale dots (GLUE, PUNCH)

def rect_mask(shape, rects, grow=0):
    m = np.zeros(shape[:2], np.uint8)
    for x, y, w, h in rects:
        cv2.rectangle(m, (int(x) - grow, int(y) - grow), (int(x + w) + grow, int(y + h) + grow), 255, -1)
    return m


def rib_period(plate, mask, c):
    """Angular period (degrees) of the ribbed skirt, by autocorrelation of
    the skirt ring outside the masked pointer; None if no clear period."""
    cx, cy = c["centre"]
    g = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    r = c["capRadius"] + 7.0
    th = np.radians(np.arange(0, 360, 0.25))
    px = (cx + r * np.sin(th)).astype(np.float32)
    py = (cy - r * np.cos(th)).astype(np.float32)
    v = cv2.remap(g, px.reshape(-1, 1), py.reshape(-1, 1), cv2.INTER_LINEAR).ravel()
    ok = cv2.remap(mask.astype(np.float32), px.reshape(-1, 1), py.reshape(-1, 1), cv2.INTER_NEAREST).ravel() == 0
    v = v - v[ok].mean()
    lags = np.arange(int(4 / 0.25), int(40 / 0.25))
    corr = np.full(len(lags), -1.0)
    for i, lag in enumerate(lags):
        both = ok & np.roll(ok, -lag)
        if both.sum() >= 200:
            a, b = v[both], np.roll(v, -lag)[both]
            corr[i] = float((a * b).mean() / (a.std() * b.std() + 1e-6))
    # first clear peak after the correlation has dropped (smooth signals
    # correlate at every small lag)
    dropped = False
    for i in range(4, len(lags) - 4):
        if corr[i] < 0.0:
            dropped = True
        if dropped and corr[i] > 0.3 and corr[i] == corr[i - 4:i + 5].max():
            return float(lags[i] * 0.25)
    return None


def source_turn(plate, mask, c, r, a):
    """Smallest rib-aligned rotation that moves the whole mask off itself.
    The knobs are drawn in perspective (side wall visible at the bottom), so
    a small turn keeps the source at nearly the same height on the knob; of
    the two directions, the one whose source sits at the closer height wins."""
    sel = (mask > 0) & (r > c["capRadius"] - 4)
    angles = a[sel]
    centre_angle = np.degrees(np.arctan2(np.sin(np.radians(angles)).mean(), np.cos(np.radians(angles)).mean()))
    spread = np.abs(((angles - centre_angle + 180) % 360) - 180).max()
    need = 2 * spread + 6
    period = rib_period(plate, mask, c)
    turn = need if period is None else period * np.ceil(need / period)
    best = None
    for sign in (1, -1):
        src = centre_angle - sign * turn  # where the copied pixels come from
        score = abs(np.cos(np.radians(src)) - np.cos(np.radians(centre_angle)))
        if best is None or score < best[0]:
            best = (score, sign * turn)
    return float(best[1])


def remove_pointer(img, plate, c, yy, xx, reach=None):
    """The lit pointer and its glow, filled along the circle around the cap.
    `reach` limits the search for the pointer's light (default: a bit beyond
    the cap); on a warm, stained panel it must stay inside the knob's rim."""
    cx, cy = c["centre"]
    if reach is None:
        reach = max(c["capRadius"] * 1.45, c["pointer"]["outer"] + 14)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    near = r <= reach
    hot = ((hsv[..., 0] >= 5) & (hsv[..., 0] <= 32) & (hsv[..., 1] > 90) & (hsv[..., 2] > 150) & near).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(hot)
    biggest = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))
    core = (lab == biggest).astype(np.uint8) * 255
    around = cv2.dilate(core, np.ones((3, 3), np.uint8), iterations=12) > 0
    glow = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 34) & (hsv[..., 1] > 60) & (hsv[..., 2] > 45) & around
    # the pointer continues as a lit notch on the skirt: extend along its direction
    ys, xs = np.nonzero(core)
    far = int(np.argmax((xs - cx) ** 2 + (ys - cy) ** 2))
    ang = np.arctan2(xs[far] - cx, -(ys[far] - cy))
    ext = np.zeros(img.shape[:2], np.uint8)
    r0 = c["pointer"]["outer"] - 6
    r1 = reach
    cv2.line(ext, (int(round(cx + np.sin(ang) * r0)), int(round(cy - np.cos(ang) * r0))),
             (int(round(cx + np.sin(ang) * r1)), int(round(cy - np.cos(ang) * r1))), 255, 9)
    warm = (hsv[..., 0] >= 4) & (hsv[..., 0] <= 34) & (hsv[..., 1] > 50) & (hsv[..., 2] > 70)
    bright = hsv[..., 2] > 150
    glow |= (ext > 0) & (warm | bright)
    mask = cv2.dilate((glow | (core > 0)).astype(np.uint8) * 255, np.ones((3, 3), np.uint8), iterations=3)
    mask[r <= 8] = 255
    mask[~near] = 0
    ang_deg = np.degrees(np.arctan2(xx - cx, -(yy - cy)))
    turn = source_turn(plate, mask, c, r, ang_deg)
    smooth = angular_fill(plate, mask, (float(cx), float(cy))).astype(np.float32)
    rotated = rotate_fill(plate, mask, (float(cx), float(cy)), turn, cv2.INTER_LINEAR).astype(np.float32)
    # ribs and brushing come whole from the rib-aligned rotated copy; only the
    # slow brightness change around the cap comes from the angular fill
    m = cv2.getRotationMatrix2D((float(cx), float(cy)), turn, 1.0)
    source_was_pointer = cv2.warpAffine(mask, m, (mask.shape[1], mask.shape[0])) > 0
    # skirt: ribs whole from the rotated copy, brightness from the angular fill
    detail = rotated - cv2.GaussianBlur(rotated, (0, 0), 4.0)
    detail[source_was_pointer] = 0
    skirt = cv2.GaussianBlur(smooth, (0, 0), 4.0) + detail
    # face: brushed cone, brightness follows the angle; fine grain only
    grain = rotated - cv2.GaussianBlur(rotated, (0, 0), 1.2)
    grain[source_was_pointer | (r <= 12)] = 0
    face = smooth + grain
    on_face = (r < c["capRadius"] - 2)[..., None]
    filled = np.clip(np.where(on_face, face, skirt), 0, 255).astype(np.uint8)
    return feather_composite(plate, filled, mask, 1.0), mask


def erase_rect_beside(plate, rect, centre, sigma=4.0):
    """Printed text: panel texture from beside the text (away from the knob),
    smooth brightness from the surroundings."""
    x, y, w, h = rect
    mask = rect_mask(plate.shape, [(x, y, w, h)])
    # candidate sources beside and above/below the text; the one with the least
    # fine detail wins, so section dividers and scale dots are not copied in
    gray = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    detail = np.abs(gray - cv2.GaussianBlur(gray, (0, 0), 2.0))
    best = None
    sx = 1 if x + w / 2 > centre[0] else -1
    sy = 1 if y + h / 2 > centre[1] else -1
    for dx, dy in ((sx * (w + 6), 0), (sx * (w + 14), 0), (0, sy * (h + 4)), (sx * (w + 6), sy * (h + 4))):
        x0, y0 = x + dx, y + dy
        if x0 < 0 or y0 < 0 or x0 + w >= plate.shape[1] or y0 + h >= plate.shape[0]:
            continue
        score = float(detail[y0:y0 + h, x0:x0 + w].mean())
        if best is None or score < best[0]:
            best = (score, dx, dy)
    _, dx, dy = best
    source = np.roll(np.roll(plate, -dy, axis=0), -dx, axis=1)
    filled = match_illumination(plate, source, mask, sigma=sigma)
    return feather_composite(plate, filled, cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1), 1.2)


def clean_patch(plate, centre, avoid, size=24, search=70):
    """The quietest size x size patch of panel near `centre` (least fine
    detail, nothing from `avoid`), for film grain."""
    g = cv2.cvtColor(plate, cv2.COLOR_BGR2GRAY).astype(np.float32)
    detail = np.abs(g - cv2.GaussianBlur(g, (0, 0), 2.0))
    cx, cy = int(centre[0]), int(centre[1])
    best = None
    for y in range(cy - search, cy + search - size, 4):
        for x in range(cx - search, cx + search - size, 4):
            if x < 0 or y < 0 or x + size >= plate.shape[1] or y + size >= plate.shape[0]:
                continue
            if avoid[y:y + size, x:x + size].any():
                continue
            score = float(detail[y:y + size, x:x + size].mean()) + 0.02 * float(g[y:y + size, x:x + size].std())
            if best is None or score < best[0]:
                best = (score, (x, y, size, size))
    return best[1]


def erase_dot(plate, centre, avoid, radius=11):
    """A printed scale dot and its soft halo: a clean patch of the same
    panel, brought to the brightness of the panel around the dot."""
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
    x, y, w, h = clean_patch(plate, centre, avoid | (mask > 0), size=2 * radius + 4)
    patch = plate[y:y + h, x:x + w].astype(np.float32)
    patch = patch * (level / np.maximum(np.median(patch.reshape(-1, 3), axis=0), 1.0))
    pasted = plate.copy()
    half = w // 2
    pasted[iy - half:iy - half + h, ix - half:ix - half + w] = np.clip(patch, 0, 255).astype(np.uint8)
    return feather_composite(plate, pasted, mask, 2.0)


def remove_pointer_mirrored(img, plate, c, yy, xx, axis_degrees=0.0):
    """Pointer and glow out. The face (brushed cone) is filled along its
    circles; the ribbed skirt is taken from the mirror image about the
    knob's vertical axis (the knobs are drawn tilted only vertically, so the
    mirrored ribs fit), brought to the brightness around the gap. A pointer
    close to that axis would mirror onto itself: then the mirror axis is
    turned by `axis_degrees` (0 = vertical, clockwise)."""
    cx, cy = c["centre"]
    face_done, mask = remove_pointer(img, plate, c, yy, xx)
    r = np.hypot(xx - cx, yy - cy)
    mask = mask.copy()
    mask[r > c["rimRadius"]] = 0
    if axis_degrees == 0.0:
        mx = (2.0 * cx - xx).astype(np.float32)
        my = yy.astype(np.float32)
    else:
        t = np.radians(axis_degrees)
        ux, uy = np.sin(t), -np.cos(t)
        px, py = xx - cx, yy - cy
        along = px * ux + py * uy
        mx = (cx + 2.0 * along * ux - px).astype(np.float32)
        my = (cy + 2.0 * along * uy - py).astype(np.float32)
    mirrored = cv2.remap(plate, mx, my, cv2.INTER_LINEAR).astype(np.float32)
    smooth = angular_fill(plate, mask, (float(cx), float(cy))).astype(np.float32)
    gain = cv2.GaussianBlur(smooth, (0, 0), 5.0) / np.maximum(cv2.GaussianBlur(mirrored, (0, 0), 5.0), 1.0)
    skirt = mirrored * np.clip(gain, 0.5, 2.0)
    on_face = (r < c["capRadius"] - 2)[..., None]
    filled = np.where(on_face, face_done.astype(np.float32), skirt)
    return feather_composite(plate, np.clip(filled, 0, 255).astype(np.uint8), mask, 1.0)


def warm_mask(img):
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    return (hsv[..., 0] >= 4) & (hsv[..., 0] <= 34) & (hsv[..., 1] > 50) & (hsv[..., 2] > 50)


def lamp_off(img, plate, sw):
    """The OverEasy lamp: unlit on the plate, the lit original as a sprite."""
    x, y, w, h = sw["lamp"]["rect"]
    crop = img[y:y + h, x:x + w]
    # the glow is warm; the lamp's hot core is near white
    hot = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)[..., 2] > 140
    lit = (warm_mask(crop) | hot).astype(np.uint8) * 255
    lit = cv2.dilate(lit, np.ones((3, 3), np.uint8), iterations=1)
    alpha = cv2.GaussianBlur(lit.astype(np.float32) / 255.0, (0, 0), 1.2)

    # unlit lamp: the same shapes, desaturated and dark like the paddle below it
    hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV).astype(np.float32)
    hsv[..., 1] *= 0.3
    hsv[..., 2] = 16.0 + 0.2 * hsv[..., 2]
    dark = cv2.cvtColor(np.clip(hsv, 0, 255).astype(np.uint8), cv2.COLOR_HSV2BGR).astype(np.float32)
    region = plate[y:y + h, x:x + w].astype(np.float32)
    a = alpha[..., None]
    plate = plate.copy()
    plate[y:y + h, x:x + w] = np.clip(region * (1 - a) + dark * a, 0, 255).astype(np.uint8)

    sprite = np.dstack([crop, np.clip(alpha * 255.0, 0, 255).astype(np.uint8)])
    return plate, sprite
