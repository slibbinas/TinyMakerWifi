// Paprastas modelis detalės dervoms (standartinė, ABS-like, karščiui atspari):
// šachmatų bokštas. Lenktas profilis rodo sluoksnių laiptelius, dantys ir
// langeliai - briaunų aštrumą, vidinė ertmė - ar derva išteka (yra skylė apačioje).
$fn = 64;
H = 22;
difference() {
  rotate_extrude() polygon([[0,0],[8,0],[8,1.5],[6.5,2.5],[5,5],[4.2,H-6],[5.5,H-4],[5.5,H],[0,H]]);
  // dantys viršuje
  for (a = [0:60:359]) rotate([0,0,a]) translate([3.2,-0.8,H-2.2]) cube([3,1.6,3]);
  // viršaus įduba
  translate([0,0,H-1.5]) cylinder(r = 3.6, h = 2);
  // langeliai
  for (a = [30, 150, 270]) rotate([0,0,a]) translate([3,0,H-9]) rotate([0,90,0]) cylinder(d = 1.6, h = 3);
  // ertmė ir nutekėjimo skylė
  translate([0,0,2.5]) cylinder(r1 = 3.2, r2 = 2.6, h = H-7);
  translate([0,0,-0.1]) cylinder(d = 1.5, h = 3);
}
