/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2020-2022 Prusa Research s.r.o. (Tomas Meszaros) - originalas
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/Clustering.cpp
 */

/**
 * `cluster` (Clustering.cpp:26-77).
 *
 * Auga klasteriai REKURSIJA: paemus taska, randami visi ji tenkinantys
 * kaimynai, tie prijungiami, ir tada paieska kartojama JAU NUO PRIJUNGTU. Del
 * to klasteris gali nusidriekti toli - grandinele A-B-C, kur A ir C tarpusavyje
 * salygos netenkina.
 *
 * ⚠️ `maxPoints` yra KIETA riba: kai klasteris ja pasiekia, likusieji kaimynai
 * NEBEPRIJUNGIAMI ir liks kitiems klasteriams. Todel `add_pinheads` su
 * `maxPoints = 2` sujungia tik POROMIS, o ne visa kruva i viena.
 *
 * Originale erdvinis indeksas yra boost R-tree; cia tiesine paieska. Rezultatas
 * tas pats - skiriasi tik greitis, o tasku cia siaip jau ne tukstanciai.
 *
 * @param indices  indeksu masyvas
 * @param pointfn  indeksas -> [x,y,z]
 * @param qfn      (visi, taskas) -> kandidatu indeksai
 * @param maxPoints 0 = be ribos
 */
function clusterCore(indices, pointfn, qfn, maxPoints) {
  const liko = new Set(indices);
  const out = [];

  while (liko.size) {
    const pirmas = liko.values().next().value;
    const cluster = [];
    const eile = [pirmas];

    while (eile.length) {
      const p = eile.shift();
      const kand = qfn(indices, p).filter(i => liko.has(i) && !cluster.includes(i));
      /* Rusiuojam pagal indeksa - originalas irgi lygina per `e1.second < e2.second`,
         ir nuo tvarkos priklauso, kurie taskai patenka i klasteri pasiekus riba. */
      kand.sort((a, b) => a - b);

      let imk = kand.length;
      if (maxPoints && cluster.length + imk > maxPoints) imk = maxPoints - cluster.length;
      if (imk < 0) imk = 0;

      for (let k = 0; k < imk; k++) {
        cluster.push(kand[k]);
        liko.delete(kand[k]);
        eile.push(kand[k]);
      }
      cluster.sort((a, b) => a - b);
      if (maxPoints && cluster.length >= maxPoints) break;
    }

    if (!cluster.length) { cluster.push(pirmas); liko.delete(pirmas); }
    out.push(cluster);
  }
  return out;
}

const dist3 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);

/**
 * `distance_queryfn` (Clustering.cpp:79-94) - PAZODZIUI, su viskuo, kas jame yra.
 *
 * Originalas NEIMA visu kaimynu atstumu `dist`. Jis ima `bgi::nearest(p, max_points)`,
 * t. y. tik `max_points` ARTIMIAUSIU (skaitant ir pati taska, nes jis irgi
 * indekse), ir tik tada ismeta tuos, kurie toliau nei `dist`.
 *
 * ⚠️ IR ORIGINALE CIA YRA KLAIDA, KURIA ATKARTOJAM SAMONINGAI:
 *
 *     for (auto it = tmp.begin(); it < tmp.end(); ++it)
 *         if ((p.first - it->first).norm() > dist) it = tmp.erase(it);
 *
 * Po `erase(it)` iteratorius jau rodo i KITA elementa, o `++it` ji persoka.
 * Tad kas antras per toli esantis taskas LIEKA klasteryje. Tai ne musu
 * sprendimas ir ne pagerinimas - tai originalo elgesys, ir istaisius ji musu
 * klasteriai skirtusi nuo PrusaSlicer'io. Zymima cia, kad butu matoma.
 */
function distanceQueryFn(visi, p, pointfn, dist, maxPoints) {
  const pp = pointfn(p);
  /* `bgi::nearest(p, max_points)` */
  let tmp = visi.slice().sort((a, b) => dist3(pointfn(a), pp) - dist3(pointfn(b), pp));
  if (maxPoints) tmp = tmp.slice(0, maxPoints);

  /* Ta pati kilpa su ta pacia klaida: istrynus elementa, kitas persokamas. */
  for (let i = 0; i < tmp.length; i++) {
    if (dist3(pointfn(tmp[i]), pp) > dist) { tmp.splice(i, 1); /* be i--, kaip originale */ }
  }
  return tmp;
}

/** `cluster(indices, pointfn, dist, max_points)` (Clustering.cpp:98-116). */
export function clusterByDistance(indices, pointfn, dist, maxPoints) {
  const qfn = (visi, p) => distanceQueryFn(visi, p, pointfn, dist, maxPoints);
  return clusterCore(indices, pointfn, qfn, maxPoints);
}

/** `cluster(indices, pointfn, predicate, max_points)` (Clustering.cpp:118-139). */
export function clusterByPredicate(indices, pointfn, predicate, maxPoints) {
  const qfn = (visi, p) => {
    const e1 = [pointfn(p), p];
    return visi.filter(i => predicate(e1, [pointfn(i), i]));
  };
  return clusterCore(indices, pointfn, qfn, maxPoints);
}

/**
 * `cluster_centroid` - klasterio elementas, esantis arciausiai visu kitu.
 * Butent jis tampa CENTRINIU stulpu, o likusieji jungiasi i ji tiltais.
 */
export function clusterCentroid(cluster, pointfn, distfn) {
  switch (cluster.length) {
    case 0: return -1;
    case 1: return 0;
    case 2: return 0;
    default: break;
  }

  let bestIdx = 0, bestSum = Infinity;
  for (let i = 0; i < cluster.length; i++) {
    let s = 0;
    for (let j = 0; j < cluster.length; j++)
      if (i !== j) s += distfn(pointfn(cluster[i]), pointfn(cluster[j]));
    if (s < bestSum) { bestSum = s; bestIdx = i; }
  }
  return bestIdx;
}
