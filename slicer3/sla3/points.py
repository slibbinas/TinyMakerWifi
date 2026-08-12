"""Kur sėti atramas — `SupportPointGenerator` logika.

Esmė, kurios JS versijoje TRŪKO: tankio nevaldo pastovus žingsnis. Nuokabos
kraštas smulkiai diskretizuojamas (`discretize_overhang_step` = 2 mm), o po to
kandidatas tampa atrama TIK jei jo neuždengia jau esančių atramų įtakos
spindulys, kuris AUGA kylant aukštyn (`support_curve`: 3,2 mm ties 0 -> 6 mm
ties 40 mm). Todėl ant glotnaus kūno atramos retėja, o ant šviežios nuokabos
tankėja.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from shapely.geometry import MultiPolygon, Polygon
from shapely import minimum_bounding_radius
from shapely.ops import unary_union

from . import config
from .supconfig import SupportConfig


@dataclass
class SupportPoint:
    x: float
    y: float
    z: float
    island: bool = False        # sala: sluoksnis be jokio pirmtako


def _polys(geom) -> list[Polygon]:
    if geom.is_empty:
        return []
    if isinstance(geom, Polygon):
        return [geom]
    if isinstance(geom, MultiPolygon):
        return list(geom.geoms)
    return [g for g in getattr(geom, 'geoms', []) if isinstance(g, Polygon)]


def discretize(ring, step: float) -> np.ndarray:
    """Kilpa -> taškai vienodu žingsniu (`sample()`, SPG.cpp:361)."""
    line = np.asarray(ring.coords, dtype=float)[:, :2]
    seg = np.diff(line, axis=0)
    lens = np.hypot(seg[:, 0], seg[:, 1])
    total = lens.sum()
    if total < step:
        return line[:1]
    n = max(1, int(np.floor(total / step)))
    want = np.arange(n) * (total / n)
    cum = np.concatenate([[0.0], np.cumsum(lens)])
    idx = np.clip(np.searchsorted(cum, want, side='right') - 1, 0, len(seg) - 1)
    t = (want - cum[idx]) / np.where(lens[idx] > 0, lens[idx], 1.0)
    return line[idx] + seg[idx] * t[:, None]


def overhang_candidates(cur: list[Polygon], prev: list[Polygon],
                        cfg: SupportConfig) -> tuple[np.ndarray, bool]:
    """Nuokabos kandidatai šiam sluoksniui.

    `sample_overhangs` (SPG.cpp:409): nuokaba = SKIRTUMAS tarp sluoksnio ir
    ankstesnio. Sėjami kraštai — ir kontūro, IR kiekvienos skylės. Kraštas,
    sutampantis su ankstesniu sluoksniu, PRALEIDŽIAMAS (`contain_point(p,
    prev_points)`, cpp:429) — tai jau paremta „sausuma", ne nuokabos krantas.
    """
    if not cur:
        return np.empty((0, 2)), False
    cur_u = unary_union(cur)
    if not prev:
        # Sala: viskas kabo. Originale tam yra atskiras `support_island`;
        # čia sėjam krantą ir centrą — supaprastinimas, pažymėtas sąmoningai.
        pts = []
        for p in _polys(cur_u):
            pts.append(discretize(p.exterior, cfg.discretize_overhang_step_mm))
            c = p.representative_point()
            pts.append(np.array([[c.x, c.y]]))
        return (np.vstack(pts) if pts else np.empty((0, 2))), True

    prev_u = unary_union(prev)
    over = cur_u.difference(prev_u)
    if over.is_empty:
        return np.empty((0, 2)), False

    land = prev_u.boundary
    out = []
    for p in _polys(over):
        if p.area < (config.PIXEL_MM ** 2) * 4:      # kontūro drebėjimas
            continue
        for ring in [p.exterior, *p.interiors]:
            pts = discretize(ring, cfg.discretize_overhang_step_mm)
            if len(pts) == 0:
                continue
            # „sausumos" kraštas: taškai, gulintys ant ankstesnio sluoksnio ribos
            keep = np.array([land.distance(_pt(x, y)) > 1e-6 for x, y in pts])
            if keep.any():
                out.append(pts[keep])
    return (np.vstack(out) if out else np.empty((0, 2))), False


def _pt(x, y):
    from shapely.geometry import Point
    return Point(x, y)


def select(candidates: np.ndarray, z: float, chosen: list[SupportPoint],
           active: set[int], cfg: SupportConfig, island: bool = False) -> set[int]:
    """Kandidatai -> atramos taškai per augantį įtakos spindulį.

    `prepare_supports_for_layer` (SPG.cpp:495): kiekviena jau pastatyta atrama
    turi `current_radius`, priklausantį nuo to, kiek mes virš jos. Kandidatas,
    patenkantis į tokį spindulį, atramos negauna.

    **Skaičiuojami tik `active`** — atramos, pasiekiamos per sluoksnių dalių
    grandinę (`is_active`, cpp:503-508). Taikant įtaką visai XY plokštumai
    keletas apatinių atramų „uždengia" viską aukščiau ir modelis virš 10 mm
    negauna nieko (išmatuota: 33 taškai vietoj kelių šimtų).
    """
    added: set[int] = set()
    if len(candidates) == 0:
        return added
    idx = sorted(active)
    pos = (np.array([[chosen[i].x, chosen[i].y] for i in idx])
           if idx else np.empty((0, 2)))
    radii = (np.array([cfg.influence_radius(max(0.0, z - chosen[i].z)) for i in idx])
             if idx else np.empty(0))

    for x, y in candidates:
        if len(pos) and (np.hypot(pos[:, 0] - x, pos[:, 1] - y) < radii).any():
            continue
        chosen.append(SupportPoint(float(x), float(y), float(z), island))
        added.add(len(chosen) - 1)
        pos = np.vstack([pos, [x, y]])
        radii = np.append(radii, cfg.influence_radius(0.0))
    return added


def generate(layers: list[list[Polygon]], cfg: SupportConfig,
             progress=None) -> list[SupportPoint]:
    """Visi atramos taškai modeliui, sluoksnis po sluoksnio.

    Sluoksnis skaidomas į DALIS (atskiras figūras), dalys siejamos su
    ankstesnio sluoksnio dalimis, ir atramų įtaka keliauja tik tomis
    jungtimis (`create_near_points`, SPG.cpp:210-240). Dėl to atrama vienoje
    kojoje nelaiko kitos kojos, esančios šalia XY plokštumoje.
    """
    chosen: list[SupportPoint] = []
    prev_parts: list[tuple[Polygon, set[int]]] = []
    for i, cur in enumerate(layers):
        z = (i + 0.5) * config.LAYER_MM
        parts: list[tuple[Polygon, set[int]]] = []
        for poly in cur:
            # `get_small_parts` (SPG.cpp:1032): dalys, mažesnės už
            # minimal_bounding_sphere_radius, IŠMETAMOS dar prieš sėją -
            # jų vis tiek neįmanoma atspausdinti kitaip nei rutuliuku nuo
            # galvutės. Be šito kiekvienas mesh triukšmo taškelis virsta
            # „sala" ir gauna atramą.
            if minimum_bounding_radius(poly) < cfg.minimal_part_radius_mm:
                continue
            below = [(p, s) for p, s in prev_parts if poly.intersects(p)]
            active: set[int] = set()
            for _, s in below:
                active |= s
            if z >= cfg.base_height_mm:      # prie pat plokštės laikosi pats
                cand, island = overhang_candidates([poly], [p for p, _ in below], cfg)
                active |= select(cand, z, chosen, active, cfg, island)
            parts.append((poly, active))
        # prev VISADA ankstesnis sluoksnis, net tuščias: kitaip virš tuštumos
        # atsiradusi sala nebūtų skirtumas ir liktų be nieko.
        prev_parts = parts
        if progress and i % 64 == 0:
            progress(i + 1, len(layers))
    return chosen
