"""Atramų medis — `DefaultSupportTree` etapų grandinė.

Tvarka privaloma ir tokia pat kaip `DefaultSupportTree::execute()`:
  add_pinheads -> classify -> routing_to_ground -> routing_to_model ->
  interconnect_pillars -> merge_result

Kolizijos — `rays.py` (trimesh/embree), ne savo rankomis.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

from .points import SupportPoint
from .rays import DOWN, INF, Rays
from .supconfig import SupportConfig


@dataclass
class Head:
    pos: np.ndarray             # smaigalys ant detalės
    direction: np.ndarray
    r_back: float
    width: float
    junction: np.ndarray
    on_model: bool = False
    ground_hit: float = INF
    pillar: int = -1


@dataclass
class Pillar:
    x: float
    y: float
    top: float
    bottom: float
    r_top: float
    on_model: bool = False
    partial: bool = False
    bridges: int = 0
    links: int = 0


@dataclass
class Segment:
    a: np.ndarray
    b: np.ndarray
    r: float
    head_tip: bool = False      # smailėja į detalę


@dataclass
class Tree:
    heads: list[Head] = field(default_factory=list)
    pillars: list[Pillar] = field(default_factory=list)
    bridges: list[Segment] = field(default_factory=list)
    links: list[Segment] = field(default_factory=list)
    log: dict = field(default_factory=dict)


def _dir_from_polar(polar, azimuth):
    st, ct = np.sin(polar), np.cos(polar)
    return np.stack([st * np.cos(azimuth), st * np.sin(azimuth), ct], axis=-1)


# --------------------------------------------------------------- 1 · galvutės
def add_pinheads(pts: list[SupportPoint], rays: Rays, cfg: SupportConfig) -> list[Head]:
    """`add_pinheads` (DefaultSupportTree.cpp:385).

    Kryptis prisotinama: polar = max(polar, PI - bridge_slope), t. y. galvutė
    turi eiti bent bridge_slope žemiau horizontalės. Taškai sėti iš sluoksnių,
    tad pradinė kryptis — žemyn.
    """
    if not pts:
        return []
    P = np.array([[p.x, p.y, p.z] for p in pts])
    n = len(P)
    r_pin = cfg.head_front_radius_mm
    r_back = np.full(n, cfg.head_back_radius_mm)
    width = cfg.head_width_mm
    need = width + 2 * r_back + 2 * r_pin - cfg.head_penetration_mm

    d = np.tile(DOWN, (n, 1))
    hit = rays.pinhead_hit(P, d, np.full(n, r_pin), r_back, need)

    # Nepavykus originalas ieško kitos krypties (cpp:467-499, NLopt). Čia —
    # tinklelio apžvalga tais pačiais rėžiais: polar nuo PI-bridge_slope iki PI,
    # azimutas visas ratas. Vienas spindulys ATRENKA, pluoštas SPRENDŽIA.
    bad = hit < need
    if bad.any():
        idx = np.flatnonzero(bad)
        cand_dirs, cand_hits = [], []
        for k in range(4):
            polar = math.pi - (k / 3) * cfg.bridge_slope
            for a in range(12):
                az = (a / 12) * 2 * math.pi
                dd = np.tile(_dir_from_polar(polar, az), (len(idx), 1))
                dist, _ = rays.first_hit(P[idx] + dd * r_pin, dd)
                cand_dirs.append(dd)
                cand_hits.append(dist)
        H = np.vstack(cand_hits)                       # (48, len(idx))
        order = np.argsort(-H, axis=0)[:3]             # trys geriausi kiekvienam
        for rank in range(order.shape[0]):
            still = hit[idx] < need[idx]
            if not still.any():
                break
            sel = order[rank]
            dd = np.stack([cand_dirs[sel[j]][j] for j in range(len(idx))])
            full = rays.pinhead_hit(P[idx], dd, np.full(len(idx), r_pin),
                                    r_back[idx], need[idx])
            better = full > hit[idx]
            upd = still & better
            if upd.any():
                rows = idx[upd]
                d[rows] = dd[upd]
                hit[rows] = full[upd]

    # Plonesnė galvutė kaip paskutinė išeitis (support_small_pillar_diameter).
    bad = hit < need
    if bad.any():
        idx = np.flatnonzero(bad)
        r2 = np.full(len(idx), cfg.head_fallback_radius_mm)
        need2 = width + 2 * r2 + 2 * r_pin - cfg.head_penetration_mm
        h2 = rays.pinhead_hit(P[idx], d[idx], np.full(len(idx), r_pin), r2, need2)
        ok = h2 > need2
        r_back[idx[ok]] = cfg.head_fallback_radius_mm
        hit[idx[ok]] = h2[ok]
        need[idx[ok]] = need2[ok]

    heads = []
    for i in range(n):
        if not hit[i] > need[i]:
            continue                                   # netelpa — taško atsisakom
        j = P[i] + d[i] * width
        if j[2] < cfg.base_height_mm:
            continue
        heads.append(Head(P[i], d[i], float(r_back[i]), width, j))
    return heads


# --------------------------------------------------------------- 2 · classify
def classify(heads: list[Head], rays: Rays, cfg: SupportConfig):
    """`classify` (cpp:528). Pluoštas ŽEMYN be saugos atstumo — klausiama tik
    „ar kelias laisvas". Pridėjus atsargą pluoštas užkabina pačią detalę."""
    if not heads:
        return [], []
    J = np.array([h.junction for h in heads])
    R = np.array([h.r_back for h in heads])
    hit = rays.beam_hit(J, np.tile(DOWN, (len(heads), 1)), R, R)
    ground, on_model = [], []
    for i, h in enumerate(heads):
        h.ground_hit = float(hit[i])
        if not np.isfinite(hit[i]):
            ground.append(i)
        elif cfg.ground_facing_only:
            continue
        else:
            h.on_model = True
            on_model.append(i)
    return ground, on_model


