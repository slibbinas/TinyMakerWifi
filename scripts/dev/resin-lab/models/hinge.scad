// Sudėtingas modelis tvirtoms dervoms: vyris, spausdinamas surinktas.
// Lapas A turi pilnas kilpas ir ašį, lapas B - kilpas su skyle aplink ašį.
// Tarpas tol ir tarp kilpų, ir aplink ašį. Pavyko = po plovimo ir kietinimo
// vyris prasisuka ranka.
$fn = 48;
tol = 0.35;          // tarpas
R = 2.2;             // kilpos spindulys
L = 24;              // vyrio ilgis
leaf = 9;            // lapo plotis nuo ašies
t = 1.6;             // lapo storis
k = 5;               // kilpų skaičius
pinR = R - 1.0;
s = L / k;
module seg(i) translate([i*s + tol/2, 0, R]) rotate([0, 90, 0]) cylinder(r = R, h = s - tol);
module side(sel, sign) {
  // lapas baigiasi toliau nuo ašies nei svetimos kilpos, prie savų kilpų - jungtys
  translate([0, sign > 0 ? R + tol : -leaf, 0]) cube([L, leaf - R - tol, t]);
  for (i = [0:k-1]) if (i % 2 == sel) {
    seg(i);
    translate([i*s + tol/2, sign > 0 ? 0 : -(R + tol), 0]) cube([s - tol, R + tol, t]);
  }
}
// A: lyginės kilpos pilnos + ašis per visą ilgį
union() { side(0, -1); translate([0, 0, R]) rotate([0, 90, 0]) cylinder(r = pinR, h = L); }
// B: nelyginės kilpos su skyle
difference() { side(1, 1); translate([-1, 0, R]) rotate([0, 90, 0]) cylinder(r = pinR + tol, h = L + 2); }
