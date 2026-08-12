"""slicer3 — pirmas žingsnis: STL -> sluoksniai -> kaukės -> ZIP. BE supportų.

    python slice_stl.py <model.stl> <isvestis.zip> [--no-center]

Supportų čia nėra sąmoningai. Šiandien pamatėm, kas nutinka, kai supportai
statomi ant nepatikrintos santechnikos — pirma įrodom pjaustymą.
"""
from __future__ import annotations

import sys
import time

from sla3 import config, pack, raster, slicing


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, dst = argv[1], argv[2]
    center = '--no-center' not in argv
    if '--no-mirror' in argv:
        # Profilyje display_mirror_x = 1, tad numatytai veidrodis TAIKOMAS.
        # Jungiklis yra tam, kad būtų galima palyginti su esamu JS sliceriu,
        # kuris jo netaiko (išmatuota 2026-08-12) — kol neaišku, kuris teisus.
        config.MIRROR_X = False

    t0 = time.time()
    mesh = slicing.load(src)
    m = slicing.place(mesh, center_xy=center)
    n = slicing.layer_count(m)
    size = m.extents
    print(f'{src}: {len(m.faces)} trikampiu · {size[0]:.2f} x {size[1]:.2f} x '
          f'{size[2]:.2f} mm · {n} sluoksniu · telpa: '
          f'{"taip" if slicing.fits(m) else "NE"}')

    def masks():
        for i in range(n):
            polys = slicing.section(m, slicing.layer_z(i))
            if i % 100 == 0:
                print(f'  {i + 1}/{n}', end='\r', flush=True)
            yield raster.rasterize(polys)

    count = pack.write_zip(dst, masks())
    print(f'\nirasyta: {dst} · {count} sluoksniu · {time.time() - t0:.1f} s '
          f'· {config.RES_X}x{config.RES_Y}, {config.PIXEL_MM:.4f} mm/px')
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
