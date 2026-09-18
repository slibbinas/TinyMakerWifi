// TinyMaker Resin Lab - kaištukai, skylutės ir plyšiai (paprastas modelis).
// Greitas, pigus spaudinys: plokštelė 34 × 20 × 1,5 mm, ~1,0 ml.
//   1 eilė: kaištukai Ø0,3-1,2 mm, 4 mm aukščio - ploniausias išlikęs = Regular užtenka.
//   2 eilė: skylutės kiaurai Ø0,3-1,2 mm - užakusios = per ilga ekspozicija.
//   3 eilė: plyšiai kiaurai 0,2-0,8 mm.
// Nupjautas kampas rodo, kuri eilė kuri (kampas - prie ploniausių).
// STL: openscad -o TM_kaistukai.stl tm_kaistukai.scad

$fn = 24;
W = 34; D = 20; T = 1.5;
dia = [0.3, 0.4, 0.5, 0.6, 0.8, 1.0, 1.2];
gaps = [0.2, 0.3, 0.4, 0.5, 0.6, 0.8];
x0 = 4; dx = 4.4;

difference() {
  linear_extrude(T)
    polygon([[2, 0], [W, 0], [W, D], [0, D], [0, 2]]);
  for (i = [0 : len(dia) - 1])
    translate([x0 + i * dx, 10, -1]) cylinder(d = dia[i], h = T + 2);
  for (i = [0 : len(gaps) - 1])
    translate([x0 + i * 5 - gaps[i] / 2, 14.5, -1]) cube([gaps[i], 4, T + 2]);
}
for (i = [0 : len(dia) - 1])
  translate([x0 + i * dx, 4, T - 0.01]) cylinder(d = dia[i], h = 4);
