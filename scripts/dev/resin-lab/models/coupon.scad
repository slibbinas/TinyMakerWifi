// Kalibracinis kuponas - bendras visiems dervų tipams.
// Spausdinamas tiesiai ant plokštės (be atramų). Eilės iš priekio į galą:
//   A smeigtukai, B skylutės, C plonos sienelės, D tarpai tarp blokelių.
// Skaičiai sąraše = matmuo mm, iš kairės į dešinę. Ekrano pikselis 0,1275 mm,
// tad 0,3 mm yra apie 2 pikselius - apatinė riba, kurią dar verta tikrinti.
$fn = 32;
L = 30; W = 21; T = 1.2;            // pagrindas
sizes = [0.3, 0.4, 0.5, 0.6, 0.8, 1.0];
walls = [0.2, 0.3, 0.4, 0.6, 0.8, 1.0];
gaps  = [0.1, 0.2, 0.3, 0.4, 0.5];
x0 = 3; dx = 4.8;

difference() {
  cube([L, W, T]);
  // B: skylutės per pagrindą (plokštė jas uždaro iš apačios - tikrinam, ar neužsipildo)
  for (i = [0:len(sizes)-1]) translate([x0 + i*dx, 7, -0.1]) cylinder(d = sizes[i], h = T + 0.2);
}
// A: smeigtukai 3 mm aukščio
for (i = [0:len(sizes)-1]) translate([x0 + i*dx, 3, T]) cylinder(d = sizes[i], h = 3);
// C: sienelės 3,5 mm ilgio, 3 mm aukščio
for (i = [0:len(walls)-1]) translate([x0 + i*dx - walls[i]/2, 10, T]) cube([walls[i], 3.5, 3]);
// D: blokeliai su vis platesniu tarpu
bx = 2.4;
for (i = [0:len(gaps)]) {
  xs = 2 + i*bx + (i > 0 ? sum(gaps, i) : 0);
  translate([xs, 16, T]) cube([bx, 3.5, 2.5]);
}
function sum(v, n) = n <= 0 ? 0 : v[n-1] + sum(v, n-1);
