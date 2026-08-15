# -*- coding: utf-8 -*-
"""3D perziura PULTO piesejo kodu (be jokiu savo isradimu).

Funkcijos ISTRAUKIAMOS is `web/dashboard.html` lygiai taip pat, kaip tai daro
skill'o `mk_stand2.py` — tas pats `grab`, ta pati eile, tie patys sImai. Mano
darbas cia tik vienas: paduoti `slicesCache` is DVIEJU failu (PrusaSlicer .sl1 ir
musu .zip), kad ta pati ranka nupiestu abu.

    python mk_pultas3d.py            -> C:/PIO-build/3d/pultas.html
"""
import base64
import io
import json
import os
import re
import sys
import zipfile

import numpy as np
from PIL import Image

DASH = 'C:/Users/SViktoras/Documents/PlatformIO/Projects/TinyMakerWiFi/web/dashboard.html'
LAB = 'C:/PIO-build/slicer-lab'
OUT = 'C:/PIO-build/3d/pultas.html'
BIN = 'C:/PIO-build/3d'
# Du tinkleliai: GREITAS sukiojimui (perskaiciuojamas kas kadra) ir DETALUS
# ziurejimui (pulto „Detailed" rezimas). V pastaba: 160x120 kokybe per zema.
DRAFT = (160, 120, 90)
PILNAS = (320, 240, 360)   # 0,157 mm auksciai (auditorius prase <=0,15)

src = io.open(DASH, encoding='utf-8').read()


def grab(decl):
    """Istraukia sakini nuo `decl` iki subalansuotu skliaustu pabaigos."""
    i = src.index(decl)
    j, depth, started = i, 0, False
    while j < len(src):
        c = src[j]
        if c == '{':
            depth += 1
            started = True
        elif c == '}':
            depth -= 1
            if started and depth == 0:
                k = src.find(';', j)
                # uz paskutinio } dar gali buti IIFE uodega `)();` — ja imam kartu
                uodega = src[j + 1:k] if k > j else 'x'
                return src[i:(k + 1 if 0 <= k <= j + 6 and set(uodega) <= set('()')
                             else j + 1)]
        j += 1
    raise SystemExit('nerasta pabaiga: ' + decl[:40])


parts = []
# EILE SVARBI (zr. skill: const'ai skaiciuojasi is karto).
for pat in (r'^const CAM_PITCH_DEG=.*$', r'^const CAM_PITCH=.*$',
            r'^const ISO_KY=.*$', r'^const ISO_Z =.*$',
            r'^const BOX_W_U=.*$', r'^const BOX_H_U=.*$', r'^const BOX_CY_U=.*$',
            r'^let CAM_C=.*$', r'^let PREV_W=.*$', r'^const PREV_AR=.*$',
            r'^const pvDpr=.*$', r'^const PLATE_T=.*$', r'^const MACH_DEEP=.*$',
            r'^let PV_FORCE_DARK=.*$'):
    m = re.search(pat, src, re.M)
    if not m:
        raise SystemExit('nerasta: ' + pat)
    parts.append(m.group(0))
def galbut(decl):
    """Kai kuriu funkciju senesnese/naujesnese versijose nera — stendas jas irgi
    ima salygiskai. Praleidziam ir pasakom, o ne griuvam."""
    try:
        parts.append(grab(decl))
    except ValueError:
        print('  (nera, praleidziam):', decl[:34])


# isoPt — vienos israiskos strele per kelias eilutes; imam iki kabliataskio
i = src.index('const isoPt=(x,y,z)=>')
parts.append(src[i:src.index(';', i) + 1])
parts.append(grab('const pvSetField=(w,h)=>{'))
galbut('const pvSkin=()=>{')
parts.append('const plateZFor=()=>0;')
galbut('const drawPlate=(ctx,z0,MX,MY,fromBelow)=>{')
parts.append(grab('const pvFit=cv=>{'))
parts.append(grab('let coarseCache={key:'))
parts.append(grab('const coarseSlices=()=>{'))
galbut('const ghostSlices=()=>{')
galbut('const drawVolumeBox=cv=>{')
galbut('const quad=(ctx,fill,pts)=>{')
galbut('const machVoxel=(ctx,slices,gw,gh,k,i,j,t,fRight,fLeft,fTop,cx,cy,z,hx,hy,hz)=>{')
galbut('const drawMach=(cv,doneFrac)=>{')
# GLOTNUS pavirsius (#93) — butent jis pulte duoda ta vaizda, kuri V mato
# perziuroje. Be jo liktu voxeliu „burbulai".
m = re.search(r'^const SM_K=.*$', src, re.M)
parts.append(m.group(0))
parts.append(grab('let SM={key:'))
parts.append(grab('const smPause=(()=>{'))
parts.append(grab('const smRunAsync=async(it,onProg)=>{'))
parts.append(grab('function* smBuildFieldGen(){'))
parts.append(grab('function* smSurfaceNetsGen(fld,iso){'))
parts.append(grab('function* smMeshGen(){'))
parts.append(grab('function* smSubGen(it,base,span){'))
parts.append(grab('function* drawSmoothGen(cv){'))
m = re.search(r'^const smMesh=.*$', src, re.M)
parts.append(m.group(0))
parts.append(grab('function* drawIsoGen(cv,doneFrac){'))
parts.append('const drawIso=(cv,f)=>{const it=drawIsoGen(cv,f);'
             'let r;while(!(r=it.next()).done);return r.value;};')
