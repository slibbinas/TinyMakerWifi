/* TinyMakerWiFi — SLA slicer, antras algoritmas.
 *
 * Rašomas pagal PrusaSlicer libslic3r ŠALTINIUS, ne pagal atmintį. Kiekvienas
 * etapas turi nuorodą į failą ir eilutę, iš kurios paimta logika:
 *
 *   src/libslic3r/SLA/DefaultSupportTree.hpp/.cpp   — etapų grandinė
 *   src/libslic3r/SLA/SupportTreeUtils.hpp          — spindulių pluoštas (Beam)
 *   src/libslic3r/SLA/SupportPointGenerator.hpp     — kur sėjami taškai
 *
 * Skiriasi nuo `slicer.js` iš esmės: ten supportai augo iš rastrinių euristikų,
 * čia — iš tos pačios etapų grandinės, kurią vykdo PrusaSlicer, su tikra
 * kolizijų patikra spinduliais. Abu moduliai gyvena greta ir stende
 * perjungiami, kad tą patį modelį būtų galima pamatyti abiem (V 08-13).
 *
 * Bendra infrastruktūra (STL skaitymas, orientavimas, pjaustymas, rastrizacija,
 * ZIP) imama iš `slicer.js` — tai ne algoritmas, o įrankiai; dubliuoti juos
 * reikštų dvi vietas tai pačiai klaidai.
 */
import {
  PLATE, PIXEL_MM, RES, LAYER_MM, SUP,
  parseSTL, bounds, surfaceArea, detailBudget, makeTransform, place, fitCheck,
  autoOrient, toSceneMesh, sliceAt, layerMask, zipStore,
  pillarDiscs, braceDiscs, supportMesh,
  slice as sliceBase,
} from './slicer.js';

export {
  PLATE, PIXEL_MM, RES, LAYER_MM, SUP,
  parseSTL, bounds, surfaceArea, detailBudget, makeTransform, place, fitCheck,
  autoOrient, toSceneMesh, sliceAt, layerMask, zipStore,
  supportMesh,
};
export { pillarDiscs2 as pillarDiscs, braceDiscs2 as braceDiscs };

export const VERSION = '2.0.1-dev';

/* ------------------------------------------------------------------ config */
/* Vardai palikti tokie patys kaip PrusaSlicer'io nustatymuose, kad būtų
   matyti, iš kur kiekvienas skaičius. Reikšmės — iš V profilio
   (TinyMaker + „Universal 0.05 - Light Supports"), ne iš numatytųjų. */
export const CFG = {
  /* Kontaktas su detale ⌀0,4 (ne PrusaSlicer profilio 0,5) — V sprendimas
     2026-08-13: SUNLU derva kieta ir tvirta, plonesnis antgalis laiko, o žymė
     mažesnė ir lengviau atsilupa. Šaltiniai riba deda ties 0,4–0,5: po 0,3
     antgaliai lūžta ir lieka detalėje. Iš šio skaičiaus išvedamas ir visas
     sėjos konfigas (žr. bloką po CFG). */
  head_front_radius_mm: 0.20,
  head_back_radius_mm:  0.5,    // support_pillar_diameter 1
  head_fallback_radius_mm: 0.3, // 60 % — support_small_pillar_diameter_percent
  head_penetration_mm:  0.3,    // support_head_penetration
  head_width_mm:        3.0,    // support_head_width
  pillar_radius_mm:     0.5,    // support_pillar_diameter 1
  base_radius_mm:       1.5,    // support_base_diameter 3
  base_height_mm:       1.0,    // support_base_height
  /* SupportTree.hpp:110 — KOMPILIAVIMO METO konstanta 0.5, NE profilio
     `support_base_safety_distance`. Tas profilio skaičius yra stulpo PĖDOS
     atstumas nuo detalės (`pillar_base_safety_distance_mm`), visai kitas
     dalykas. Paėmus 1.0 galvutės žiedas išeina 1,67× per platus. */
  safety_distance_mm:   0.5,
  /* PRASILENKIMO tarpas — V sprendimas 2026-08-13: 1 mm, kad tarp atramos ir
     detalės tilptų replės. Galioja TIK tam, kas eina PRO ŠALĮ (tiltai, stulpų
     jungtys), ne tam, kas į detalę atsiremia: ten atrama privalo liesti, kitaip
     ji lieka kaboti (sargas tai pagavo iškart — 0,8 mm tarpas, 08-13).
     `safety_distance_mm` lieka geometrinis 0,5 (SupportTree.hpp:110). */
  clearance_mm:         1.0,
  pillar_base_safety_distance_mm: 1.0,  // support_base_safety_distance
  max_bridge_length_mm: 10.0,   // support_max_bridge_length
  max_pillar_link_distance_mm: 10.0,  // support_max_pillar_link_distance
  max_bridges_on_pillar: 3,     // support_max_bridges_on_pillar
  bridge_slope:         Math.PI / 4,  // 45°, kaip jo numatytasis
  normal_cutoff_angle:  150 * Math.PI / 180,  // SupportTree.hpp:105 — 150°, ne 90°
  removing_delta_mm:    5.0,    // SampleConfig.hpp:31
  ground_facing_only:   false,  // support_buildplate_only = 0
  object_elevation_mm:  0,      // pad_around_object = 1 -> nekeliam
  /* Taškų sėja. PrusaSlicer'io density_relative = 100 %; mūsų pikselis
     0.1275 mm, tad tankį išreiškiam atstumu tarp taškų. */
  support_points_density: 1.0,     // support_points_density_relative 100 %
  /* Nuokabos krašto diskretizavimo žingsnis — `discretize_overhang_step`
     (SampleConfig.hpp:18). Tai NE tankis: tankį lemia įtakos spindulys žemiau. */
  discretize_overhang_step_mm: 2.0,
  /* `create_default_support_curve()` (SupportPointGenerator.cpp:1453).
     [atstumas sluoksnyje XY, aukščio skirtumas Z] milimetrais. Ką tik pastatyta
     atrama „dengia" 3,2 mm spindulį, o kylant aukštyn tas spindulys AUGA iki
     6 mm ties 40 mm. Būtent tai, o ne pastovus žingsnis, ir valdo tankį —
     todėl ant glotnaus kūno atramos retėja, o ant šviežios nuokabos tankėja.
     Senasis pastovus 3 mm tinklelis niekada neaugo, ir dėl to narvas išeidavo
     2–3× tankesnis nei PrusaSlicer'io (išmatuota 08-12). */
  support_curve: [[3.2, 0], [4.0, 3.9], [5.0, 15.0], [6.0, 40.0]],
  /* SampleConfig.hpp:47-58 — salų sėjos atstumai: kontūras 5*3/4, vidus 5,
     plonos dalies nugarkaulis 5. */
  /* Sėjos žingsniai ir atitraukimai NEPASIRENKAMI — jie išvedami iš galvutės
     skersmens (žr. bloką po CFG). Čia palikti tik kaip vietos ženklai. */
  island_outline_step_mm: 0,
  island_inner_step_mm:   0,
  island_thin_step_mm:    0,
  min_dist_from_outline_mm: 0,
  /* 4-as kriterijus (V, 08-13): kiek toliausiai nuokabos vieta gali būti nuo
     artimiausios atramos. Iš čia sėjamas salų ir pusiasalių vidus. */
  coverage_max_mm:        3.0,
  max_dist_from_outline_mm: 0,
  thin_max_width_mm:      0,
  thick_min_width_mm:     0,
  /* SampleConfig.hpp:20-24 — vieno sluoksnio nuokaba tampa „pussaliu", jei
     išsikiša toliau nei `peninsula_min_width`; kas arčiau nei
     `peninsula_self_supported_width` — laikosi pati. */
  peninsula_min_width_mm: 2.0,
  peninsula_self_supported_width_mm: 1.5,
  /* `minimal_bounding_sphere_radius` (SampleConfig.hpp:35): mažesnės dalys
     išmetamos dar prieš sėją — jų neįmanoma atspausdinti kitaip nei rutuliuku
     nuo galvutės. */
  minimal_part_radius_mm: 0.2,
  critical_angle:        Math.PI / 4,  // support_critical_angle 45
  /* Savilaikio riba. Geometriškai tai sluoksnio postūmis prie kritinio kampo
     (0,05 mm), bet fiziškai riba didesnė: UV šviesa dervoje išsisklaido
     ~0,05-0,1 mm plačiau nei LCD kaukė, tad siauresnė juosta susikietina su
     kaimynu ir atramos nereikia. Auditoriaus (Gemini) siūlymas 08-15;
     išmatuota: „evil" pagrinde dingsta visi 7 kelmeliai, stulpų 25 -> 18. */
  self_support_mm:       0.10,
  /* Klasteriai: du taškai jungiasi į vieną stulpą, jei XY atstumas mažesnis
     nei 2 × base_radius IR 3D atstumas mažesnis nei max_bridge_length
     (DefaultSupportTree.cpp:565-571). */
  cluster_size:          3,     // = max_bridges_on_pillar
  pillar_cascade_neighbors: 3,  // kiek kaimynų vienas stulpas jungia
  /* SupportTree.hpp:112-113 — nuo šių aukščių stulpas laikomas „vienišu" ir
     jungčių skaičiavimas griežtėja. */
  max_solo_pillar_height_mm: 15.0,
  max_dual_pillar_height_mm: 35.0,
  pillar_connection_mode: 'zigzag',   // support_pillar_connection_mode
  /* SLA/Pad.hpp + V profilis: pad_wall_height 0, pad_wall_thickness 0.15,
     pad_brim_size 1.6. full_height = wall_height + wall_thickness. */
  /* Pado storis 0,3 mm = 6 sluoksniai. V taisyklė iš praktikos (08-15): per
     plonas — mentele neužgriebsi (tikra bėda), per storas — sunkiau atlenkti,
     bet atšaldžius platformą atšoka (nepatogumas). Rizika asimetriška, tad
     klystam į storesnę pusę. PrusaSlicer čia turi 0,3 mm (vienas jo storesnis
     pirmas sluoksnis) ir V žodžiais „jo raftas idealus". Buvo 0,15. */
  pad_thickness_mm:      0.3,
  pad_brim_mm:           1.6,
  pad_object_gap_mm:     1.0,   // pad_object_gap — tarpas tarp pado ir detales
  pad_layers:            6,     // 0.3 mm / 0.05
  /* MŪSŲ, ne PrusaSlicer'io taisyklė. Plokštė 40,8 × 30,6 mm yra maža, ir
     dideliems modeliams atramos pėda nebetelpa — PrusaSlicer tokią tiesiog
     nukerta ties LCD kraštu (išmatuota 08-15: biuste 1453 taškai ties kraštu
     prieš mūsų 806, „evil" 112 prieš 0). Nukirsta pėda = stulpas, stovintis
     ant nieko. Todėl pėdai, kuri netelpa, pirma ieškom kelio Į VIDŲ.
     `null` = skaičiuojam patys: tilpti PRIVALO pėdos diskas
     (`base_radius_mm`). Pado apvado čia NEskaičiuojam, nors iš pradžių
     skaičiavau: jis yra dekoratyvus sijonas aplink padą, ir nukirstas jis
     stulpo ant nieko nepalieka. Su apvadu (riba 3,1 mm) biuste atsirado
     TREČIA sala ties z = 43,3 — atrama pasitraukė per toli į vidų ir paliko
     lopinėlį be dangos (išmatuota 08-15). */
  plate_edge_margin_mm:  null,
};

/* `SampleConfigFactory::create` (SLA/SupportIslands/SampleConfigFactory.cpp:55,
   2.9.6) — VISI sėjos dydžiai išvedami iš galvutės skersmens, ne parenkami.
   Anksčiau čia stovėjo mano spėti 3,75 / 5,0 / 5,0 ir jokio atitraukimo nuo
   krašto; dėl to taškai sėdėjo TIKSLIAI ant kontūro, ties briauna normalė
   išeidavo 45° laukan (`get_normal` ten vidurkina apačią su sienele), ir
   puodelio atramos nukeliaudavo už atbrailos — 49 stulpai vietoj 15 (08-13).

   Prie ⌀0,5 galvutės: vienam taškui 1,87 · dviem 7,29 · siauras iki 4,67 ·
   storas nuo 4,02 · žingsniai 5,47 (kontūras) / 7,29 (vidus) / 5,83 (siaura) ·
   atitraukimas 0,25…1,94. */
{
  const d = CFG.head_front_radius_mm * 2;
  const one = Math.PI * (d / 2) ** 2 * 2.9 + 1.3;   // max_length_for_one_support_point
  const two = one * 3.9;                            // max_length_for_two_support_points
  CFG.thin_max_width_mm  = one * 2.5;
  CFG.thick_min_width_mm = one * 2.15;
  CFG.island_thin_step_mm    = two * 0.8;           // thin_max_distance
  CFG.island_inner_step_mm   = two;                 // thick_inner_max_distance
  CFG.island_outline_step_mm = two * 0.75;          // thick_outline_max_distance
  CFG.min_dist_from_outline_mm = d / 2;             // = head_radius
  CFG.max_dist_from_outline_mm = two * 0.8 / 3;
}

const DOWN = [0, 0, -1];
const INF = Infinity;

/* ------------------------------------------------------------ vektoriukai */
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1],
                         a[2] * b[0] - a[0] * b[2],
                         a[0] * b[1] - a[1] * b[0]];
const len = a => Math.hypot(a[0], a[1], a[2]);
const norm = a => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const dist2d = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
const dist3d = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);

/* ------------------------------------------------------- erdvinis indeksas */
/* AABBMesh atitikmuo: tinklelis XY plokštumoje, kiekviename langelyje —
   trikampių, kurių gabaritas jį kerta, sąrašas. Spindulys eina per langelius
   ir tikrina tik juos. Tikras BVH būtų greitesnis, bet čia užtenka: modelis
   telpa į 40×30 mm, o langelių tinklelis jį suskaido į šimtus dalių. */
class MeshIndex {
  constructor(pos, cell = 2.0) {
    this.pos = pos;
    this.cell = cell;
    this.map = new Map();
    const b = bounds(pos);
    this.min = b.min;
    this.nx = Math.max(1, Math.ceil(b.size[0] / cell) + 1);
    this.ny = Math.max(1, Math.ceil(b.size[1] / cell) + 1);
    for (let t = 0; t + 8 < pos.length; t += 9) {
      const x0 = Math.min(pos[t], pos[t + 3], pos[t + 6]);
      const x1 = Math.max(pos[t], pos[t + 3], pos[t + 6]);
      const y0 = Math.min(pos[t + 1], pos[t + 4], pos[t + 7]);
      const y1 = Math.max(pos[t + 1], pos[t + 4], pos[t + 7]);
      const i0 = this.cx(x0), i1 = this.cx(x1);
      const j0 = this.cy(y0), j1 = this.cy(y1);
      for (let j = j0; j <= j1; j++)
        for (let i = i0; i <= i1; i++) {
          const k = j * this.nx + i;
          let l = this.map.get(k);
          if (!l) { l = []; this.map.set(k, l); }
          l.push(t);
        }
    }
  }
  cx(x) { return Math.max(0, Math.min(this.nx - 1, Math.floor((x - this.min[0]) / this.cell))); }
  cy(y) { return Math.max(0, Math.min(this.ny - 1, Math.floor((y - this.min[1]) / this.cell))); }

