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
