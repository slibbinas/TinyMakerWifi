// TinyMaker Resin Lab - tilteliai (paprastas modelis).
// Sija 0,6 mm storio ant penkių stulpelių, tarpai 2, 4, 6 ir 8 mm be atramų.
// Ilgiausias tarpas be įlinkio ir be skylių = Regular ir kėlimas tinka.
// STL: openscad -o TM_tiltai.stl tm_tiltai.scad

W = 3; P = 2; Hp = 4; Tb = 0.6; B = 1.5;
spans = [2, 4, 6, 8];
function px(i) = 2 + i * P + (i > 0 ? [for (j = [0 : i - 1]) spans[j]] * [for (j = [0 : i - 1]) 1] : 0);
L = px(len(spans)) + P + 2;

cube([L, 8, B]);
for (i = [0 : len(spans)])
  translate([px(i), 2.5, B - 0.01]) cube([P, W, Hp]);
translate([px(0), 2.5, B + Hp - Tb]) cube([px(len(spans)) + P - px(0), W, Tb]);
