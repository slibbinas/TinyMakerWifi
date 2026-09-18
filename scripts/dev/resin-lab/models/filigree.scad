// Sudėtingas modelis lydomoms ir vaškinėms dervoms: ažūrinis žiedas.
// Plonos perėjos tarp skylučių (~0,5 mm) rodo, ar derva išlaiko smulkią
// juvelyrinę detalę. Spausdinamas gulsčias; vidinis skersmuo 17,3 mm (US 7).
$fn = 96;
ID = 17.3; th = 1.4; band = 6; n = 18;
difference() {
  cylinder(d = ID + 2*th, h = band);
  translate([0,0,-0.1]) cylinder(d = ID, h = band + 0.2);
  for (row = [0, 1]) for (i = [0:n-1])
    rotate([0, 0, i*360/n + row*180/n]) translate([ID/2 + th/2, 0, 1.6 + row*2.8])
      rotate([0, 90, 0]) rotate([0, 0, 45]) cube([1.5, 1.5, 4], center = true);
}
