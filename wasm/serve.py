"""Mazas serveris demo puslapiui: atiduoda failus IR priima PNG per POST.

Reikia todel, kad narsykleje nupiestas vaizdas atsidurtu diske - kitaip ji
tektu nesti per konsole base64 pavidalu.

    python serve.py [portas]
"""
import base64
import http.server
import os
import sys

KATALOGAS = os.path.dirname(os.path.abspath(__file__))


class Tvarkytojas(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=KATALOGAS, **kw)

    def do_POST(self):
        ilgis = int(self.headers.get('Content-Length', 0))
        kunas = self.rfile.read(ilgis)
        vardas = os.path.basename(self.path.lstrip('/')) or 'vaizdas.png'
        duom = kunas
        # data:image/png;base64,.... -> baitai
        if kunas[:5] == b'data:':
            duom = base64.b64decode(kunas.split(b',', 1)[1])
        kelias = os.path.join(KATALOGAS, vardas)
        with open(kelias, 'wb') as f:
            f.write(duom)
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.end_headers()
        self.wfile.write(('irasyta %s (%d B)' % (vardas, len(duom))).encode())

    def log_message(self, *a):
        pass


if __name__ == '__main__':
    portas = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    http.server.ThreadingHTTPServer(('127.0.0.1', portas), Tvarkytojas).serve_forever()
