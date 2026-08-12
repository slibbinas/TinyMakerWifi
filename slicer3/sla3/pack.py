"""Kaukės -> ZIP su PNG.

Formatas TYČIA toks pat, kokį duoda esamas JS sliceris (`00001.png`, pilkas
8 bitų PNG): tai VIENINTELIS sąlyčio taškas tarp senojo ir naujojo kelio.
Bendro kodo nėra — tik bendras failo pavidalas, kad tas pats matuoklis ir tas
pats piešėjas galėtų palyginti abu.
"""
from __future__ import annotations

import io
import zipfile

import numpy as np
from PIL import Image


def png_bytes(mask: np.ndarray) -> bytes:
    buf = io.BytesIO()
    Image.fromarray(mask, mode='L').save(buf, format='PNG', optimize=True)
    return buf.getvalue()


def write_zip(path: str, masks) -> int:
    """Įrašo sluoksnius. `masks` — iteruojamas uint8 masyvų srautas."""
    n = 0
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_STORED) as z:
        for i, mask in enumerate(masks):
            z.writestr(f'{i + 1:05d}.png', png_bytes(mask))
            n += 1
    return n
