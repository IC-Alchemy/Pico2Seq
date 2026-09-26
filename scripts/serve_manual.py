"""Serve the Pico2Seq manual on the local network.

Usage:  python scripts/serve_manual.py   (Ctrl+C to stop)

Serves the repo's docs/ directory on 0.0.0.0:80; http://<this-host>/ opens the
manual directly. No dependencies beyond the Python standard library.
"""
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs")
PORT = 80


class ManualHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=os.path.abspath(ROOT), **kwargs)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.path = "/manual.html"
        return super().do_GET()

    def log_message(self, fmt, *args):
        print("[manual] %s" % (fmt % args))


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", PORT), ManualHandler)
    print(f"Pico2Seq manual: http://0.0.0.0:{PORT}/  (docs/ -> {os.path.abspath(ROOT)})")
    server.serve_forever()