  /** Möller–Trumbore. Grąžina atstumą iki trikampio arba Infinity.
   *  `inside` sako, ar pataikyta į paviršių iš vidaus (normalė su spinduliu
   *  vienakryptė) — libslic3r tuo remiasi permesdamas spindulį (Beam logika). */
  triHit(t, o, d) {
    const p = this.pos;
    const ax = p[t], ay = p[t + 1], az = p[t + 2];
    const e1 = [p[t + 3] - ax, p[t + 4] - ay, p[t + 5] - az];
    const e2 = [p[t + 6] - ax, p[t + 7] - ay, p[t + 8] - az];
    const h = cross(d, e2);
    const a = dot(e1, h);
    if (Math.abs(a) < 1e-12) return null;
    const f = 1 / a;
    const s = [o[0] - ax, o[1] - ay, o[2] - az];
    const u = f * dot(s, h);
    if (u < 0 || u > 1) return null;
    const q = cross(s, e1);
    const v = f * dot(d, q);
    if (v < 0 || u + v > 1) return null;
    const dist = f * dot(e2, q);
    if (dist <= 1e-9) return null;
    /* a = -dot(spindulys, normalė): a > 0 reiškia pataikymą iš IŠORĖS,
       tad „iš vidaus" yra a < 0. Apverstas ženklas griovė visą kolizijų
       patikrą — beamHit grąžindavo 0 visur, kur kliūtis toliau nei 2r+sd,
       ir tiltai buvo atmetami be priežasties (auditas 08-13). */
    return { dist, inside: a < 0 };
  }

  /** Artimiausias trikampio taškas (Ericson). `get_normal` sprendžia pagal
   *  PROJEKCIJĄ ant tinklo, ne pagal patį tašką, tad jos reikia atskirai. */
  closestOnTri(p, a, b, c) {
    const ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const bp = sub(p, b), d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, mul(ab, d1 / (d1 - d3)));
    const cp = sub(p, c), d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, mul(ac, d2 / (d2 - d6)));
    const va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
      return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const den = 1 / (va + vb + vc);
    return add(a, add(mul(ab, vb * den), mul(ac, vc * den)));
  }

  /** Trikampio vienetinė normalė — pagal apvijos kryptį. */
  faceNormal(t) {
    const q = this.pos;
    return norm(cross([q[t + 3] - q[t], q[t + 4] - q[t + 1], q[t + 5] - q[t + 2]],
                      [q[t + 6] - q[t], q[t + 7] - q[t + 1], q[t + 8] - q[t + 2]]));
  }

  /** `vertex_face_index` atitikmuo: viršūnė -> ją naudojantys trikampiai.
   *  STL viršūnės kartojasi kiekviename trikampyje atskirai, tad raktas —
   *  suapvalintos koordinatės. Statoma tik pareikalavus. */
  vertexIndex() {
    if (this._vmap) return this._vmap;
    const q = this.pos, m = new Map();
    this._vkey = (x, y, z) =>
      Math.round(x * 1e4) + ',' + Math.round(y * 1e4) + ',' + Math.round(z * 1e4);
    for (let t = 0; t + 8 < q.length; t += 9)
      for (let k = 0; k < 3; k++) {
        const s = this._vkey(q[t + k * 3], q[t + k * 3 + 1], q[t + k * 3 + 2]);
        let l = m.get(s);
        if (!l) { l = []; m.set(s, l); }
        l.push(t);
      }
    return (this._vmap = m);
  }

  /** `face_neighbor_index()[faceid](edge_idx)` — trikampis anapus briaunos. */
  faceAcrossEdge(t, e1, e2) {
    const vm = this.vertexIndex(), k = this._vkey;
    const l1 = vm.get(k(e1[0], e1[1], e1[2])) || [];
    const l2 = new Set(vm.get(k(e2[0], e2[1], e2[2])) || []);
    for (const f of l1) if (f !== t && l2.has(f)) return f;
    return -1;
  }

  /** `squared_distance(point, faceid, p)` — artimiausias tinklo trikampis ir
   *  taško projekcija ant jo. Langelių žiedas plečiamas, kol rastas atstumas
   *  telpa į jau apžiūrėtą plotą — kitaip artimiausias galėtų likti gretimame
   *  langelyje. */
  closestFace(p) {
    let best = Infinity, bt = -1, bq = null;
    for (let w = this.cell; w <= this.cell * 8; w *= 2) {
      const i0 = this.cx(p[0] - w), i1 = this.cx(p[0] + w);
      const j0 = this.cy(p[1] - w), j1 = this.cy(p[1] + w);
      for (let j = j0; j <= j1; j++)
        for (let i = i0; i <= i1; i++) {
          const l = this.map.get(j * this.nx + i);
          if (!l) continue;
          for (const t of l) {
            const q = this.pos;
            const a = [q[t], q[t + 1], q[t + 2]];
            const b = [q[t + 3], q[t + 4], q[t + 5]];
            const c = [q[t + 6], q[t + 7], q[t + 8]];
            const d = this.pointTriDist2(p, a, b, c);
            if (d < best) { best = d; bt = t; bq = this.closestOnTri(p, a, b, c); }
          }
        }
      if (bt >= 0 && Math.sqrt(best) <= w) break;
    }
    return bt < 0 ? null : { t: bt, q: bq };
  }

  /** Paviršiaus normalė taške — `get_normal` (MeshNormals.cpp:31) PAŽODŽIUI.
   *
   *  Čia buvo savas variantas: sumuodavo VISŲ trikampių, esančių arčiau nei r,
   *  normales su plotu. Skamba panašiai, bet elgiasi kitaip — ties plokštės
   *  kiaurymės kraštu jis sumaišydavo apačią su sienele ir grąžindavo įstrižą
   *  kryptį, tad galvutės pasvirdavo į skylę, o stulpai nusileisdavo pro ją
   *  (8 iš 38, 08-13). Originalas taip nedaro: ima ARTIMIAUSIO trikampio
   *  normalę ir vidurkina tik tada, kai projekcija patenka ant briaunos
   *  (2 trikampiai) ar viršūnės (visi tą viršūnę dalijantys). */
  normalAt(p, eps) {
    const hit = this.closestFace(p);
    if (!hit) return null;
    const q = this.pos, t = hit.t;
    const v = [[q[t], q[t + 1], q[t + 2]],
               [q[t + 3], q[t + 4], q[t + 5]],
               [q[t + 6], q[t + 7], q[t + 8]]];
    const epsSq = eps * eps;
    const d2 = (a, b) => {
      const x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
      return x * x + y * y + z * z;
    };
    /* point_on_edge (MeshNormals.cpp:20): atstumas iki TIESĖS per briaunos
       galus, ne iki atkarpos — taip parašyta originale. */
    const lineD2 = (pt, e1, e2) => {
      const d = sub(e2, e1), L = dot(d, d);
      if (L < 1e-18) return d2(pt, e1);
      return d2(pt, add(e1, mul(d, dot(sub(pt, e1), d) / L)));
    };
    let vi = -1, ei = -1;
    if (d2(hit.q, v[0]) < epsSq) vi = 0;
    else if (d2(hit.q, v[1]) < epsSq) vi = 1;
    else if (d2(hit.q, v[2]) < epsSq) vi = 2;
    else if (lineD2(hit.q, v[0], v[1]) < epsSq) ei = 0;
    else if (lineD2(hit.q, v[1], v[2]) < epsSq) ei = 1;
    else if (lineD2(hit.q, v[0], v[2]) < epsSq) ei = 2;

    const neigh = [];
    /* Kaimynai DEDUPLIKUOJAMI pagal normalę (eqfn, 1e-3): plokštuma, viršūnę
       dalijanti šešiais trikampiais, į sumą įeina VIENĄ kartą — kitaip ji
       nusvertų greta stovinčią sienelę vien trikampių skaičiumi. */
    const push = n => {
      for (const m of neigh)
        if (Math.abs(m[0] - n[0]) < 1e-3 && Math.abs(m[1] - n[1]) < 1e-3 &&
            Math.abs(m[2] - n[2]) < 1e-3) return;
      neigh.push(n);
    };
    if (vi >= 0) {
      const vm = this.vertexIndex();
      for (const f of (vm.get(this._vkey(v[vi][0], v[vi][1], v[vi][2])) || []))
        push(this.faceNormal(f));
    } else if (ei >= 0) {
      // briaunų numeracija kaip originale: 0=(p1,p2), 1=(p2,p3), 2=(p1,p3)
      const E = [[0, 1], [1, 2], [0, 2]][ei];
      const f2 = this.faceAcrossEdge(t, v[E[0]], v[E[1]]);
      if (f2 >= 0) { push(this.faceNormal(t)); push(this.faceNormal(f2)); }
    }
    if (neigh.length) {
      const acc = neigh.reduce((a, n) => [a[0] + n[0], a[1] + n[1], a[2] + n[2]],
                               [0, 0, 0]);
      const L = Math.hypot(acc[0], acc[1], acc[2]);
      return L > 1e-12 ? [acc[0] / L, acc[1] / L, acc[2] / L] : null;
    }
    return this.faceNormal(t);
  }

  /** Kvadratinis atstumas nuo taško iki trikampio (Ericson, Real-Time
   *  Collision Detection). Reikia normalės žiedui — be jo imtume ir tolimų
   *  trikampių normales. */
  pointTriDist2(p, a, b, c) {
    const ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return dot(ap, ap);
    const bp = sub(p, b), d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return dot(bp, bp);
    const vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
      const v = d1 / (d1 - d3), q = sub(ap, mul(ab, v));
      return dot(q, q);
    }
    const cp = sub(p, c), d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return dot(cp, cp);
    const vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
      const w = d2 / (d2 - d6), q = sub(ap, mul(ac, w));
      return dot(q, q);
    }
    const va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
      const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
      const q = sub(sub(p, b), mul(sub(c, b), w));
      return dot(q, q);
    }
    const den = 1 / (va + vb + vc);
    const v = vb * den, w = vc * den;
    const q = sub(ap, add(mul(ab, v), mul(ac, w)));
    return dot(q, q);
  }

  /** Vieno spindulio metimas — AABBMesh::query_ray_hit atitikmuo. */
  rayHit(o, d) {
    let best = { dist: INF, inside: false };
    /* Einam per XY langelius palei spindulį; žingsnis — pusė langelio.
       Pakartotinius langelius atmetam lygindami su ankstesniu, ne per Set:
       spindulys eina tiesiai, tad kartojasi tik gretimi žingsniai, o Set
       kiekvienam spinduliui kainavo daugiau nei pats tikrinimas. */
    /* Tikras DDA: einam per KIEKVIENĄ langelį, kurį spindulys kerta. Žingsnis
       „pusė langelio" praleisdavo langelius įstrižai einantiems spinduliams —
       0,5 % atsakymų buvo per toli, t. y. „laisva" ten, kur laisva nebuvo
       (auditas 08-13, palyginta su brute force). */
    let ix = this.cx(o[0]), iy = this.cy(o[1]);
    const stepX = d[0] > 0 ? 1 : -1, stepY = d[1] > 0 ? 1 : -1;
    const c = this.cell;
    const bx = this.min[0] + (ix + (d[0] > 0 ? 1 : 0)) * c;
    const by = this.min[1] + (iy + (d[1] > 0 ? 1 : 0)) * c;
    let tMaxX = Math.abs(d[0]) > 1e-12 ? (bx - o[0]) / d[0] : INF;
    let tMaxY = Math.abs(d[1]) > 1e-12 ? (by - o[1]) / d[1] : INF;
    const tDeltaX = Math.abs(d[0]) > 1e-12 ? c / Math.abs(d[0]) : INF;
    const tDeltaY = Math.abs(d[1]) > 1e-12 ? c / Math.abs(d[1]) : INF;
    let travelled = 0;
    const far = 200;                       // toliau nei plokštės įstrižainė
    for (let guard = 0; guard < 4096; guard++) {
      const list = this.map.get(iy * this.nx + ix);
      if (list)
        for (const t of list) {
          const h = this.triHit(t, o, d);
          if (h && h.dist < best.dist) best = h;
        }
      // Toliau eiti nėra ko, kai pataikymas arčiau nei jau nueitas kelias.
      if (best.dist < travelled) break;
      if (tMaxX < tMaxY) { travelled = tMaxX; ix += stepX; tMaxX += tDeltaX; }
      else               { travelled = tMaxY; iy += stepY; tMaxY += tDeltaY; }
      if (travelled > far) break;
      if (ix < 0 || ix >= this.nx || iy < 0 || iy >= this.ny) {
        // Išėjus iš tinklelio vertikalus spindulys vis tiek lieka savo langelyje.
        if (tDeltaX === INF && tDeltaY === INF) break;
        if (travelled > far) break;
        if (ix < -1 || ix > this.nx || iy < -1 || iy > this.ny) break;
      }
    }
    return best;
  }
}

/* --------------------------------------------------------- spindulių žiedas */
/* PointRing (SupportTreeUtils.hpp): aštuoni taškai ant apskritimo, statmeno
   krypčiai. Sukam bet kurį statmeną vektorių apie ašį. */
function ringBasis(dir) {
  const d = norm(dir);
  const helper = Math.abs(d[2]) < 0.9 ? [0, 0, 1] : [1, 0, 0];
  const a = norm(cross(d, helper));
  const b = norm(cross(d, a));
  return [a, b];
}
function ringPoint(centre, a, b, r, i, n) {
  const t = 2 * Math.PI * i / n;
  return add(centre, add(mul(a, r * Math.cos(t)), mul(b, r * Math.sin(t))));
}

const BEAM_SAMPLES = 8;   // SupportTreeUtils.hpp: Beam_<Samples = 8>

/** beam_mesh_hit (SupportTreeUtils.hpp:150-194): aštuoni spinduliai palei kūgio
 *  paviršių, rezultatas — mažiausias pataikymas. `sd` yra saugos atstumas. */
/** Kaip `beamHit`, tik grąžina ir pataikymo TAŠKĄ. Jo reikia apverstai
 *  galvutei: kai ašis ir pluoštas nesutampa, originalas remiasi į pluošto
 *  pataikymo vietą, o ji yra šalia ašies (SupportTreeUtils.hpp: hit.position()
 *  = spindulio pradžia + kryptis × atstumas). */
