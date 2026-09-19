// TinyMaker Resin Lab - spaustukas ant strypo (paprastas modelis).
// Ar derva daro tai, kam skirta: apkabą užmauti ant strypo ir nuimti 10 kartų.
//   Strypas Ø5 mm, 10 mm aukščio, ant 10 × 10 mm pado.
//   Dvi apkabos: sienelė 1,5 ir 2,5 mm, anga 4,2 mm (siauresnė už strypą - turi
//   išsilenkti). Trapi derva plona apkaba skils; kieta - laikys, bet sunkiai užsimaus.
// STL: openscad -o TM_spaustukas.stl tm_spaustukas.scad

$fn = 48;
ROD = 5; GAP = 4.2; H = 3;

module clip(wall) {
  difference() {
    cylinder(d = ROD + 0.1 + 2 * wall, h = H);
    translate([0, 0, -1]) cylinder(d = ROD + 0.1, h = H + 2);
    translate([0, -GAP / 2, -1]) cube([ROD + 2 * wall, GAP, H + 2]);
  }
}

cube([10, 10, 1.5]);
translate([5, 5, 1.49]) cylinder(d = ROD, h = 10);
translate([17, 5, 0]) clip(1.5);
translate([28, 5, 0]) clip(2.5);
