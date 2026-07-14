"""Generate two TMB1 boot animations for the TinyMaker (160x80 ST7735):

  bunny.tmb      - the TinyMaker bag mascot: bunny wearing the printer as a hat;
                   blinks, glances left/right, the printer LED winks orange.
  resin-drip.tmb - orange resin drops falling from the vat, pool rising below.

TMB1: 'TMB1' + u16le width + u16le height + u16le frames + u16le fps,
then raw RGB565 little-endian frames (w*h*2 bytes each).
Also writes x3-scaled GIF previews next to the .tmb files.
"""
from PIL import Image, ImageDraw
import struct, os

W, H, FPS = 160, 80, 12
OUT = os.path.dirname(os.path.abspath(__file__))
INK = (235, 235, 235)
ORANGE = (232, 114, 12)
ORANGE_HI = (255, 150, 40)


def new_frame():
    img = Image.new('RGB', (W, H), (0, 0, 0))
    return img, ImageDraw.Draw(img)


def rgb565(img):
    out = bytearray(W * H * 2)
    i = 0
    for r, g, b in img.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[i] = v & 0xFF
        out[i + 1] = v >> 8
        i += 2
    return bytes(out)


def save_tmb(frames, path):
    with open(path, 'wb') as f:
        f.write(b'TMB1' + struct.pack('<HHHH', W, H, len(frames), FPS))
        for fr in frames:
            f.write(rgb565(fr))
    print(f"{os.path.basename(path)}: {len(frames)} frames, "
          f"{os.path.getsize(path)} bytes, {len(frames)/FPS:.1f}s")


def save_gif(frames, path):
    big = [f.resize((W * 3, H * 3), Image.NEAREST) for f in frames]
    big[0].save(path, save_all=True, append_images=big[1:],
                duration=int(1000 / FPS), loop=0)


# ---------------- bunny ----------------
def bunny_frame(eye_open=1.0, pupil_dx=0, led_on=False, whisker_up=False):
    """eye_open: 1 open, 0 closed; pupil_dx: pupil shift; led_on: hat LED."""
    img, d = new_frame()
    lw = 2

    # printer hat: top plate, body, knob + LED
    d.rounded_rectangle([58, 2, 102, 8], radius=2, outline=INK, width=lw)
    d.rounded_rectangle([63, 8, 97, 20], radius=2, outline=INK, width=lw)
    d.line([66, 14, 84, 14], fill=INK, width=1)
    led = ORANGE_HI if led_on else (90, 90, 90)
    d.ellipse([90, 11, 94, 15], fill=led)
    d.line([56, 22, 104, 22], fill=INK, width=lw)  # hat brim

    # head: house-shaped arch
    d.line([48, 79, 48, 46], fill=INK, width=lw)
    d.line([48, 46, 64, 26], fill=INK, width=lw)
    d.line([64, 26, 96, 26], fill=INK, width=lw)
    d.line([96, 26, 112, 46], fill=INK, width=lw)
    d.line([112, 46, 112, 79], fill=INK, width=lw)

    # whiskers (3 a side, slight wiggle)
    wo = -2 if whisker_up else 0
    for i, y in enumerate((50, 56, 62)):
        d.line([26, y + 2 + wo, 44, y - 1 + wo], fill=INK, width=1)
        d.line([116, y - 1 + wo, 134, y + 2 + wo], fill=INK, width=1)

    # eyes: big outlined circles
    for cx in (67, 93):
        d.ellipse([cx - 13, 52 - 13, cx + 13, 52 + 13], outline=INK, width=lw)
    if eye_open <= 0.05:
        for cx in (67, 93):  # closed: happy arcs
            d.arc([cx - 9, 48, cx + 9, 60], 20, 160, fill=INK, width=lw)
    else:
        ph = max(2, int(8 * eye_open))  # pupil half-height with lid squash
        for cx in (67, 93):
            x = cx + pupil_dx
            d.ellipse([x - 7, 52 - ph, x + 7, 52 + ph], fill=INK)
            d.ellipse([x - 4, 52 - ph + 1, x - 1, 52 - ph + 4], fill=(0, 0, 0))
            d.ellipse([x + 2, 52 + ph - 5, x + 4, 52 + ph - 3], fill=(60, 60, 60))

    # nose + smile
    d.ellipse([78, 60, 82, 64], fill=INK)
    d.arc([73, 58, 87, 70], 25, 155, fill=INK, width=lw)
    return img


