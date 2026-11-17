#!/usr/bin/env python3
"""Test-only loopback server. No Bluetooth or physical keyboard operations."""
import argparse
import socketserver
import threading
from host_support import Backend, frames, encode

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=19761)
    parser.add_argument('--sensors', type=int, choices=range(4), default=3)
    args = parser.parse_args()
    backend = Backend(args.sensors); lock = threading.Lock()
    class Handler(socketserver.StreamRequestHandler):
        def handle(self):
            for raw in frames(self.rfile):
                with lock: result = backend.envelope(raw)
                self.wfile.write(encode(result)); self.wfile.flush()
    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        daemon_threads = True
    with Server(('127.0.0.1', args.port), Handler) as server:
        print(f'Local C backend listening on 127.0.0.1:{args.port}; sensors={args.sensors}', flush=True)
        server.serve_forever()