export function beamHitFull(mesh, src, dir, r1, r2, sd = 0) {
  const d = norm(dir);
  const dst = add(src, d);
  const [a, b] = ringBasis(d);
  let best = INF, bestPos = null;
  for (let i = 0; i < BEAM_SAMPLES; i++) {
    const ps = ringPoint(src, a, b, r1 + sd, i, BEAM_SAMPLES);
    const pd = ringPoint(dst, a, b, r2 + sd, i, BEAM_SAMPLES);
    const rd = norm(sub(pd, ps));
    let hr = mesh.rayHit(add(ps, mul(rd, r1)), rd);
    if (hr.inside && hr.dist < INF) {
      // Pataikyta iš vidaus — permetam iš išorės, kaip daro originalas.
      if (hr.dist > 2 * r1 + sd) { best = 0; bestPos = null; continue; }
      /* hr.dist matuojamas nuo TAŠKO, iš kurio šauta (ps + rd*r1), tad
         permetant reikia to paties poslinkio — kitaip naujas spindulys
         atsiduria prieš paviršių ir pataiko į jį patį (rezultatas visada
         išeidavo ≈ r1). */
      const q = add(ps, mul(rd, r1 + hr.dist + 1e-6));
      hr = mesh.rayHit(q, rd);
    }
    if (hr.dist < best) {
      best = hr.dist;
      bestPos = Number.isFinite(hr.dist)
        ? add(add(ps, mul(rd, r1)), mul(rd, hr.dist)) : null;
    }
  }
  return { dist: best, pos: bestPos };
}

/** Tik atstumas — taip jis naudojamas beveik visur. */
export function beamHit(mesh, src, dir, r1, r2, sd = 0) {
  return beamHitFull(mesh, src, dir, r1, r2, sd).dist;
}

/** Saugos atstumai. `safety_distance(r)` — galvutei (SupportTree.hpp:95),
 *  `bridgeSafety(r)` — tiltams ir `classify` (DefaultSupportTree.hpp:165).
 *  Abu perkrovimai patys skaičiuoja atstumą; perdavus nulį tikrinama be jokios
 *  atsargos, o tai ne tas pats. */
export function safetyDistance(r, cfg = CFG) {
  return Math.min(cfg.safety_distance_mm,
                  r * cfg.safety_distance_mm / cfg.head_back_radius_mm);
}
export function bridgeSafety(r, cfg = CFG) {
  return r * cfg.safety_distance_mm / cfg.head_back_radius_mm;
}
/** Prasilenkimo tarpas: kiek atrama turi apeiti detalę, kad tarp jų tilptų
 *  replės. Skiriasi nuo `bridgeSafety` tik konstanta — žr. `clearance_mm`. */
export function passSafety(r, cfg = CFG) {
  return r * cfg.clearance_mm / cfg.head_back_radius_mm;
}

const PINHEAD_SAMPLES = 16;   // „8 is almost ok … 16 is necessary"

/** `pinhead_mesh_hit` (SupportTreeUtils.hpp:196-280).
 *
 *  Tai NE `beam_mesh_hit` su kitais parametrais, nors ilgai buvo taip parašyta.
 *  Savas kūnas: 16 spindulių; smaigalio žiedas ties PAČIU tašku spinduliu
 *  `rPin + sd`, nugarėlės — ties `s + (rPin + width + rBack) * dir` spinduliu
 *  `rBack + sd`; spindulys leidžiamas iš `ps + sd * n`, pasistūmėjus nuo
 *  lietimosi taško; „iš vidaus" riba yra `rPin + sd`, o permetama su
 *  `dist + 2*sd` poslinkiu. */
export function pinheadHit(mesh, s, dir, rPin, rBack, width, sd = 0) {
  const d = norm(dir);
  const spin = s;
  const sback = add(s, mul(d, rPin + width + rBack));
  const rpin = rPin + sd, rback = rBack + sd;
  const [a, b] = ringBasis(d);
  let best = INF;
  for (let i = 0; i < PINHEAD_SAMPLES; i++) {
    const ps = ringPoint(spin, a, b, rpin, i, PINHEAD_SAMPLES);
    const pd = ringPoint(sback, a, b, rback, i, PINHEAD_SAMPLES);
    const n = norm(sub(pd, ps));
    let hr = mesh.rayHit(add(ps, mul(n, sd)), n);
    if (hr.inside && hr.dist < INF) {
      if (hr.dist > rpin) { best = 0; continue; }
      hr = mesh.rayHit(add(ps, mul(n, hr.dist + 2 * sd)), n);
    }
    if (hr.dist < best) best = hr.dist;
  }
  return best;
}

/* ------------------------------------------------------------ taškų sėja */
/* SupportPointGenerator: taškai sėjami ant to, kas kabo. Kampo riba —
   support_critical_angle (45°). Retinimas — vienodu atstumu, kad taškai
   nesusigrūstų (originale Poisson tipo atranka ant kontūro). */
export function samplePoints(pos, cfg = CFG) {
  const cosLimit = -Math.cos(cfg.critical_angle);
  const step = cfg.point_spacing_mm / Math.max(0.01, cfg.support_points_density);
  const cells = new Map();
  const out = [];
  const put = (x, y, z, n) => {
    if (z < cfg.base_height_mm) return;     // prie pat plokštės laikosi pats
    const k = [Math.floor(x / step), Math.floor(y / step), Math.floor(z / step)].join(',');
    if (cells.has(k)) return;
    cells.set(k, 1);
    out.push({ pos: [x, y, z], normal: n });
  };
  for (let t = 0; t + 8 < pos.length; t += 9) {
    const ax = pos[t], ay = pos[t + 1], az = pos[t + 2];
    const u = [pos[t + 3] - ax, pos[t + 4] - ay, pos[t + 5] - az];
    const v = [pos[t + 6] - ax, pos[t + 7] - ay, pos[t + 8] - az];
    const n = cross(u, v);
    const nl = len(n);
    if (nl < 1e-12) continue;
    const nz = n[2] / nl;
    if (nz >= cosLimit) continue;           // kabo per mažai
    const nn = [n[0] / nl, n[1] / nl, nz];
    put(ax + (u[0] + v[0]) / 3, ay + (u[1] + v[1]) / 3, az + (u[2] + v[2]) / 3, nn);
    const nu = Math.min(24, Math.floor(len(u) / step));
    const nv = Math.min(24, Math.floor(len(v) / step));
    for (let iu = 0; iu <= nu; iu++)
      for (let iv = 0; iv <= nv; iv++) {
        if (!nu && !nv) break;
        const cu = nu ? iu / nu : 0, cv = nv ? iv / nv : 0;
        if (cu + cv > 1) continue;
        put(ax + u[0] * cu + v[0] * cv, ay + u[1] * cu + v[1] * cv,
            az + u[2] * cu + v[2] * cv, nn);
      }
  }
  return out;
}

/* ------------------------------------------------------ klasterizacija */
/* DefaultSupportTree.cpp:565-574. Du taškai vienam stulpui, jei XY atstumas
   mažesnis nei 2 × base_radius IR 3D atstumas mažesnis nei max_bridge_length.
   Klasterio dydis ribojamas max_bridges_on_pillar. */
function clusterHeads(heads, cfg) {
  const used = new Array(heads.length).fill(false);
  const out = [];
  for (let i = 0; i < heads.length; i++) {
    if (used[i]) continue;
    const cl = [i]; used[i] = true;
    for (let j = i + 1; j < heads.length && cl.length <= cfg.cluster_size; j++) {
      if (used[j]) continue;
      const a = heads[i].junction, b = heads[j].junction;
      if (dist2d(a, b) < 2 * cfg.base_radius_mm &&
          dist3d(a, b) < cfg.max_bridge_length_mm) { cl.push(j); used[j] = true; }
    }
    out.push(cl);
  }
  return out;
}

/** Klasterio centras pagal XY atstumą — cluster_centroid. */
function centroidOf(cl, heads) {
  let best = cl[0], bestSum = INF;
  for (const a of cl) {
    let s = 0;
    for (const b of cl) s += dist2d(heads[a].junction, heads[b].junction);
    if (s < bestSum) { bestSum = s; best = a; }
  }
  return best;
}

/* ------------------------------------------------------------- grandinė */
/** Etapai tokia pat tvarka, kaip DefaultSupportTree::execute():
 *  add_pinheads -> classify -> routing_to_ground -> routing_to_model ->
 *  interconnect_pillars -> merge_result. */
