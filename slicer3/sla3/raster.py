"""Poligonai -> 320x240 kaukė.

Skylės piešiamos ne „kita spalva ant viršaus", o antru praėjimu juodai: taip
poligono skylė lieka skyle net tada, kai figūros persidengia.
"""
from __future__ import annotations

import numpy as np
from PIL import Image, ImageDraw
from shapely.geometry import Polygon

from . import config


def mm_to_px(x_mm: np.ndarray, y_mm: np.ndarray, scale: int):
    """Milimetrai -> pikseliai. Plokštės centras yra (0, 0)."""
    px = (x_mm + config.PLATE_W_MM / 2.0) / config.PIXEL_MM * scale
    py = (y_mm + config.PLATE_H_MM / 2.0) / config.PIXEL_MM * scale
    return px, py


def _ring(coords, scale: int):
    a = np.asarray(coords, dtype=float)
    px, py = mm_to_px(a[:, 0], a[:, 1], scale)
    return list(zip(px.tolist(), py.tolist()))


def rasterize(polys: list[Polygon], supersample: int | None = None) -> np.ndarray:
    """Grąžina uint8 kaukę (RES_Y, RES_X); 0 = tamsu, 255 = eksponuojama."""
    ss = config.SUPERSAMPLE if supersample is None else supersample
    W, H = config.RES_X * ss, config.RES_Y * ss
    img = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(img)
    for p in polys:
        if p.is_empty:
            continue
        d.polygon(_ring(p.exterior.coords, ss), fill=255)
    # Skylės — atskiru praėjimu, po visų kontūrų.
    for p in polys:
        for hole in p.interiors:
            d.polygon(_ring(hole.coords, ss), fill=0)

    arr = np.asarray(img, dtype=np.uint8)
    if ss > 1:
        # Vidurkis po ss x ss langelį — pilki kraštai vietoj laiptelių.
        arr = (arr.reshape(config.RES_Y, ss, config.RES_X, ss)
                  .mean(axis=(1, 3)).round().astype(np.uint8))
    if config.MIRROR_X:
        arr = np.fliplr(arr)
    if config.MIRROR_Y:
        arr = np.flipud(arr)
    return arr


def area_mm2(mask: np.ndarray) -> float:
    """Kaukės plotas mm². Pilki kraštai skaičiuojami proporcingai."""
    return float(mask.sum()) / 255.0 * config.PIXEL_MM ** 2
