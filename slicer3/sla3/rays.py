"""Kolizijos spinduliais — per `trimesh.ray`, ne savo rankomis.

JS versijoje čia gyveno savas trikampių tinklelis, savas Möller–Trumbore ir
savas DDA; dvi iš trijų 2026-08-12 taisytų klaidų buvo būtent ten. Čia to kodo
nėra. Plius viskas skaičiuojama PAKETAIS: 20 000 spindulių ~7 ms, tad galima
šaudyti visiems taškams iš karto, o ne po vieną.
"""
from __future__ import annotations

import numpy as np
import trimesh

INF = np.inf
DOWN = np.array([0.0, 0.0, -1.0])
BEAM_SAMPLES = 8          # SupportTreeUtils.hpp: Beam_<Samples = 8>


class Rays:
    """Spindulių paketai vienam tinklui."""

    def __init__(self, mesh: trimesh.Trimesh):
        self.mesh = mesh
        self.ray = mesh.ray

    def first_hit(self, origins, directions):
        """Artimiausias pataikymas. Grąžina (atstumai, is_vidaus).

        `is_vidaus` — ar pataikyta į paviršių iš vidaus (spindulys ir trikampio
        normalė vienakrypčiai). libslic3r tuo remiasi permesdamas spindulį.
        """
        o = np.asarray(origins, dtype=np.float64).reshape(-1, 3)
        d = np.asarray(directions, dtype=np.float64).reshape(-1, 3)
        d = d / np.linalg.norm(d, axis=1, keepdims=True)
        loc, idx_ray, idx_tri = self.ray.intersects_location(
            o, d, multiple_hits=False)
        dist = np.full(len(o), INF)
        inside = np.zeros(len(o), dtype=bool)
        if len(idx_ray):
            dist[idx_ray] = np.linalg.norm(loc - o[idx_ray], axis=1)
            n = self.mesh.face_normals[idx_tri]
            inside[idx_ray] = np.einsum('ij,ij->i', n, d[idx_ray]) > 0
        return dist, inside

    # ----------------------------------------------------------- pluoštas
    @staticmethod
    def _ring_basis(d):
        """Du statmenys krypčiai — PointRing (SupportTreeUtils.hpp)."""
        helper = np.where(np.abs(d[:, 2:3]) < 0.9,
                          np.array([0.0, 0.0, 1.0]), np.array([1.0, 0.0, 0.0]))
        a = np.cross(d, helper)
        a /= np.linalg.norm(a, axis=1, keepdims=True)
        b = np.cross(d, a)
        b /= np.linalg.norm(b, axis=1, keepdims=True)
        return a, b

    def beam_hit(self, src, direction, r1, r2, sd=0.0):
        """`beam_mesh_hit` (SupportTreeUtils.hpp:149-194).

        Aštuoni spinduliai palei kūgio paviršių; rezultatas — MAŽIAUSIAS
        pataikymas. Atstumas matuojamas nuo `p_src + r1 * raydir`, kaip
        originale — tad tai atstumas nuo žiedo, ne nuo ašies.
        """
        src = np.asarray(src, dtype=np.float64).reshape(-1, 3)
        d = np.asarray(direction, dtype=np.float64).reshape(-1, 3)
        d = d / np.linalg.norm(d, axis=1, keepdims=True)
        r1 = np.broadcast_to(np.asarray(r1, dtype=np.float64).reshape(-1), len(src))
        r2 = np.broadcast_to(np.asarray(r2, dtype=np.float64).reshape(-1), len(src))
        a, b = self._ring_basis(d)

        best = np.full(len(src), INF)
        for i in range(BEAM_SAMPLES):
            t = 2 * np.pi * i / BEAM_SAMPLES
            off = np.cos(t) * a + np.sin(t) * b
            p_src = src + off * (r1 + sd)[:, None]
            p_dst = src + d + off * (r2 + sd)[:, None]
            rd = p_dst - p_src
            rd /= np.linalg.norm(rd, axis=1, keepdims=True)
            dist, inside = self.first_hit(p_src + rd * r1[:, None], rd)

            # Pataikyta iš vidaus -> permetam iš išorės, kaip originalas.
            redo = inside & np.isfinite(dist)
            if redo.any():
                far = redo & (dist > 2 * r1 + sd)
                dist[far] = 0.0                       # hit = Hit(0.0)
                again = redo & ~far
                if again.any():
                    q = p_src[again] + rd[again] * (r1[again] + dist[again] + 1e-6)[:, None]
                    d2, _ = self.first_hit(q, rd[again])
                    dist[again] = d2
            best = np.minimum(best, dist)
        return best

    def pinhead_hit(self, s, direction, r_pin, r_back, width, sd=0.0):
        """`pinhead_mesh_hit` — ar galvutė telpa neliesdama modelio.

        Tas pats pluoštas, tik nuo smaigalio ir kūgiu, kuris prasiskleidžia nuo
        r_pin iki r_back per VISĄ galvutės ilgį (r2 skaičiuojamas 1 mm atstumui,
        nes `dst = src + dir`).
        """
        s = np.asarray(s, dtype=np.float64).reshape(-1, 3)
        d = np.asarray(direction, dtype=np.float64).reshape(-1, 3)
        d = d / np.linalg.norm(d, axis=1, keepdims=True)
        w = np.maximum(1e-6, np.asarray(width, dtype=np.float64).reshape(-1))
        r_pin = np.asarray(r_pin, dtype=np.float64).reshape(-1)
        r_back = np.asarray(r_back, dtype=np.float64).reshape(-1)
        start = s + d * r_pin[:, None]
        return self.beam_hit(start, d, r_pin, r_pin + (r_back - r_pin) / w, sd)