export async function buildSupportTree(pos, cfg = CFG, onProgress) {
  /* Etapų laikai — kad optimizuotume tai, kas iš tikrųjų lėta, o ne tai, kas
     atrodo lėta (pirmas spėjimas buvo krypties paieška, o kainavo visai kas
     kita). */
  const clock = typeof performance !== 'undefined' ? () => performance.now()
                                                   : () => Number(process.hrtime.bigint() / 1000000n);
  let t0 = clock();
  const lap = k => { const t = clock(); log.ms[k] = Math.round(t - t0); t0 = t; };

  const log = { ms: {} };
  const mesh = new MeshIndex(pos);
  lap('index');
  /* Taškai iš SLUOKSNIŲ, kaip SupportPointGenerator; sena sėja pagal mesh
     veidų kampą lieka faile palyginimui, bet grandinėje nebenaudojama. */
  const pts = await samplePointsFromLayers(pos, cfg, onProgress);
  log.sampled = pts.length;
  lap('sample');

  /* --- 1 · add_pinheads (DefaultSupportTree.cpp:385) --------------------- */
  const heads = [];
  for (const p of pts) {
    /* Galvutė eina PAGAL paviršiaus normalę (originale nn = prisotinta
       normalė, DefaultSupportTree.cpp:462), o kabančio paviršiaus normalė jau
       rodo žemyn. Prisotinimas: polar = max(polar, PI - bridge_slope) reiškia,
       kad kryptis turi būti bent bridge_slope žemiau horizontalės. */
    /* Kryptis — iš TINKLO normalės (cpp:404,455). Sėja duoda tik [0,0,-1],
       o tikroji normalė ties briauna įstriža, ir kaip tik ji atitraukia
       jungtį nuo detalės. Nepavykus — grįžtam prie sėjos normalės. */
    const nAt = mesh.normalAt(p.pos, cfg.head_front_radius_mm);
    let dir = norm(nAt || p.normal);
    /* `if (polar < PI - normal_cutoff_angle) return;` (cpp:441) — normalė,
       rodanti beveik tiksliai į viršų, taško netenka. */
    if (dir[2] > Math.cos(Math.PI - cfg.normal_cutoff_angle)) continue;
    const maxDown = -Math.cos(cfg.bridge_slope);   // -0.707 prie 45°
    if (dir[2] > maxDown) {
      // per mažas polinkis žemyn — pakreipiam iki leistinos ribos
      const h = Math.hypot(dir[0], dir[1]) || 1;
      const s = Math.sqrt(Math.max(0, 1 - maxDown * maxDown));
      dir = [dir[0] / h * s, dir[1] / h * s, maxDown];
    }
    let rBack = cfg.head_back_radius_mm;
    let width = cfg.head_width_mm;
    /* Laisvo kelio reikalavimas w (DefaultSupportTree.cpp:449-456):
         lmin = head_width; if (back_r < head_back_radius) { lmin = 0; }
         w = lmin + 2*back_r + 2*head_front_radius - penetration
       Prie plonos galvutės lmin krenta į NULĮ, tad w = 0,80 mm vietoj 4,20 —
       būtent tam ji ir yra, ankštoms vietoms. */
    const lmin = r => (r < cfg.head_back_radius_mm ? 0 : cfg.head_width_mm);
    const need = r => lmin(r) + 2 * r + 2 * cfg.head_front_radius_mm - cfg.head_penetration_mm;
    // Galvutė statoma nuo paviršiaus taško kryptimi dir.
    let hit = pinheadHit(mesh, p.pos, dir, cfg.head_front_radius_mm, rBack,
                         need(rBack), safetyDistance(rBack, cfg));
    /* Nepavykus originalas NEmeta taško, o ieško kitos krypties, kuri
       nesikirstų su modeliu ir būtų kuo arčiau normalės
       (DefaultSupportTree.cpp:467-499; ten tam naudojamas NLopt genetinis
       optimizatorius). Tikslo funkcija ir rėžiai čia tie patys —
       maksimizuojamas pinhead_mesh_hit atstumas, polar leidžiamas nuo
       PI-bridge_slope iki PI, azimutas visas ratas. Skiriasi tik paieškos
       būdas: tvarkinga tinklelio apžvalga vietoj genetinės, nes ji
       determinuota ir nereikalauja bibliotekos (V: įrankis gali skirtis,
       matematika ne). */
    if (hit < need(rBack)) {
      const base = Math.atan2(dir[1], dir[0]);
      let bestDir = dir, bestHit = hit;
      const want = need(rBack);
      /* Kandidatai atrenkami VIENU spinduliu palei ašį — aštuonių spindulių
         pluoštas kiekvienai iš 48 krypčių suėsdavo 32 s iš 39 (išmatuota
         etapų laikmačiais 08-13). Laimėtojas patikrinamas pilnu pluoštu, tad
         priimamas atsakymas lieka toks pat griežtas. */
      const probes = [];
      for (let a = 0; a < 12; a++) {
        const az = base + (a / 12) * 2 * Math.PI;
        for (let k = 0; k <= 3; k++) {
          const polar = Math.PI - (k / 3) * cfg.bridge_slope;
          const st = Math.sin(polar), ct = Math.cos(polar);
          const d2 = [st * Math.cos(az), st * Math.sin(az), ct];
          const h2 = mesh.rayHit(add(p.pos, mul(d2, cfg.head_front_radius_mm)), d2).dist;
          probes.push({ d: d2, h: h2 });
        }
      }
      /* Vienas spindulys tik ATRENKA; sprendžia pilnas pluoštas. Tikrinam tris
         geriausius, nes pirmasis dažnai krenta pluošte — tikrinant tik jį
         galvučių likdavo 189 vietoj 373, o tikrinant visus 48 pilnai
         skaičiavimas užtrukdavo 39 s vietoj 7. */
      probes.sort((x, y) => y.h - x.h);
      for (const pr of probes.slice(0, 3)) {
        const full = pinheadHit(mesh, p.pos, pr.d, cfg.head_front_radius_mm, rBack,
                                want, safetyDistance(rBack, cfg));
        if (full > bestHit) { bestHit = full; bestDir = pr.d; }
        if (bestHit > want) break;
      }
      dir = bestDir; hit = bestHit;
    }
    if (hit < need(rBack) && rBack > cfg.head_fallback_radius_mm) {
      rBack = cfg.head_fallback_radius_mm;
      width = lmin(rBack);                         // plona galvutė gali būti 0 ilgio
      hit = pinheadHit(mesh, p.pos, dir, cfg.head_front_radius_mm, rBack,
                       need(rBack), safetyDistance(rBack, cfg));
    }
    if (!(hit > need(rBack))) continue;            // netelpa — taško atsisakom
    const junction = add(p.pos, mul(dir, width));
    if (junction[2] < cfg.base_height_mm) continue;
    heads.push({ pos: p.pos, dir, rBack, width, junction, pillar: -1, onModel: false });
  }
  log.heads = heads.length;
  lap('pinheads');
  /* Pultas laukia (padaryta, iš viso) — vardinis etapas jam duodavo NaN%. */
  if (onProgress) onProgress(pts.length, pts.length);

  /* --- 2 · classify (DefaultSupportTree.cpp:528) ------------------------- */
  /* Ar atramos pėda telpa ant plokštės. Skaičiuojam ne „nuo akies": pėda yra
     `base_radius_mm` spindulio diskas, o aplink jį dar spausdinamas pado
     apvadas — abu turi tilpti į 40,8 × 30,6 mm ekraną (modelio koordinatės
     centruotos ties nuliu). */
  /* DVI ribos, ir eiliškumas svarbus:
       PLATI  — pėda + pado apvadas: gražiausia, viskas telpa su atsarga;
       SIAURA — tik pėdos diskas: dar priimtina, apvadas gali būti apkarpytas.
     Tilto nusileidimo taško ieškom PIRMA su plačiąja; neradę — su siaurąja; ir
     tik tada leidžiam stotis ties kraštu. Vien plačioji buvo per griežta
     (biuste atsirado trečia sala), vien siauroji — per atlaidi (puodelis liko
     su 180 taškų ties kraštu). Kaskada duoda abu (išmatuota 08-16). */
  const edgeStrict = cfg.plate_edge_margin_mm != null ? cfg.plate_edge_margin_mm
                   : cfg.base_radius_mm + cfg.pad_brim_mm;
  const edgeLoose = cfg.plate_edge_margin_mm != null ? cfg.plate_edge_margin_mm
                  : cfg.base_radius_mm;
  const fitsWith = (x, y, m) => Math.abs(x) <= PLATE.x / 2 - m &&
                                Math.abs(y) <= PLATE.y / 2 - m;
  const footFits = (x, y) => fitsWith(x, y, edgeLoose);
  log.edgeForced = 0;    // pėdų, kurioms neleidom stotis ties kraštu
  log.edgeSaved = 0;     // iš jų — kiek pavyko nuvesti į vidų

  const ground = [], onModel = [];
  for (let i = 0; i < heads.length; i++) {
    const h = heads[i];
    /* Spindulys ŽEMYN nuo jungties taško: jei niekas nekliūva — pilnas stulpas.
       Originale (DefaultSupportTree.cpp:547) čia saugos atstumas NEperduodamas —
       klausiama tik „ar kelias laisvas", ne „ar laisvas su atsarga". Pridėjus jį
       pluoštas užkabindavo pačią detalę ir 80 iš 83 taškų klaidingai virsdavo
       atramomis ant modelio. */
    /* `bridge_mesh_intersect(headjp, DOWN, r)` (cpp:547) yra 3 argumentų
       perkrova, kuri saugos atstumą PASISKAIČIUOJA pati — tai ne „be atsargos". */
    const scan = beamHitFull(mesh, h.junction, DOWN, h.rBack, h.rBack,
                             bridgeSafety(h.rBack, cfg));
    if (!(scan.dist < INF)) {
      /* Kelias žemyn laisvas, BET pėda nubėgtų už ekrano krašto. Tokią galvutę
         siunčiam tuo pačiu keliu kaip „ant modelio": pirma kabinam prie
         kaimyninio stulpo, tada ieškom tilto į vidų. Paskutinė išeitis (jei nė
         vienas nepavyksta) — vis dėlto pastatyti ties kraštu: nukirsta atrama
         vis tiek geriau nei jokios (žr. routing_to_model pabaigą). */
      if (!footFits(h.junction[0], h.junction[1])) {
        h.edgeForced = true;
        log.edgeForced++;
        onModel.push(i);
      } else ground.push(i);
    }
    else if (cfg.ground_facing_only) continue;
    else {
      h.onModel = true;
      h.groundHit = scan.dist;
      h.groundHitPos = scan.pos;      // `m_head_to_ground_scans[i] = hit`
      onModel.push(i);
    }
  }
  const clusters = clusterHeads(ground.map(i => heads[i]), cfg)
    .map(cl => cl.map(k => ground[k]));
  log.ground = ground.length;
  log.onModel = onModel.length;
  log.clusters = clusters.length;
  lap('classify');

  /* --- 3 · routing_to_ground (DefaultSupportTree.cpp:577) ---------------- */
  const pillars = [], bridges = [];
  const addPillar = (h, id) => {
    const p = { x: h.junction[0], y: h.junction[1],
                top: h.junction[2], bottom: 0, rTop: h.rBack, rBase: cfg.base_radius_mm,
                head: id, bridges: 0 };
    pillars.push(p);
    h.pillar = pillars.length - 1;
    return pillars.length - 1;
  };
  /** connect_to_nearpillar (DefaultSupportTree.cpp:282-363), eilutė po eilutės.
   *  Grąžina true, jei galvutė prikabinta prie nurodyto stulpo tiltu. */
  const connectToNearpillar = (h, pid) => {
    const pil = pillars[pid];
    if (pil.bridges > cfg.max_bridges_on_pillar) return false;
    const headjp = h.junction;
    const nearU = [pil.x, pil.y, pil.top];      // startpoint
    const nearL = [pil.x, pil.y, pil.bottom];   // endpoint
    const r = h.rBack;
    const d2d = dist2d(headjp, nearU);
    const d3d = dist3d(headjp, nearU);
    const hdiff = nearU[2] - headjp[2];
    const slope = Math.atan2(hdiff, d2d);
    let bridgestart = headjp.slice();
    let bridgeend = nearU.slice();
    const maxLen = r * cfg.max_bridge_length_mm / cfg.head_back_radius_mm;
    const maxSlope = cfg.bridge_slope;
    let zdiff = 0;

    if (d3d > maxLen || slope > -maxSlope) {
      // Tiesiai į stulpo viršūnę netinka — ieškom prisilietimo taško žemiau.
      let Zdown = headjp[2] + d2d * Math.tan(-maxSlope);
      const touch = [nearU[0], nearU[1], Zdown];
      const D = dist3d(headjp, touch);
      zdiff = Zdown - nearU[2];
      if (zdiff > 0) {
        Zdown -= zdiff;
        bridgestart[2] -= zdiff;
        // Po galvute reikia dalinio stulpelio — patikrinam, ar ten laisva.
        if (beamHit(mesh, headjp, DOWN, r, r, passSafety(r, cfg)) < zdiff) return false;
      }
      if (Zdown <= nearU[2] && Zdown >= nearL[2] && D < maxLen) bridgeend[2] = Zdown;
      else return false;
    }
    // Empirinė riba: prie pat plokštės tiltas nekabinamas.
    if (bridgeend[2] < 4 * cfg.head_back_radius_mm) return false;
    const need = dist3d(bridgestart, bridgeend);
    if (beamHit(mesh, bridgestart, norm(sub(bridgeend, bridgestart)), r, r,
                passSafety(r, cfg)) < need)
      return false;
    if (pil.bridges >= cfg.max_bridges_on_pillar) return false;
    if (zdiff > 0) {
      // Dalinis stulpelis po galvute + tiltas nuo jo.
      pillars.push({ x: headjp[0], y: headjp[1], top: headjp[2],
                     bottom: bridgestart[2], rTop: r, rBase: r,
                     head: h.id, bridges: 0, partial: true });
    }
    bridges.push({ a: bridgestart.slice(), b: bridgeend.slice(), r, head: true });
    pil.bridges++;
    h.pillar = pid;
    return true;
  };

  /** search_pillar_and_connect (cpp:723): artimiausias stulpas pagal XY; jei
   *  prikabinti nepavyko, jis išbraukiamas ir ieškoma toliau. */
  const searchPillarAndConnect = h => {
    const tried = new Set();
    for (;;) {
      let best = -1, bestD = INF;
      for (let k = 0; k < pillars.length; k++) {
        if (tried.has(k) || pillars[k].partial) continue;
        const d = Math.hypot(pillars[k].x - h.junction[0], pillars[k].y - h.junction[1]);
        if (d < bestD) { bestD = d; best = k; }
      }
      if (best < 0) return false;
      if (connectToNearpillar(h, best)) return true;
      tried.add(best);
    }
  };

  for (const cl of clusters) {
    const cIdx = centroidOf(cl, heads);
    addPillar(heads[cIdx], cIdx);
    for (const i of cl) {
      if (i === cIdx) continue;
      const h = heads[i];
      h.id = i;
      /* Originalo tvarka (cpp:639-644): centrinis stulpas -> bet kuris kitas
         -> savo stulpas. */
      if (connectToNearpillar(h, heads[cIdx].pillar)) continue;
      if (searchPillarAndConnect(h)) continue;
      addPillar(h, i);
    }
  }

  /** `connect_to_ground` -> `deepsearch_ground_connection`
   *  (SupportTreeUtils.hpp:600-700). TRŪKSTAMA VIDURINĖ PAKOPA: originale
   *  `routing_to_model` bando stulpą šalia, **kelią į plokštę**, ir tik tada
   *  remiasi į patį modelį (cpp:773-783). Be jos kiekviena „ant modelio"
   *  galvutė numeta stulpelį ant detalės, ir narvas išeina perpus retesnis.
   *
   *  Ieškoma tilto krypties (polar rėžiuose [PI - bridge_slope, PI]) ir ilgio
   *  [0, max_bridge_length] taip, kad iš nusileidimo taško vertikalus stulpas
   *  pasiektų plokštę neužkliuvęs. Originale tai NLopt MLSL; čia tinklelis, po
   *  jo — tas pats ilgio trumpinimas žingsniu r. */
  const connectToGround = h => {
    const src = h.junction, r = h.rBack, sd = passSafety(r, cfg);
    const gnd = cfg.object_elevation_mm;
    if (src[2] <= gnd + cfg.base_height_mm) return false;
    /* Kryptys paruošiamos vieną kartą, o ilgis auga VISOMS iš karto: pirmas
       radinys tada ir yra trumpiausias tiltas, ir paieška nutrūksta. Anksčiau
       kiekviena kryptis buvo perrenkama iki galo — maršrutizacija truko 8,7 s. */
    /* VISOS 48 kryptys. Bandžiau atrinkti aštuonias pagal ilgiausią laisvą
       spindulį — klaida: svarbu ne kiek laisva ta kryptimi, o ar iš
       NUSILEIDIMO TAŠKO yra laisvas kelias žemyn, o to vienas spindulys palei
       tiltą nematuoja. Su atranka dėžučių testas liko be stulpų, o tikrame
       modelyje jų sumažėjo 30 -> 26. Greitį duoda ne siauresnė paieška, o
       pigus atmetimas žemiau. */
    const dirs = [];
    for (let k = 0; k < 4; k++) {
      const polar = Math.PI - (k / 3) * cfg.bridge_slope;
      const st = Math.sin(polar), ct = Math.cos(polar);
      for (let a = 0; a < 12; a++) {
        const az = (a / 12) * 2 * Math.PI;
        const n = [st * Math.cos(az), st * Math.sin(az), ct];
        // lmax — vienu spinduliu; tiltą vis tiek patikrins pluoštas žemiau.
        const free = mesh.rayHit(add(src, mul(n, r)), n).dist;
        dirs.push({ n, lmax: Math.min(cfg.max_bridge_length_mm,
                    Number.isFinite(free) ? free : cfg.max_bridge_length_mm) });
      }
    }
    let best = null;
    const step = Math.max(r, 1e-3);
    /* Du praėjimai: pirma ieškom vietos su ATSARGA (pėda + apvadas), ir tik
       neradę tenkinamės ta, kur telpa bent pėda. Taip atrama traukiama į vidų
       tiek, kiek modelis leidžia, bet dėl to neprarandama pati atrama. */
    for (const margin of [edgeStrict, edgeLoose]) {
      if (best) break;
      for (let l = 0; l <= cfg.max_bridge_length_mm && !best; l += step)
        for (const d of dirs) {
          if (l > d.lmax) continue;
          const p = add(src, mul(d.n, l));
          if (p[2] <= gnd + cfg.base_height_mm) continue;
          /* Pigus atmetimas: viena ašis. Beveik visi kandidatai krenta čia, ir
             brangaus pluošto jiems nebereikia. */
          if (mesh.rayHit(p, DOWN).dist < p[2] - gnd) continue;
          // Nusileidimo taškas turi ir TILPTI ant plokštės - tiltas į kraštą
          // nieko neduoda, ten pėda vis tiek būtų nukirsta.
          if (!fitsWith(p[0], p[1], margin)) continue;
          // Tikras sprendimas — pluoštas su saugos atstumu.
          if (beamHit(mesh, p, DOWN, r, r, sd) < p[2] - gnd) continue;
          if (beamHit(mesh, src, d.n, r, r, sd) < l) continue;   // ir pats tiltas
          best = { l, p };
          break;
        }
    }
    if (!best) return false;
    pillars.push({ x: best.p[0], y: best.p[1], top: best.p[2], bottom: gnd,
                   rTop: r, rBase: cfg.base_radius_mm, head: h.id, bridges: 0 });
    if (best.l > 1e-6) bridges.push({ a: src.slice(), b: best.p.slice(), r });
    h.pillar = pillars.length - 1;
    return true;
  };

  /* --- 4 · routing_to_model (DefaultSupportTree.cpp:760-789) ------------- */
  /* Tvarka originale griežta: pirma ieškom stulpo šalia, tada kelio į plokštę,
     ir tik kaip PASKUTINĖ išeitis remiamės į patį modelį. Praleidus dvi
     pirmąsias pakopas 149 iš 182 atramų iškart atsidurdavo ant detalės
     (išmatuota 08-13). */
  for (const i of onModel) {
    const h = heads[i];
    h.id = i;
    if (searchPillarAndConnect(h)) { if (h.edgeForced) log.edgeSaved++; continue; }
    if (connectToGround(h)) { if (h.edgeForced) log.edgeSaved++; continue; }
    /* Krašto atvejis: kelio į vidų neradom. Geriau atrama, kurios pėda bus
       apkarpyta ekrano krašto, nei visai jokios — be jos ta vieta liktų sala.
       (Būtent taip elgiasi ir PrusaSlicer, tik jis kitaip ir nebando.) */
    if (h.edgeForced) { addPillar(h, i); continue; }
    /* connect_to_model_body (cpp:670-706). Originale atramos taškas imamas iš
       DVIEJŲ matavimų: pluošto skeno iš classify (`hit`) ir spindulio palei
       AŠĮ (`center_hit = m_sm.emesh.query_ray_hit(hjp, DOWN)`), o galutinis —
           hitdiff = center_hit.distance() - hit.distance();
           hitp = |hitdiff| < 2*head.r_back_mm ? center_hit.position()
                                              : hit.position();
       Mūsų stulpas vertikalus, tad remtis galima TIK tuo, kas po ašimi.
       Pluošto žiedas (r = 0,5 mm) mato ir tai, ko po ašimi nėra: prie žemesnio
       kūno krašto jis užkabina briauną, ir stulpas atsidurdavo ore (išmatuota
       08-13: 3 iš 3 stulpų kabojo 4 mm virš nieko). Pluoštas lieka tik
       klausimui „ar kelias laisvas" — būtent tam jis ir naudojamas classify. */
    if (!Number.isFinite(h.groundHit)) continue;     // !hit.is_hit() -> return false
    const centre = mesh.rayHit(h.junction, DOWN);
    /* `hitp` — kur atsiremia apversta galvutė (cpp:699-701):
         hitdiff = center_hit.distance() - hit.distance();
         hitp = |hitdiff| < 2*r_back ? center_hit.position() : hit.position();
       Kai ašis ir pluoštas nesutampa, atrama krypsta į pluošto pataikymo tašką
       — anksčiau tokių galvučių tiesiog atsisakydavom, nes pasvirusių nepiešėm. */
    const hitdiff = centre.dist - h.groundHit;
    const onAxis = Math.abs(hitdiff) < 2 * h.rBack && Number.isFinite(centre.dist);
    const hitp = onAxis
      ? [h.junction[0], h.junction[1], h.junction[2] - centre.dist]
      : h.groundHitPos;
    if (!hitp) continue;                    // nėra kur atsiremti
    /* Spindulys eina iš pačios jungties, tad atstumas jau tikras — pluošto
       poslinkio (+r_back, SupportTreeUtils.hpp:179) čia nebėra ką kompensuoti. */
    /* `endp.z = hjp.z - hit.distance() + h` (cpp:696) — nuo PLUOŠTO atstumo,
       kaip originale, o ne nuo ašies. */
    const surface = h.junction[2] - h.groundHit;
    /* Stulpas NEVAROMAS į paviršių: jis baigiasi `hh` aukščiau, o likusį tarpą
       uždengia APVERSTA galvutė (`add_anchor`, cpp:684-706), smailėjanti iki
       head_front_radius. Tai ne grožis — storas stulpas, atremtas į detalę,
       nulūždamas palieka 1 mm žymę, o galvutė nusilaužia švariai.
         zangle = max(asin(dir.z), PI/4);  dir = DOWN -> PI/4
         hh = min(hit.distance() - r_back, sin(zangle) * fullwidth) */
    const fullwidth = 2 * cfg.head_front_radius_mm + h.width +
                      2 * h.rBack - cfg.head_penetration_mm;
    let hh = Math.min(h.groundHit - h.rBack, Math.SQRT1_2 * fullwidth);
    if (h.rBack < cfg.head_back_radius_mm) hh = Math.max(hh, 0);
    else if (hh <= 0) continue;
    const bottom = Math.max(0, surface + hh);
    /* JOKIO minimalaus aukščio filtro: originale (cpp:684-706) po `h` patikros
       stulpas statomas visada, koks trumpas bebūtų — `add_pillar(head.id,
       hjp.z - endp.z)`. Čia buvo mano priedas „trumpesnis nei pėdos aukštis —
       atmetam", ir jis tyliai išmesdavo galvutes, kurios į modelį atsiremia vos
       0,14 mm žemiau (įdubose). Tokios galvutės likdavo NAŠLAITĖS: taškas
       pasėtas, galvutė sukurta, o piešinyje nieko — biuste 8, „evil" 2, ir
       būtent jos pulte matėsi kaip salos be atramos (08-14). */
    if (h.junction[2] - bottom <= 1e-6) continue;   // išsigimęs, nulinio ilgio
    pillars.push({ x: h.junction[0], y: h.junction[1], top: h.junction[2],
                   bottom, rTop: h.rBack, rBase: h.rBack, head: i, bridges: 0,
                   onModel: true, anchored: true });
    /* Inkaras dedamas VISADA, o ne tik kai `hh > 0`: originale `add_anchor`
       kviečiamas besąlygiškai (cpp:706-716), tik plotis apkarpomas iki nulio.
       Praleidus jį prie plonos galvutės, stulpo apačia likdavo kaboti virš
       paviršiaus — „evil" ties z=24,8 tarpas 0,98 mm (sargas pagavo 08-15).
       Tarpas atsiranda todėl, kad `groundHit` matuotas PLUOŠTU (su spinduliu ir
       atsarga), tad jis sustoja anksčiau nei tikrasis paviršius `hitp`.
       Atkarpa vertikali, kai ašis sutampa, ir pasvirusi, kai ne
       (`taildir = (endp - hitp).normalized()`, cpp:706). */
    if (dist3d([h.junction[0], h.junction[1], bottom], hitp) > 1e-6)
      bridges.push({ a: [h.junction[0], h.junction[1], bottom],
                     b: hitp.slice(), r: h.rBack, anchor: true });
    h.pillar = pillars.length - 1;
  }

  lap('routing');

  /* --- 5 · interconnect_pillars (DefaultSupportTree.cpp:189, 792) -------- */
  const links = [];
  const zmin = cfg.base_height_mm;
  /* DefaultSupportTree.cpp:815-851 `cascadefn`: kiekvienas stulpas jungiasi tik
     su ARTIMIAUSIAIS kaimynais ir tik tol, kol turi mažiau nei
     pillar_cascade_neighbors jungčių; kiekviena pora jungiama vieną kartą
     (`pairs` aibė). Jungiant visas poras iš eilės narvas išeidavo dvigubai
     tankesnis nei PrusaSlicer'io (auditas + matavimas 08-13). */
  /* Vienos poros jungimas — iškelta, nes tuo pačiu jungiami ir PAGALBINIAI
     stulpai (žr. žemiau). */
  const interconnectPair = (A, B) => {
    const d = Math.hypot(A.x - B.x, A.y - B.y);
      const bridgeDistance = d / Math.cos(-cfg.bridge_slope);
    const zstep = d * Math.tan(-cfg.bridge_slope);
    let sUp = A.top, sLo = B.top;
    let eUp = Math.max(A.bottom, zmin), eLo = Math.max(B.bottom, zmin);
    let ax = A.x, ay = A.y, bx = B.x, by = B.y;
    if (sUp - eUp < 0 || sLo - eLo < 0) return false;
    if (sUp < sLo) { [sUp, sLo] = [sLo, sUp]; [ax, bx] = [bx, ax]; [ay, by] = [by, ay]; }
    if (eUp < eLo) [eUp, eLo] = [eLo, eUp];
    let startz = (sLo - zstep < sUp) ? sLo - zstep : sLo;
    if (sLo - eUp < Math.abs(zstep)) {
      startz = Math.min(sUp, sLo - zstep);
      const endz = Math.max(eUp + zstep, eLo);
      const avail = startz - endz;
      const rounds = Math.floor(avail / Math.abs(zstep));
      startz -= 0.5 * (avail - rounds * Math.abs(zstep));
    }
    let a = [ax, ay, startz], b = [bx, by, startz + zstep];
    let made = false, guard = 0;
    while (b[2] >= eUp && guard++ < 200) {
      /* Tikrinama STULPO spinduliu (`bridge_mesh_distance(sj, dir,
         pillar.r_start)`, cpp:256), ne galvutės smaigalio — o smaigalys
         dvigubai plonesnis. Su per plonu pluoštu jungtys prasispraudžia
         pro vietas, kur jos netelpa: puodelio narvas išėjo su tankiu
         zigzagu ten, kur etalonas turi vieną žiedą. Saugos atstumą 3
         argumentų perkrova pasiskaičiuoja pati. */
      const rLink = A.rTop || cfg.pillar_radius_mm;
      if (beamHit(mesh, a, norm(sub(b, a)), rLink, rLink,
                  passSafety(rLink, cfg)) >= bridgeDistance) {
        links.push({ a: a.slice(), b: b.slice(), r: cfg.pillar_radius_mm });
        made = true;
      }
      const t = a; a = b; b = [t[0], t[1], a[2] + zstep];
    }
    return made;
  };

  for (const p of pillars) p.links = 0;
  const donePairs = new Set();
  const order = pillars.map((p, i) => i);
  for (const i of order) {
    const A = pillars[i];
    if (A.links >= cfg.pillar_cascade_neighbors) continue;
    const maxD = cfg.max_pillar_link_distance_mm *
                 (A.rTop || cfg.pillar_radius_mm) / cfg.head_back_radius_mm;
    const near = [];
    for (let j = 0; j < pillars.length; j++) {
      if (j === i) continue;
      const d = Math.hypot(A.x - pillars[j].x, A.y - pillars[j].y);
      if (d < maxD) near.push({ j, d });
    }
    near.sort((a, b) => a.d - b.d);
    for (const { j, d } of near) {
      if (A.links >= cfg.pillar_cascade_neighbors) break;
      const key = i < j ? i + ':' + j : j + ':' + i;
      if (donePairs.has(key)) continue;
      const B = pillars[j];
      if (d < 2 * cfg.head_back_radius_mm) continue;
      /* cpp:856 — plonesnis kaimynas praleidžiamas: jungtis turi eiti į bent
         tokį pat storą stulpą. */
      if ((B.rTop || cfg.pillar_radius_mm) < (A.rTop || cfg.pillar_radius_mm)) continue;
      const made = interconnectPair(A, B);
      donePairs.add(key);
      if (made) {
        /* cpp:860-869 — jungtis SKAIČIUOJAMA tik tada, kai kaimynas nėra
           daug žemesnis: „if the interconnection length between the two
           pillars is less than 50% of the longer pillar's height, don't
           count". Žemam stulpui (žemiau max_solo_pillar_height) skaičiuojama
           visada. */
        const hA = A.top - A.bottom, hB = B.top - B.bottom;
        if (hA < cfg.max_solo_pillar_height_mm || hB / hA > 0.5) A.links++;
        if (hB < cfg.max_solo_pillar_height_mm || hA / hB > 0.5)
          B.links = (B.links || 0) + 1;
      }
    }
  }

  /* Vienišam aukštam stulpui pristatomas PAGALBINIS (cpp:884-975). Kaskada
     sujungia tik tai, kas turi kaimyną; likę vieniši aukšti stulpai originale
     negalioja — jiems šalia pastatomas naujas stulpas ir su juo susijungiama,
     kad ilgas plonas strypas nesiūbuotų ir nenulūžtų.
       bridges > max_bridges_on_pillar -> 3 pagalbiniai
       links < 2 ir aukštis > 35 mm    -> 2
       links < 1 ir aukštis > 15 mm    -> 1
     Vieta ieškoma ratu spinduliu 2×base_radius, 20 kampų; kandidatas tinka, kai
     kelias žemyn laisvas IR pėda toliau nei base_safety nuo detalės. Šito etapo
     neturėjom visai, ir „evil" bei biuste likdavo po vieną vienišą 15+ mm
     stulpą (V pastaba 08-15). */
  {
    const gnd = 0, rSearch = 2 * cfg.base_radius_mm;
    const minDist = cfg.pillar_base_safety_distance_mm + cfg.base_radius_mm + 1e-6;
    const count = pillars.length;                 // naujų nebeapdorojam
    for (let pid = 0; pid < count; pid++) {
      const P = pillars[pid];
      if (P.partial || P.onModel) continue;       // tik tie, kur stovi ant plokštės
      const hgt = P.top - P.bottom;
      let need = 0;
      if ((P.bridges || 0) > cfg.max_bridges_on_pillar) need = 3;
      else if ((P.links || 0) < 2 && hgt > cfg.max_dual_pillar_height_mm) need = 2;
      else if ((P.links || 0) < 1 && hgt > cfg.max_solo_pillar_height_mm) need = 1;
      need = Math.max(P.links || 0, need) - (P.links || 0);
      if (need <= 0) continue;

      let found = false, spts = null;
      for (let k = 0; k < 20 && !found; k++) {
        const alpha = k * 0.1 * Math.PI;
        const cand = [];
        for (let n = 0; n < need; n++) {
          const ang = alpha + n * Math.PI / 3;
          const sx = P.x + Math.cos(ang) * rSearch;
          const sy = P.y + Math.sin(ang) * rSearch;
          const sz = P.top - rSearch;
          if (sz <= gnd + cfg.base_height_mm) break;
          if (!footFits(sx, sy)) break;   // pagalbinė pėda irgi turi tilpti
          const r0 = P.rTop || cfg.pillar_radius_mm;
          // kelias žemyn turi būti laisvas per visą aukštį
          if (Number.isFinite(beamHit(mesh, [sx, sy, sz + r0], DOWN, r0, r0,
                                      passSafety(r0, cfg)))) break;
          // pėda neturi lįsti prie detalės (`squared_distance(gndsp) > min_dist`)
          const hit = mesh.closestFace([sx, sy, gnd]);
          if (hit && dist3d([sx, sy, gnd], hit.q) < minDist) break;
          cand.push([sx, sy, sz]);
        }
        if (cand.length === need) { found = true; spts = cand; }
      }
      if (!found) continue;
      for (const sp of spts) {
        const np = { x: sp[0], y: sp[1], top: sp[2], bottom: gnd,
                     rTop: P.rTop || cfg.pillar_radius_mm, rBase: cfg.base_radius_mm,
                     head: -1, bridges: 0, links: 0, helper: true };
        if (interconnectPair(P, np)) {
          pillars.push(np);
          P.links = (P.links || 0) + 1;
          np.links = 1;
          /* Ir TILTAS nuo vieniso stulpo viršaus į pagalbinio viršų
             (`add_bridge(pillarsp, s)`, cpp:975-977). Be jo pagalbinio galas
             styro į nieką: zigzagas prisikabina žemiau, o virš jo lieka
             3 mm bereikalingo strypo — būtent tą V ir pamatė (08-15). */
          const topA = [P.x, P.y, P.top], topB = [np.x, np.y, np.top];
          const need = dist3d(topA, topB);
          const rB = P.rTop || cfg.pillar_radius_mm;
          if (beamHit(mesh, topA, norm(sub(topB, topA)), rB, rB,
                      passSafety(rB, cfg)) >= need)
            bridges.push({ a: topA, b: topB, r: rB });
        }
      }
    }
  }

  /* Nė vienas stulpas nekyla aukščiau savo AUKŠČIAUSIO sujungimo (V taisyklė
     08-15: „stulpas turėtų baigtis ties sujungimu"). Viršus prasmingas tik
     tada, kai ten yra galvutė (tada `head >= 0`) arba kažkas prisikabina;
     virš to strypas nieko nelaiko, tik eikvoja dervą ir lūžinėja. */
  for (const p of pillars) {
    if (p.head >= 0 || p.partial) continue;        // viršuje galvutė — paliekam
    let hi = -Infinity;
    for (const l of links)
      for (const e of [l.a, l.b])
        if (Math.hypot(e[0] - p.x, e[1] - p.y) < 1e-6) hi = Math.max(hi, e[2]);
    for (const c of bridges)
      for (const e of [c.a, c.b])
        if (Math.hypot(e[0] - p.x, e[1] - p.y) < 1e-6) hi = Math.max(hi, e[2]);
    if (Number.isFinite(hi) && hi > p.bottom && p.top - hi > 1e-6) p.top = hi;
  }

  /* --- 6 · merge_result -------------------------------------------------- */
  lap('interconnect');
  /* Prasilenkimo tarpas (`clearance_mm`, 1 mm) yra PAGEIDAVIMAS, ne absoliutas.
     Ankštoje vietoje su juo kelio gali nebūti visai — dviejų dėžių tarpe (4 mm)
     jis palieka 0 stulpų vietoj 2, t. y. visa viršutinė dalis liktų be atramos.
     Nepriremta detalė blogiau nei atrama, prie kurios sunkiau prilįsti replėmis,
     tad neradus kelio perstatom su geometriniu 0,5 (08-13, V taisyklė). */
  if (cfg.clearance_mm > cfg.safety_distance_mm &&
      heads.some(h => !(h.pillar >= 0 && h.pillar < pillars.length))) {
    /* Tikrinam INDEKSĄ, ne `undefined`: nepavykus inkarui `h.pillar` lieka
       `pillars.length - 1` = −1, tad „undefined" patikra nieko negaudė. */
    const relaxed = { ...cfg, clearance_mm: cfg.safety_distance_mm };
    const again = await buildSupportTree(pos, relaxed, onProgress);
    again.log.relaxed = true;
    return again;
  }
  /* Padas — po viskuo, kas stovi ant plokštės (SLA/Pad.hpp). */
  const pad = await buildPad(pos, pillars, cfg);
  log.pillars = pillars.length;
  log.bridges = bridges.length;
  log.links = links.length;
  return { pillars, bridges, links, heads, mesh, pad, log };
}

