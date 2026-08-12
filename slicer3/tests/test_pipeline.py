"""Priėmimo kriterijai pirmam žingsniui.

Skaičiai TIE PATYS, kuriais jau pasitikim JS pusėje (`sla_tests.mjs`): kubas
10x10x10 pjūvyje duoda 100 mm², su 4x4 kanalu — 84 mm². Jei geometrija kada
nors „patobulės", tai kris čia, o ne ant dervos.

    python -m unittest discover -s tests -v
"""
import unittest

import numpy as np
import trimesh
from shapely.geometry import Polygon

import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from sla3 import config, raster, slicing  # noqa: E402


def cube(side=10.0):
    m = trimesh.creation.box(extents=(side, side, side))
    m.apply_translation((0, 0, side / 2))          # apačia ant z = 0
    return m


def cube_with_channel(side=10.0, hole=4.0):
    """Kubas su kvadratiniu kanalu kiaurai — per shapely, ne per boolean."""
    a, b = side / 2, hole / 2
    shell = [(-a, -a), (a, -a), (a, a), (-a, a)]
    hole_ring = [(-b, -b), (b, -b), (b, b), (-b, b)]
    return trimesh.creation.extrude_polygon(Polygon(shell, [hole_ring]), side)


class Pjuviai(unittest.TestCase):

    def test_kubo_plotas(self):
        polys = slicing.section(cube(), 5.0)
        area = sum(p.area for p in polys)
        self.assertAlmostEqual(area, 100.0, delta=0.001, msg=f'gauta {area:.6f}')

    def test_pjuvio_aukstis_nekeicia_ploto(self):
        m = cube()
        for z in (0.05, 2.5, 5.0, 7.5, 9.95):
            area = sum(p.area for p in slicing.section(m, z))
            self.assertAlmostEqual(area, 100.0, delta=0.001, msg=f'z={z}')

    def test_virs_ir_po_kunu_nieko_nera(self):
        m = cube()
        for z in (-1.0, 10.5, 50.0):
            self.assertEqual(slicing.section(m, z), [], msg=f'z={z}')

    def test_skyle_atima_savo_plota(self):
        polys = slicing.section(cube_with_channel(), 5.0)
        area = sum(p.area for p in polys)
        self.assertAlmostEqual(area, 84.0, delta=0.001, msg=f'gauta {area:.6f}')
        # Ir tai turi būti VIENA figūra su skyle, ne dvi atskiros kilpos.
        self.assertEqual(len(polys), 1)
        self.assertEqual(len(polys[0].interiors), 1)


class Rastras(unittest.TestCase):

    def test_rastro_plotas_sutampa_su_geometrija(self):
        polys = slicing.section(cube(), 5.0)
        got = raster.area_mm2(raster.rasterize(polys))
        self.assertAlmostEqual(got, 100.0, delta=0.5, msg=f'gauta {got:.4f}')

    def test_skyle_lieka_skyle_ir_rastre(self):
        polys = slicing.section(cube_with_channel(), 5.0)
        got = raster.area_mm2(raster.rasterize(polys))
        self.assertAlmostEqual(got, 84.0, delta=0.5, msg=f'gauta {got:.4f}')

    def test_veidrodis_taikomas(self):
        """display_mirror_x = 1: figūra kairėje plokštės pusėje kaukėje turi
        atsidurti dešinėje. Be šito spaudinys išeitų apverstas."""
        m = cube(6.0)
        m.apply_translation((-12.0, 0, 0))
        mask = raster.rasterize(slicing.section(m, 3.0))
        kaire = mask[:, :config.RES_X // 2].sum()
        desine = mask[:, config.RES_X // 2:].sum()
        self.assertGreater(desine, kaire * 10)


if __name__ == '__main__':
    unittest.main(verbosity=2)
