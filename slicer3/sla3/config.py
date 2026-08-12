"""Printerio geometrija.

Visi skaičiai paimti iš TIKRO profilio (`prusa-full.ini`, presetas TinyMaker),
ne iš atminties ir ne iš kito mūsų modulio:

    display_width  = 40.8      display_pixels_x = 320
    display_height = 30.6      display_pixels_y = 240
    layer_height   = 0.05      display_mirror_x = 1
"""

PLATE_W_MM = 40.8
PLATE_H_MM = 30.6
RES_X = 320
RES_Y = 240
LAYER_MM = 0.05

# Pikselis kvadratinis: 40.8/320 == 30.6/240 == 0.1275
PIXEL_MM = PLATE_W_MM / RES_X

#: display_mirror_x = 1 — ekranas rodo veidrodiškai, tad kaukė verčiama.
MIRROR_X = True
MIRROR_Y = False

#: Rastrizuojam didesnėje skiriamojoje gebos ir sumažinam — taip gaunam pilkus
#: kraštus (PrusaSlicer tam naudoja AGG antialiasing). 1 = išjungta.
SUPERSAMPLE = 4

assert abs(PLATE_H_MM / RES_Y - PIXEL_MM) < 1e-12, 'pikselis turi būti kvadratinis'
