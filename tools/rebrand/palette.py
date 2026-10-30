"""РИЛАЙ palette transform shared by the skin recolour and the source colour
rewrite, so baked artwork and code-drawn parts shift the same way.

Brand colours (from the РИЛАЙ logo and site mock): near-black, chalk white
and a signal red. Everything neutral or cool becomes neutral grey with a
faint chalk tint in the highlights; warm glows (amber lamps, pointers,
backlit faces) and existing reds become brand red.
"""
import numpy as np

RED_HUE = 358.0 / 360.0
CHALK = np.array([1.0, 0.975, 0.945], np.float32)     # tint of the brightest neutrals


def _smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _hsv_to_rgb(h, s, v):
    i = np.floor(h * 6.0)
    f = h * 6.0 - i
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    i = i.astype(np.int32) % 6
    r = np.choose(i, [v, q, p, p, t, v])
    g = np.choose(i, [t, v, v, q, p, p])
    b = np.choose(i, [p, p, t, v, v, q])
    return np.stack([r, g, b], axis=-1)


def recolour(rgb):
    """rgb: float array [..., 3] in 0..1 (RGB order). Returns the same shape."""
    rgb = np.asarray(rgb, np.float32)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    v = mx
    s = np.where(mx > 1e-6, (mx - mn) / np.maximum(mx, 1e-6), 0.0)
    d = np.maximum(mx - mn, 1e-6)
    h = np.where(mx == r, ((g - b) / d) % 6.0, np.where(mx == g, (b - r) / d + 2.0, (r - g) / d + 4.0)) / 6.0
    hd = h * 360.0

    # warm = amber/orange/gold; reds = already red
    warm = _smooth(4.0, 14.0, hd) * (1.0 - _smooth(55.0, 70.0, hd))
    reds = np.maximum(1.0 - _smooth(4.0, 14.0, hd), _smooth(335.0, 350.0, hd))
    glow = np.maximum(warm * _smooth(0.58, 0.88, s), reds * _smooth(0.30, 0.50, s)) * _smooth(0.25, 0.50, v)

    lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
    lum = np.power(np.clip(lum, 0, 1), 1.06)            # slightly deeper blacks
    tint = _smooth(0.45, 0.95, lum)[..., None]
    neutral = lum[..., None] * (1.0 + (CHALK - 1.0) * tint)

    red_s = np.clip(0.55 + 0.45 * s, 0, 1) * 0.92
    red_v = np.clip(v * 0.95, 0, 1)
    red = _hsv_to_rgb(np.full_like(h, RED_HUE), red_s, red_v)
    # very bright glow cores stay hot (whiter), like a red neon tube
    core = _smooth(0.92, 1.0, v) * (1.0 - _smooth(0.55, 0.8, s))
    red = red * (1 - core[..., None]) + np.array([1.0, 0.86, 0.84], np.float32) * core[..., None]

    out = neutral * (1 - glow[..., None]) + red * glow[..., None]
    return np.clip(out, 0.0, 1.0)


def recolour_hex(argb):
    """0xAARRGGBB -> 0xAARRGGBB through the same transform."""
    a = (argb >> 24) & 0xff
    rgb = np.array([[(argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff]], np.float32) / 255.0
    o = np.round(recolour(rgb)[0] * 255.0).astype(int)
    return (a << 24) | (int(o[0]) << 16) | (int(o[1]) << 8) | int(o[2])
