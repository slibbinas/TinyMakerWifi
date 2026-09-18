// Paprastas modelis tvirtoms dervoms: spragtukas iš dviejų dalių, spausdinamų
// greta, gulsčių. Kištukas su dviem lanksčiomis kojelėmis įstumiamas į lizdą:
// užkabos susispaudžia kanale ir išsiskečia kameroje. Pavyko = įsispaudžia ir
// laikosi; kojelės lūžta = derva per trapi arba perkietinta.
leg = 1.0;       // kojelės storis, mm
tol = 0.25;      // tarpas lizde
h = 2;           // kištuko storis
barb = 0.8;      // užkabos iškyša į šoną
// kištukas: galvutė y 0..6, kojelės y 6..18, užkabos kojelių galuose
cube([12, 6, h]);
translate([2, 6, 0]) cube([leg, 12, h]);
translate([10 - leg, 6, 0]) cube([leg, 12, h]);
translate([2 - barb, 16, 0]) cube([barb + leg, 2, h]);
translate([10 - leg, 16, 0]) cube([barb + leg, 2, h]);
// lizdas: kanalas 10 mm, už jo kamera užkaboms
translate([16, 0, 0]) difference() {
  cube([14, 16, 5]);
  translate([3 - tol, -0.1, 1.5 - tol]) cube([8 + 2*tol, 10.2, h + 2*tol]);
  translate([3 - barb - tol, 10, 1.5 - tol]) cube([8 + 2*barb + 2*tol, 4, h + 2*tol]);
}
