# 시험용 SMTP 받기 (가입 인증 메일): 받은 메일 본문을 out 폴더에 파일로 남긴다. 사용: python3 -I smtp_capture.py 포트 폴더
import socket, sys, os, threading, time
port, out = int(sys.argv[1]), sys.argv[2]
os.makedirs(out, exist_ok=True)
def handle(c):
    f = c.makefile('rb'); w = lambda s: c.sendall((s + "\r\n").encode())
    w("220 test"); data = None
    while True:
        line = f.readline()
        if not line: break
        cmd = line.decode(errors='replace').strip().upper()
        if cmd.startswith("EHLO") or cmd.startswith("HELO"): w("250 ok")
        elif cmd.startswith("MAIL") or cmd.startswith("RCPT") or cmd.startswith("RSET") or cmd.startswith("NOOP"): w("250 ok")
        elif cmd == "DATA":
            w("354 go"); buf = b""
            while True:
                l = f.readline()
                if l in (b".\r\n", b".\n", b""): break
                buf += l
            open(os.path.join(out, "%f.eml" % time.time()), "wb").write(buf); w("250 ok")
        elif cmd == "QUIT": w("221 bye"); break
        else: w("250 ok")
    c.close()
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("127.0.0.1", port)); s.listen(5)
while True:
    c, _ = s.accept(); threading.Thread(target=handle, args=(c,), daemon=True).start()
