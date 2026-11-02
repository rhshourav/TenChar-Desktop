#!/usr/bin/env python3
"""
TenChar reference vectors, written from the spec in the desktop plan
("The protocol the desktop must implement"), NOT from the Kotlin or C++ cores.
HKDF is hand-written (RFC 5869) and self-checked; X25519/AES-GCM/PBKDF2 come
from `cryptography` / hashlib. Randomness is fixed so output is deterministic.
Run: python3 gen_vectors.py > vectors.json
"""
import hashlib, hmac, json, sys
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives import serialization as S

H = lambda b: b.hex()
def sha(*p): return hashlib.sha256(b"".join(p)).digest()
def hm(k, *p):
    m = hmac.new(k, digestmod=hashlib.sha256)
    for x in p: m.update(x)
    return m.digest()

def hkdf(salt, ikm, info, n):
    prk = hm(salt if salt else b"\0" * 32, ikm)
    out, t, i = b"", b"", 1
    while len(out) < n:
        t = hm(prk, t, info, bytes([i])); out += t; i += 1
    return out[:n]

# --- self-check against RFC 5869 test case 1 -------------------------------
assert hkdf(bytes.fromhex("000102030405060708090a0b0c"),
            bytes.fromhex("0b" * 22),
            bytes.fromhex("f0f1f2f3f4f5f6f7f8f9"), 42).hex() == \
    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"

def pub(priv):
    return X25519PrivateKey.from_private_bytes(priv).public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw)
def dh(priv, peer):
    out = X25519PrivateKey.from_private_bytes(priv).exchange(X25519PublicKey.from_public_bytes(peer))
    assert out != bytes(32); return out
def fixed(label): return sha(b"tenchar-test-vector/" + label.encode())  # deterministic "random"

# --- 1. pairing code derivations -------------------------------------------
CODE = "ABCDEFGHJKLMNPQR"
def pbk(salt): return hashlib.pbkdf2_hmac("sha256", CODE.encode(), salt, 600_000, 32)
room, tok, key = pbk(b"tenchar-room-v1"), pbk(b"tenchar-auth-v1"), pbk(b"tenchar-key-v1")
pairing = {"code": CODE, "room": H(room)[:32], "codeToken": H(tok)[:48], "codeKey": H(key)}

# --- 2. members, commitments -----------------------------------------------
def member(label, mid):
    ik, ek = fixed(label + "/ik"), fixed(label + "/ek")
    mid = bytes.fromhex(mid)
    return dict(id=mid, ik=ik, ek=ek, ikp=pub(ik), ekp=pub(ek),
                commit=sha(b"tenchar-commit-v1", pub(ik), pub(ek), mid))
A = member("A", "11" * 16)   # smaller id -> initiator
B = member("B", "22" * 16)
assert A["id"].hex() < B["id"].hex()
I, R = A, B

# --- 3. handshake, computed from both sides --------------------------------
dh1 = dh(I["ik"], R["ekp"]); dh2 = dh(I["ek"], R["ikp"])
dh3 = dh(I["ek"], R["ekp"]); dh4 = dh(I["ik"], R["ikp"])
# responder's view must agree
assert (dh(R["ek"], I["ikp"]), dh(R["ik"], I["ekp"]), dh(R["ek"], I["ekp"]), dh(R["ik"], I["ikp"])) == (dh1, dh2, dh3, dh4)
ikm = b"\xff" * 32 + dh1 + dh2 + dh3 + dh4
info = b"tenchar-x3dh-v1" + I["ikp"] + I["ekp"] + R["ikp"] + R["ekp"] + I["id"] + R["id"]
SK = hkdf(key, ikm, info, 32)
AD = I["ikp"] + R["ikp"]
v = int.from_bytes(hkdf(b"", SK, b"tenchar-verify-v1" + info, 7), "big") % 10**8
number = f"{v:08d}"; number = number[:4] + " " + number[4:]
handshake = {"initiator_member": "A", "A": {k: H(x) for k, x in A.items()}, "B": {k: H(x) for k, x in B.items()},
             "dh": [H(dh1), H(dh2), H(dh3), H(dh4)], "SK": H(SK), "AD": H(AD), "verification": number}

# --- 4. ratchet -------------------------------------------------------------
def kdf_rk(rk, d):
    o = hkdf(rk, d, b"tenchar-ratchet-v1", 64); return o[:32], o[32:]
def kdf_ck(ck): return hm(ck, b"\x02"), hm(ck, b"\x01")        # (next ck, mk)
def msg_key(mk): return hkdf(b"", mk, b"tenchar-message-v1", 32)

def frame(text):
    b = text.encode(); assert 1 <= len(b) <= 64
    return bytes([1, len(b)]) + b + bytes(64 - len(b))