/* ------------------------------------------------- suderinamas paviršius */
/* Kad pultas ir stendas galėtų įkelti šį modulį nieko nekeisdami, grąžinam
   tokios pat formos rezultatą kaip `slicer.js`: stulpeliai su x/y/cx/cy/top/
   bottom ir jungtys su ax/ay/z0/bx/by/z1. */
export async function findOverhangs(pos, layers, onProgress) {
  const t = await buildSupportTree(pos, CFG, onProgress);
  const pillars = t.pillars.map(p => ({
    x: p.x, y: p.y, cx: p.x, cy: p.y, top: p.top, bottom: p.bottom,
    tower: !p.onModel,
    anchored: !!p.anchored,
    /* `partial` keliaujam kartu — be jo savikontrolė reikalaudavo medžiagos po
       stulpeliu, kuris remiasi į tiltą, ir visada degdavo raudonai. */
    partial: !!p.partial,
  }));
  /* Galvutės (heads) — atkarpa nuo jungties taško iki paties paviršiaus,
     smailėjanti į head_front_radius. Be jos stulpas baigiasi head_width_mm
     atstumu nuo detalės ir nieko nelaiko (matyta renderyje, 08-13).
     Piešiama kaip „bridge", nes piešėjas būtent tiltams daro smaigalį. */
  const heads = t.heads.filter(h => h.pillar >= 0).map(h => ({
    a: h.junction.slice(), b: h.pos.slice(), headTip: true,
  }));
  /* Piešėjas eina iš apačios į viršų, tad žemesnis galas turi būti pirmas.
     Tiltas nuo galvutės į stulpą leidžiasi žemyn — pirmiau jį tiesiog
     išmesdavau, ir dėl to galvutės kabodavo atskirai nuo narvo (08-13).
     Smaigalį daro tik GALVUTĖ (`bridge`); tiltai ir jungtys — vienodo storio,
     kaip ir originale. */
  const braces = [...t.bridges, ...t.links, ...heads].map(c => {
    const up = c.b[2] >= c.a[2];
    const lo = up ? c.a : c.b, hi = up ? c.b : c.a;
    return {
      ax: lo[0], ay: lo[1], z0: lo[2],
      bx: hi[0], by: hi[1], z1: hi[2],
      bridge: c.headTip === true,
      anchor: c.anchor === true,
    };
  });
  return {
    pillars, braces, companions: [],
    /* `slicer.js` jungtis piešia tik nuo `SUP.padMm` (1,5 mm) — senojo
       algoritmo taisyklė. Mūsų tiltai prasideda ir žemiau, tad ties ta riba
       jie „išnirdavo" ore: „evil" sluoksnyje z=1,55 — 5 salos be atramos iš 8
       (08-13). Vėliavėle prašom piešti nuo nulio, senojo elgesio neliečiant. */
    bracesFromZero: true,
    padLayers: CFG.pad_layers,      // pado storis piešiant — iš to paties konfigo
    /* Piešiam SAVO matmenimis (žr. `discsFor`) — `layerMask` be šito imtų
       senojo modulio SUP konstantas. */
    discsFor: zz => discsFor({ pillars, braces }, zz, CFG),
    towers: t.pillars.filter(p => !p.onModel).length,
    bridges: t.bridges.length,
    pad: t.pad,
    padMm2: t.pad ? t.pad.reduce((s2, v) => s2 + v, 0) * PIXEL_MM * PIXEL_MM : 0,
    /* Savikontrolė tikra, ne kietai įrašytas nulis. */
    islands: selfCheck({ pillars, braces }, t.mesh, CFG), firstIsland: 0,
    onModel: t.pillars.filter(p => p.onModel).length,
    dropped: false,
    log: t.log,
  };
}

