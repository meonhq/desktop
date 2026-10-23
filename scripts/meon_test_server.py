#!/usr/bin/env python3
"""MEON 전송층 시험용 가짜 HTTP·WebSocket 서버 (R11). 표준 라이브러리만 쓴다 (CI 3 OS 공통).

    python3 scripts/meon_test_server.py 18080      # 127.0.0.1:18080
    MEON_TEST_MOCK=http://127.0.0.1:18080 MEON_Tests Transport

경로 (API 는 /v1 아래):
    ANY  /v1/echo/...          받은 요청을 JSON 으로 돌려준다 {method, path, query, headers, body}
    ANY  /v1/status/<code>     그 상태 코드와 {"error":{"code":"x_<code>","message":"m"}} 본문. 429 면 Retry-After: 7
    GET  /v1/ws                WebSocket. 받은 텍스트를 그대로 돌려준다.
                               {"close": 4000} 을 받으면 그 코드로 닫는다. {"drop": true} 면 닫는 프레임 없이 끊는다
업데이트 받기 시험 (UpdateFetch: 리디렉션을 따르지 않기, 크기 한도):
    GET  /dl/bytes/<n>         n 바이트 ('a')
    GET  /dl/redirect          302 → /dl/bytes/10
    GET  /dl/status/<code>     그 상태 코드, 빈 본문
"""
import base64
import hashlib
import json
import struct
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("mock: " + (fmt % args) + "\n")

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n > 0 else b""

    def _json(self, status, obj, extra=None):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def _handle(self):
        u = urlsplit(self.path)
        if u.path == "/v1/ws" and self.headers.get("Upgrade", "").lower() == "websocket":
            return self._websocket()
        body = self._body()
        if u.path.startswith("/v1/echo"):
            try:
                parsed = json.loads(body) if body else None
            except ValueError:
                parsed = {"raw": body.decode(errors="replace")}
            return self._json(200, {
                "method": self.command,
                "path": u.path,
                "query": {k: v[0] for k, v in parse_qs(u.query).items()},
                "headers": {k.lower(): v for k, v in self.headers.items()},
                "body": parsed,
                "body_length": len(body),
            })
        if u.path.startswith("/dl/"):
            return self._dl(u.path)
        if u.path.startswith("/v1/status/"):
            code = int(u.path.rsplit("/", 1)[1])
            extra = {"Retry-After": "7"} if code == 429 else None
            if code == 204:
                self.send_response(204)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            return self._json(code, {"error": {"code": "x_%d" % code, "message": "m", "field": "f"}}, extra)
        return self._json(404, {"error": {"code": "not_found", "message": "없음"}})

    do_GET = do_POST = do_PATCH = do_DELETE = do_PUT = _handle

    def _dl(self, path):
        def reply(status, data=b"", extra=None):
            self.send_response(status)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(data)
        if path.startswith("/dl/bytes/"):
            return reply(200, b"a" * int(path.rsplit("/", 1)[1]))
        if path == "/dl/redirect":
            return reply(302, b"", {"Location": "/dl/bytes/10"})
        if path.startswith("/dl/status/"):
            return reply(int(path.rsplit("/", 1)[1]))
        return reply(404)

    # ---- WebSocket (RFC 6455, 텍스트·닫기·핑만) ----

    def _websocket(self):
        key = self.headers.get("Sec-WebSocket-Key", "")
        accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()
        self.close_connection = True
        while True:
            frame = self._read_frame()
            if frame is None:
                return
            opcode, payload = frame
            if opcode == 0x8:  # 닫기 → 같은 코드로 답하고 끝
                self._send_frame(0x8, payload[:2])
                return
            if opcode == 0x9:  # ping → pong
                self._send_frame(0xA, payload)
                continue
            if opcode != 0x1:
                continue
            text = payload.decode("utf-8", errors="replace")
            try:
                msg = json.loads(text)
            except ValueError:
                msg = None
            if isinstance(msg, dict) and "close" in msg:
                self._send_frame(0x8, struct.pack("!H", int(msg["close"])) + b"bye")
                # 클라이언트의 닫기 답을 잠깐 기다린다
                self._read_frame()
                return
            if isinstance(msg, dict) and msg.get("drop"):
                self.connection.shutdown(2)
                return
            self._send_frame(0x1, payload)

    def _read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.rfile.read(n - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _read_frame(self):
        head = self._read_exact(2)
        if head is None:
            return None
        b1, b2 = head
        opcode = b1 & 0x0F
        masked = b2 & 0x80
        length = b2 & 0x7F
        if length == 126:
            ext = self._read_exact(2)
            if ext is None:
                return None
            length = struct.unpack("!H", ext)[0]
        elif length == 127:
            ext = self._read_exact(8)
            if ext is None:
                return None
            length = struct.unpack("!Q", ext)[0]
        mask = self._read_exact(4) if masked else b"\0\0\0\0"
        if mask is None:
            return None
        payload = self._read_exact(length) if length else b""
        if payload is None:
            return None
        payload = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
        return opcode, payload

    def _send_frame(self, opcode, payload):
        header = bytes([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header += bytes([n])
        elif n < 65536:
            header += bytes([126]) + struct.pack("!H", n)
        else:
            header += bytes([127]) + struct.pack("!Q", n)
        try:
            self.wfile.write(header + payload)
            self.wfile.flush()
        except OSError:
            pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    sys.stderr.write("mock: listening on 127.0.0.1:%d\n" % port)
    server.serve_forever()


if __name__ == "__main__":
    main()
