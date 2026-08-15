/* Ar stulpai persidengia? (V pastebejimas: „supportas suporte")
 *
 * Du stulpai fiziskai persidengia, kai atstumas tarp asiu XY plokstumoje
 * mazesnis uz ju spinduliu suma IR ju aukscio ruozai kertasi. Tada spausdinam
 * ta pacia derva du kartus, o narvas atrodo tankesnis, nei is tikruju yra.
 *
 * Skaiciuojam tris lygius:
 *   PERSIDENGIA  d < r1+r2          - tikrai viena kitame
 *   LIECIASI     d < 1,5*(r1+r2)    - tarpo tarp ju praktiskai nera
 *   ARTI         d < 3*(r1+r2)      - kandidatai sulieti i viena
 *
 *   node persidengimai.mjs <model.stl> [raw]
 */
import { readFileSync } from 'fs';

const MOD = process.env.SLICER || 'C:/PIO-build/exp2-wt/web/lib/slicer2.js';
const M = await import('file:///' + MOD.split('\\').join('/') + '?t=' + Date.now());

const buf = readFileSync(process.argv[2]);
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
let pos = positions;
if (process.argv[3] !== 'raw') {
  const best = M.autoOrient(positions);
  if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
  pos = M.place(positions, best.tr);
}

const layers = Math.max(1, Math.ceil(M.bounds(pos).size[2] / M.LAYER_MM));
/* Imam TIESIAI is medzio: findOverhangs grazina suplota pavidala, kuriame
   nebelieka nei spindulio (rTop), nei kilmes (onModel/helper) - matavau ji ir
   gavau visiems ta pati numatytaji spinduli (08-16). */
const sup = await M.buildSupportTree(pos, M.CFG, null);
const P = sup.pillars;
console.log('  log: ' + JSON.stringify({bandyta: sup.log.mergeTry || 0, sulieta: sup.log.merged || 0, nuoPlokstesTiltu: sup.log.fromGround || 0, antModelio: sup.log.fromModel || 0, atlaisvinta: !!sup.log.relaxed}));

const R = p => (p.rTop || M.CFG.pillar_radius_mm);
let pers = 0, liec = 0, arti = 0;
const blogiausi = [];
for (let i = 0; i < P.length; i++)
  for (let j = i + 1; j < P.length; j++) {
    const a = P[i], b = P[j];
    // ar aukscio ruozai issikerta - kitaip vienas virs kito, ne salia
    const lo = Math.max(a.bottom, b.bottom), hi = Math.min(a.top, b.top);
    if (hi - lo <= 0.05) continue;
    const d = Math.hypot(a.x - b.x, a.y - b.y);
    const s = R(a) + R(b);
    if (d < s) { pers++; blogiausi.push({ d, s, h: hi - lo, i, j }); }
    else if (d < 1.5 * s) liec++;
    else if (d < 3 * s) arti++;
  }
blogiausi.sort((x, y) => x.d - y.d);
console.log(`${process.argv[2]}: stulpu ${P.length}`);
for (const b of blogiausi.slice(0,3)) {
  for (const k of [b.i, b.j]) {
    const p = P[k];
    console.log(`    #${k} kilme: ${p.onModel?'ant modelio':(p.helper?'pagalbinis':(p.partial?'dalinis':'nuo plokstes'))}`
      + ` bottom=${p.bottom.toFixed(2)} top=${p.top.toFixed(2)} rTop=${(p.rTop||0).toFixed(2)} head=${p.head}`);
  }
}
console.log(`  PERSIDENGIA (d < r1+r2)     ${pers}`);
console.log(`  LIECIASI    (d < 1,5x)      ${liec}`);
console.log(`  ARTI        (d < 3x)        ${arti}`);
for (const b of blogiausi.slice(0, 5))
  console.log(`    #${b.i}-#${b.j}: atstumas ${b.d.toFixed(2)} mm, spinduliu suma `
    + `${b.s.toFixed(2)} mm, bendras aukstis ${b.h.toFixed(1)} mm`);