/** Sluoksniai, peržiūra ir ZIP — ta pati funkcija kaip pirmajame algoritme,
 *  tik supportus jai duodam savo. Be šito pultas kviesdavo bazinį `slice()`,
 *  tas pasidarydavo SENUS supportus, ir ekrane matėsi trys stulpeliai vietoj
 *  viso narvo (V 08-13: „naujas algoritmas nupiešia tik tris suportus"). */
export function slice(pos, opts, onProgress) {
  return sliceBase(pos, { ...(opts || {}), findSupports: findOverhangs,
                          discsFor: (sup, z) => discsFor(sup, z, CFG),
                          padLayers: CFG.pad_layers }, onProgress);
}

/* ------------------------------------------------------------------- padas */
/* SLA/Pad.hpp: PadConfig { wall_thickness_mm, wall_height_mm, brim_size_mm,
   wall_slope = pi/4 }, full_height = wall_height + wall_thickness. V profilyje
   pad_wall_height = 0, pad_wall_thickness = 0.15, pad_brim_size = 1.6,
   pad_around_object = 1 — tad padas plonas ir apjuosia tai, kas ant plokštės.

   2D dalis (sujungimas ir apvado offset) daroma Clipper'iu, kaip nurodyta:
   savo poligonų matematikos nerašom. Clipper dirba sveikais skaičiais, tad
   milimetrai keliami SCALE kartų. */
const SCALE = 1000;

/** Atkarpos -> uždari keliai. sliceAt grąžina jas orientuotas (medžiaga
 *  kairėje), tad jungiam paprastai: kiekvienos galas yra kitos pradžia. */
function stitch(seg, eps = 1e-4) {
  const key = (x, y) => Math.round(x / eps) + ',' + Math.round(y / eps);
  const start = new Map();
  for (let i = 0; i < seg.length; i += 4) {
    const k = key(seg[i], seg[i + 1]);
    let l = start.get(k);
    if (!l) { l = []; start.set(k, l); }
    l.push(i);
  }
  const used = new Uint8Array(seg.length / 4);
  const paths = [];
  for (let i = 0; i < seg.length; i += 4) {
    if (used[i / 4]) continue;
    const path = [];
    let cur = i, guard = 0;
    while (cur !== undefined && !used[cur / 4] && guard++ < 1e6) {
      used[cur / 4] = 1;
      path.push({ X: Math.round(seg[cur] * SCALE), Y: Math.round(seg[cur + 1] * SCALE) });
      const l = start.get(key(seg[cur + 2], seg[cur + 3]));
      cur = l && l.find(j => !used[j / 4]);
    }
    if (path.length >= 3) paths.push(path);
  }
  return paths;
}

/** Apskritimas kaip Clipper kelias. */
function circlePath(cx, cy, r, n = 24) {
  const p = [];
  for (let i = 0; i < n; i++) {
    const a = 2 * Math.PI * i / n;
    p.push({ X: Math.round((cx + r * Math.cos(a)) * SCALE),
             Y: Math.round((cy + r * Math.sin(a)) * SCALE) });
  }
  return p;
}

/** Pado kaukė: viskas, kas stovi ant plokštės, sujungiama ir išplečiama
 *  brim_size. Grąžina Uint8Array (RES.w × RES.h), tokį patį, kokio tikisi
 *  sluoksnių piešėjas. */
export async function buildPad(pos, pillars, cfg = CFG) {
  const CL = (await import('./clipper.js')).default;
  const seg = [];
  sliceAt(pos, cfg.pad_thickness_mm * 0.5, seg);
  const foot = stitch(seg);          // pačios detalės pėdsakas
  const paths = foot.slice();
  for (const p of pillars)
    if (p.bottom <= 1e-6) paths.push(circlePath(p.x, p.y, cfg.base_radius_mm));
  if (!paths.length) return null;

  // Sujungiam viską į vieną figūrą (non-zero, kaip libslic3r).
  const c = new CL.Clipper();
  c.AddPaths(paths, CL.PolyType.ptSubject, true);
  const united = new CL.Paths();
  c.Execute(CL.ClipType.ctUnion, united, CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);

  // Apvadas: offset brim_size_mm.
  const co = new CL.ClipperOffset();
  co.AddPaths(united, CL.JoinType.jtRound, CL.EndType.etClosedPolygon);
  let grown = new CL.Paths();
  co.Execute(grown, cfg.pad_brim_mm * SCALE);

  /* `pad_around_object = 1` su `pad_object_gap = 1` (V profilis, prusa-full.ini):
     padas yra ŽIEDAS aplink detalę, o ne kilimas po ja — detalė pirmu sluoksniu
     lipa tiesiai prie plokštės. Iki šiol klojom ir po detale, ir tai buvo
     didžiausia dervos eilutė: puodeliui 94 mm³ iš 230 (41 %), kai visos atramos
     kartu sudaro 136 (išmatuota 08-15). */
  if (cfg.pad_object_gap_mm > 0 && foot.length) {
    const go = new CL.ClipperOffset();
    go.AddPaths(foot, CL.JoinType.jtRound, CL.EndType.etClosedPolygon);
    const gap = new CL.Paths();
    go.Execute(gap, cfg.pad_object_gap_mm * SCALE);
    const cut = new CL.Clipper();
    cut.AddPaths(grown, CL.PolyType.ptSubject, true);
    cut.AddPaths(gap, CL.PolyType.ptClip, true);
    const ring = new CL.Paths();
    cut.Execute(CL.ClipType.ctDifference, ring,
                CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
    grown = ring;
  }

  // Į kaukę: skenavimo eilutės per gautus poligonus.
  const W = RES.w, H = RES.h;
  const sx = W / PLATE.x, sy = H / PLATE.y;
  const mask = new Uint8Array(W * H);
  const xs = [];
  for (let row = 0; row < H; row++) {
    const yMm = (row + 0.5) / sy - PLATE.y / 2;
    xs.length = 0;
    for (const path of grown) {
      for (let i = 0; i < path.length; i++) {
        const a = path[i], b = path[(i + 1) % path.length];
        const ay = a.Y / SCALE, by = b.Y / SCALE;
        if ((ay > yMm) === (by > yMm)) continue;
        const t = (yMm - ay) / (by - ay);
        xs.push((a.X / SCALE + (b.X / SCALE - a.X / SCALE) * t + PLATE.x / 2) * sx);
      }
    }
    if (xs.length < 2) continue;
    xs.sort((p, q) => p - q);
    for (let k = 0; k + 1 < xs.length; k += 2) {
      const x0 = Math.max(0, Math.round(xs[k])), x1 = Math.min(W - 1, Math.round(xs[k + 1]));
      for (let x = x0; x <= x1; x++) mask[row * W + x] = 1;
    }
  }
  return mask;
}

/* ------------------------------------------------- taškai iš SLUOKSNIŲ */
/* SupportPointGenerator.cpp:409 `sample_overhangs`:
     overhangs = diff_ex(shape, prev_shapes)
   — nuokaba yra ne veido kampas, o SLUOKSNIO IR ANKSTESNIO SLUOKSNIO
   SKIRTUMAS, ir taškai sėjami ant to skirtumo KONTŪRO vienodu žingsniu.

   Būtent to trūko pirmojoje sėjoje: ėmus mesh veidus pagal normalę, 40 iš 261
   galvutės atsidurdavo viršutinėje modelio dalyje, kur niekas nekaba
   (V 08-13: „pankas"). Sluoksniuose ten figūra tik mažėja, tad skirtumo nėra
   ir taškų neatsiranda. */
/** Atramos taško įtakos spindulys, kai esam `dz` mm virš jo.
 *  `prepare_supports_for_layer` (SPG.cpp:495-543): tiesinė interpoliacija
 *  kreivėje; density mažina spindulį per sqrt(r² / density). */
function influenceRadius(dz, cfg) {
  const c = cfg.support_curve;
  let r;
  if (dz <= c[0][1]) r = c[0][0];
  else if (dz >= c[c.length - 1][1]) r = c[c.length - 1][0];
  else {
    r = c[c.length - 1][0];
    for (let k = 0; k + 1 < c.length; k++)
      if (dz >= c[k][1] && dz <= c[k + 1][1]) {
        const t = (dz - c[k][1]) / ((c[k + 1][1] - c[k][1]) || 1);
        r = c[k][0] + t * (c[k + 1][0] - c[k][0]);
        break;
      }
  }
  const d = cfg.support_points_density;
  return Math.abs(d - 1) > 1e-4 ? Math.sqrt(r * r / d) : r;
}

/** Clipper rezultatas -> ExPolygon atitikmenys (kontūras + jo skylės). */
function toExPolys(CL, tree) {
  const out = [];
  const stack = tree.Childs().slice();
  while (stack.length) {
    const n = stack.pop();
    if (n.IsHole()) { for (const ch of n.Childs()) stack.push(ch); continue; }
    const holes = n.Childs();
    out.push([n.Contour(), ...holes.map(h => h.Contour())]);
    for (const h of holes) stack.push(h);
  }
  return out;
}

function pathsBBox(paths) {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const p of paths) for (const q of p) {
    if (q.X < x0) x0 = q.X; if (q.X > x1) x1 = q.X;
    if (q.Y < y0) y0 = q.Y; if (q.Y > y1) y1 = q.Y;
  }
  return [x0, y0, x1, y1];
}

/** Kilpa -> taškai vienodu žingsniu (`sample()`, SPG.cpp:361). */
/** Kontūro taškus atitraukia nuo krašto, o siaurus ruožus perkelia į jų vidurio
 *  ašį — `minimal_distance_from_outline` (= galvutės spindulys) ir „thin"
 *  taisyklė iš 2.9.6 `UniformSupportIsland`.
 *
 *  Originalas siaurą ruožą atpažįsta iš Voronojaus diagramos ir sėja per jos
 *  medialinę ašį. Čia ašis randama zondu: nuo kontūro taško einam į vidų, kol
 *  išeinam anapus — tai vietinis PLOTIS; siaurame ruože taškas dedamas ties
 *  puse to pločio (juostai tai lygiai medialinė ašis), plačiame — atitraukiamas
 *  per `min_dist_from_outline`. Įrankis kitas, taisyklė ta pati.
 *
 *  Puodelio atbraila (žiedas 9…12 mm, plotis 3 < 4,67) taip atsiduria ties
 *  r = 10,5 — kaip etalone, o ne ant briaunos ties r = 12. */
function insetPoints(paths, pts, cfg, medial) {
  if (!paths || !paths.length) return pts.map(p => [p[0], p[1]]);
  const inside = (x, y) => pointInPaths(paths, x, y);   // even-odd, su skylėm
  const probe = 0.05, lim = cfg.thin_max_width_mm;
  const out = [];
  for (const p of pts) {
    const [x, y, tx, ty] = p;
    if (tx === undefined) { out.push([x, y]); continue; }
    let nx = -ty, ny = tx;
    if (!inside(x + nx * probe, y + ny * probe)) { nx = -nx; ny = -ny; }
    if (!inside(x + nx * probe, y + ny * probe)) { out.push([x, y]); continue; }
    let w = lim;
    for (let d = probe; d <= lim; d += 0.1)
      if (!inside(x + nx * d, y + ny * d)) { w = d; break; }
    const off = (medial && w < lim) ? w / 2 : cfg.min_dist_from_outline_mm;
    out.push([x + nx * off, y + ny * off]);
  }
  return out;
}

