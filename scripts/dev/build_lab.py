"""Slicerio stendas: tas pats pultas, tik du blokai ir jau įkeltas modelis.

Ima gatavą demo puslapį (tikras dashboard + demo_shim.js netikras printeris) ir
prideda lab_shim.js, kuris palieka ekrane peržiūrą ir slicerio bloką, o testinį
STL įkelia bei suslicina iškart — kad kiekvienas bandymas prasidėtų ties tuo,
kas įdomu, o ne ties „Choose STL“ (V 08-13).

  python scripts/dev/build_lab.py [out.html]

Modeliai imami iš /models/<vardas> to paties serverio, tad prieš tai jie turi
būti nukopijuoti šalia išvesties.
"""
import io
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "lab.html")
DEMO = OUT + ".demo.tmp"

subprocess.check_call([sys.executable, os.path.join(HERE, "build_demo.py"), DEMO])
html = io.open(DEMO, encoding="utf-8").read()
os.remove(DEMO)

# Ką stende slepiam — įrašoma STATIŠKAI į <head>, ne per skriptą. Per skriptą
# įterpiamas stilius suveikdavo tik po sekundės, ir kraunant mirktelėdavo
# skirtukai su pradžios vedikliu (V 08-13). Printerio dashboard.html
# nekeičiamas — tai tik stendo priedas.
HIDE_CSS = """<style id="lab-hide">
section:not(#printPreviewCard):not(#slicerCard){display:none!important}
nav,footer,.head,.hint{display:none!important}
#gsCard{display:none!important}
main > .toolbar{display:none!important}
#connectView,#configView,#statsView,#updateView{display:none!important}
</style>"""
html = html.replace("</head>", HIDE_CSS + "\n</head>", 1) if "</head>" in html \
       else html.replace("<title>TinyMaker</title>", "<title>TinyMaker</title>" + HIDE_CSS, 1)

shim = io.open(os.path.join(HERE, "lab_shim.js"), encoding="utf-8").read()
# Po demo shim'o: jis pirmas pastato netikrą printerį, mes tik perdengiam UI.
marker = "</body>"
if marker in html:
    html = html.replace(marker, "<script>\n" + shim + "\n</script>\n" + marker, 1)
else:
    html += "<script>\n" + shim + "\n</script>"

io.open(OUT, "w", encoding="utf-8").write(html)
print(f"wrote {OUT}: {len(html)} bytes")
