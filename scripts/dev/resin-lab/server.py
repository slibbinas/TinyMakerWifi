# -*- coding: utf-8 -*-
"""Dervų testavimo įrankio serveris.

Kodėl serveris, o ne vien puslapis: naršyklė negali pati rašyti į diską
(Google Drive aplanką), o vėliau per jį eis ir kalba su printeriu - printeris
priima rašymą tik iš savo pulto arba iš programos su mūsų antrašte, ne iš
svetimo puslapio (Network.ino, requestFromOwnUi).

Tik Python standartinė biblioteka - nieko diegti nereikia. Paleidimas:

    ~/.platformio/penv/Scripts/python.exe scripts/dev/resin-lab/server.py

ir naršyklėje http://localhost:8897/resin-lab/ . Kasdien paleidžia darbastalio
nuoroda (memory kataloge resin-lab.vbs).

Serveris klauso tik 127.0.0.1: kitas tinklo įrenginys prie duomenų neprieina.
Statinius failus jis dalija iš scripts/dev, tad tame pačiame adrese gyvena ir
resin-publish.html - abu puslapiai mato tą pačią naršyklės atmintį.
"""
import hashlib, io, json, os, re, struct, subprocess, sys, threading, time
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, unquote

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)
LOCAL = os.path.join(HERE, "local.json")          # .gitignore: asmeniniai keliai
PORT = 8897
DEFAULT_DATA = os.path.join(os.path.expanduser("~"), "My Drive", "3Dprinter",
                            "20 TinyMakerWifi", "Dervoms", "Testai")
# Testiniai modeliai gyvena V aplanke, ne repo: dalis jų svetimi (AmeraLabs Town
# ir kt.), o viešoje repo jų platinti negalim.
DEFAULT_MODELS = os.path.join(os.path.expanduser("~"), "My Drive", "3Dprinter",
                              "00_TinyMaker", "00 STLs Tests")
OPENSCAD = os.environ.get("OPENSCAD", r"C:/Users/SViktoras/Tools/OpenSCAD/openscad.com")
PLATE = (40.8, 30.6, 60.0)        # PrusaSlicer/TinyMaker.ini: ekranas ir aukštis

SLUG = re.compile(r"^[a-z0-9][a-z0-9_-]{0,39}$")
PHOTO = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,80}\.(jpg|jpeg|png|webp)$", re.I)
MAX_PHOTO = 25 * 1024 * 1024
MAX_JSON = 2 * 1024 * 1024


def config():
    c = {}
    try:
        c = json.load(io.open(LOCAL, encoding="utf-8"))
    except (OSError, ValueError):
        pass
    # RESIN_LAB_DATA - bandymams: kad testuojant įrankį į tikrą Drive aplanką
    # nepatektų netikros dervos.
    return {"data_dir": os.environ.get("RESIN_LAB_DATA") or c.get("data_dir") or DEFAULT_DATA,
            "models_dir": c.get("models_dir") or DEFAULT_MODELS}


def data_dir():
    return config()["data_dir"]


def write_atomic(path, data):
    """Per laikiną failą: nutrūkęs įrašas nepalieka pusės JSON Drive aplanke."""
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def models_dir():
    return config()["models_dir"]


def model_file(rel):
    """Kelias modelių aplanke; niekada už jo ribų."""
    root = os.path.realpath(models_dir())
    f = os.path.realpath(os.path.join(root, rel))
    if not f.startswith(root + os.sep) or not f.lower().endswith(".stl") or not os.path.isfile(f):
        return None
    return f


_stl_cache = {}


def stl_info(path):
    """Gabaritai ir trikampių skaičius; ASCII ir dvejetainis STL. Kešuojama pagal
    dydį ir laiką - 15 MB failo perskaitymas užtrunka kelias sekundes, o sąrašas
    klausiamas kaskart atidarius įrankį."""
    st = os.stat(path)
    key = (path, st.st_size, st.st_mtime)
    if key in _stl_cache:
        return _stl_cache[key]
    lo, hi, n = [1e9] * 3, [-1e9] * 3, 0
    with open(path, "rb") as f:
        head = f.read(84)
        count = struct.unpack("<I", head[80:84])[0] if len(head) == 84 else -1
        if count >= 0 and 84 + 50 * count == st.st_size:
            n = count
            body = f.read()
            for v in struct.iter_unpack("<12x9f2x", body):
                for i in range(3):
                    a, b, c = v[i], v[3 + i], v[6 + i]
                    lo[i] = min(lo[i], a, b, c)
                    hi[i] = max(hi[i], a, b, c)
        else:
            f.seek(0)
            for line in f:
                line = line.strip()
                if line.startswith(b"vertex"):
                    q = line.split()
                    for i in range(3):
                        c = float(q[1 + i])
                        lo[i] = min(lo[i], c)
                        hi[i] = max(hi[i], c)
                elif line.startswith(b"endfacet"):
                    n += 1
    size = [round(hi[i] - lo[i], 1) for i in range(3)] if n else [0, 0, 0]
    x, y, z = size
    fits = max(x, y) <= PLATE[0] and min(x, y) <= PLATE[1] and z <= PLATE[2]
    # Ar telpa kitaip paguldžius (bet kuri ašis į viršų) - tik užuomina žmogui.
    a = sorted(size)
    fits_any = a[2] <= PLATE[2] and a[1] <= PLATE[0] and a[0] <= PLATE[1]
    info = {"size": size, "tri": n, "fits": fits, "fits_any": fits_any}
    _stl_cache[key] = info
    return info


