# -*- coding: utf-8 -*-
"""TinyMaker Resin Lab - vietinis serveris dervų testams.

Kodėl serveris, o ne vien puslapis: naršyklė negali pati rašyti į diską (Google
Drive aplanką), paleisti PrusaSlicer'io ar kalbėti su printeriu - printeris priima
rašymą tik iš savo pulto arba iš programos su mūsų antrašte X-TinyMaker, ne iš
svetimo puslapio (Network.ino, requestFromOwnUi).

Tik Python standartinė biblioteka - nieko diegti nereikia. Paleidimas:

    ~/.platformio/penv/Scripts/python.exe scripts/dev/resin-lab/server.py

ir naršyklėje http://localhost:8897/resin-lab/ . Kasdien paleidžia darbastalio
nuoroda (memory kataloge resin-lab.vbs).

Serveris klauso tik 127.0.0.1: kitas tinklo įrenginys prie duomenų neprieina.
Statinius failus jis dalija iš scripts/dev, tad tame pačiame adrese gyvena ir
resin-publish.html - abu puslapiai mato tą pačią naršyklės atmintį.

Asmeniniai nustatymai - resin-lab/local.json (.gitignore):
  data_dir    dervų įrašai ir nuotraukos          (numatyta: Drive .../Dervoms/Testai)
  models_dir  testiniai STL                       (numatyta: Drive .../00 STLs Tests)
  printer     printerio adresas, pvz. 192.168.1.138
  prusaslicer prusa-slicer-console.exe kelias
"""
import hashlib, io, json, os, re, shutil, struct, subprocess, sys, threading, time, uuid
import urllib.error, urllib.parse, urllib.request
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, unquote

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(DEV))
LOCAL = os.environ.get("RESIN_LAB_LOCAL") or os.path.join(HERE, "local.json")   # kitas failas - tik bandymams
PORT = int(os.environ.get("RESIN_LAB_PORT", "8897"))   # kitas prievadas - tik bandymams, kai tikrasis jau veikia
HOME = os.path.expanduser("~")
DEFAULT_DATA = os.path.join(HOME, "My Drive", "3Dprinter", "20 TinyMakerWifi", "Dervoms", "Testai")
# Testiniai modeliai gyvena V aplanke, ne repo: dalis jų svetimi (AmeraLabs Town
# ir kt.), o viešoje repo jų platinti negalim.
DEFAULT_MODELS = os.path.join(HOME, "My Drive", "3Dprinter", "00_TinyMaker", "00 STLs Tests")
DEFAULT_PRUSA = r"C:/Program Files/Prusa3D/PrusaSlicer/prusa-slicer-console.exe"
PRINTER_INI = os.path.join(REPO, "PrusaSlicer", "TinyMaker.ini")
DEFAULT_OPENSCAD = r"C:/Users/SViktoras/Tools/OpenSCAD/openscad.com"
PLATE = (40.8, 30.6, 60.0)        # PrusaSlicer/TinyMaker.ini: ekranas ir aukštis
LAB_PROFILE = "lab-test"          # vienas laikinas profilis: printeris laiko daugiausia 16

SLUG = re.compile(r"^[a-z0-9][a-z0-9_-]{0,39}$")
PHOTO = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,80}\.(jpg|jpeg|png|webp)$", re.I)
HOST = re.compile(r"^[A-Za-z0-9.-]{1,64}(:\d{1,5})?$")
MAX_PHOTO = 25 * 1024 * 1024
MAX_JSON = 2 * 1024 * 1024
PARAM_KEYS = ("base_exposure", "regular_exposure", "base_layers", "transition_layers",
              "slow_lift_distance", "fast_lift_distance", "slow_lift_feedrate",
              "fast_lift_feedrate", "drop_back_feedrate", "density", "cal_factor", "cal_fixed_ml")