code = '\n'.join(parts)
# Vienintelis prisilietimas prie pulto kodo: glotnu keliɑ apeinam tempiant
# (perskaiciuoti pavirsiu kas kadra neimanoma), tad ji perjungia jungiklis.
code = code.replace('function* drawSmoothGen(cv){',
                    'function* drawSmoothReal(cv){')
# Artinimas: pvFit kviecia pvSetField, tad mastelio daugikli idedam apvalkale
# (formule lieka pulto).
code = code.replace('const pvSetField=(w,h)=>{', 'const pvSetFieldReal=(w,h)=>{')
code += ('\nfunction* drawSmoothGen(cv){'
         'if(!GLOTNU)throw new Error("greitas rezimas");yield* drawSmoothReal(cv);}\n')
for c in ('const ISO_KY=', 'const ISO_Z =', 'const BOX_H_U=', 'const BOX_CY_U='):
    code = code.replace(c, 'let ' + c[6:])

# ------------------------------------------------------------------ GPU dalis
# Pultas detalu vaizda piesia NE ant drobes, o vaizdo plokste (three.js) —
# butent todel printeryje jis sukasi akimirksniu, o mano drobes variantas
# spraude po viena keturkampi (375 tukst.). Imam ta pati moduli.
GA = src.index("(async()=>{\n  let T=null,src='';")
GB = src.index('  window.DBG3D=src;', GA)
gpu = src[GA:src.index('})();', GB) + len('})();')]
# Trys pazymeti pakeitimai (daugiau — jokiu):
#  1) is IIFE padarom funkcija su HOST parametru, kad butu DU nepriklausomi vaizdai;
gpu = gpu.replace("(async()=>{\n  let T=null,src='';",
                  "window.MK3D=async(HOST)=>{\n  let T=null,src='';", 1)
gpu = gpu[:-len('})();')] + '};'
gpu = gpu.replace("$('gl3d')", '$(HOST)')
#  2) atramas dazom kita spalva (V prasymas) — tik viena eilute pries medziaga;
gpu = gpu.replace(
    "mesh=new T.Mesh(geo,new T.MeshStandardMaterial({color:0xe8720c,roughness:.92,",
    "let _sp=null;\n"
    "    if(window.spalvink){_sp=window.spalvink(verts,Wi,Hi,Di);\n"
    "      if(_sp)geo.setAttribute('color',new T.BufferAttribute(_sp,3));}\n"
    "    mesh=new T.Mesh(geo,new T.MeshStandardMaterial("
    "{vertexColors:!!_sp,color:_sp?0xffffff:0xe8720c,roughness:.92,")
