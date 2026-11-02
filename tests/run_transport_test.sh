#!/bin/sh
# Starts the local HTTPS server with a throwaway self-signed certificate, runs the transport test, cleans up.
set -e
TEST_BIN="$1"; HERE="$(cd "$(dirname "$0")" && pwd)"; DIR="$(mktemp -d)"; PORT=$(( 20000 + $$ % 20000 ))
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$DIR/key.pem" -out "$DIR/cert.pem" -days 1 -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" >/dev/null 2>&1
python3 "$HERE/transport_server.py" "$DIR/cert.pem" "$DIR/key.pem" "$PORT" & SERVER=$!
trap 'kill $SERVER 2>/dev/null; rm -rf "$DIR"' EXIT
sleep 1
QT_QPA_PLATFORM=offscreen "$TEST_BIN" "$DIR/cert.pem" "$PORT"
