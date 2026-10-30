#!/usr/bin/env python3
"""Cut the РИЛАЙ emblem and wordmark out of assets/brand/realie_logo.jpg
(black background) into RGBA sprites used by rebrand_skins.py and the editors.

Writes assets/brand/emblem.png and assets/brand/wordmark.png.
Usage: python3 tools/rebrand/extract_logo.py
"""
import os

import cv2
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "assets", "brand", "realie_logo.jpg")
BG = 12.0


def cut(img, x0, y0, x1, y1):
    crop = img[y0:y1, x0:x1].astype(np.float32)
    mx = crop.max(axis=2)
    alpha = np.clip((mx - 30.0) / (140.0 - 30.0), 0.0, 1.0)
    safe = np.maximum(alpha, 1e-3)[..., None]
    rgb = np.clip((crop - BG * (1.0 - alpha[..., None])) / safe, 0, 255)
    out = np.dstack([rgb, alpha * 255.0]).astype(np.uint8)
    ys, xs = np.nonzero(alpha > 0.05)
    return out[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def main():
    img = cv2.imread(SRC, cv2.IMREAD_COLOR)
    cv2.imwrite(os.path.join(ROOT, "assets", "brand", "emblem.png"), cut(img, 220, 70, 800, 700))
    cv2.imwrite(os.path.join(ROOT, "assets", "brand", "wordmark.png"), cut(img, 200, 702, 820, 900))


if __name__ == "__main__":
    main()