#  3) taskas po zymekliu — zymejimui reikia TIKRO pavirsiaus tasko;
#  4) pjuvio plokstuma ir salu taskai — auditoriaus (004_Gem_Cld) reikalavimai:
#     be ju pultas SLA auditui netinka.
gpu = gpu.replace(
    "  window.DBG3D=src;",
    "  window.gl3dPick=(nx,ny)=>{if(!mesh||!cam)return null;\n"
    "    const rc=new T.Raycaster(); rc.setFromCamera(new T.Vector2(nx,ny),cam);\n"
    "    const h=rc.intersectObject(mesh); return h.length?h[0].point.toArray():null;};\n"
    "  window.gl3dHost=host;\n"
    "  window.__kam=window.__kam||{};\n"
    # Pjuvis: viena plokstuma. `ash` 0/1/2 = X/Y/Z modelio prasme, `vieta` — mm
    # nuo dezes centro, `kryptis` +-1 (kuria puse nupjaunam). null — pjuvio nera.
    "  window.gl3dPjuvis=(ash,vieta,kryptis)=>{\n"
    "    if(!ren||!mesh)return false;\n"
    "    ren.localClippingEnabled=ash!==null;\n"
    "    if(ash===null){mesh.material.clippingPlanes=[];mesh.material.needsUpdate=true;return true;}\n"
    "    const n=[[1,0,0],[0,0,1],[0,1,0]][ash];\n"
    # Paliekam ta puse, kuri PRIES plokstuma. Su priesinga normale pjaunant nuo
    # virsaus dingdavo visas modelis (V pastebejo 08-15).
    "    const v=new T.Vector3(-n[0]*kryptis,-n[1]*kryptis,-n[2]*kryptis);\n"
    "    mesh.material.clippingPlanes=[new T.Plane(v,vieta*kryptis)];\n"
    "    mesh.material.side=T.DoubleSide; mesh.material.needsUpdate=true; return true;};\n"
    # Salos: raudoni rutuliukai ten, kur sluoksnis liko be jokios atramos.
    # Matuoklis: ar pjuvis TIKRAI uzdetas butent siam vaizdui (spejant jau
    # du kartus apsirikau — V 08-15).
    "  window.gl3dBusena=()=>({pl:mesh?(mesh.material.clippingPlanes||[]).length:-1,\n"
    "    lc:ren?ren.localClippingEnabled:null, host:HOST});\n"
    # Uzdaros ertmes („siurbtukai") - pusiau permatomi geltoni langeliai.
    # Skaiciuojam jas seniai, bet rankomis slankioti pjuvi ju ieskant buvo
    # silpnoji vieta (auditoriaus 006 pastaba).
    "  let ertmes=null;\n"
    "  window.gl3dErtmes=(taskai,langelis)=>{\n"
    "    if(!scene)return false;\n"
    "    if(ertmes){scene.remove(ertmes);ertmes.geometry.dispose();ertmes=null;}\n"
    "    if(!taskai||!taskai.length)return true;\n"
    "    const g=new T.BoxGeometry(langelis[0],langelis[2],langelis[1]);\n"
    "    const m=new T.MeshBasicMaterial({color:0xffd23f,transparent:true,\n"
    "      opacity:0.5,depthWrite:false});\n"
    "    ertmes=new T.InstancedMesh(g,m,taskai.length); const mt=new T.Matrix4();\n"
    "    taskai.forEach((t,i)=>{mt.makeTranslation(t[0]-MX/2,t[2]-MZ/2,t[1]-MY/2);\n"
    "      ertmes.setMatrixAt(i,mt);});\n"
    "    ertmes.renderOrder=2; scene.add(ertmes); return true;};\n"
    "  let salos=null;\n"
    "  window.gl3dSalos=taskai=>{\n"
    "    if(!scene)return false;\n"
    "    if(salos){scene.remove(salos);salos.geometry.dispose();salos=null;}\n"
    "    if(!taskai||!taskai.length)return true;\n"
    "    const g=new T.SphereGeometry(0.6,10,8);\n"
    "    const m=new T.MeshBasicMaterial({color:0xff2d2d,depthTest:false});\n"
    "    salos=new T.InstancedMesh(g,m,taskai.length); const mt=new T.Matrix4();\n"
    "    taskai.forEach((t,i)=>{mt.makeTranslation(t[0]-MX/2,t[2]-MZ/2,t[1]-MY/2);\n"
    "      salos.setMatrixAt(i,mt);});\n"
    "    salos.renderOrder=3; scene.add(salos); return true;};\n"
    "  window.DBG3D=src;")
#  6) kameros busena i isore. Sinchroninis sukimas persiunciant peles ivykius
#     DREIFUOJA (V 08-15: „kampai dabar skirtingi"), nes ratukas ir stumimas
#     priklauso nuo zymeklio vietos. Tad ne judesius kartojam, o tiesiog
#     nukopijuojam kampus - taip nukrypti neimanoma.
gpu = gpu.replace(
    "    place();\n    /* Naujas modelis - naujas 'namu' taskas ir atstumas",
    "    place();\n"
    "    window.__kam=window.__kam||{};\n"
    "    window.__kam[HOST]={\n"
    "      imk:()=>({yaw:yaw,pitch:pitch,dist:dist,tgt:tgt.toArray()}),\n"
    "      dek:s=>{yaw=s.yaw;pitch=s.pitch;dist=s.dist;tgt.fromArray(s.tgt);place();}};\n"
    "    /* Naujas modelis - naujas 'namu' taskas ir atstumas", 1)
