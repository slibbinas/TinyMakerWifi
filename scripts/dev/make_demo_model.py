"""Demo SD model: every STEP-th layer of a real sliced .sl1, stored (PNG is
already compressed). Prints the numbers the shim needs: the ORIGINAL layer
count, height and resin volume, measured from ALL layers."""
import io, re, sys, zipfile
from PIL import Image

SRC, OUT, STEP = sys.argv[1], sys.argv[2], int(sys.argv[3])
PX_MM, LAYER_MM = 0.1275, 0.05          # LCD pixel, source layer height

z = zipfile.ZipFile(SRC)
lay = [i for i in z.infolist()
       if re.search(r"(\d+)\.png$", i.filename) and not i.filename.lower().startswith("thumbnail")]
lay.sort(key=lambda i: int(re.search(r"(\d+)\.png$", i.filename).group(1)))

white = 0.0
for i in lay:
    im = Image.open(io.BytesIO(z.read(i.filename))).convert("L")
    white += sum(v * c for v, c in enumerate(im.histogram())) / 255.0
ml = white * PX_MM * PX_MM * LAYER_MM / 1000.0

keep = lay[::STEP]
if keep[-1] is not lay[-1]:
    keep.append(lay[-1])                   # keep the top, or the model loses its tip
with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_STORED) as o:
    for n, i in enumerate(keep, 1):
        o.writestr("%05d.png" % n, z.read(i.filename))

import os
print(f"layers={len(lay)} kept={len(keep)} height_mm={len(lay)*LAYER_MM:.2f} "
      f"resin_ml={ml:.1f} out_kb={os.path.getsize(OUT)//1024}")
