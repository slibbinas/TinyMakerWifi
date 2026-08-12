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

    def pinhead_hit(self, s, direction, r_pin, r_back, width, sd=None):
        """`pinhead_mesh_hit` (SupportTreeUtils.hpp:196-280).

        Tai NE `beam_mesh_hit` su kitais parametrais, nors ilgai buvo taip
        parašyta. Savas kūnas, ir kiekviena smulkmena turi reikšmę:

        * **16** spindulių, ne 8 („8 is almost ok, but to prevent rare cases of
          collision, 16 is necessary");
        * žiedai: smaigalio ties PAČIU tašku `s` spinduliu `r_pin + sd`,
          nugarėlės ties `s + (r_pin + width + r_back) * dir` spinduliu
          `r_back + sd` — ne `src + dir` (1 mm) konstrukcija;
        * spindulys leidžiamas iš `ps + sd * n`, t. y. PASISTŪMĖJUS per saugos
          atstumą — „move away slightly from the touching point to avoid
          raycasting on the inner surface of the mesh";
        * „iš vidaus" riba yra `r_pin + sd`, ne `2*r_src + sd`, o permetama su
          `q.distance() + 2*sd` poslinkiu.

        `sd` numatytoji reikšmė — ne nulis, o `r_back * safety_distance /
        head_back_radius` (DefaultSupportTree.hpp:140), t. y. pilnai galvutei
        **1,0 mm**. Perdavus nulį spinduliai startuoja tiksliai ant paviršiaus,
        pataiko į jį iš vidaus ir funkcija grąžina gryną 0 — taip 32 taškai iš
        92 buvo atmesti be priežasties (išmatuota 2026-08-12).
        """
        SAMPLES = 16
        s = np.asarray(s, dtype=np.float64).reshape(-1, 3)
        d = np.asarray(direction, dtype=np.float64).reshape(-1, 3)
        d = d / np.linalg.norm(d, axis=1, keepdims=True)
        w = np.asarray(width, dtype=np.float64).reshape(-1)
        r_pin = np.broadcast_to(np.asarray(r_pin, dtype=np.float64).reshape(-1), len(s))
        r_back = np.broadcast_to(np.asarray(r_back, dtype=np.float64).reshape(-1), len(s))
        if sd is None:
            raise ValueError('sd privalo būti perduotas (žr. DefaultSupportTree.hpp:140)')
        sd = np.broadcast_to(np.asarray(sd, dtype=np.float64).reshape(-1), len(s))

        spin = s
        sback = s + d * (r_pin + w + r_back)[:, None]
        rpin, rback = r_pin + sd, r_back + sd
        a, b = self._ring_basis(d)

        best = np.full(len(s), INF)
        for i in range(SAMPLES):
            t = 2 * np.pi * i / SAMPLES
            off = np.cos(t) * a + np.sin(t) * b
            ps = spin + off * rpin[:, None]
            p = sback + off * rback[:, None]
            n = p - ps
            n /= np.linalg.norm(n, axis=1, keepdims=True)
            dist, inside = self.first_hit(ps + n * sd[:, None], n)

            redo = inside & np.isfinite(dist)
            if redo.any():
                far = redo & (dist > rpin)
                dist[far] = 0.0
                again = redo & ~far
                if again.any():
                    q = ps[again] + n[again] * (dist[again] + 2 * sd[again])[:, None]
                    d2, _ = self.first_hit(q, n[again])
                    dist[again] = d2
            best = np.minimum(best, dist)
        return best