class Ratchet:
    def __init__(s, role, sk, ad, ek, peer_ek, rand):
        s.rand, s.ad, s.skipped = list(rand), ad, {}
        s.ns = s.nr = s.pn = 0
        if role == "I":
            s.rk, s.cks = kdf_rk(sk, dh(ek, peer_ek)); s.ckr = None
            s.priv, s.pub = ek, pub(ek)
        else:
            rk1, s.ckr = kdf_rk(sk, dh(ek, peer_ek))
            s.priv = s.rand.pop(0)
            s.rk, s.cks = kdf_rk(rk1, dh(s.priv, peer_ek)); s.pub = pub(s.priv)
        s.dhr = peer_ek
    def encrypt(s, text):
        s.cks, mk = kdf_ck(s.cks)
        hdr = b"\x01" + s.pub + s.pn.to_bytes(4, "big") + s.ns.to_bytes(4, "big")
        s.ns += 1
        nonce = s.rand.pop(0)[:12]
        return hdr + nonce + AESGCM(msg_key(mk)).encrypt(nonce, frame(text), hdr + s.ad)
    def _skip(s, until):
        while s.nr < until:
            s.ckr, mk = kdf_ck(s.ckr); s.skipped[(s.dhr, s.nr)] = mk; s.nr += 1
    def decrypt(s, env):
        assert len(env) == 135 and env[0] == 1
        dhp, pn, n = env[1:33], int.from_bytes(env[33:37], "big"), int.from_bytes(env[37:41], "big")
        nonce, sealed, aad = env[41:53], env[53:], env[:41] + s.ad
        if (dhp, n) in s.skipped:
            mk = s.skipped.pop((dhp, n))
        else:
            if dhp != s.dhr:
                if s.ckr: s._skip(pn)
                s.pn, s.ns, s.nr, s.dhr = s.ns, 0, 0, dhp
                rk1, s.ckr = kdf_rk(s.rk, dh(s.priv, dhp))
                s.priv = s.rand.pop(0); s.pub = pub(s.priv)
                s.rk, s.cks = kdf_rk(rk1, dh(s.priv, dhp))
            assert n >= s.nr, "replay"
            s._skip(n); s.ckr, mk = kdf_ck(s.ckr); s.nr += 1
        pt = AESGCM(msg_key(mk)).decrypt(nonce, sealed, aad)
        assert pt[0] == 1 and pt[2 + pt[1]:] == bytes(64 - pt[1])
        return pt[2:2 + pt[1]].decode()

nonce = lambda i: sha(b"nonce", bytes([i]))[:12]
# injected randomness, in the order each side consumes it
rand_I = [fixed("I/ratchet2"), nonce(1), nonce(2), nonce(3)]
rand_R = [fixed("R/ratchet1"), fixed("R/ratchet3"), nonce(4), nonce(5)]
ri = Ratchet("I", SK, AD, I["ek"], R["ekp"], rand_I)
rr = Ratchet("R", SK, AD, R["ek"], I["ekp"], rand_R)
# rand_I order: [ratchet key on first DH step, nonce for each send...] -> reorder so nonces
# are popped in use order: I sends first (needs nonce before ratchet key). Use separate queues:
ri.rand = [nonce(1)]; ri_keys = [fixed("I/ratchet2")]
rr.rand = [nonce(4)]; rr_keys = [fixed("R/ratchet3")]

steps = []
def step(frm, to, text, order_note, sender, receiver, nonce_bytes, new_key=None):
    sender.rand = [nonce_bytes] 
    env = sender.encrypt(text)
    steps.append({"from": frm, "text": text, "envelope_hex": H(env), "note": order_note})
    return env

# 1. I -> R "hi"
e1 = step("I", "R", "hi", "first message, no DH step", ri, rr, nonce(1))
rr.rand = [fixed("unused")]; assert rr.decrypt(e1) == "hi"
# 2. R -> I "yo"  (R's first sending key is its init key, injected earlier)
e2 = step("R", "I", "yo", "responder's first send", rr, ri, nonce(2))
ri.rand = [fixed("I/ratchet2")]; assert ri.decrypt(e2) == "yo"   # I does DH step, new key I/ratchet2
# 3-4. I -> R twice with new ratchet key; pn must be 1
e3 = step("I", "R", "ok", "new ratchet key, pn=1, n=0", ri, rr, nonce(3))
e4 = step("I", "R", "ok2", "n=1", ri, rr, nonce(5))
# R receives out of order: n=1 first, then n=0
rr.rand = [fixed("R/ratchet3")]; assert rr.decrypt(e4) == "ok2"   # DH step on R, skips n=0
assert rr.decrypt(e3) == "ok"                                      # from skipped store
steps[2]["note"] += "; delivered AFTER message 4 (out of order)"
# 5. R -> I with R's ratchet3 key
e5 = step("R", "I", "bye", "R's new ratchet key after DH step", rr, ri, nonce(6))
ri.rand = [fixed("I/ratchet4")]; assert ri.decrypt(e5) == "bye"
# replay must fail
try: rr.decrypt(e3); raise SystemExit("replay accepted!")
except AssertionError: pass

ratchet = {"AD": H(AD), "SK": H(SK),
           "injected": {"R_first_ratchet_priv": H(fixed("R/ratchet1")), "I_ratchet2_priv": H(fixed("I/ratchet2")),
                        "R_ratchet3_priv": H(fixed("R/ratchet3")), "I_ratchet4_priv": H(fixed("I/ratchet4")),
                        "nonces_in_send_order": [H(nonce(i)) for i in (1, 2, 3, 5, 6)]},
           "delivery_order": [1, 2, 4, 3, 5], "steps": steps}
json.dump({"version": 1, "spec": "TenChar Desktop plan, Oct 28 2026",
           "pairing": pairing, "handshake": handshake, "ratchet": ratchet}, sys.stdout, indent=1)
print("all self-checks passed", file=sys.stderr)