def list_models():
    root = models_dir()
    out = []
    if not os.path.isdir(root):
        return out
    for d, _, files in os.walk(root):
        for fn in sorted(files):
            if not fn.lower().endswith(".stl"):
                continue
            full = os.path.join(d, fn)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            try:
                info = stl_info(full)
            except (OSError, struct.error, ValueError):
                info = {"broken": True}
            out.append(dict(info, file=rel))
    return out


_thumb_lock = threading.Lock()


def thumb_for(rel):
    """PNG peržiūra per OpenSCAD, kešuojama duomenų aplanke. Nėra OpenSCAD -
    nėra peržiūros, bet visa kita veikia."""
    f = model_file(rel)
    if not f or not os.path.isfile(OPENSCAD):
        return None
    st = os.stat(f)
    h = hashlib.sha1(("%s|%d|%d" % (rel, st.st_size, int(st.st_mtime))).encode("utf-8")).hexdigest()[:16]
    cache = os.path.join(data_dir(), "_thumbs")
    png = os.path.join(cache, h + ".png")
    if os.path.isfile(png):
        return png
    with _thumb_lock:
        if os.path.isfile(png):
            return png
        os.makedirs(cache, exist_ok=True)
        scad = os.path.join(cache, h + ".scad")
        with io.open(scad, "w", encoding="utf-8") as sf:
            sf.write('import("%s");\n' % f.replace("\\", "/"))
        try:
            subprocess.run([OPENSCAD, "-o", png, "--imgsize=320,240", "--viewall", "--autocenter",
                            "--colorscheme=Tomorrow", "--camera=0,0,0,55,0,25,0", scad],
                           check=True, capture_output=True, timeout=180)
        except (subprocess.SubprocessError, OSError):
            return None
        finally:
            try:
                os.remove(scad)
            except OSError:
                pass
    return png if os.path.isfile(png) else None


def modelmap_path():
    return os.path.join(data_dir(), "_modeliai.json")


def resin_path(slug):
    return os.path.join(data_dir(), slug, "resin.json")


