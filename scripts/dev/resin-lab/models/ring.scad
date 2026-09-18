// Paprastas modelis lydomoms ir vaškinėms dervoms: lygus žiedas.
// Spausdinamas gulsčias - vertinamas paviršius (lydymui svarbu glotnumas)
// ir ar žiedas neįskilęs. Vidinis skersmuo 17,3 mm (US 7).
$fn = 128;
ID = 17.3; band = 2.2; th = 1.6;
difference() { cylinder(d = ID + 2*th, h = band); translate([0,0,-0.1]) cylinder(d = ID, h = band + 0.2); }
