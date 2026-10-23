#!/usr/bin/env python3
"""서명 키 비밀값(DL_SIGNING_KEY_GPL1)을 openssl 이 읽는 Ed25519 PKCS#8 PEM 으로 맞춘다. 표준 라이브러리만 쓴다.

비밀값은 환경 변수 SIGNING_KEY 로만 받고(명령줄에 두지 않는다) 결과는 인자로 받은 파일(권한 600)에 쓴다.
키의 내용은 출력하지 않는다. 표준 오류에 모양(형식·줄 수·CR·"\\n" 글자 여부)만 적는다.

받는 모양
- PEM "PRIVATE KEY" (openssl genpkey -algorithm ed25519): 줄바꿈이 공백이나 "\\n" 글자로 바뀌었거나 CR 이 섞여도 맞춘다
- OpenSSH "OPENSSH PRIVATE KEY" (ssh-keygen -t ed25519, 암호 없음)
- 머리 없는 base64 (PKCS#8 DER 48바이트, 씨앗 32바이트, 씨앗+공개 키 64바이트), hex (64자 씨앗, 128자 씨앗+공개 키)
못 받는 모양: 암호가 걸린 키, 공개 키, Ed25519 가 아닌 키 → 1 로 끝나고 이유를 적는다

    SIGNING_KEY="$SECRET" python3 scripts/dl_signing_key.py key.pem
"""
import base64
import binascii
import os
import re
import struct
import sys

PKCS8_PREFIX = bytes.fromhex("302e020100300506032b657004220420")  # Ed25519 개인 키 PKCS#8 머리 (뒤에 씨앗 32바이트)


def fail(msg):
    sys.stderr.write("서명 키: " + msg + "\n")
    sys.exit(1)


def pem_from_seed(seed):
    der = PKCS8_PREFIX + seed
    b64 = base64.b64encode(der).decode()
    return "-----BEGIN PRIVATE KEY-----\n" + b64 + "\n-----END PRIVATE KEY-----\n"


def seed_from_der(der):
    if len(der) == 48 and der.startswith(PKCS8_PREFIX):
        return der[16:]
    return None


def seed_from_openssh(blob):
    # openssh-key-v1 (PROTOCOL.key): magic, cipher, kdf, kdfoptions, nkeys, pubkey, private section
    magic = b"openssh-key-v1\x00"
    if not blob.startswith(magic):
        fail("OPENSSH 형식이 아님")
    pos = len(magic)

    def string():
        nonlocal pos
        (n,) = struct.unpack(">I", blob[pos:pos + 4])
        pos += 4
        s = blob[pos:pos + n]
        pos += n
        return s

    cipher = string()
    string()  # kdfname
    string()  # kdfoptions
    (nkeys,) = struct.unpack(">I", blob[pos:pos + 4])
    pos += 4
    if cipher != b"none":
        fail("OpenSSH 키에 암호가 걸려 있음 (암호 없는 키가 필요)")
    if nkeys != 1:
        fail("OpenSSH 파일에 키가 %d 개" % nkeys)
    string()  # public key blob
    priv = string()
    p = 8  # checkint 두 개
    def pstring():
        nonlocal p
        (n,) = struct.unpack(">I", priv[p:p + 4])
        p += 4
        s = priv[p:p + n]
        p += n
        return s
    keytype = pstring()
    if keytype != b"ssh-ed25519":
        fail("OpenSSH 키가 Ed25519 가 아님 (%s)" % keytype.decode(errors="replace"))
    pstring()  # 공개 키 32
    sk = pstring()  # 씨앗 32 + 공개 키 32
    if len(sk) != 64:
        fail("OpenSSH Ed25519 개인 키 길이가 이상함")
    return sk[:32]


def main():
    if len(sys.argv) != 2:
        fail("사용법: SIGNING_KEY=... dl_signing_key.py <출력 PEM>")
    raw = os.environ.get("SIGNING_KEY", "")
    if not raw.strip():
        fail("비어 있음")

    shape = []
    shape.append("줄 %d" % (raw.count("\n") + 1))
    shape.append("CR 있음" if "\r" in raw else "CR 없음")
    shape.append("'\\n' 글자 있음" if "\\n" in raw else "'\\n' 글자 없음")
    text = raw.replace("\\n", "\n").replace("\r", "")

    seed = None
    m = re.search(r"-----BEGIN ([A-Z0-9 ]+)-----(.*?)-----END \1-----", text, re.S)
    if m:
        label = m.group(1)
        body = re.sub(r"\s+", "", m.group(2))
        shape.insert(0, "PEM(%s)" % label)
        shape.append("본문 %d자" % len(body))
        sys.stderr.write("서명 키 모양: " + ", ".join(shape) + "\n")
        if "ENCRYPTED" in label or "Proc-Type:" in m.group(2):
            fail("암호가 걸린 개인 키 (암호 없는 PEM 이 필요)")
        if "PUBLIC" in label:
            fail("공개 키가 들어 있음 (개인 키가 필요)")
        try:
            der = base64.b64decode(body, validate=True)
        except (binascii.Error, ValueError):
            fail("PEM 본문이 base64 가 아님")
        if label == "PRIVATE KEY":
            seed = seed_from_der(der)
            if seed is None:
                fail("PKCS#8 이지만 Ed25519 개인 키가 아님 (DER %d바이트)" % len(der))
        elif label == "OPENSSH PRIVATE KEY":
            seed = seed_from_openssh(der)
        else:
            fail("모르는 PEM 종류 (%s)" % label)
    else:
        compact = re.sub(r"\s+", "", text)
        if re.fullmatch(r"[0-9a-fA-F]{64}|[0-9a-fA-F]{128}", compact):
            shape.insert(0, "hex %d자" % len(compact))
            seed = bytes.fromhex(compact)[:32]
        else:
            try:
                der = base64.b64decode(compact, validate=True)
            except (binascii.Error, ValueError):
                sys.stderr.write("서명 키 모양: 머리 없음, " + ", ".join(shape) + ", %d자\n" % len(compact))
                fail("PEM·hex·base64 어느 것도 아님")
            shape.insert(0, "base64 %d바이트" % len(der))
            if len(der) in (32, 64):
                seed = der[:32]
            else:
                seed = seed_from_der(der)
            if seed is None:
                sys.stderr.write("서명 키 모양: " + ", ".join(shape) + "\n")
                fail("base64 이지만 Ed25519 개인 키 길이가 아님")
        sys.stderr.write("서명 키 모양: " + ", ".join(shape) + "\n")

    fd = os.open(sys.argv[1], os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(pem_from_seed(seed))


if __name__ == "__main__":
    main()