def cluster(idx: list[int], heads: list[Head], cfg: SupportConfig) -> list[list[int]]:
    """cpp:565-574: XY < 2*base_radius IR 3D < max_bridge_length; ne daugiau
    kaip max_bridges_on_pillar viename klasteryje."""
    used, out = set(), []
    for a in idx:
        if a in used:
            continue
        cl = [a]
        used.add(a)
        for b in idx:
            if b in used or len(cl) > cfg.max_bridges_on_pillar:
                continue
            pa, pb = heads[a].junction, heads[b].junction
            if (math.dist(pa[:2], pb[:2]) < 2 * cfg.base_radius_mm and
                    math.dist(pa, pb) < cfg.max_bridge_length_mm):
                cl.append(b)
                used.add(b)
        out.append(cl)
    return out


def centroid(cl: list[int], heads: list[Head]) -> int:
    best, best_sum = cl[0], INF
    for a in cl:
        s = sum(math.dist(heads[a].junction[:2], heads[b].junction[:2]) for b in cl)
        if s < best_sum:
            best_sum, best = s, a
    return best


# ------------------------------------------------------------ 3-5 · maršrutai
def build(pts: list[SupportPoint], mesh, cfg: SupportConfig) -> Tree:
    rays = Rays(mesh)
    t = Tree()
    heads = add_pinheads(pts, rays, cfg)
    t.heads = heads
    t.log['points'] = len(pts)
    t.log['heads'] = len(heads)

    ground, on_model = classify(heads, rays, cfg)
    t.log['ground'] = len(ground)
    t.log['on_model'] = len(on_model)
    clusters = cluster(ground, heads, cfg)
    t.log['clusters'] = len(clusters)

    pillars, bridges = t.pillars, t.bridges

    def add_pillar(h: Head) -> int:
        pillars.append(Pillar(float(h.junction[0]), float(h.junction[1]),
                              float(h.junction[2]), 0.0, h.r_back))
        h.pillar = len(pillars) - 1
        return h.pillar

    def connect_to_nearpillar(h: Head, pid: int) -> bool:
        """`connect_to_nearpillar` (cpp:282-363)."""
        pil = pillars[pid]
        if pil.bridges >= cfg.max_bridges_on_pillar:
            return False
        jp = h.junction
        near_u = np.array([pil.x, pil.y, pil.top])
        near_l = np.array([pil.x, pil.y, pil.bottom])
        r = h.r_back
        d2d = math.dist(jp[:2], near_u[:2])
        d3d = math.dist(jp, near_u)
        slope = math.atan2(near_u[2] - jp[2], d2d) if d2d else math.pi / 2
        start, end = jp.copy(), near_u.copy()
        max_len = r * cfg.max_bridge_length_mm / cfg.head_back_radius_mm
        zdiff = 0.0

        if d3d > max_len or slope > -cfg.bridge_slope:
            zdown = jp[2] + d2d * math.tan(-cfg.bridge_slope)
            D = math.dist(jp, np.array([near_u[0], near_u[1], zdown]))
            zdiff = zdown - near_u[2]
            if zdiff > 0:
                zdown -= zdiff
                start[2] -= zdiff
                if rays.beam_hit(jp, DOWN, r, r)[0] < zdiff:
                    return False
            if near_l[2] <= zdown <= near_u[2] and D < max_len:
                end[2] = zdown
            else:
                return False
        if end[2] < 4 * cfg.head_back_radius_mm:
            return False
        need = math.dist(start, end)
        if rays.beam_hit(start, end - start, r, r)[0] < need:
            return False
        if zdiff > 0:
            pillars.append(Pillar(float(jp[0]), float(jp[1]), float(jp[2]),
                                  float(start[2]), r, partial=True))
        bridges.append(Segment(start, end, r))
        pil.bridges += 1
        h.pillar = pid
        return True

    def search_pillar_and_connect(h: Head) -> bool:
        """`search_pillar_and_connect` (cpp:723)."""
        tried = set()
        while True:
            best, best_d = -1, INF
            for k, p in enumerate(pillars):
                if k in tried or p.partial:
                    continue
                dd = math.hypot(p.x - h.junction[0], p.y - h.junction[1])
                if dd < best_d:
                    best_d, best = dd, k
            if best < 0:
                return False
            if connect_to_nearpillar(h, best):
                return True
            tried.add(best)

    # --- 3 · routing_to_ground (cpp:577) ---
    for cl in clusters:
        c = centroid(cl, heads)
        add_pillar(heads[c])
        for i in cl:
            if i == c:
                continue
            h = heads[i]
            # Originalo tvarka (cpp:639-644): centrinis -> bet kuris -> savas.
            if connect_to_nearpillar(h, heads[c].pillar):
                continue
            if search_pillar_and_connect(h):
                continue
            add_pillar(h)

    # --- 4 · routing_to_model (cpp:760-789) ---
    for i in on_model:
        h = heads[i]
        if search_pillar_and_connect(h):
            continue
        if not np.isfinite(h.ground_hit):
            continue
        # Atramos taškas — iš AŠIES spindulio (`center_hit`, cpp:697). Pluošto
        # žiedas mato ir tai, ko po ašimi nėra, ir stulpas lieka ore.
        dist, _ = rays.first_hit(h.junction, DOWN)
        if not np.isfinite(dist[0]):
            continue
        bottom = max(0.0, float(h.junction[2] - dist[0]))
        if h.junction[2] - bottom < cfg.base_height_mm:
            continue
        pillars.append(Pillar(float(h.junction[0]), float(h.junction[1]),
                              float(h.junction[2]), bottom, h.r_back, on_model=True))
        h.pillar = len(pillars) - 1

    # --- 5 · interconnect_pillars (cpp:189, 792) ---
    zmin = cfg.base_height_mm
    done = set()
    for i, A in enumerate(pillars):
        if A.links >= cfg.pillar_cascade_neighbors:
            continue
        max_d = cfg.max_pillar_link_distance_mm * A.r_top / cfg.head_back_radius_mm
        near = sorted(((math.hypot(A.x - B.x, A.y - B.y), j)
                       for j, B in enumerate(pillars) if j != i),
                      key=lambda p: p[0])
        for d, j in near:
            if A.links >= cfg.pillar_cascade_neighbors or d >= max_d:
                break
            key = (min(i, j), max(i, j))
            if key in done or d < 2 * cfg.head_back_radius_mm:
                continue
            B = pillars[j]
            bridge_distance = d / math.cos(-cfg.bridge_slope)
            zstep = d * math.tan(-cfg.bridge_slope)        # MINUSAS — leidžiasi
            s_up, s_lo = A.top, B.top
            e_up, e_lo = max(A.bottom, zmin), max(B.bottom, zmin)
            ax, ay, bx, by = A.x, A.y, B.x, B.y
            if s_up - e_up < 0 or s_lo - e_lo < 0:
                continue
            if s_up < s_lo:
                s_up, s_lo = s_lo, s_up
                ax, bx, ay, by = bx, ax, by, ay
            if e_up < e_lo:
                e_up, e_lo = e_lo, e_up
            startz = s_lo - zstep if s_lo - zstep < s_up else s_lo
            if s_lo - e_up < abs(zstep):
                startz = min(s_up, s_lo - zstep)
                endz = max(e_up + zstep, e_lo)
                avail = startz - endz
                rounds = math.floor(avail / abs(zstep)) if zstep else 0
                startz -= 0.5 * (avail - rounds * abs(zstep))
            a = np.array([ax, ay, startz])
            b = np.array([bx, by, startz + zstep])
            made, guard = False, 0
            while b[2] >= e_up and guard < 200:
                guard += 1
                if rays.beam_hit(a, b - a, cfg.head_front_radius_mm,
                                 cfg.head_front_radius_mm,
                                 cfg.safety_distance_mm)[0] >= bridge_distance:
                    t.links.append(Segment(a.copy(), b.copy(), cfg.pillar_radius_mm))
                    made = True
                a, b = b, np.array([b[0], b[1], b[2] + zstep])
            done.add(key)
            if made:
                A.links += 1
                B.links += 1

    t.log['pillars'] = len(pillars)
    t.log['bridges'] = len(bridges)
    t.log['links'] = len(t.links)
    return t


# ------------------------------------------------------------ savikontrolė
def self_check(t: Tree, mesh, cfg: SupportConfig) -> int:
    """Kiek stulpų prasideda ore. Zondas leidžiamas iš `bottom + eps`, t. y. iš
    TUŠTUMOS: iš `bottom - eps` jis startuotų medžiagos viduje ir matuotų kūno
    storį (JS versijoje dėl to visi pranešimai buvo klaidingi)."""
    rays = Rays(mesh)
    eps, tol = 1e-3, 1e-3 + 0.05
    check = [p for p in t.pillars if p.bottom > 1e-6 and not p.partial]
    if not check:
        return 0
    o = np.array([[p.x, p.y, p.bottom + eps] for p in check])
    dist, inside = rays.first_hit(o, np.tile(DOWN, (len(o), 1)))
    return int(np.sum(~(inside | (dist <= tol))))
