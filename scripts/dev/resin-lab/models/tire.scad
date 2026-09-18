// Sudėtingas modelis lanksčioms dervoms: padangėlė su protektoriumi.
// Ją reikia užtempti ant 16 mm ratlankio (ar pirštų) - turi išsitempti ir grįžti.
$fn = 96;
OD = 26; ID = 16; W = 8; n = 24;
difference() {
  cylinder(d = OD, h = W);
  translate([0,0,-0.1]) cylinder(d = ID, h = W + 0.2);
  for (i = [0:n-1]) rotate([0,0,i*360/n]) translate([OD/2 - 0.8, -0.6, (i % 2) ? 1 : W/2])
    cube([1.2, 1.2, W/2 - 1]);
}
translate([0,0,W/2 - 0.4]) difference() { cylinder(d = ID + 0.1, h = 0.8); translate([0,0,-0.1]) cylinder(d = ID - 1.6, h = 1); }
