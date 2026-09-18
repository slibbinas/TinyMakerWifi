// Paprastas modelis lanksčioms dervoms: juostelė su trimis storiais.
// Lenkiama per pusę: turi grįžti į formą ir neįtrūkti. Storiausia dalis
// parodo, ar derva išlieka lanksti storesniame sluoksnyje.
th = [0.8, 1.2, 1.6];
seg = 10; w = 6;
for (i = [0:len(th)-1]) translate([i*seg, 0, 0]) cube([seg, w, th[i]]);
// ąselė galui sugriebti
translate([-4, 0, 0]) difference() { cube([4.2, w, 1.6]); translate([2, w/2, -0.1]) cylinder(d = 2, h = 2, $fn = 24); }
