# Atramų santvara - V algoritmas (2026-08-18)

Ši specifikacija yra **V vizija, užrašyta prieš kodą**, kad būtų ką tikslinti, o
ne mano interpretacija galvoje. Kiekvienas punktas - iš V žodžių; mano
pastabos atskirtos ir pažymėtos.

## Kodėl kitas algoritmas

Dabartinis medis yra `DefaultSupportTree` tipo: kiekvienas taškas gauna savo
vertikalų stulpą, o stulpai vėliau susiejami zigzago tiltais. Iš to gaunasi
atskiros kojos, susigrūdusios grupėmis, ir jungtys, kur ant stulpo galo
užmaunamas kūgis („sijonas"). V vizualinė patikra to nepraeina, o skaičiai
(danga, salos, derva) tos bėdos nemato - jie matuoja blogo sprendimo pasekmes.

Ir dar viena riba, kurios nebeslėpsim: **PrusaSlicer čia nebėra etalonas.** Jo
profilyje `support_tree_type = default`, tad jo failai turi tokias pat atskiras
kojas. Einam savo keliu.

## Principas

**Ne „stulpas per tašką", o santvara su šakomis.**

1. Randam taškus, kur atramų **reikia**.
2. Saugiu atstumu nuo detalės auginam **kamieną** iki tų taškų aukščio.
3. Kamienas viršuje **išsiskiria į šakas**, kurios prieina prie taškų.

## Kamienas

Kamienas **nėra vienas storas strypas**. Jis yra **keli ploni stulpai, stovintys
šalia per fiksuotą atstumą ir susieti įstrižomis sijomis** - kaip statybinis
kranas.

Stulpų skaičius pagal reikalą:

| kada | kamienas |
|---|---|
| trumpas | vienas stulpas |
| aukštesnis | du arba trys |
| didelė apkrova | keturi ir daugiau |
| kraštinis atvejis | eilė stulpų, tampanti siena („uola") |

*(Mano pastaba: sijų žingsnis per aukštį ir atstumas tarp stulpų kol kas
neapibrėžti skaičiais - nustatysim matuodami, ne spėdami.)*

## Šakos ir jų galai

- Šakos nuo kamieno viršaus prieina prie atramos taškų.
- **Galai visada palaipsniui plonėjantys iki detalės**, kad nulaužti būtų
  lengva.
- **Perėjimas tarp šakos ir kamieno (ir tarp šakų) turi atrodyti kaip du
  įstrižai nupjauti ir suglausti pagaliukai** - viena tolydi forma. NE kaip
  sijonas, užmautas ant pagalio.

*(Mano pastaba: tai reiškia ir piešimo pakeitimą - `braceDiscs2` dabar deda
diskus statmenai sluoksniui, o įstrižai nupjautam strypui reikia elipsės.)*

## Kai kelio iki plokštės nėra

Jei medžio iki taško nupiešti **negalima** - per didelis atstumas, šakos
kamieno nebeprieina, tektų zigzaguoti - **tada formuojam atramą, kuri liečia
kitą detalės vietą.**

Tai svarbu: taškas **neiškrenta**. Atrama randa atspirtį ant paties modelio, ir
tik tada, kai plokštė nebeprieinama.

## Ką iš esamo kodo panaudojam

Nereikia rašyti nuo nulio - pusė mechanikos jau yra ir yra patikrinta:

| esama | kam santvaroje |
|---|---|
| `beamHit`, `beamHitFull` | ar kelias laisvas: šakoms ir sijoms tas pats |
| `pinheadHit` | galvutės tikrinimas prie detalės |
| `clusterHeads` | taškų grupavimas - bus grupavimas į kamienus |
| `braceDiscs2` | įstrižų elementų piešimas (reikia elipsės) |
| `interconnect` zigzagas | jau iš esmės sija, tik kitoje vietoje |
| `selfCheck`, invariantų sargai | veikia ir naujam medžiui |

Nauja bus **struktūra**: šiandien `pillars[]` (stulpas per tašką) plius
`braces[]`; reikės `trunks[]` (kamienas = stulpų grupė su sijomis) plius
`branches[]` (šakos iki taškų).

## Ko šis darbas kainuos

Dalis to, kas išmatuota per 08-13...08-18, nustos galioti: dervos skaičiai,
pado plotas, kontaktų tankis, laikymo atsarga. Tai ne regresija, o kitas
medis - bet reiškia, kad matavimų juostą teks perstatyti iš naujo.

Orientacijos klausimas (`autoOrient` kelia modelį ant atramų, nors galėtų
paguldyti) lieka **atskiru punktu po** santvaros - V sprendimas: jei medis
geras, jis geras bet kokioje padėtyje.