def list_resins():
    out = []
    root = data_dir()
    if not os.path.isdir(root):
        return out
    for name in sorted(os.listdir(root)):
        p = os.path.join(root, name, "resin.json")
        if not SLUG.match(name) or not os.path.isfile(p):
            continue
        try:
            r = json.load(io.open(p, encoding="utf-8"))
        except ValueError:
            out.append({"slug": name, "broken": True})
            continue
        out.append({"slug": name, "maker": r.get("maker", ""), "name": r.get("name", ""),
                    "type": r.get("type", ""), "status": r.get("status", ""),
                    "tests": len(r.get("tests") or [])})
    return out


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=DEV, **kw)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    # Puslapiai nekešuojami: taisant įrankį naršyklė turi imti naują failą.
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def fail(self, code, msg):
        self.send_json({"error": msg}, code)

    def body(self, limit):
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0 or n > limit:
            return None
        return self.rfile.read(n)

    # Rašymą priimam tik iš savo puslapio: kitaip bet kuri naršyklėje atidaryta
    # svetainė galėtų tyliai rašyti į Drive aplanką per localhost.
    def own_origin(self):
        o = self.headers.get("Origin")
        return o is None or o in ("http://localhost:%d" % PORT, "http://127.0.0.1:%d" % PORT)

    def do_GET(self):
        u = urlparse(self.path)
        p = unquote(u.path)
        if p == "/api/lab/config":
            d = data_dir()
            return self.send_json({"data_dir": d, "data_ok": os.path.isdir(os.path.dirname(d)),
                                   "models_dir": models_dir(), "models_ok": os.path.isdir(models_dir())})
        if p == "/api/lab/catalog":
            return self.send_json(json.load(io.open(os.path.join(HERE, "catalog.json"), encoding="utf-8")))
        if p == "/api/lab/resins":
            return self.send_json(list_resins())
        if p == "/api/lab/models":
            return self.send_json({"dir": models_dir(), "plate": PLATE, "models": list_models()})
        if p == "/api/lab/modelmap":
            try:
                return self.send_json(json.load(io.open(modelmap_path(), encoding="utf-8")))
            except (OSError, ValueError):
                return self.send_json({"assign": {}, "notes": {}})
        if p.startswith("/lib/") or p.startswith("/thumb/"):
            rel = p.split("/", 2)[2]
            f = model_file(rel) if p.startswith("/lib/") else thumb_for(rel)
            if not f:
                return self.fail(404, "modelio nėra")
            data = open(f, "rb").read()
            self.send_response(200)
            if p.startswith("/lib/"):
                self.send_header("Content-Type", "model/stl")
                self.send_header("Content-Disposition", 'attachment; filename="%s"' % os.path.basename(f))
            else:
                self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        m = re.match(r"^/api/lab/resin/([^/]+)$", p)
        if m:
            slug = m.group(1)
            if not SLUG.match(slug) or not os.path.isfile(resin_path(slug)):
                return self.fail(404, "tokios dervos nėra")
            return self.send_json(json.load(io.open(resin_path(slug), encoding="utf-8")))
        m = re.match(r"^/data/([^/]+)/photos/([^/]+)$", p)
        if m:
            slug, name = m.group(1), m.group(2)
            f = os.path.join(data_dir(), slug, "photos", name)
            if not SLUG.match(slug) or not PHOTO.match(name) or not os.path.isfile(f):
                return self.fail(404, "nuotraukos nėra")
            ext = name.rsplit(".", 1)[1].lower()
            ctype = {"jpg": "image/jpeg", "jpeg": "image/jpeg", "png": "image/png", "webp": "image/webp"}[ext]
            data = open(f, "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        return super().do_GET()

    def do_PUT(self):
        if not self.own_origin():
            return self.fail(403, "svetimas puslapis")
        p = unquote(urlparse(self.path).path)
        if p == "/api/lab/modelmap":
            raw = self.body(MAX_JSON)
            try:
                obj = json.loads((raw or b"").decode("utf-8"))
            except ValueError as e:
                return self.fail(400, "ne JSON: %s" % e)
            if not isinstance(obj, dict) or not isinstance(obj.get("assign"), dict):
                return self.fail(400, "trūksta assign")
            os.makedirs(data_dir(), exist_ok=True)
            write_atomic(modelmap_path(), (json.dumps(obj, ensure_ascii=False, indent=1) + "\n").encode("utf-8"))
            return self.send_json({"ok": True})
        m = re.match(r"^/api/lab/resin/([^/]+)$", p)
        if not m or not SLUG.match(m.group(1)):
            return self.fail(400, "blogas dervos vardas")
        slug = m.group(1)
        raw = self.body(MAX_JSON)
        if raw is None:
            return self.fail(400, "tuščias arba per didelis įrašas")
        try:
            obj = json.loads(raw.decode("utf-8"))
        except ValueError as e:
            return self.fail(400, "ne JSON: %s" % e)
        if not isinstance(obj, dict) or obj.get("slug") != slug:
            return self.fail(400, "įrašo slug nesutampa su adresu")
        # Naujos dervos vardas negali užgožti esamos: „Nauja derva" su jau
        # užimtu vardu kitaip tyliai perrašytų kito butelio istoriją.
        if self.headers.get("X-Lab-New") == "1" and os.path.exists(resin_path(slug)):
            return self.fail(409, "tokiu vardu derva jau yra")
        os.makedirs(os.path.join(data_dir(), slug, "photos"), exist_ok=True)
        obj["updated"] = time.strftime("%Y-%m-%d %H:%M")
        write_atomic(resin_path(slug),
                     (json.dumps(obj, ensure_ascii=False, indent=1) + "\n").encode("utf-8"))
        return self.send_json({"ok": True, "updated": obj["updated"]})

    def do_POST(self):
        if not self.own_origin():
            return self.fail(403, "svetimas puslapis")
        p = unquote(urlparse(self.path).path)
        m = re.match(r"^/api/lab/photo/([^/]+)/([^/]+)$", p)
        if not m:
            return self.fail(404, "nėra tokio kelio")
        slug, want = m.group(1), m.group(2)
        if not SLUG.match(slug) or not os.path.isfile(resin_path(slug)):
            return self.fail(404, "tokios dervos nėra")
        if not PHOTO.match(want):
            return self.fail(400, "nuotrauka turi būti jpg, png arba webp")
        raw = self.body(MAX_PHOTO)
        if raw is None:
            return self.fail(400, "tuščia arba didesnė nei 25 MB")
        folder = os.path.join(data_dir(), slug, "photos")
        os.makedirs(folder, exist_ok=True)
        # Vardas niekada neperrašo esamos nuotraukos: tas pats telefono vardas
        # (IMG_0001.jpg) ateina iš skirtingų dienų.
        base, ext = want.rsplit(".", 1)
        name, i = want, 2
        while os.path.exists(os.path.join(folder, name)):
            name = "%s-%d.%s" % (base, i, ext)
            i += 1
        write_atomic(os.path.join(folder, name), raw)
        return self.send_json({"ok": True, "name": name})


def main():
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("Dervų testai: http://localhost:%d/resin-lab/  (duomenys: %s)" % (PORT, data_dir()))
    srv.serve_forever()


if __name__ == "__main__":
    main()
