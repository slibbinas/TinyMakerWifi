# -*- coding: utf-8 -*-
"""Vieno modelio balas — tas pats matas kaip score.py, tik greitas.

    python scoreone.py biowoman [kronsteinas ...]

Istorijos NERASO: tai derinimo irankis, o i zurnala eina tik pilnas score.py.
"""
import sys

import numpy as np

import score

for name in (sys.argv[1:] or list(score.MODELS)):
    stl, ref, first, zs, oriented = score.MODELS[name]
    out = 'one-%s.zip' % name
    score.slice_ours(stl, out, oriented)
    r = [score.blobs(stl, ref, first, z, oriented) for z in zs]
    o = [score.blobs(stl, out, 0.05, z, oriented) for z in zs]
    errs = [abs(a - b) / max(b, 1) for a, b in zip(o, r)]
    print('%-12s etalonas %s' % (name, r))
    print('%-12s musu     %s -> balas %.1f'
          % ('', o, max(0.0, 100.0 * (1 - float(np.mean(errs))))))