def build_bunny():
    fr = []
    A = fr.append
    open_led = lambda led, n, **kw: [A(bunny_frame(led_on=led, **kw)) for _ in range(n)]
    open_led(False, 3)                                   # idle
    A(bunny_frame(eye_open=0.4))                         # blink
    A(bunny_frame(eye_open=0.0))
    A(bunny_frame(eye_open=0.4))
    open_led(False, 2)
    open_led(False, 3, pupil_dx=-4)                      # glance left
    open_led(True, 3, pupil_dx=4)                        # glance right + LED
    open_led(False, 2)
    A(bunny_frame(eye_open=0.4, whisker_up=True))        # wink + whisker twitch
    A(bunny_frame(eye_open=0.0))
    open_led(True, 3)                                    # goodbye LED
    return fr


# ---------------- resin drip ----------------
def drip_frame(drop_y, drop_stretch, pool_y, ripple, hang_r):
    """drop_y: falling drop center (None = none); hang_r: forming drop radius;
    pool_y: pool surface; ripple: 0..1 splash ripple progress (None = none)."""
    img, d = new_frame()
    # vat bottom with nozzle
    d.line([18, 8, 72, 8], fill=INK, width=2)
    d.line([88, 8, 142, 8], fill=INK, width=2)
    d.line([72, 8, 74, 12], fill=INK, width=2)
    d.line([88, 8, 86, 12], fill=INK, width=2)
    d.line([74, 12, 86, 12], fill=INK, width=2)

    if hang_r > 0:  # forming drop under the nozzle
        d.ellipse([80 - hang_r, 13, 80 + hang_r, 13 + hang_r * 2 + drop_stretch],
                  fill=ORANGE)
    if drop_y is not None:  # falling drop (teardrop)
        r = 4
        d.ellipse([80 - r, drop_y - r, 80 + r, drop_y + r], fill=ORANGE)
        d.polygon([(80 - r + 1, drop_y), (80 + r - 1, drop_y),
                   (80, drop_y - r - 5)], fill=ORANGE)
        d.ellipse([78, drop_y - 3, 80, drop_y - 1], fill=ORANGE_HI)

    # pool
    d.rectangle([0, pool_y, 159, 79], fill=(150, 70, 8))
    d.line([0, pool_y, 159, pool_y], fill=ORANGE, width=2)
    if ripple is not None:
        rw = int(6 + 26 * ripple)
        col = ORANGE_HI if ripple < 0.5 else ORANGE
        d.ellipse([80 - rw, pool_y - 3, 80 + rw, pool_y + 3], outline=col, width=1)
        if ripple < 0.4:  # splash sparks
            d.ellipse([73 - 1, pool_y - 7, 73 + 1, pool_y - 5], fill=ORANGE_HI)
            d.ellipse([87 - 1, pool_y - 8, 87 + 1, pool_y - 6], fill=ORANGE_HI)
    return img


def build_drip():
    fr = []
    pool = 74
    for cycle in range(2):
        # drop forms and swells (3 frames)
        for hr, st in ((2, 0), (3, 2), (4, 4)):
            fr.append(drip_frame(None, st, pool, None, hr))
        # falls (accelerating)
        y, v = 22, 6
        while y < pool - 4:
            fr.append(drip_frame(y, 0, pool, None, 0))
            v += 4
            y += v
        # splash + pool rises
        pool -= 4
        for rp in (0.2, 0.55, 0.9):
            fr.append(drip_frame(None, 0, pool, rp, 0))
    for _ in range(3):  # calm full pool
        fr.append(drip_frame(None, 0, pool, None, 0))
    return fr


if __name__ == '__main__':
    b = build_bunny()
    save_tmb(b, os.path.join(OUT, 'bunny.tmb'))
    save_gif(b, os.path.join(OUT, 'bunny-preview.gif'))
    d = build_drip()
    save_tmb(d, os.path.join(OUT, 'resin-drip.tmb'))
    save_gif(d, os.path.join(OUT, 'resin-drip-preview.gif'))
