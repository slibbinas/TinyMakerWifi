/* Kupono kadras: abu variantai vienoje HTML peržiūroje.
 *
 * Python kelias (kupono_perziura.py) reikalauja numpy+PIL, o šios mašinos
 * Python 3.14 aplinkoje jų nėra. Kadangi ZIP rašomas BE suspaudimo
 * (`zipStore`), sluoksnių PNG galima paimti tiesiai iš archyvo baitų ir
 * atiduoti naršyklei - dekoduoti nieko nereikia.
 *
 *     node kupono_kadras.mjs kadras.html a.zip b.zip
 */
import { readFileSync, writeFileSync } from 'fs';

/* Store-only ZIP: einam per vietinius antraštės įrašus. */
function zipFailai(buf) {
  const out = new Map();
  for (let i = 0; i + 30 <= buf.length; ) {
    if (buf.readUInt32LE(i) !== 0x04034b50) { i++; continue; }
    const dydis = buf.readUInt32LE(i + 18);
    const vLen = buf.readUInt16LE(i + 26), eLen = buf.readUInt16LE(i + 28);
    const vardas = buf.slice(i + 30, i + 30 + vLen).toString();
    const nuo = i + 30 + vLen + eLen;
    out.set(vardas, buf.slice(nuo, nuo + dydis));
    i = nuo + dydis;
  }
  return out;
}

const argv = process.argv.slice(2);
/* `--sl=1,7,21` - kuriuos sluoksnius rodyti. Kuponui tinka numatytieji, bet
   rafto testui reikia kitų: ten įdomu ties kiekvieno rafto pabaiga. */
const SL = (argv.find(a => a.startsWith('--sl=')) || '--sl=6,31').slice(5)
  .split(',').map(Number);
const [OUT = 'C:/PIO-build/kupono-kadras.html', ...zipai] =
  argv.filter(a => !a.startsWith('--'));
const saltiniai = zipai.length ? zipai
  : ['C:/PIO-build/kuponas.zip', 'C:/PIO-build/kuponas-siauras.zip'];

let blokai = '';
for (const kelias of saltiniai) {
  const f = zipFailai(readFileSync(kelias));
  /* Vardai skiriasi pagal kilmę: mūsų generatoriai rašo `7.png`, o variklio
     SL1 eksportas - `tinymaker/00007.png`. Ieškom pagal numerį, ne pagal
     tikslų vardą. */
  const pagalNr = new Map();
  for (const v of f.keys()) {
    const m = v.match(/(\d+)\.png$/);
    if (m) pagalNr.set(parseInt(m[1], 10), v);
  }
  const kadrai = SL.map(n => [pagalNr.get(n), `sluoksnis ${n}`]).filter(([v]) => v);
  let vid = '';
  for (const [vardas, antraste] of kadrai) {
    const b64 = f.get(vardas).toString('base64');
    vid += `<figure><figcaption>${antraste}</figcaption>` +
      `<img src="data:image/png;base64,${b64}" alt="${vardas}"></figure>`;
  }
  blokai += `<section><h2>${kelias.split('/').pop()}</h2>${vid}</section>`;
}

writeFileSync(OUT, `<!doctype html><meta charset="utf-8">
<title>Kupono kadras</title>
<style>
 body{background:#17171b;color:#e8e8e0;font:14px system-ui;margin:24px}
 section{margin-bottom:32px}
 h2{font-size:15px;color:#9fd0ff;margin:0 0 10px}
 figure{margin:0 0 14px}
 figcaption{font-size:12px;color:#a0a0a8;margin-bottom:4px}
 img{width:960px;image-rendering:pixelated;border:1px solid #333;background:#000}
</style>${blokai}`);

console.log('kadras:', OUT);
