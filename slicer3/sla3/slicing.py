"""STL įkėlimas, pastatymas ir pjaustymas į 2D poligonus.

Geometrijos čia savo ranka nerašom — tai visa esmė. `trimesh` duoda pjūvį,
`shapely` — poligonus su skylėmis. Trys klaidos, kurias per dieną gaudėm
JS versijoje (savas point-in-polygon, savas spindulio zondas, savas trikampių
indeksas), šitoje vietoje neįmanomos.
"""
from __future__ import annotations

import numpy as np
import trimesh
from shapely.geometry import Polygon

from . import config


def load(path: str) -> trimesh.Trimesh:
    """Įkelia STL. Scenos atveju sulipdo į vieną tinklą."""
    m = trimesh.load(path, force='mesh')
    if not isinstance(m, trimesh.Trimesh):
        raise TypeError(f'ne tinklas: {type(m)}')
    return m


def place(mesh: trimesh.Trimesh, center_xy: bool = True) -> trimesh.Trimesh:
    """Pastato ant plokštės: apačia į z = 0, pasirinktinai centruoja XY.

    Orientavimo (autoOrient) čia SĄMONINGAI nėra — pirmas žingsnis yra apie
    pjaustymą, ne apie pasukimą. Modelis paduodamas jau pasuktas.
    """
    m = mesh.copy()
    lo, hi = m.bounds
    shift = np.array([0.0, 0.0, -lo[2]])
    if center_xy:
        shift[0] = -(lo[0] + hi[0]) / 2.0
        shift[1] = -(lo[1] + hi[1]) / 2.0
    m.apply_translation(shift)
    return m


def fits(mesh: trimesh.Trimesh) -> bool:
    """Ar telpa į plokštę."""
    size = mesh.extents
    return bool(size[0] <= config.PLATE_W_MM and size[1] <= config.PLATE_H_MM)


def layer_count(mesh: trimesh.Trimesh) -> int:
    return max(1, int(np.ceil(mesh.extents[2] / config.LAYER_MM)))


def layer_z(index: int) -> float:
    """Sluoksnio vidurio aukštis — pjaunam per vidurį, ne per kraštą."""
    return (index + 0.5) * config.LAYER_MM


def section(mesh: trimesh.Trimesh, z: float) -> list[Polygon]:
    """Pjūvis aukštyje z -> shapely poligonai (su skylėmis).

    `polygons_full` grąžina užpildytas figūras: kontūras ir jo skylės vienoje
    `Polygon`, o ne plokščiu kilpų sąrašu. Būtent plokščias sąrašas ir buvo
    klaida, dėl kurios atramos atsidurdavo kiaurymėse.
    """
    sec = mesh.section(plane_origin=(0.0, 0.0, z), plane_normal=(0.0, 0.0, 1.0))
    if sec is None:
        return []
    # Pjūvis guli aukštyje z; nuleidžiam į z = 0, kad liktų tik x, y. Savos
    # plokštumos `to_planar` nefituojam — ji pasuktų ašis ir XY nebeatitiktų
    # plokštės. `to_2D` (didžioji D) — trimesh 5 vardas.
    to_2d = np.eye(4)
    to_2d[2, 3] = -z
    planar, _ = sec.to_2D(to_2D=to_2d, check=False)
    return [p for p in planar.polygons_full if not p.is_empty]


def slice_all(mesh: trimesh.Trimesh, progress=None) -> list[list[Polygon]]:
    """Visi sluoksniai iš eilės."""
    n = layer_count(mesh)
    out = []
    for i in range(n):
        out.append(section(mesh, layer_z(i)))
        if progress and i % 32 == 0:
            progress(i + 1, n)
    if progress:
        progress(n, n)
    return out