/** Vidaus taškai pagal DANGOS kriterijų, ne pagal fiksuotą tinklelį.
 *
 *  Dedam tašką ten, kur nuo ploto iki artimiausios atramos toliausia, ir
 *  kartojam, kol niekur nelieka toliau nei `coverage_max_mm`. Tai tiesiogiai
 *  užrašytas 4-as kriterijus (V, 08-13) ir kartu „nė vieno taško daugiau" —
 *  ciklas sustoja vos danga pasiekiama.
 *
 *  Anksčiau čia buvo `thick_inner_max_distance` tinklelis (6,5 mm). Ant didelės
 *  plokščios nuokabos jis paremdavo tik pakraštį: kronšteino viršus (16×10 mm)
 *  gaudavo vieną vidinį tašką, ir 17 mm² likdavo toliau nei 3 mm nuo bet ko
 *  (išmatuota 08-13; PrusaSlicer ten deda keturis). */
function coverInterior(paths, pbb, out, cfg, land) {
  const step = 0.4;                       // tinklelis PAIEŠKAI, ne taškams
  const cells = [];
  for (let y = pbb[1] / SCALE; y <= pbb[3] / SCALE; y += step)
    for (let x = pbb[0] / SCALE; x <= pbb[2] / SCALE; x += step)
      if (pointInPaths(paths, x, y)) cells.push([x, y]);
  if (!cells.length) return;
  /* Dangą duoda ne tik atramos, bet ir SAUSUMA — tai, kas jau sukietinta žemiau
     (pvz. sienelės, ant kurių guli plokštės kraštai). Be jos sėjom ir ten, kur
     detalė jau laikosi pati: kronšteino viršus gaudavo 15 kontaktų vietoj
     PrusaSlicer 6 prie tos pačios 2,91 mm dangos (08-14). */
  const d2 = cells.map(c => {
    let m = Infinity;
    for (const p of out) {
      const v = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2;
      if (v < m) m = v;
    }
    if (land && land.length) {
      const dl = pointInPaths(land, c[0], c[1]) ? 0 : distToPaths(land, c[0], c[1]);
      if (dl * dl < m) m = dl * dl;
    }
    return m;
  });
  const lim2 = cfg.coverage_max_mm ** 2;
  for (let guard = 0; guard < 400; guard++) {
    let best = -1, bestD = lim2;
    for (let i = 0; i < cells.length; i++) if (d2[i] > bestD) { bestD = d2[i]; best = i; }
    if (best < 0) break;                  // visur padengta
    const p = cells[best];
    out.push(p);
    for (let i = 0; i < cells.length; i++) {
      const v = (p[0] - cells[i][0]) ** 2 + (p[1] - cells[i][1]) ** 2;
      if (v < d2[i]) d2[i] = v;
    }
  }
}

function walkRing(path, step, into) {
  const n = path.length;
  let total = 0;
  for (let k = 0; k < n; k++) {
    const a = path[k], b = path[(k + 1) % n];
    total += Math.hypot((b.X - a.X) / SCALE, (b.Y - a.Y) / SCALE);
  }
  if (total < step) {
    const a0 = path[0], b0 = path[1 % n];
    const dx = (b0.X - a0.X) / SCALE, dy = (b0.Y - a0.Y) / SCALE;
    const L0 = Math.hypot(dx, dy) || 1;
    into.push([a0.X / SCALE, a0.Y / SCALE, dx / L0, dy / L0]);
    return;
  }
  const count = Math.max(1, Math.floor(total / step));
  const want = total / count;
  let acc = 0, next = 0;
  for (let k = 0; k < n; k++) {
    const a = path[k], b = path[(k + 1) % n];
    const ax = a.X / SCALE, ay = a.Y / SCALE;
    const L = Math.hypot(b.X / SCALE - ax, b.Y / SCALE - ay);
    while (next <= acc + L && into.length < 1e5) {
      const u = L ? (next - acc) / L : 0;
      /* Kartu įrašom liestinę — iš jos `insetPoints` gauna kryptį į vidų.
         Be jos taškas liktų ant pačios briaunos. */
      const L1 = L || 1;
      into.push([ax + (b.X / SCALE - ax) * u, ay + (b.Y / SCALE - ay) * u,
                 (b.X / SCALE - ax) / L1, (b.Y / SCALE - ay) / L1]);
      next += want;
    }
    acc += L;
  }
}

/** Ar taškas ExPolygon viduje (even-odd prieš visą rinkinį). */
function pointInPaths(paths, x, y) {
  let inside = false;
  for (const p of paths)
    for (let k = 0, j = p.length - 1, n = p.length; k < n; j = k++) {
      const kx = p[k].X / SCALE, ky = p[k].Y / SCALE;
      const jx = p[j].X / SCALE, jy = p[j].Y / SCALE;
      if ((ky > y) !== (jy > y) && x < (jx - kx) * (y - ky) / (jy - ky) + kx)
        inside = !inside;
    }
  return inside;
}

/** Mažiausias atstumas nuo taško iki kelių rinkinio kraštinių (mm). */
function distToPaths(paths, x, y) {
  let best = Infinity;
  for (const p of paths) {
    for (let k = 0, n = p.length; k < n; k++) {
      const a = p[k], b = p[(k + 1) % n];
      const ax = a.X / SCALE, ay = a.Y / SCALE;
      const bx = b.X / SCALE, by = b.Y / SCALE;
      const dx = bx - ax, dy = by - ay;
      const L2 = dx * dx + dy * dy;
      let t = L2 ? ((x - ax) * dx + (y - ay) * dy) / L2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      const d = Math.hypot(x - (ax + dx * t), y - (ay + dy * t));
      if (d < best) { best = d; if (best < 1e-7) return best; }
    }
  }
  return best;
}

/* ------------------------------------------------- taškai iš SLUOKSNIŲ */
/* SupportPointGenerator.cpp:409 `sample_overhangs`:
     overhangs = diff_ex(shape, prev_shapes)
   — nuokaba yra ne veido kampas, o SLUOKSNIO IR ANKSTESNIO SLUOKSNIO
   SKIRTUMAS, ir taškai sėjami ant to skirtumo KONTŪRO.

   Tankio NEVALDO pastovus žingsnis. Kraštas smulkiai diskretizuojamas
   (`discretize_overhang_step` = 2 mm), o kandidatas tampa atrama TIK jei jo
   neuždengia jau esančių atramų įtakos spindulys, kuris AUGA kylant aukštyn
   (`support_curve`). Įtaka keliauja tik per SUSIJUSIAS sluoksnio dalis
   (`create_near_points`, SPG.cpp:210), ne per visą XY plokštumą — taikant ją
   globaliai kelios apatinės atramos „uždengia" viską aukščiau.

   Perkelta iš slicer3 (Python laboratorijos), kur mechanizmas buvo išbandytas
   pirmas: JS versijos pastovus 3 mm tinklelis niekada neaugo, ir dėl to narvas
   išeidavo 2–3× tankesnis nei etalono (išmatuota 2026-08-12). */
