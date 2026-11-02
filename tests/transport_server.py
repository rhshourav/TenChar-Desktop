#!/usr/bin/env python3
"""A tiny local HTTPS server for exercising QtTransport. Usage: transport_server.py CERT KEY PORT"""
import http.server, json, ssl, sys

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, status, body=b"", headers=None):
        self.send_response(status)
        for k, v in (headers or {}).items(): self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def _handle(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        p = self.path
        if p.endswith("/redirect"):
            self._send(302, b"", {"Location": f"https://localhost:{PORT}/v1/rooms/{'a'*32}/state"})
        elif p.endswith("/big"):
            self._send(200, b"x" * 300_000, {"Content-Type": "application/json"})
        elif p.endswith("/missing"):
            self._send(404, json.dumps({"error": "not found"}).encode(), {"Content-Type": "application/json"})
        else:
            self._send(200, json.dumps({"method": self.command, "auth": self.headers.get("Authorization"),
                                        "ua": self.headers.get("User-Agent"), "ctype": self.headers.get("Content-Type"),
                                        "body": body.decode()}).encode(), {"Content-Type": "application/json"})
    do_GET = do_POST = do_DELETE = _handle

cert, key, PORT = sys.argv[1], sys.argv[2], int(sys.argv[3])
srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(cert, key)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