#  5) „gryni kubeliai": praleidziam suliejima — auditoriui reikia matyti tikrus
#     0,1275 mm laiptelius, ne glotninta pavirsiu. Vienas `if` lauko gamyboje.
code = code.replace(
    "  yield* blur(a,b,1,W,0,6,1,0.10,0.20);",
    "  if(window.beGlotninimo)return {f:a,W:W,H:H,D:D,Wi:Wi,Hi:Hi,Di:Di};\n"
    "  yield* blur(a,b,1,W,0,6,1,0.10,0.20);", 1)

# three.js — ta pati vietine kopija, kuria naudoja printeris
os.makedirs('C:/PIO-build/lib', exist_ok=True)
io.open('C:/PIO-build/lib/three.js', 'wb').write(io.open(
    'C:/Users/SViktoras/Documents/PlatformIO/Projects/TinyMakerWiFi/web/lib/'
    'three-0.160.0.min.js', 'rb').read())


# ---------------------------------------------------------------- duomenys
def tinklelis(path, first, stl=None, oriented=False, GW=160, GH=120, NSL=90):
    """Sluoksniu failas -> bitais supakuotas GW x GH x NSL tinklelis."""
    zf = zipfile.ZipFile(path)
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    idx = np.linspace(0, len(names) - 1, NSL).round().astype(int)
    per = (GW * GH + 7) >> 3
    buf = bytearray()
    sbuf = bytearray()
    sup_mask = None
    if stl:
        import fizika
        from sla3 import raster, slicing
        mesh = slicing.place(slicing.load(stl)) if oriented else slicing.load(stl)
        dx0, dy0, flip0, _ = fizika.find_shift(stl, path, first, oriented)
        lay = names

        from scipy import ndimage

        def sup_mask(lo, hi, img, _m=mesh, _f=first):
            # Detales pjuvi imam ruozo VIDURYJE. Imant tiksliai sluoksnio
            # virsuje, ties horizontalia plokstuma (kronsteino virsus) pjuvis
            # iseina tuscias, ir visas virsus butu palaikytas atrama (V 08-15).
            # `img` yra VISO ruozo sajunga, tad ir detales kontura imam per visa
            # ruoza (apacia + virsus). Lyginant su vienu pjuviu, statmenose
            # vietose spaudinys „issikisdavo" uz konturo ir buvo palaikytas
            # atrama - ant biusto plauku matesi melynos demeles (V 08-15).
            part = None
            for k in (lo, (lo + hi) / 2.0, hi):
                # -0.025 = sluoksnio VIDURYS. Be jo virsutine horizontali
                # plokstuma duoda tuscia pjuvi ir visas virsus pasidazo kaip
                # atrama (kronsteinas, 08-15 — antra karta ta pati duobe).
                z = (_f + k * 0.05 if _f > 0.05 else (k + 1) * 0.05) - 0.025
                p1 = raster.rasterize(slicing.section(_m, z)) > 127
                part = p1 if part is None else (part | p1)
            if flip0:
                part = np.fliplr(part)
            part = np.roll(np.roll(part, dy0, 0), dx0, 1)
            return img & ~ndimage.binary_dilation(part, iterations=1)   # 1 px = 0,1275 mm
    for ki, k in enumerate(idx):
        # Vienam rodomam pjuviui tenka ~6 tikri sluoksniai. Imant tik viena is ju,
        # plonos jungtys ir galvutes, patekusios i tarpa, tiesiog dingdavo. Todel
        # SUJUNGIAM visa ruoza (kaip PNG.ino: jei bent kur sviesu - langelis pilnas).
        lo = idx[ki - 1] + 1 if ki else 0
        hi = k
        # Trumpuose modeliuose (295 sluoksniai, o rodom 360) ruozas gali iseiti
        # tuscias - tada tiesiog kartojam ta pati sluoksni.
        if lo > hi:
            lo = hi
        a = np.zeros((0, 0), bool)
        for kk in range(lo, hi + 1):
            b1 = np.asarray(Image.open(io.BytesIO(zf.read(names[kk]))).convert('L')) > 127
            a = b1 if a.size == 0 else (a | b1)
        # sumazinam DAUGUMOS taisykle (kaip printerio kesas), ne kas n-tas taskas
        h, w = a.shape
        yi = (np.arange(GH) * h / GH).astype(int)
        xi = (np.arange(GW) * w / GW).astype(int)
        sy, sx = max(1, h // GH), max(1, w // GW)
        cell = np.zeros((GH, GW), bool)
        for dy in range(sy):
            for dx in range(sx):
                cell |= a[np.clip(yi + dy, 0, h - 1)][:, np.clip(xi + dx, 0, w - 1)]
        bits = np.packbits(cell.reshape(-1), bitorder='little')
        b = bytearray(per)
        b[:len(bits)] = bits.tobytes()
        buf += b
        # atskira ATRAMU kauke: reikia tik zymejimo statistikai („cia 14 atramu"),
        # piesimas ja nenaudoja — pultas piesia vientisa tinkleli
        if sup_mask is not None:
            sm = np.zeros((GH, GW), bool)
            am = sup_mask(lo, hi, a)
            for dy in range(sy):
                for dx in range(sx):
                    sm |= am[np.clip(yi + dy, 0, h - 1)][:, np.clip(xi + dx, 0, w - 1)]
            sb = np.packbits(sm.reshape(-1), bitorder='little')
            bb = bytearray(per); bb[:len(sb)] = sb.tobytes(); sbuf += bb
    hmm = len(names) * 0.05
    d = dict(gw=GW, gh=GH, n=NSL, mh=round(hmm, 2), layers=len(names),
             per=per, b64=base64.b64encode(bytes(buf)).decode())
    if sbuf:
        d['sup64'] = base64.b64encode(bytes(sbuf)).decode()
    return d


MODELIAI = {
    'biustas': ('woman-prusa.sl1', 'one-biowoman.zip', 0.3, 'woman-placed.stl', True),
    'evil': ('evil-prusa.sl1', 'one-evil.zip', 0.3, 'evil-placed.stl', True),
    'kronsteinas': ('bracket-up-prusa.sl1', 'one-kronsteinas.zip', 0.3, 'bracket2.stl', False),
    'puodelis': ('cup-prusa.sl1', 'one-puodelis.zip', 0.3, 'cup.stl', False),
    # Be PrusaSlicer atitikmens: sis modelis yra TIK ertmiu vaizdavimui tikrinti
    # (fizika.suction jame randa 224,7 mm3 uzdaros ertmes).
    'ertme-testas': ('nera.sl1', 'one-ertme-testas.zip', 0.05, 'ertme-testas.stl', False),
    # Kaklelio kriterijui: grybas (kotelis 2 mm, kepure 12 mm). Be atramu
    # santykis 33,5, su musu atramomis 17,0 (`kaklelis.py`).
    'kaklelis-testas': ('nera.sl1', 'one-kaklelis-testas.zip', 0.05, 'kaklelis-testas.stl', False),
}

SARASAS = {}
# Galima persukti tik viena modeli: python mk_pultas3d.py ertme-testas
TIK = [a for a in sys.argv[1:] if not a.startswith('-')]
for vardas, (ref, ours, first, stl, ori) in MODELIAI.items():
    if TIK and vardas not in TIK:
        continue
    for kas, path, f in (('prusa', ref, first), ('musu', ours, 0.05)):
        p = os.path.join(LAB, path)
        if not os.path.exists(p):
            print('nerastas:', p)
            continue
        for zyme, (gw, gh, ns) in (('d', DRAFT), ('p', PILNAS)):
            d = tinklelis(p, f, os.path.join(LAB, stl), ori, gw, gh, ns)
            vardasf = '%s_%s_%s.json' % (vardas, kas, zyme)
            io.open(os.path.join(BIN, vardasf), 'w', encoding='utf-8').write(json.dumps(d))
            SARASAS.setdefault('%s_%s' % (vardas, kas), {})[zyme] = vardasf
            print('%-11s %-5s %-7s %3dx%3dx%3d  %5.1f MB'
                  % (vardas, kas, zyme, gw, gh, ns,
                     (len(d['b64']) + len(d.get('sup64', ''))) / 1.4e6))

HTML = io.open(os.path.join(LAB, 'pultas_tmpl.html'), encoding='utf-8').read()

html = (HTML.replace('__CODE__', code)
            .replace('__GPU__', gpu)
            .replace('__DATA__', json.dumps(SARASAS)))
io.open(OUT, 'w', encoding='utf-8').write(html)
print('irasyta', OUT, round(len(html) / 1e6, 1), 'MB')
