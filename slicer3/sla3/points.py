"""Kur sėti atramas — `SupportPointGenerator` logika.

Esmė, kurios JS versijoje TRŪKO: tankio nevaldo pastovus žingsnis. Nuokabos
kraštas smulkiai diskretizuojamas (`discretize_overhang_step` = 2 mm), o po to
kandidatas tampa atrama TIK jei jo neuždengia jau esančių atramų įtakos
spindulys, kuris AUGA kylant aukštyn (`support_curve`: 3,2 mm ties 0 -> 6 mm
ties 40 mm). Todėl ant glotnaus kūno atramos retėja, o ant šviežios nuokabos
tankėja.
"""
from __future__ import annotations

import math
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
        pts = [sample_island(p, cfg) for p in _polys(cur_u)]
        pts = [p for p in pts if len(p)]
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
            # `remove_supports_out_of_part` (SPG.cpp:555): atrama nustoja
            # dengti, kai ši dalis nuo jos nutolsta daugiau nei
            # removing_delta. Be šito senos apatinės atramos blokuoja
            # kandidatus per visą modelio aukštį ir viršus lieka be nieko.
            if active:
                reach = poly.buffer(cfg.removing_delta_mm, join_style=2)
                active = {k for k in active
                          if reach.contains(_pt(chosen[k].x, chosen[k].y))}
            if z >= cfg.base_height_mm:      # prie pat plokštės laikosi pats
                cand, island = overhang_candidates([poly], [p for p, _ in below], cfg)
                active |= select(cand, z, chosen, active, cfg, island)
                # Pussaliai - PAPILDOMAI prie nuokabos sejos (SPG.cpp:1529).
                pen = peninsula_candidates(poly, [p for p, _ in below], cfg)
                active |= select(pen, z, chosen, active, cfg, True)
            parts.append((poly, active))
        # prev VISADA ankstesnis sluoksnis, net tuščias: kitaip virš tuštumos
        # atsiradusi sala nebūtų skirtumas ir liktų be nieko.
        prev_parts = parts
        if progress and i % 64 == 0:
            progress(i + 1, len(layers))
    return chosen


def _triangular_grid(poly: Polygon, step: float) -> np.ndarray:
    """Vidaus taškai trikampiu tinkleliu (`sample_expolygon`, USI.cpp:1324).

    Trikampis, ne kvadratas: taip taškai išsidėsto tolygiau tame pačiame plote.
    """
    x0, y0, x1, y1 = poly.bounds
    h = step * math.sqrt(3) / 2
    out = []
    row = 0
    y = y0 + h / 2
    while y <= y1:
        x = x0 + (step / 2 if row % 2 else 0) + step / 2
        while x <= x1:
            if poly.contains(_pt(x, y)):
                out.append([x, y])
            x += step
        y += h
        row += 1
    return np.array(out) if out else np.empty((0, 2))


def sample_island(poly: Polygon, cfg: SupportConfig) -> np.ndarray:
    """Salos / pussalio sėja — `uniform_support_island` atitikmuo.

    Originalas (UniformSupportIsland.cpp, 2850 eilučių) skaido figūrą Voronoi
    skeletu į „plonas" ir „storas" dalis ir kiekvienai taiko savo taisyklę.
    Skeleto čia nėra — imam abi kraštines taisykles, kurias jis naudoja
    storajai daliai: kontūras kas `thick_outline_max_distance` (3,75 mm) ir
    vidus trikampiu tinkleliu `thick_inner_max_distance` (5 mm). Tai
    SUPAPRASTINIMAS, ne kopija, ir pažymėtas kaip toks.
    """
    pts = [discretize(poly.exterior, cfg.island_outline_step_mm)]
    for ring in poly.interiors:
        pts.append(discretize(ring, cfg.island_outline_step_mm))
    inner = poly.buffer(-cfg.island_outline_step_mm / 2)
    for p in _polys(inner):
        g = _triangular_grid(p, cfg.island_inner_step_mm)
        if len(g):
            pts.append(g)
    pts = [p for p in pts if len(p)]
    return np.vstack(pts) if pts else np.empty((0, 2))


def peninsula_candidates(poly: Polygon, below: list[Polygon],
                         cfg: SupportConfig) -> np.ndarray:
    """`create_peninsulas` (SPG.cpp:567) + `support_peninsulas` (SPG.cpp:316).

    Vieno sluoksnio nuokaba, kuri išsikiša toliau nei `peninsula_min_width`
    (2 mm) už žemiau esančios dalies, yra „pussalis" ir remiama ATSKIRAI, be to,
    kas jau gauta iš `sample_overhangs`. Savaime laikosi tik tai, kas arčiau nei
    `peninsula_self_supported_width` (1,5 mm) nuo „sausumos".

    Būtent šito mums ir trūko: tai PRIDEDANTIS mechanizmas, o ne atimantis, ir
    ant organinio modelio (plaukų sruogos, antakiai) jis duoda daug atramų.
    """
    if not below:
        return np.empty((0, 2))
    land = unary_union(below)
    # jtSquare -> mitre; apvalus offsetas duotų kitokį kraštą
    expanded = land.buffer(cfg.peninsula_min_width_mm, join_style=2)
    over = poly.difference(expanded)
    if over.is_empty:
        return np.empty((0, 2))          # tik smulkios nuokabos
    self_sup = land.buffer(cfg.peninsula_self_supported_width_mm, join_style=2)
    shape = poly.difference(self_sup)
    if shape.is_empty:
        return np.empty((0, 2))
    out = []
    for p in _polys(shape):
        if p.intersection(over).is_empty:
            continue                     # per siauras, kad būtų pussalis
        out.append(sample_island(p, cfg))
    out = [o for o in out if len(o)]
    return np.vstack(out) if out else np.empty((0, 2))
