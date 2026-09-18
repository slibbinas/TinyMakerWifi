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
import io, json, os, re, sys, time
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, unquote

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)
LOCAL = os.path.join(HERE, "local.json")          # .gitignore: asmeniniai keliai
PORT = 8897
DEFAULT_DATA = os.path.join(os.path.expanduser("~"), "My Drive", "3Dprinter",
                            "20 TinyMakerWifi", "Dervoms", "Testai")

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
    return {"data_dir": os.environ.get("RESIN_LAB_DATA") or c.get("data_dir") or DEFAULT_DATA}


def data_dir():
    return config()["data_dir"]


def write_atomic(path, data):
    """Per laikiną failą: nutrūkęs įrašas nepalieka pusės JSON Drive aplanke."""
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


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
            return self.send_json({"data_dir": d, "data_ok": os.path.isdir(os.path.dirname(d))})
        if p == "/api/lab/catalog":
            return self.send_json(json.load(io.open(os.path.join(HERE, "catalog.json"), encoding="utf-8")))
        if p == "/api/lab/resins":
            return self.send_json(list_resins())
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
