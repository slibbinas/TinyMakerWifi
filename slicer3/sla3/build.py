"""Medis -> sluoksnių kaukės.

Piešiama TAIS PAČIAIS skaičiais, kuriais skaičiuota (`SupportConfig`), o ne
kokiais nors kitais — JS versijoje būtent dėl to ekrane matėsi 2,7× storesnis
padas ir 2× storesnės jungtys, nei suskaičiavo algoritmas.
"""
from __future__ import annotations

import numpy as np
from shapely.geometry import Point, Polygon
from shapely.ops import unary_union

from . import config, raster
from .supconfig import SupportConfig
from .tree import Segment, Tree


def pillar_discs(t: Tree, z: float, cfg: SupportConfig) -> list[tuple[float, float, float]]:
    out = []
    for p in t.pillars:
        if z > p.top or z < p.bottom:
            continue
        r = p.r_top
        up = z - p.bottom
        if p.bottom <= 1e-6 and up < cfg.base_height_mm:
            # Pėda platėja iki base_radius per base_height (support_base_*).
            k = up / cfg.base_height_mm
            r = cfg.base_radius_mm + (r - cfg.base_radius_mm) * k
        out.append((p.x, p.y, r))
    return out


def segment_discs(segs: list[Segment], z: float, cfg: SupportConfig):
    out = []
    for s in segs:
        lo, hi = (s.a, s.b) if s.a[2] <= s.b[2] else (s.b, s.a)
        if z < lo[2] or z > hi[2]:
            continue
        k = (z - lo[2]) / ((hi[2] - lo[2]) or 1.0)
        r = cfg.pillar_radius_mm
        if s.head_tip:
            # Galvutė siaurėja iki head_front_radius per head_width — tai jos
            # visa prasmė: laikyti, bet nusilaužti.
            left = hi[2] - z
            if left < cfg.head_width_mm:
                r = (cfg.head_front_radius_mm +
                     (cfg.pillar_radius_mm - cfg.head_front_radius_mm)
                     * (left / cfg.head_width_mm))
        out.append((lo[0] + (hi[0] - lo[0]) * k, lo[1] + (hi[1] - lo[1]) * k, r))
    return out


def head_segments(t: Tree) -> list[Segment]:
    """Galvutė — atkarpa nuo jungties iki paties paviršiaus. Be jos stulpas
    baigiasi head_width atstumu nuo detalės ir nieko nelaiko."""
    return [Segment(h.junction, h.pos, 0.0, head_tip=True)
            for h in t.heads if h.pillar >= 0]


def support_polys(t: Tree, z: float, cfg: SupportConfig,
                  heads: list[Segment]) -> list[Polygon]:
    discs = pillar_discs(t, z, cfg)
    if z >= cfg.base_height_mm:
        discs += segment_discs(t.bridges, z, cfg)
        discs += segment_discs(t.links, z, cfg)
    discs += segment_discs(heads, z, cfg)
    return [Point(x, y).buffer(r, quad_segs=8) for x, y, r in discs if r > 0]


def build_pad(first_layer: list[Polygon], t: Tree, cfg: SupportConfig):
    """Padas (SLA/Pad.hpp): viskas, kas stovi ant plokštės, sujungiama ir
    išplečiama brim_size. pad_wall_height = 0, tad tai plona plokštelė."""
    parts = list(first_layer)
    parts += [Point(p.x, p.y).buffer(cfg.base_radius_mm, quad_segs=8)
              for p in t.pillars if p.bottom <= 1e-6]
    if not parts:
        return None
    return unary_union(parts).buffer(cfg.pad_brim_mm, quad_segs=8)


def masks(layers: list[list[Polygon]], t: Tree, cfg: SupportConfig, progress=None):
    """Sluoksnių kaukės su supportais ir padu."""
    heads = head_segments(t)
    pad = build_pad(layers[0] if layers else [], t, cfg)
    pad_layers = max(1, round(cfg.pad_thickness_mm / config.LAYER_MM))
    for i, part in enumerate(layers):
        z = (i + 0.5) * config.LAYER_MM
        polys = list(part) + support_polys(t, z, cfg, heads)
        if pad is not None and i < pad_layers:
            polys.append(pad)
        if progress and i % 64 == 0:
            progress(i + 1, len(layers))
        yield raster.rasterize(polys)
