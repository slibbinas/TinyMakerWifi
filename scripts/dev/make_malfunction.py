"""'Malfunction' prank boot animation (TMB1 160x80 @15fps, ~7s):
calm boot -> glitches -> static -> red MALFUNCTION -> fake error dump ->
jumpscare (hollow-eyed mascot, chromatic aberration, shake) -> 'gotcha ;)'.
"""
from PIL import Image, ImageDraw, ImageFont, ImageChops
import struct, os, random

W, H, FPS = 160, 80, 15
OUT = os.path.dirname(os.path.abspath(__file__))
INK = (235, 235, 235)
RED = (255, 40, 30)
random.seed(66)

try:
    F_BIG = ImageFont.truetype(r'C:\Windows\Fonts\arialbd.ttf', 20)
    F_MED = ImageFont.truetype(r'C:\Windows\Fonts\consola.ttf', 11)
    F_SM = ImageFont.truetype(r'C:\Windows\Fonts\consola.ttf', 9)
except OSError:
    F_BIG = F_MED = F_SM = ImageFont.load_default()


def new():
    img = Image.new('RGB', (W, H), (0, 0, 0))
    return img, ImageDraw.Draw(img)


def rgb565(img):
    out = bytearray(W * H * 2)
    i = 0
    for r, g, b in img.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[i] = v & 0xFF; out[i + 1] = v >> 8; i += 2
    return bytes(out)


def save_tmb(frames, path):
    with open(path, 'wb') as f:
        f.write(b'TMB1' + struct.pack('<HHHH', W, H, len(frames), FPS))
        for fr in frames: f.write(rgb565(fr))
    print(f"{os.path.basename(path)}: {len(frames)} fr, "
          f"{os.path.getsize(path)} B, {len(frames)/FPS:.1f}s")


def boot_text(glitch=0):
    img, d = new()
    d.text((28, 28), 'TinyMaker', font=F_BIG, fill=INK)
    d.text((60, 54), 'booting...', font=F_SM, fill=(120, 120, 120))
    if glitch:  # horizontal slice tears
        for _ in range(glitch):
            y = random.randint(10, 70); hgt = random.randint(2, 6)
            dx = random.choice((-8, -5, 5, 9))
            band = img.crop((0, y, W, y + hgt))
            img.paste(band, (dx, y))
        d2 = ImageDraw.Draw(img)
        d2.text((28 + 2, 28), 'TinyMaker', font=F_BIG, fill=(255, 0, 0))
        d2.text((28, 28), 'TinyMaker', font=F_BIG, fill=(0, 255, 255))
    return img


def static_noise(density=0.5):
    img = Image.new('RGB', (W, H))
    px = [(random.randint(0, 255),) * 3 if random.random() < density else (0, 0, 0)
          for _ in range(W * H)]
    img.putdata(px)
    return img


def malfunction_flash(on):
    img, d = new()
    if on:
        d.rectangle([0, 0, W, H], fill=(120, 0, 0))
        d.text((8, 26), 'MALFUNCTION', font=F_BIG, fill=(255, 230, 230))
    else:
        d.text((8, 26), 'MALFUNCTION', font=F_BIG, fill=RED)
    return img


ERRS = ['ERR 0xDE4D MOTOR OVERRIDE', 'UV LED: UNCONTROLLED',
        'TEMP 214C  >> RISING <<', 'VAT BREACH DETECTED',
        'Z-AXIS: NO RESPONSE', 'FIRMWARE CORRUPTED']


def error_dump(nlines, flicker=False):
    img, d = new()
    for i in range(min(nlines, len(ERRS))):
        col = RED if (not flicker or random.random() > 0.3) else (90, 0, 0)
        d.text((4, 2 + i * 12), ERRS[i], font=F_MED, fill=col)
    if flicker and random.random() > 0.5:
        y = random.randint(0, 70)
        d.rectangle([0, y, W, y + 3], fill=(60, 0, 0))
    return img


def evil_bunny(shake=0, aberr=2):
    base = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(base)
    lw = 2
    d.rounded_rectangle([58, 2, 102, 8], radius=2, outline=200, width=lw)
    d.rounded_rectangle([63, 8, 97, 20], radius=2, outline=200, width=lw)
    d.line([56, 22, 104, 22], fill=200, width=lw)
    d.line([48, 79, 48, 46], fill=200, width=lw)
    d.line([48, 46, 64, 26], fill=200, width=lw)
    d.line([64, 26, 96, 26], fill=200, width=lw)
    d.line([96, 26, 112, 46], fill=200, width=lw)
    d.line([112, 46, 112, 79], fill=200, width=lw)
    for y in (52, 58, 64):  # drooping whiskers
        d.line([26, y + 6, 44, y - 1], fill=200, width=1)
        d.line([116, y - 1, 134, y + 6], fill=200, width=1)
    for cx in (67, 93):  # hollow eyes
        d.ellipse([cx - 13, 39, cx + 13, 65], outline=230, width=lw)
        d.ellipse([cx - 11, 41, cx + 11, 63], fill=15)
    # jagged mouth
    pts = [(64, 68), (69, 72), (74, 68), (80, 73), (86, 68), (91, 72), (96, 68)]
    d.line(pts, fill=230, width=2)
    # chromatic aberration: split channels
    r = ImageChops.offset(base, -aberr, 0)
    g = base
    b = ImageChops.offset(base, aberr, 0)
    img = Image.merge('RGB', (r, g, b))
    if shake:
        img = ImageChops.offset(img, random.randint(-shake, shake),
                                random.randint(-shake, shake))
    d2 = ImageDraw.Draw(img)
    for cx in (67, 93):  # red pupils, unaffected by the split
        d2.ellipse([cx - 3, 49, cx + 3, 55], fill=(255, 0, 0))
    return img


def gotcha():
    img, d = new()
    d.text((96, 64), 'gotcha ;)', font=F_SM, fill=(90, 90, 90))
    return img


frames = []
A = frames.append
for _ in range(7): A(boot_text())             # calm boot ~0.5s
A(boot_text(glitch=2))                        # tick
for _ in range(3): A(boot_text())
A(boot_text(glitch=3)); A(boot_text(glitch=4))
for _ in range(3): A(static_noise(0.45))      # static burst
for i in range(6): A(malfunction_flash(i % 2 == 0))
for i in range(1, 7): A(error_dump(i))        # errors type in
for _ in range(5): A(error_dump(6, flicker=True))
for _ in range(2): A(static_noise(0.6))
for _ in range(2): A(static_noise(0.9))
for _ in range(8): A(evil_bunny(shake=3, aberr=3))   # JUMPSCARE ~0.5s
for _ in range(2): A(evil_bunny(shake=1, aberr=2))
for _ in range(3): A(new()[0])                # dead black
for _ in range(6): A(gotcha())

save_tmb(frames, os.path.join(OUT, 'malfunction.tmb'))
big = [f.resize((W * 3, H * 3), Image.NEAREST) for f in frames]
big[0].save(os.path.join(OUT, 'malfunction-preview.gif'), save_all=True,
            append_images=big[1:], duration=int(1000 / FPS), loop=0)
print('gif ok')