export async function samplePointsFromLayers(pos, cfg = CFG, onProgress) {
  const CL = (await import('./clipper.js')).default;
  const b = bounds(pos);
  const layers = Math.max(1, Math.ceil(b.size[2] / LAYER_MM));
  const step = cfg.discretize_overhang_step_mm;
  const minR = cfg.minimal_part_radius_mm * SCALE;
  const out = [];                 // pasirinkti atramos taškai
  let prevParts = [];             // [{ paths, bbox, active:Set }]
  const seg = [];
  /* Laikmačiai: kartą jau „pagreitinau" ne tą vietą, tad daugiau nespėliojam. */
  const T = { slice: 0, tree: 0, below: 0, over: 0, land: 0, island: 0, pick: 0 };
  const now = () => (typeof performance !== 'undefined' ? performance.now() : Number(process.hrtime.bigint() / 1000000n));
  let tk;

  for (let i = 0; i < layers; i++) {
    const z = (i + 0.5) * LAYER_MM;
    tk = now(); sliceAt(pos, z, seg);
    const cur = stitch(seg); T.slice += now() - tk;
    const parts = [];
    if (cur.length) {
      tk = now();
      const tree = new CL.PolyTree();
      const c0 = new CL.Clipper();
      c0.AddPaths(cur, CL.PolyType.ptSubject, true);
      c0.Execute(CL.ClipType.ctUnion, tree,
                 CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
      const expolys = toExPolys(CL, tree); T.tree += now() - tk;
      for (const ex of expolys) {
        const bb = pathsBBox(ex);
        /* `get_small_parts` (SPG.cpp:1032): neatspausdinamos dalys išmetamos
           dar prieš sėją, kitaip kiekvienas mesh triukšmo taškelis virsta sala. */
        if (Math.max(bb[2] - bb[0], bb[3] - bb[1]) < 2 * minR) continue;

        /* Siejam su ankstesnio sluoksnio dalimis TIKRU persidengimu, ne
           gabaritais: gabaritai persidengia beveik visada, tad viskas
           susisieja su viskuo, salų nebelieka ir atramų kiekis krenta
           (išmatuota: 12 stulpų vietoj 23). Gabaritai — tik pigus sietas
           prieš tikrą patikrą. */
        tk = now();
        const below = prevParts.filter(pp => {
          if (bb[2] < pp.bbox[0] || bb[0] > pp.bbox[2] ||
              bb[3] < pp.bbox[1] || bb[1] > pp.bbox[3]) return false;
          /* Gretimi sluoksniai beveik sutampa, tad daugumą porų išsprendžia
             VIENAS taškas: jei vienos figūros viršūnė yra kitos viduje —
             persidengia, ir Clipper'io kviesti nereikia. Pilna sankirta lieka
             tik neaiškiems atvejams. Išmatuota: šis žingsnis buvo 7,8 s iš
             17,1 s visos sėjos. */
          const a0 = ex[0][0], b0 = pp.paths[0][0];
          if (pointInPaths(pp.paths, a0.X / SCALE, a0.Y / SCALE)) return true;
          if (pointInPaths(ex, b0.X / SCALE, b0.Y / SCALE)) return true;
          const ci = new CL.Clipper();
          ci.AddPaths(ex, CL.PolyType.ptSubject, true);
          ci.AddPaths(pp.paths, CL.PolyType.ptClip, true);
          const inter = new CL.Paths();
          ci.Execute(CL.ClipType.ctIntersection, inter,
                     CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
          return inter.length > 0;
        });
        T.below += now() - tk;
        const active = new Set();
        for (const pp of below) for (const k of pp.active) active.add(k);
        /* `remove_supports_out_of_part` (SPG.cpp:555): atrama nustoja dengti,
           kai ši dalis nuo jos nutolsta daugiau nei removing_delta. Be šito
           senos apatinės atramos blokuoja kandidatus per visą modelio aukštį. */
        for (const k of [...active]) {
          const s2 = out[k];
          if (!(s2.pos[0] >= bb[0] / SCALE - cfg.removing_delta_mm &&
                s2.pos[0] <= bb[2] / SCALE + cfg.removing_delta_mm &&
                s2.pos[1] >= bb[1] / SCALE - cfg.removing_delta_mm &&
                s2.pos[1] <= bb[3] / SCALE + cfg.removing_delta_mm) ||
              distToPaths(ex, s2.pos[0], s2.pos[1]) > cfg.removing_delta_mm &&
              !pointInPaths(ex, s2.pos[0], s2.pos[1]))
            active.delete(k);
        }

        if (z >= cfg.base_height_mm) {
          // Nuokaba = ši dalis MINUS po ja esančios dalys.
          const cand = [];      // per įtakos spindulio filtrą
          const free = [];      // BE filtro — salos ir pussaliai (SPG.cpp:300,316)
          let island = false;

          /* Salos/pussalio sėja: plonoms dalims — „nugarkaulis", storoms —
             kontūras plius retas vidaus tinklelis. Ta pati taisyklė abiem, tad
             gyvena vienoje vietoje. */
          const islandLike = (paths, pbb, out, land) => {
            const thin = cfg.island_outline_step_mm / 2;
            const cin = new CL.ClipperOffset();
            cin.AddPaths(paths, CL.JoinType.jtMiter, CL.EndType.etClosedPolygon);
            const shrunk = new CL.Paths();
            cin.Execute(shrunk, -thin * SCALE);
            if (shrunk.length === 0) {
              const deep = [];
              for (let gy = pbb[1] / SCALE; gy <= pbb[3] / SCALE; gy += 0.3)
                for (let gx = pbb[0] / SCALE; gx <= pbb[2] / SCALE; gx += 0.3)
                  if (pointInPaths(paths, gx, gy)) {
                    const d = distToPaths(paths, gx, gy);
                    if (d > 0.05) deep.push([d, gx, gy]);
                  }
              deep.sort((p1, p2) => p2[0] - p1[0]);
              const sp2 = cfg.island_thin_step_mm * cfg.island_thin_step_mm;
              for (const [, gx, gy] of deep)
                if (!out.some(c => (c[0] - gx) ** 2 + (c[1] - gy) ** 2 < sp2))
                  out.push([gx, gy]);
              if (!out.length) {
                const rp0 = [];
                walkRing(paths[0], cfg.island_outline_step_mm, rp0);
                for (const q of insetPoints(paths, rp0, cfg, true)) out.push(q);
              }
            } else {
              const rp = [];
              for (const ring of paths) walkRing(ring, cfg.island_outline_step_mm, rp);
              for (const q of insetPoints(paths, rp, cfg, true)) out.push(q);
              coverInterior(shrunk, pbb, out, cfg, land);
            }
          };

          if (!below.length) {
            island = true;                        // sala: kabo visa
            islandLike(ex, bb, free);
          } else {
            tk = now();
            const clip = [];
            for (const pp of below) for (const p of pp.paths) clip.push(p);
            const c1 = new CL.Clipper();
            c1.AddPaths(ex, CL.PolyType.ptSubject, true);
            c1.AddPaths(clip, CL.PolyType.ptClip, true);
            const over = new CL.PolyTree();
            c1.Execute(CL.ClipType.ctDifference, over,
                       CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
            /* Ruožas, siauresnis nei sluoksnio postūmis prie KRITINIO kampo, nėra
               nuokaba — tai vertikali (ar statesnė nei support_critical_angle)
               siena, kuri laikosi pati. Prie 0,05 mm sluoksnio ir 45° tai
               0,05 mm.

               Be šio filtro trianguliacijos triukšmas virsta atramų taškais:
               `revolve` cilindro pjūvis su aukščiu truputį pasisuka, gretimų
               sluoksnių skirtumas išeina 128 drožlės po ~5 µm, jos kartu
               peršoka ploto ribą, ir ant vertikalios sienos atsiranda 13 taškų.
               Galvučių jie negauna, bet užima įtakos spindulį ir nutildo tikrą
               nuokabą aukščiau (puodelio atbraila: 13 atramų vietoj ~16). */
            /* Savilaikio riba. Geometriškai tai sluoksnio postūmis prie
               kritinio kampo (0,05 mm), bet FIZIŠKAI riba didesnė: UV šviesa
               dervoje išsisklaido ~0,05-0,1 mm plačiau nei LCD kaukė, tad
               siauresnė juosta tiesiog susikietina su kaimynu. Auditorius
               (Gemini, 08-15) siūlo 0,15 mm — TIKRINAMA. */
            const selfSup = cfg.self_support_mm || (LAYER_MM / Math.tan(cfg.critical_angle));
            const thinOut = new CL.ClipperOffset();
            for (const oex of toExPolys(CL, over))
              thinOut.AddPaths(oex, CL.JoinType.jtMiter, CL.EndType.etClosedPolygon);
            const solid = new CL.Paths();
            thinOut.Execute(solid, -selfSup / 2 * SCALE);
            const raw = [];
            let region = null;          // pilno pločio nuokabos sritis — jos reikia
            if (solid.length) {         // atitraukimui po „sausumos" filtro
              const cs = new CL.Clipper();
              cs.AddPaths(solid, CL.PolyType.ptSubject, true);
              const back = new CL.PolyTree();
              cs.Execute(CL.ClipType.ctUnion, back,
                         CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
              // atgal į pilną plotį — filtruotas, bet ne suplonintas
              const wide = new CL.ClipperOffset();
              for (const sx of toExPolys(CL, back))
                wide.AddPaths(sx, CL.JoinType.jtMiter, CL.EndType.etClosedPolygon);
              const restored = new CL.Paths();
              wide.Execute(restored, selfSup / 2 * SCALE);
              region = restored;
              const c2 = new CL.Clipper();
              c2.AddPaths(restored, CL.PolyType.ptSubject, true);
              const rt = new CL.PolyTree();
              c2.Execute(CL.ClipType.ctUnion, rt,
                         CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
              for (const oex of toExPolys(CL, rt))
                for (const ring of oex) walkRing(ring, step, raw);
            }
            /* Kraštas, sutampantis su ankstesniu sluoksniu, praleidžiamas
               (`contain_point(p, prev_points)`, cpp:429): tai jau paremta
               „sausuma", ne nuokabos krantas.

               Tolerancija — ne nulinė. Originale `diff_ex` palieka TIKSLIAI
               bendras viršūnes, tad ten užtenka tapatybės. Mūsų sluoksniai
               ateina iš trianguliacijos, kuri su aukščiu truputį pasisuka, ir
               bendras kraštas prasiskiria mikronais; su 1 nm tolerancija virš
               pačios puodelio sienos atsirasdavo 14 „nuokabos" taškų ten, kur
               nuokabos nėra. Riba ta pati, kuria matuojam ir savilaikį: sluoksnio
               postūmis prie kritinio kampo. */
            T.over += now() - tk;
            tk = now();
            const landTol = LAYER_MM / Math.tan(cfg.critical_angle);
            /* EILIŠKUMAS: pirma „sausumos" filtras ant NEPAJUDINTŲ taškų, tik
               paskui atitraukimas. Atvirkščiai buvo klaida — juostos vidinis
               kraštas sutampa su apatiniu sluoksniu, tad tie taškai turi iškristi;
               atitraukti per 0,25 mm jie nustodavo sutapti ir prasprūsdavo pro
               filtrą. Biustui tai davė 13 laisvų kelių į plokštę vietoj 7 ir
               22 kolonas vietoj 18 (08-13). */
            /* Nuokabos taškai lieka TIKSLIAI ant kontūro — `sample_overhangs`
               (SPG.cpp:409) juos ima iš pačių daugiakampio viršūnių ir tik
               praleidžia tas atkarpas, kurios sutampa su apatiniu sluoksniu.
               Jokio atitraukimo ten nėra; į vidurio ašį traukiami tik salų ir
               pusiasalių taškai (`uniform_support_island`). */
            for (const p of raw)
              if (distToPaths(clip, p[0], p[1]) > landTol) cand.push([p[0], p[1]]);
            T.land += now() - tk;

            /* `create_peninsulas` (SPG.cpp:567) + `support_peninsulas`
               (SPG.cpp:316). Vieno sluoksnio nuokaba, išsikišusi toliau nei
               `peninsula_min_width` (2 mm) už to, kas po ja, yra „pussalis" ir
               remiama ATSKIRAI — ne tik kraštas, o visas plotas, kaip sala.
               Savaime laikosi tik tai, kas arčiau nei
               `peninsula_self_supported_width` (1,5 mm) nuo „sausumos".

               Tai PRIDEDANTIS mechanizmas. Be jo plokščia nuokaba gauna tik
               kontūro taškus: kronsteine mūsų 8 prieš etalono 12–20
               (išmatuota 08-13). Ant glotnaus kūno jis netyli — ten sluoksnis
               retai išsikiša 2 mm per vieną žingsnį. */
            tk = now();
            const grow = (delta) => {
              const co = new CL.ClipperOffset();
              co.AddPaths(clip, CL.JoinType.jtMiter, CL.EndType.etClosedPolygon);
              const o = new CL.Paths();
              co.Execute(o, delta * SCALE);
              return o;
            };
            const diff = (subj, cl) => {
              const c = new CL.Clipper();
              c.AddPaths(subj, CL.PolyType.ptSubject, true);
              if (cl.length) c.AddPaths(cl, CL.PolyType.ptClip, true);
              const tr = new CL.PolyTree();
              c.Execute(CL.ClipType.ctDifference, tr,
                        CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
              return tr;
            };
            const overPen = toExPolys(CL, diff(ex, grow(cfg.peninsula_min_width_mm)));
            if (overPen.length) {
              const shapes = toExPolys(CL, diff(ex, grow(cfg.peninsula_self_supported_width_mm)));
              for (const pex of shapes) {
                // pakankamai platus? — turi persidengti su `overPen`
                const ci = new CL.Clipper();
                ci.AddPaths(pex, CL.PolyType.ptSubject, true);
                for (const oe of overPen) ci.AddPaths(oe, CL.PolyType.ptClip, true);
                const inter = new CL.Paths();
                ci.Execute(CL.ClipType.ctIntersection, inter,
                           CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
                if (!inter.length) continue;
                /* Sausumos pusė nesėjama: `create_peninsulas` kiekvienai
                   kraštinei žymi `is_outline` ir tik krantą laiko nuokaba
                   (cpp:675-680). Mūsų pussalio vidinė riba kaip tik ir eina per
                   `below_self_supported` — be šito 9 iš 12 jo taškų dubliavo
                   nuokabos taškus. */
                const pen = [];
                islandLike(pex, pathsBBox(pex), pen, clip);
                const coast = grow(cfg.peninsula_self_supported_width_mm);
                for (const [x, y] of pen)
                  if (distToPaths(coast, x, y) > landTol) free.push([x, y]);
              }
            }
            T.island += now() - tk;
          }
          /* EILIŠKUMAS (generate_support_points, SPG.cpp:1528-1537): pirma
             `support_peninsulas`, tik paskui `support_part_overhangs`. Tvarka
             nėra kosmetinė — pusiasalio taškai jau guli `near_points` sąraše,
             kai tikrinami nuokabos taškai, tad kontūro taškai, patenkantys į jų
             įtakos spindulį, iškrinta. Buvo atvirkščiai: puodelio atbraila
             gaudavo IR pusiasalio žiedą ties r=11,2, IR kontūro žiedą ties
             r=12 — 26 taškai vietoj 15 (08-13). */
          tk = now();
          for (const [x, y] of free) {
            out.push({ pos: [x, y, z], normal: [0, 0, -1], island: true });
            active.add(out.length - 1);
          }
          // Atranka pagal augantį įtakos spindulį.
          for (const [x, y] of cand) {
            let covered = false;
            for (const k of active) {
              const s = out[k];
              if (Math.hypot(s.pos[0] - x, s.pos[1] - y) <
                  influenceRadius(z - s.pos[2], cfg)) { covered = true; break; }
            }
            if (covered) continue;
            out.push({ pos: [x, y, z], normal: [0, 0, -1], island });
            active.add(out.length - 1);
          }
          T.pick += now() - tk;
        }
        parts.push({ paths: ex, bbox: bb, active });
      }
    }
    /* prev VISADA ankstesnis sluoksnis, net tuščias: kitaip virš tuštumos
       atsiradusi sala nebūtų skirtumas ir liktų be nieko. */
    prevParts = parts;
    if (onProgress && (i % 32 === 0)) onProgress(i + 1, layers);
  }
  for (const k in T) T[k] = Math.round(T[k]);
  /* Naršyklėje `process` neegzistuoja, o `process?.env` nuo to neapsaugo —
     neapibrėžtas identifikatorius meta ReferenceError dar prieš optional
     chaining. Tikrinam per typeof. */
  if (typeof process !== 'undefined' && process.env && process.env.SLICER_TIMES)
    console.log('  sėjos vidus (ms):', JSON.stringify(T));
  return out;
}

/* ------------------------------------------------- savas piešimas (CFG) */
/* Iki šiol geometriją brėžė `slicer.js` funkcijos, o jos skaito `SUP` — todėl
   ekrane matėsi ne tie matmenys, kuriuos suskaičiavo šis modulis: padas 2,7×
   storesnis, jungtys 2× storesnės, koja siauresnė ir aukštesnė (auditas
   08-13). Čia viskas piešiama TAIS PAČIAIS skaičiais, kuriais skaičiuota. */

/** Stulpelio apskritimai duotam aukščiui. Pėda platėja per base_height_mm iki
 *  base_radius_mm (SLA/Pad: support_base_diameter 3, support_base_height 1). */
export function pillarDiscs2(pillars, z, cfg = CFG) {
  const out = [];
  for (const p of pillars) {
    if (z > p.top || z < p.bottom) continue;
    let r = p.rTop || cfg.pillar_radius_mm;
    const up = z - p.bottom;
    if (p.bottom <= 1e-6 && up < cfg.base_height_mm) {
      const t = up / cfg.base_height_mm;
      r = cfg.base_radius_mm + (r - cfg.base_radius_mm) * t;
    }
    out.push({ x: p.x, y: p.y, r });
  }
  return out;
}

/** Tiltų, jungčių ir galvučių apskritimai. Galvutė (headTip) siaurėja iki
 *  head_front_radius_mm per head_width_mm — support_head_front_diameter 0.5. */
export function braceDiscs2(braces, z, cfg = CFG) {
  const out = [];
  for (const c of braces) {
    if (z < c.z0 || z > c.z1) continue;
    const t = (z - c.z0) / ((c.z1 - c.z0) || 1);
    let r = cfg.pillar_radius_mm;
    if (c.anchor) {
      /* Apversta galvutė: platus galas viršuje prie stulpo, smaigalys apačioje,
         detalėje. Kūgis per visą atkarpą. */
      r = cfg.head_front_radius_mm +
          (cfg.pillar_radius_mm - cfg.head_front_radius_mm) * t;
    } else if (c.bridge) {
      const left = c.z1 - z;
      if (left < cfg.head_width_mm)
        r = cfg.head_front_radius_mm +
            (cfg.pillar_radius_mm - cfg.head_front_radius_mm) * (left / cfg.head_width_mm);
    }
    out.push({ x: c.ax + (c.bx - c.ax) * t, y: c.ay + (c.by - c.ay) * t, r });
  }
  return out;
}

/** Vieno sluoksnio diskai — tai, ką `slice()` paims per opts.discsFor. */
export function discsFor(sup, z, cfg = CFG) {
  const d = pillarDiscs2(sup.pillars, z, cfg);
  /* Jungtys piešiamos VISU aukščiu, įskaitant pėdos zoną. Anksčiau žemiau
     `base_height_mm` jos buvo praleidžiamos („ten stulpas ir taip platus") —
     bet tiltas, prasidedantis žemiau tos ribos, tada išnirdavo ore: „evil"
     sluoksnyje z=1,55 atsirasdavo 5 salos be atramos iš 8 (08-13). */
  if (sup.braces && sup.braces.length)
    for (const b of braceDiscs2(sup.braces, z, cfg)) d.push(b);
  return d;
}

/* --------------------------------------------------- savikontrolė (#4) */
/* Iki šiol naujam algoritmui ji buvo išjungta ir visada sakė „švaru" — pulto
   įspėjimas „would print hanging in the air" niekada neužsidegdavo (auditas
   08-13). Tikrinam tiesiogiai geometriją: kiekvienas stulpas turi remtis į
   plokštę arba į paviršių, o kiekviena gija — turėti bent vieną tvirtą galą. */
export function selfCheck(sup, mesh, cfg = CFG) {
  if (!mesh) return 0;
  /* Zondas leidžiamas iš `bottom + EPS`, t. y. iš TUŠTUMOS virš atramos taško.
     Anksčiau jis buvo leidžiamas iš `bottom - 1e-3` — MEDŽIAGOS VIDUJE, tad
     spindulys išeidavo pro apatinį paviršių ir grąžindavo kūno storį, ne nulį:
     visi 17 pranešimų buvo klaidingi (auditas 08-13). Dvi teisingos baigtys:
       hr.inside      — apačia įleista į medžiagą (spindulys pataiko iš vidaus),
       hr.dist <= tol — apačia guli ant paviršiaus. */
  const EPS = 1e-3;
  const tol = EPS + LAYER_MM;                          // vienas sluoksnis atsargos
  let hanging = 0;
  for (const p of sup.pillars) {
    if (p.bottom <= 1e-6) continue;                    // stovi ant plokštės
    /* `partial` stulpelis po galvute remiasi į TILTĄ, ne į medžiagą
       (connect_to_nearpillar, cpp:282-363) — medžiagos po juo ir neturi būti. */
    if (p.partial) continue;
    /* `anchored` stulpo apačioje medžiagos NĖRA ir neturi būti — po juo eina
       apversta galvutė iki paviršiaus; ją tikrina `braces` dalis. */
    if (p.anchored) continue;
    const hr = mesh.rayHit([p.x, p.y, p.bottom + EPS], DOWN);
    if (hr.inside || hr.dist <= tol) continue;
    hanging++;
  }
  /* Gijos tikrinamos atskirai ir savo matu: tiltas ar jungtis „kabo" tada, kai
     nė vienas galas neremiasi į stulpą. Anksčiau jos nebuvo tikrinamos išvis. */
  if (sup.braces) hanging += danglingBraces(sup.pillars, sup.braces).length;
  return hanging;
}

/** Gijos, kurių abu galai kabo. Tiltai ir galvutės, kurių viršus remiasi į
 *  detalę, NĖRA kabantys — senoji funkcija to neskyrė ir grybo scenoje rodė
 *  7 melagingus pavojus iš 75 (auditas 08-13). */
export function danglingBraces(pillars, braces) {
  const at = new Set();
  for (const p of pillars) {
    at.add(p.x.toFixed(2) + ',' + p.y.toFixed(2));
  }
  const out = [];
  for (const c of braces || []) {
    if (c.bridge) continue;                            // galvutė kimba į detalę
    const a = at.has(c.ax.toFixed(2) + ',' + c.ay.toFixed(2));
    const b = at.has(c.bx.toFixed(2) + ',' + c.by.toFixed(2));
    if (!a && !b) out.push(c);
  }
  return out;
}