# /api/resin-profile rašo camelCase; įrankis ir profilio failai - snake_case.
FROM_PRINTER = {"baseExposure": "base_exposure", "regularExposure": "regular_exposure",
                "baseLayers": "base_layers", "transitionLayers": "transition_layers",
                "slowLiftDistance": "slow_lift_distance", "fastLiftDistance": "fast_lift_distance",
                "slowLiftFeedrate": "slow_lift_feedrate", "fastLiftFeedrate": "fast_lift_feedrate",
                "dropBackFeedrate": "drop_back_feedrate", "density": "density",
                "calFactor": "cal_factor", "calFixedMl": "cal_fixed_ml", "layerHeight": "layer_height"}


class LabError(Exception):
    """Klaida su kodu, kurį puslapis išverčia į pasirinktą kalbą; detail - tekstas iš šaltinio."""
    def __init__(self, code, detail="", http=400):
        super().__init__(code)
        self.code, self.detail, self.http = code, detail, http


# ---- nustatymai --------------------------------------------------------------

def local_cfg():
    try:
        return json.load(io.open(LOCAL, encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def config():
    c = local_cfg()
    # RESIN_LAB_DATA - bandymams: kad testuojant įrankį į tikrą Drive aplanką
    # nepatektų netikros dervos.
    return {"data_dir": os.environ.get("RESIN_LAB_DATA") or c.get("data_dir") or DEFAULT_DATA,
            "models_dir": c.get("models_dir") or DEFAULT_MODELS,
            "printer": c.get("printer") or "",
            "prusaslicer": c.get("prusaslicer") or DEFAULT_PRUSA,
            "openscad": c.get("openscad") or os.environ.get("OPENSCAD") or DEFAULT_OPENSCAD}


def data_dir():
    return config()["data_dir"]


def models_dir():
    return config()["models_dir"]


def write_atomic(path, data):
    """Per laikiną failą: nutrūkęs įrašas nepalieka pusės failo Drive aplanke."""
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    write_atomic(path, (json.dumps(obj, ensure_ascii=False, indent=1) + "\n").encode("utf-8"))


def read_json(path, default):
    try:
        return json.load(io.open(path, encoding="utf-8"))
    except (OSError, ValueError):
        return default


# ---- modeliai ----------------------------------------------------------------

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
            for v in struct.iter_unpack("<12x9f2x", f.read()):
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
    a = sorted(size)   # ar tilptų kitaip paguldytas - tik užuomina žmogui
    fits_any = a[2] <= PLATE[2] and a[1] <= PLATE[0] and a[0] <= PLATE[1]
    info = {"size": size, "tri": n, "fits": fits, "fits_any": fits_any}
    _stl_cache[key] = info
    return info


def modelmap_path():
    return os.path.join(data_dir(), "_modeliai.json")


def modelmap():
    m = read_json(modelmap_path(), {})
    m.setdefault("assign", {}); m.setdefault("notes", {}); m.setdefault("supports", {})
    return m


def lh_tag(lh):
    return "005" if float(lh) < 0.075 else "010"


def sliced_path(rel, lh, supports):
    stem = os.path.splitext(rel.replace("/", "__"))[0]
    return os.path.join(models_dir(), "_sliced", "%s_%s%s.sl1" % (stem, lh_tag(lh), "_s" if supports else ""))


def sliced_ok(rel, lh, supports):
    f, src = sliced_path(rel, lh, supports), model_file(rel)
    return bool(src) and os.path.isfile(f) and os.path.getmtime(f) >= os.path.getmtime(src)


def list_models():
    root = models_dir()
    out = []
    if not os.path.isdir(root):
        return out
    sup = modelmap()["supports"]
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if not x.startswith("_")]      # _sliced ir pan.
        for fn in sorted(files):
            if not fn.lower().endswith(".stl"):
                continue
            full = os.path.join(d, fn)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            try:
                info = stl_info(full)
            except (OSError, struct.error, ValueError):
                info = {"broken": True}
            s = bool(sup.get(rel))
            info = dict(info, file=rel, supports=s,
                        sliced={"0.05": sliced_ok(rel, 0.05, s), "0.1": sliced_ok(rel, 0.1, s)})
            out.append(info)
    return out


_thumb_lock = threading.Lock()


def thumb_for(rel):
    """PNG peržiūra per OpenSCAD, kešuojama duomenų aplanke. Nėra OpenSCAD -
    nėra peržiūros, bet visa kita veikia."""
    f = model_file(rel)
    openscad = config()["openscad"]
    if not f or not os.path.isfile(openscad):
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
            subprocess.run([openscad, "-o", png, "--imgsize=480,360", "--viewall", "--autocenter",
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


def slice_model(rel, lh, supports):
    """STL -> SL1 per PrusaSlicer su mūsų TinyMaker.ini. Karpoma vieną kartą kiekvienam
    sluoksnio aukščiui: printeris iš SL1 ima tik aukštį (Import.ino, config.ini
    layerHeight), o ekspoziciją - iš dervos profilio, tad tas pats failas tinka
    visoms dervoms ir jų bandymai lieka palyginami."""
    src = model_file(rel)
    if not src:
        raise LabError("no_model", rel, 404)
    out = sliced_path(rel, lh, supports)
    if sliced_ok(rel, lh, supports):
        return out
    prusa = config()["prusaslicer"]
    if not os.path.isfile(prusa):
        raise LabError("slicer_missing", prusa, 500)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tmp = out + ".tmp.sl1"
    cmd = [prusa, "--export-sla", "--load", PRINTER_INI, "--layer-height", "%.2f" % float(lh)]
    cmd += ["--supports-enable", "--pad-enable"] if supports else ["--no-supports-enable", "--no-pad-enable"]
    cmd += ["--output", tmp, src]
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=600)
    except (subprocess.SubprocessError, OSError) as e:
        raise LabError("slice_failed", str(e), 500)
    if r.returncode != 0 or not os.path.isfile(tmp) or os.path.getsize(tmp) < 1000:
        raise LabError("slice_failed", (r.stdout + r.stderr).decode("utf-8", "replace")[-400:], 500)
    os.replace(tmp, out)
    return out


def printer_model_name(rel, lh, supports):
    """Pastovus vardas kortelėje; printerio safeModelName leidžia raides, skaitmenis, - ir _ iki 40."""
    stem = re.sub(r"[^A-Za-z0-9_-]+", "-", os.path.splitext(os.path.basename(rel))[0]).strip("-")[:26]
    return "Lab-%s-%s%s" % (stem or "model", lh_tag(lh), "s" if supports else "")


# ---- printeris ----------------------------------------------------------------

def printer_host():
    h = config()["printer"]
    if not h:
        raise LabError("printer_unset", "", 409)
    return h


def _multipart(field, filename, data):
    b = "----resinlab" + uuid.uuid4().hex
    head = ('--%s\r\nContent-Disposition: form-data; name="%s"; filename="%s"\r\n'
            'Content-Type: application/octet-stream\r\n\r\n' % (b, field, filename)).encode()
    return head + data + ("\r\n--%s--\r\n" % b).encode(), "multipart/form-data; boundary=" + b


def pr(method, path, fields=None, upload=None, timeout=15):
    """Kreipinys į printerį su mūsų antrašte. Grąžina JSON; printerio klaidą paverčia LabError."""
    url = "http://%s%s" % (printer_host(), path)
    headers = {"X-TinyMaker": "1"}
    body = None
    if upload:
        body, ctype = _multipart("file", upload[0], upload[1])
        headers["Content-Type"] = ctype
    elif fields is not None:
        body = urllib.parse.urlencode(fields).encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    req = urllib.request.Request(url, data=body, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read()
    except urllib.error.HTTPError as e:
        raw = e.read()
        try:
            msg = json.loads(raw.decode("utf-8", "replace")).get("error", "")
        except ValueError:
            msg = raw.decode("utf-8", "replace")[:200]
        if e.code == 403 and "web control" in msg:
            raise LabError("web_control_off", msg, 409)
        if e.code == 409 and "busy" in msg:
            raise LabError("printer_busy", msg, 409)
        raise LabError("printer_http", "%d %s" % (e.code, msg), 502)
    except (urllib.error.URLError, OSError) as e:
        raise LabError("printer_unreachable", str(getattr(e, "reason", e)), 502)
    try:
        return json.loads(raw.decode("utf-8", "replace"))
    except ValueError:
        return {}


def lab_state_path():
    return os.path.join(data_dir(), "_printeris.json")


def printer_status():
    s = pr("GET", "/api/status", timeout=5)
    keep = ("busy", "paused", "state", "stateCode", "model", "layerText", "currentLayer", "totalLayers",
            "remainingSecs", "resinUsedMl", "runSecs", "resinSet", "fwVersion", "version")
    out = {k: s[k] for k in keep if k in s}
    out["prev"] = read_json(lab_state_path(), {}).get("prev", "")
    return out


def printer_current():
    """Pasirinkto profilio reikšmės formos laukams (tik kai printeris laisvas)."""
    lst = pr("GET", "/api/resin-profile", timeout=8)
    sel = lst.get("selected", "")
    for p in lst.get("profiles", []):
        if p.get("name") == sel:
            vals = {FROM_PRINTER[k]: v for k, v in p.items() if k in FROM_PRINTER}
            return {"name": sel, "display": p.get("display", ""), "values": vals}
    raise LabError("no_profile", sel, 404)


def profile_fields(params, lh):
    f = {k: params[k] for k in PARAM_KEYS if k in params}
    f["layer_height"] = "%.2f" % float(lh)
    return f


# ---- ilgi darbai (karpymas, įkėlimas) ----------------------------------------
# Vienas darbas vienu metu: printeris vienas, o du lygiagretūs įkėlimai jį tik užkimštų.

_job = {"id": "", "kind": "", "stage": "", "done": True, "error": None, "detail": "", "result": None}
_job_lock = threading.Lock()


def job_start(kind, fn):
    with _job_lock:
        if not _job["done"]:
            raise LabError("job_running", _job["kind"], 409)
        _job.update(id=uuid.uuid4().hex[:8], kind=kind, stage="", done=False, error=None, detail="", result=None)
        jid = _job["id"]

    def run():
        try:
            res = fn(lambda st: _job.update(stage=st))
            _job.update(result=res)
        except LabError as e:
            _job.update(error=e.code, detail=e.detail)
        except Exception as e:           # netikėta klaida turi pasiekti žmogų, ne tik konsolę
            _job.update(error="internal", detail=repr(e))
        finally:
            _job.update(done=True)
    threading.Thread(target=run, daemon=True).start()
    return jid


def run_test(req, stage):
    """Bandymo paleidimas: sukarpyti, įrašyti parametrus į laikiną profilį, jį pasirinkti,
    įkelti failą ir paleisti. Prieš tai pasirinktas profilis įsimenamas, kad po testo
    būtų galima grįžti ir kitas spaudinys neišeitų su testo ekspozicija."""
    rel, lh = req["model"], float(req["lh"])
    sup = bool(modelmap()["supports"].get(rel))
    stage("slice")
    sl1 = slice_model(rel, lh, sup)
    stage("check")
    st = pr("GET", "/api/status", timeout=6)
    if st.get("busy"):
        raise LabError("printer_busy", st.get("state", ""), 409)
    stage("profile")
    cur = pr("GET", "/api/resin-profile", timeout=8).get("selected", "")
    state = read_json(lab_state_path(), {})
    if cur and cur != LAB_PROFILE:
        state["prev"] = cur
        write_json(lab_state_path(), state)
    fields = profile_fields(req["params"], lh)
    fields.update(name=LAB_PROFILE, display=("Lab: %s" % req.get("label", ""))[:40])
    pr("POST", "/api/resin-profile/save", fields)
    pr("POST", "/api/resin-profile/select", {"name": LAB_PROFILE})
    stage("upload")
    name = printer_model_name(rel, lh, sup)
    pr("POST", "/api/files/local", upload=(name + ".sl1", open(sl1, "rb").read()), timeout=900)
    stage("start")
    return start_print(name, bool(req.get("force")))


def start_print(name, force):
    f = {"name": name}
    if force:
        f["force"] = "1"
    r = pr("POST", "/api/print/start", f)
    if r.get("warning"):
        return {"name": name, "warning": r.get("warning"), "detail": r}
    return {"name": name, "started": True}


# ---- dervos ------------------------------------------------------------------

def resin_path(slug):
    return os.path.join(data_dir(), slug, "resin.json")


def is_published(r):
    return r.get("status") == "paskelbta" or bool(r.get("library"))


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


# ---- nustatymai: keliai, kopijavimas, aplanko atidarymas ----------------------

DIR_KEYS = ("data_dir", "models_dir")
EXE_KEYS = ("prusaslicer", "openscad")
DEFAULTS = {"data_dir": DEFAULT_DATA, "models_dir": DEFAULT_MODELS, "prusaslicer": DEFAULT_PRUSA,
            "openscad": DEFAULT_OPENSCAD, "printer": ""}


def settings_view():
    c = config()
    out = {"env_data": bool(os.environ.get("RESIN_LAB_DATA")), "local_file": LOCAL, "defaults": DEFAULTS}
    for k in DIR_KEYS:
        out[k] = {"value": c[k], "exists": os.path.isdir(c[k])}
    for k in EXE_KEYS:
        out[k] = {"value": c[k], "exists": os.path.isfile(c[k])}
    out["printer"] = {"value": c["printer"]}
    return out


def _norm(path):
    return os.path.normcase(os.path.realpath(path))


def nested(a, b):
    """Ar vienas aplankas kito viduje (arba tas pats) - tada kopijuoti negalima."""
    a, b = _norm(a), _norm(b)
    return a == b or a.startswith(b + os.sep) or b.startswith(a + os.sep)


def copy_tree(src, dst, stage, label):
    """Kopija, ne perkėlimas: senas aplankas lieka, kol žmogus jo neištrina pats.
    Jau esantys tie patys failai nepakeičiami, jei naujesni (tik papildoma)."""
    n = 0
    for d, dirs, files in os.walk(src):
        rel = os.path.relpath(d, src)
        tgt = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(tgt, exist_ok=True)
        for fn in files:
            if fn.endswith(".tmp"):
                continue
            a, b = os.path.join(d, fn), os.path.join(tgt, fn)
            if os.path.exists(b) and os.path.getmtime(b) >= os.path.getmtime(a):
                continue
            shutil.copy2(a, b)
            n += 1
            if n % 20 == 0:
                stage("%s %d" % (label, n))
    return n


def _clean(v):
    """Windows „Kopijuoti kaip kelią" įdeda kelią į kabutes - jos ne kelio dalis."""
    return str(v or "").strip().strip('"').strip()


def apply_settings(req, stage):
    """Tikrina, prireikus kopijuoja duomenis ir tik tada įrašo local.json: jei kopija
    nepavyksta, įrankis lieka dirbti su senu aplanku ir niekas nepasimeta."""
    cur = config()
    new = dict(local_cfg())
    copied = {}
    for k in DIR_KEYS:
        v = _clean(req.get(k))
        if not v or _norm(v) == _norm(DEFAULTS[k]):
            v = ""
        path = v or DEFAULTS[k]
        if not os.path.isabs(path):
            raise LabError("bad_path", path)
        if _norm(path) != _norm(cur[k]) and req.get("copy_" + k) and os.path.isdir(cur[k]):
            if nested(path, cur[k]):
                raise LabError("nested_path", path)
            stage("copy_" + k)
            copied[k] = copy_tree(cur[k], path, stage, "copy_" + k)
        os.makedirs(path, exist_ok=True)
        new[k] = v
    for k in EXE_KEYS:
        v = _clean(req.get(k))
        if v and not os.path.isfile(v):
            raise LabError("no_file", v)
        new[k] = "" if not v or _norm(v) == _norm(DEFAULTS[k]) else v
    host = str(req.get("printer", "")).strip()
    if host and not HOST.match(host):
        raise LabError("bad_host", host)
    new["printer"] = host
    new = {k: v for k, v in new.items() if v}
    write_atomic(LOCAL, (json.dumps(new, ensure_ascii=False, indent=1) + "\n").encode("utf-8"))
    return {"ok": True, "copied": copied}


def open_folder(what):
    """Atidaro aplanką Windows naršyklėje. Tik žinomi keliai - ne bet koks iš užklausos."""
    c = config()
    path = {"data_dir": c["data_dir"], "models_dir": c["models_dir"],
            "sliced": os.path.join(c["models_dir"], "_sliced"),
            "prusaslicer": os.path.dirname(c["prusaslicer"]), "openscad": os.path.dirname(c["openscad"]),
            "local": os.path.dirname(LOCAL)}.get(what)
    if not path:
        raise LabError("bad_json", what)
    if not os.path.isdir(path):
        raise LabError("no_folder", path, 404)
    os.startfile(path)
    return {"ok": True, "path": path}


# ---- HTTP --------------------------------------------------------------------

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

    def send_file(self, path, ctype, download=None):
        data = open(path, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        if download:
            self.send_header("Content-Disposition", 'attachment; filename="%s"' % download)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def fail(self, code, key, detail=""):
        self.send_json({"error": key, "detail": detail}, code)

    def body(self, limit):
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0 or n > limit:
            return None
        return self.rfile.read(n)

    def json_body(self):
        raw = self.body(MAX_JSON)
        try:
            obj = json.loads((raw or b"").decode("utf-8"))
        except ValueError as e:
            raise LabError("bad_json", str(e))
        if not isinstance(obj, dict):
            raise LabError("bad_json")
        return obj

    # Rašymą priimam tik iš savo puslapio: kitaip bet kuri naršyklėje atidaryta
    # svetainė galėtų tyliai rašyti į Drive aplanką ar valdyti printerį per localhost.
    def own_origin(self):
        o = self.headers.get("Origin")
        return o is None or o in ("http://localhost:%d" % PORT, "http://127.0.0.1:%d" % PORT)

    def handle_lab(self, method):
        try:
            if method != "GET" and not self.own_origin():
                raise LabError("foreign_origin", "", 403)
            return (self.get if method == "GET" else self.put if method == "PUT" else self.post)()
        except LabError as e:
            return self.fail(e.http, e.code, e.detail)

    def do_GET(self):
        return self.handle_lab("GET")

    def do_PUT(self):
        return self.handle_lab("PUT")

    def do_POST(self):
        return self.handle_lab("POST")

    def get(self):
        p = unquote(urlparse(self.path).path)
        q = dict(urllib.parse.parse_qsl(urlparse(self.path).query))
        if p == "/api/lab/config":
            c = config()
            return self.send_json({"data_dir": c["data_dir"], "data_ok": os.path.isdir(os.path.dirname(c["data_dir"])),
                                   "models_dir": c["models_dir"], "models_ok": os.path.isdir(c["models_dir"]),
                                   "printer": c["printer"], "slicer_ok": os.path.isfile(c["prusaslicer"])})
        if p == "/api/lab/catalog":
            return self.send_json(read_json(os.path.join(HERE, "catalog.json"), {}))
        if p == "/api/lab/resins":
            return self.send_json(list_resins())
        if p == "/api/lab/models":
            return self.send_json({"dir": models_dir(), "plate": PLATE, "models": list_models()})
        if p == "/api/lab/modelmap":
            return self.send_json(modelmap())
        if p == "/api/lab/settings":
            return self.send_json(settings_view())
        if p == "/api/lab/job":
            return self.send_json(dict(_job))
        if p == "/api/lab/printer":
            return self.send_json(printer_status())
        if p == "/api/lab/printer/current":
            return self.send_json(printer_current())
        if p.startswith("/lib/"):
            f = model_file(p[5:])
            if not f:
                raise LabError("not_found", "", 404)
            return self.send_file(f, "model/stl", os.path.basename(f))
        if p.startswith("/thumb/"):
            f = thumb_for(p[7:])
            if not f:
                raise LabError("not_found", "", 404)
            return self.send_file(f, "image/png")
        if p.startswith("/sliced/"):
            rel = p[8:]
            if not model_file(rel):
                raise LabError("not_found", "", 404)
            s = bool(modelmap()["supports"].get(rel))
            f = sliced_path(rel, float(q.get("lh", "0.05")), s)
            if not os.path.isfile(f):
                raise LabError("not_found", "", 404)
            return self.send_file(f, "application/zip", printer_model_name(rel, float(q.get("lh", "0.05")), s) + ".sl1")
        m = re.match(r"^/api/lab/resin/([^/]+)$", p)
        if m:
            slug = m.group(1)
            if not SLUG.match(slug) or not os.path.isfile(resin_path(slug)):
                raise LabError("not_found", slug, 404)
            return self.send_json(read_json(resin_path(slug), {}))
        m = re.match(r"^/data/([^/]+)/photos/([^/]+)$", p)
        if m:
            slug, name = m.group(1), m.group(2)
            f = os.path.join(data_dir(), slug, "photos", name)
            if not SLUG.match(slug) or not PHOTO.match(name) or not os.path.isfile(f):
                raise LabError("not_found", "", 404)
            ext = name.rsplit(".", 1)[1].lower()
            return self.send_file(f, {"jpg": "image/jpeg", "jpeg": "image/jpeg", "png": "image/png", "webp": "image/webp"}[ext])
        return super().do_GET()

    def put(self):
        p = unquote(urlparse(self.path).path)
        if p == "/api/lab/modelmap":
            obj = self.json_body()
            if not isinstance(obj.get("assign"), dict):
                raise LabError("bad_json", "assign")
            write_json(modelmap_path(), obj)
            return self.send_json({"ok": True})
        if p == "/api/lab/settings":
            req = self.json_body()
            # Kopijavimas gali užtrukti (nuotraukos Drive'e) - tada tai ilgas darbas su eiga.
            if any(req.get("copy_" + k) for k in DIR_KEYS):
                return self.send_json({"job": job_start("settings", lambda st: apply_settings(req, st))})
            return self.send_json(apply_settings(req, lambda st: None))
        m = re.match(r"^/api/lab/resin/([^/]+)$", p)
        if not m or not SLUG.match(m.group(1)):
            raise LabError("bad_slug")
        slug = m.group(1)
        obj = self.json_body()
        if obj.get("slug") != slug:
            raise LabError("bad_slug")
        # Naujos dervos vardas negali užgožti esamos: „Nauja derva" su jau
        # užimtu vardu kitaip tyliai perrašytų kito butelio istoriją.
        if self.headers.get("X-Lab-New") == "1" and os.path.exists(resin_path(slug)):
            raise LabError("exists", slug, 409)
        os.makedirs(os.path.join(data_dir(), slug, "photos"), exist_ok=True)
        obj["updated"] = time.strftime("%Y-%m-%d %H:%M")
        write_json(resin_path(slug), obj)
        return self.send_json({"ok": True, "updated": obj["updated"]})

    def post(self):
        p = unquote(urlparse(self.path).path)
        if p == "/api/lab/slice":
            req = self.json_body()
            rel, lh = str(req.get("model", "")), float(req.get("lh", 0.05))
            if not model_file(rel):
                raise LabError("no_model", rel, 404)
            sup = bool(modelmap()["supports"].get(rel))
            return self.send_json({"job": job_start("slice", lambda st: (st("slice"), slice_model(rel, lh, sup))[1] and {"ok": True})})
        if p == "/api/lab/printer/run":
            req = self.json_body()
            if not model_file(str(req.get("model", ""))) or not isinstance(req.get("params"), dict):
                raise LabError("bad_json", "model/params")
            printer_host()
            return self.send_json({"job": job_start("run", lambda st: run_test(req, st))})
        if p == "/api/lab/open":
            return self.send_json(open_folder(str(self.json_body().get("what", ""))))
        if p == "/api/lab/printer/start":
            req = self.json_body()
            return self.send_json(start_print(str(req.get("name", "")), bool(req.get("force"))))
        if p == "/api/lab/printer/restore":
            state = read_json(lab_state_path(), {})
            prev = state.get("prev", "")
            if not prev:
                raise LabError("nothing_to_restore", "", 409)
            pr("POST", "/api/resin-profile/select", {"name": prev})
            state["prev"] = ""
            write_json(lab_state_path(), state)
            return self.send_json({"ok": True, "selected": prev})
        if p == "/api/lab/printer/install":
            req = self.json_body()
            slug = str(req.get("slug", ""))
            if not SLUG.match(slug) or slug == LAB_PROFILE or not isinstance(req.get("params"), dict):
                raise LabError("bad_slug", slug)
            f = profile_fields(req["params"], req.get("lh", 0.05))
            f.update(name=slug, display=str(req.get("display", ""))[:40],
                     tested_by=str(req.get("tested_by", ""))[:40], tested_on=str(req.get("tested_on", ""))[:20])
            pr("POST", "/api/resin-profile/save", f)
            return self.send_json({"ok": True, "name": slug})
        m = re.match(r"^/api/lab/resin-delete/([^/]+)$", p)
        if m:
            # Derva neištrinama, o perkeliama į _deleted su laiku vardo gale: nuotraukų ir
            # bandymų istorijos iš naujo nepadarysi, o per klaidą ištrintą atstatyti - vienas perkėlimas.
            slug = m.group(1)
            if not SLUG.match(slug) or not os.path.isfile(resin_path(slug)):
                raise LabError("not_found", slug, 404)
            # Paskelbta derva turi profilį bibliotekoje: ištrynus įrašą, liktų profilis be
            # savo testų istorijos. Pirma - išimti iš bibliotekos, tada trinti.
            if is_published(read_json(resin_path(slug), {})):
                raise LabError("published", slug, 409)
            dst = os.path.join(data_dir(), "_deleted", "%s-%s" % (slug, time.strftime("%Y%m%d-%H%M%S")))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            os.replace(os.path.join(data_dir(), slug), dst)
            return self.send_json({"ok": True, "moved_to": dst})
        m = re.match(r"^/api/lab/photo-delete/([^/]+)$", p)
        if m:
            # Ištrinto bandymo nuotraukos perkeliamos, ne naikinamos: netyčia ištrintą
            # bandymą galima atkurti ranka, o nuotraukų iš naujo nepadarysi.
            slug = m.group(1)
            if not SLUG.match(slug) or not os.path.isfile(resin_path(slug)):
                raise LabError("not_found", slug, 404)
            src = os.path.join(data_dir(), slug, "photos")
            dst = os.path.join(src, "_deleted")
            moved = 0
            for name in self.json_body().get("names", []):
                if PHOTO.match(str(name)) and os.path.isfile(os.path.join(src, name)):
                    os.makedirs(dst, exist_ok=True)
                    os.replace(os.path.join(src, name), os.path.join(dst, name))
                    moved += 1
            return self.send_json({"ok": True, "moved": moved})
        m = re.match(r"^/api/lab/photo/([^/]+)/([^/]+)$", p)
        if not m:
            raise LabError("not_found", p, 404)
        slug, want = m.group(1), m.group(2)
        if not SLUG.match(slug) or not os.path.isfile(resin_path(slug)):
            raise LabError("not_found", slug, 404)
        if not PHOTO.match(want):
            raise LabError("bad_photo", want)
        raw = self.body(MAX_PHOTO)
        if raw is None:
            raise LabError("too_big")
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
    print("TinyMaker Resin Lab: http://localhost:%d/resin-lab/  (duomenys: %s)" % (PORT, data_dir()))
    srv.serve_forever()


if __name__ == "__main__":
    main()
