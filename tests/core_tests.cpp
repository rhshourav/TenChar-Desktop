#include <algorithm>

#include "kat.hpp"
#include "tc/aead.hpp"
#include "tc/constants.hpp"
#include "tc/handshake.hpp"
#include "tc/json.hpp"
#include "tc/kdf.hpp"
#include "tc/limits.hpp"
#include "tc/pairing.hpp"
#include "tc/ratchet.hpp"
#include "tc/wire.hpp"
#include "tc/x25519.hpp"
#include "testing.hpp"

using namespace tc;

static const limits::Segmenter seg = limits::codepointClusters;

// ---- primitives against published / independently computed values ----------------------------

static void testPrimitives() {
    CHECK_EQ(H(*hexDecode("00ffAb")), std::string("00ffab"));
    CHECK(!hexDecode("abc") && !hexDecode("zz") && !hexDecode("\xd9\xa0"));
    CHECK(constantTimeEquals(toBytes("abc"), toBytes("abc")) && !constantTimeEquals(toBytes("abc"), toBytes("abd")));

    // RFC 5869 case 1 and case 3 (empty salt, empty info)
    CHECK_EQ(H(kdf::hkdf(unhex("000102030405060708090a0b0c"), unhex(std::string(44, 'b').replace(0, 44, "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")),
                        unhex("f0f1f2f3f4f5f6f7f8f9"), 42)),
             std::string("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
    CHECK_EQ(H(kdf::hkdf({}, unhex(std::string(44, '0').replace(0, 44, "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")), {}, 42)),
             std::string(KAT_HKDF_TC3));
    CHECK_EQ(H(kdf::pbkdf2(asBytes("password"), asBytes("salt"), 4096, 32)), std::string(KAT_PBKDF2));
    CHECK_EQ(H(kdf::sha256({asBytes("abc")})), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    // X25519: RFC 7748 section 6.1 and the iterated test
    CHECK_EQ(H(x25519::publicKey(unhex(KAT_ALICE_PRIV))), std::string(KAT_ALICE_PUB));
    CHECK_EQ(H(x25519::publicKey(unhex(KAT_BOB_PRIV))), std::string(KAT_BOB_PUB));
    CHECK_EQ(H(x25519::sharedSecret(unhex(KAT_ALICE_PRIV), unhex(KAT_BOB_PUB))), std::string(KAT_X25519_SHARED));
    CHECK_EQ(H(x25519::sharedSecret(unhex(KAT_BOB_PRIV), unhex(KAT_ALICE_PUB))), std::string(KAT_X25519_SHARED));
    Bytes k(32, 0), u(32, 0);
    k[0] = u[0] = 9;
    for (int i = 1; i <= 1000; ++i) {
        Bytes next = x25519::sharedSecret(k, u);
        u = k;
        k = next;
        if (i == 1) CHECK_EQ(H(k), std::string(KAT_X25519_ITER1));
    }
    CHECK_EQ(H(k), std::string(KAT_X25519_ITER1000));
    // low-order points must be refused (all-zero point, and the point of order 1 = 1)
    bool threw = false;
    try { x25519::sharedSecret(unhex(KAT_ALICE_PRIV), Bytes(32, 0)); } catch (const CryptoError&) { threw = true; }
    CHECK(threw);

    // AES-256-GCM known answer (GCM spec test case 16 inputs, output from Python)
    Bytes sealed = aead::seal(unhex(KAT_GCM_KEY), unhex(KAT_GCM_IV), unhex(KAT_GCM_AAD), unhex(KAT_GCM_PT));
    CHECK_EQ(H(sealed), std::string(KAT_GCM_OUT));
    auto opened = aead::open(unhex(KAT_GCM_KEY), unhex(KAT_GCM_IV), unhex(KAT_GCM_AAD), sealed);
    CHECK(opened && H(*opened) == KAT_GCM_PT);
    sealed.back() ^= 1;
    CHECK(!aead::open(unhex(KAT_GCM_KEY), unhex(KAT_GCM_IV), unhex(KAT_GCM_AAD), sealed));
    CHECK(!aead::open(unhex(KAT_GCM_KEY), unhex(KAT_GCM_IV), unhex("00"), sealed));
}

// ---- JSON ------------------------------------------------------------------------------------

static void testJson() {
    auto j = parseJson(R"({"a":[1,-2,3.5,true,null],"s":"x\u00e9\ud83d\ude00\n","n":{"k":"v"}})");
    CHECK(j && j->find("a")->array.size() == 5);
    CHECK_EQ(j->find("a")->array[1].integer, std::int64_t(-2));
    CHECK(!j->find("a")->array[2].isInteger);
    CHECK_EQ(j->optString("s"), std::string("x\xc3\xa9\xf0\x9f\x98\x80\n"));
    CHECK_EQ(j->find("n")->optString("k"), std::string("v"));
    for (const char* bad : {"", "{", "{} x", "[1,]", "{\"a\":}", "\"\\ud800\"", "\"a\x01\"", "01", "{\"a\" 1}", "nul"})
        CHECK(!parseJson(bad));
    CHECK(!parseJson(std::string(40, '[') + std::string(40, ']')));  // too deep
    CHECK_EQ(jsonQuote("a\"b\\\n\x01"), std::string("\"a\\\"b\\\\\\n\\u0001\""));
}

// ---- limits and wire format ------------------------------------------------------------------

static void testLimitsAndWire() {
    CHECK(limits::isValidUtf8("h\xc3\xa9") && !limits::isValidUtf8("\xc0\x80") && !limits::isValidUtf8("\xed\xa0\x80") &&
          !limits::isValidUtf8("\xf4\x90\x80\x80") && !limits::isValidUtf8("\xe0\x80\x80") && !limits::isValidUtf8("\xff"));
    CHECK(limits::isBlank("") && limits::isBlank(" \t\xe3\x80\x80") && !limits::isBlank(" a"));
    CHECK(!limits::isDisplayable("a\x01") && !limits::isDisplayable("a\xe2\x80\xae") && limits::isDisplayable("\xe0\xa6\xac"));

    CHECK(wire::encodeText("0123456789", seg));       // exactly 10
    CHECK(!wire::encodeText("01234567890", seg));     // 11 clusters
    CHECK(!wire::encodeText("   ", seg));
    CHECK(!wire::encodeText("a\xe2\x80\xae" "b", seg));
    auto frame = wire::encodeText("hi", seg);
    CHECK(frame && frame->size() == 66 && (*frame)[0] == 1 && (*frame)[1] == 2);
    CHECK_EQ(*wire::decodeText(*frame), std::string("hi"));

    // Receiver does not count clusters: 12 ASCII letters are accepted if they arrive in a valid frame.
    Bytes twelve(66, 0);
    twelve[0] = 1; twelve[1] = 12;
    for (int i = 0; i < 12; ++i) twelve[2 + static_cast<std::size_t>(i)] = 'a';
    CHECK(wire::decodeText(twelve));
    CHECK(!wire::encodeText("aaaaaaaaaaaa", seg));
    // ... but every other rule still holds.
    Bytes bad = twelve; bad[0] = 2; CHECK(!wire::decodeText(bad));
    bad = twelve; bad[1] = 0; CHECK(!wire::decodeText(bad));
    bad = twelve; bad[1] = 65; CHECK(!wire::decodeText(bad));
    bad = twelve; bad[60] = 1; CHECK(!wire::decodeText(bad));                      // non-zero padding
    bad = twelve; bad[2] = 0xff; CHECK(!wire::decodeText(bad));                    // invalid UTF-8
    bad = twelve; bad[2] = 0x07; CHECK(!wire::decodeText(bad));                    // control char
    Bytes spaces(66, 0); spaces[0] = 1; spaces[1] = 3; spaces[2] = spaces[3] = spaces[4] = ' ';
    CHECK(!wire::decodeText(spaces));
    CHECK(!wire::decodeText(ByteView(twelve).first(65)));

    // Bangla: 7 consonant-virama-consonant clusters are 63 bytes; the byte cap binds before the cluster cap
    // when the segmenter counts clusters (here: one cluster per 3 code points).
    limits::Segmenter conj = [](std::string_view s) {
        std::vector<std::size_t> ends;
        std::size_t cps = 0;
        for (std::size_t i = 0; i < s.size(); ++i)
            if (i + 1 == s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80)
                if (++cps % 3 == 0) ends.push_back(i + 1);
        return ends;
    };
    std::string kssa;
    for (int i = 0; i < 8; ++i) kssa += "\xe0\xa6\x95\xe0\xa7\x8d\xe0\xa6\xb7";   // ক্ষ
    CHECK_EQ(limits::graphemeCount(kssa, conj), std::size_t(8));
    CHECK(!limits::fits(kssa, conj));                                   // 72 bytes
    CHECK_EQ(limits::clamp(kssa, conj).size(), std::size_t(63));        // 7 clusters, cut at a boundary
    CHECK_EQ(limits::clamp("0123456789abc", seg), std::string("0123456789"));

    Bytes env(135);
    for (std::size_t i = 0; i < env.size(); ++i) env[i] = static_cast<std::uint8_t>(i * 7 + 3);
    std::string b64 = wire::toBase64(env);
    CHECK_EQ(b64.size(), std::size_t(180));
    CHECK(wire::fromBase64(b64) && *wire::fromBase64(b64) == env);
    CHECK(!wire::fromBase64(b64.substr(1)) && !wire::fromBase64(b64.substr(0, 179) + "=") && !wire::fromBase64(b64.substr(0, 179) + "-"));
}

// ---- pairing code ----------------------------------------------------------------------------

static void testPairing() {
    CHECK(pairing::isValid("abcd-efgh jklm npqr"));
    CHECK(!pairing::isValid("ABCDEFGHJKLMNPQ") && !pairing::isValid("ABCDEFGHJKLMNPQ0") && !pairing::isValid("ABCDEFGHJKLMNPQI"));
    CHECK_EQ(pairing::formatPartial("ab0c-d1ef"), std::string("ABCD-EF"));
    CHECK_EQ(pairing::formatPartial("ABCDEFGHJKLMNPQRSTUV"), std::string("ABCD-EFGH-JKLM-NPQR"));
    CHECK_EQ(pairing::format("abcdefghjklmnpqr"), std::string("ABCD-EFGH-JKLM-NPQR"));
    Bytes all(16);
    for (int i = 0; i < 16; ++i) all[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i + 32 * (i % 8));  // high bits must not matter
    CHECK_EQ(pairing::fromBytes(all), std::string("ABCDEFGHJKLMNPQR"));
    ScriptedRandom rnd;
    rnd.push(all);
    CHECK_EQ(pairing::generate(rnd), std::string("ABCDEFGHJKLMNPQR"));
}

// ---- the independent reference vectors (written from the spec, not from this code) -----------

struct Member { Bytes id, ik, ek, ikp, ekp, commit; };
static Member member(const Json& j) {
    auto get = [&](const char* k) { return unhex(j.optString(k)); };
    return {get("id"), get("ik"), get("ek"), get("ikp"), get("ekp"), get("commit")};
}

static void testReferenceVectors() {
    auto parsed = parseJson(readFile(std::string(TC_VECTORS_DIR) + "/vectors.json"));
    CHECK(parsed.has_value());
    const Json& v = *parsed;

    // 1. pairing derivations (the real 600,000 iterations)
    const Json& p = *v.find("pairing");
    auto d = pairing::derive(p.optString("code"));
    CHECK_EQ(d.room, p.optString("room"));
    CHECK_EQ(d.codeToken, p.optString("codeToken"));
    CHECK_EQ(H(d.codeKey), p.optString("codeKey"));

    // 2. members and commitments
    const Json& hs = *v.find("handshake");
    Member A = member(*hs.find("A")), B = member(*hs.find("B"));
    for (const Member* m : {&A, &B}) {
        CHECK_EQ(H(x25519::publicKey(m->ik)), H(m->ikp));
        CHECK_EQ(H(x25519::publicKey(m->ek)), H(m->ekp));
        CHECK_EQ(H(handshake::commit(m->ikp, m->ekp, m->id)), H(m->commit));
    }

    // 3. handshake from both sides
    LocalKeys ka{KeyPair::fromPrivate(A.ik), KeyPair::fromPrivate(A.ek)};
    LocalKeys kb{KeyPair::fromPrivate(B.ik), KeyPair::fromPrivate(B.ek)};
    auto aa = handshake::agree(ka, H(A.id), B.ikp, B.ekp, H(B.id), d.codeKey);
    auto ab = handshake::agree(kb, H(B.id), A.ikp, A.ekp, H(A.id), d.codeKey);
    for (const auto* a : {&aa, &ab}) {
        CHECK_EQ(H(a->sk), hs.optString("SK"));
        CHECK_EQ(H(a->ad), hs.optString("AD"));
        CHECK_EQ(a->verification, hs.optString("verification"));
    }
    CHECK(aa.initiator && !ab.initiator);
    // a wrong code, or a swapped key, must give a different secret
    Bytes wrong = d.codeKey; wrong[0] ^= 1;
    CHECK(H(handshake::agree(kb, H(B.id), A.ikp, A.ekp, H(A.id), wrong).sk) != hs.optString("SK"));

    // 4. ratchet transcript, byte for byte, with injected randomness
    const Json& rj = *v.find("ratchet");
    const Json& inj = *rj.find("injected");
    std::vector<Bytes> nonces;
    for (const auto& n : inj.find("nonces_in_send_order")->array) nonces.push_back(unhex(n.string));
    auto priv = [&](const char* k) { return unhex(inj.optString(k)); };
    ScriptedRandom srcI, srcR;
    for (Bytes b : {nonces[0], priv("I_ratchet2_priv"), nonces[2], nonces[3], priv("I_ratchet4_priv")}) srcI.push(b);
    for (Bytes b : {priv("R_first_ratchet_priv"), nonces[1], priv("R_ratchet3_priv"), nonces[4]}) srcR.push(b);

    Ratchet ri = Ratchet::forInitiator(aa.sk, aa.ad, ka.ek, B.ekp, srcI);
    Ratchet rr = Ratchet::forResponder(ab.sk, ab.ad, kb.ek, A.ekp, srcR);

    const auto& steps = rj.find("steps")->array;
    auto step = [&](std::size_t k) { return unhex(steps[k].optString("envelope_hex")); };
    auto text = [&](std::size_t k) { return steps[k].optString("text"); };
    auto send = [&](Ratchet& r, std::size_t k) {
        Bytes e = r.encrypt(*wire::encodeText(text(k), seg));
        CHECK_EQ(H(e), H(step(k)));
        return e;
    };
    auto recv = [&](Ratchet& r, const Bytes& e, std::size_t k) {
        auto f = r.decrypt(e);
        CHECK(f.has_value());
        if (f) CHECK_EQ(*wire::decodeText(*f), text(k));
    };
    Bytes e1 = send(ri, 0); recv(rr, e1, 0);
    Bytes e2 = send(rr, 1); recv(ri, e2, 1);
    Bytes e3 = send(ri, 2);
    Bytes e4 = send(ri, 3);
    recv(rr, e4, 3);   // out of order: n=1 first, a DH step on R, n=0 skipped
    recv(rr, e3, 2);   // served from the skipped-key store
    Bytes e5 = send(rr, 4); recv(ri, e5, 4);
    CHECK(srcI.drained() && srcR.drained());

    Bytes before = rr.serialize();
    CHECK(!rr.decrypt(e3));                       // replay
    CHECK(rr.serialize() == before);              // ... and it changed nothing
}

// ---- ratchet behaviour beyond the vectors ----------------------------------------------------

struct Pair {
    Ratchet i, r;
};
static Pair freshPair(RandomSource& rng) {
    LocalKeys ki{KeyPair::generate(rng), KeyPair::generate(rng)}, kr{KeyPair::generate(rng), KeyPair::generate(rng)};
    Bytes sk = randomBytes(rng, 32), ad = randomBytes(rng, 64);
    return {Ratchet::forInitiator(sk, ad, ki.ek, kr.ek.pub, rng), Ratchet::forResponder(sk, ad, kr.ek, ki.ek.pub, rng)};
}
static Bytes frameOf(const char* s) { return *wire::encodeText(s, seg); }

static void testRatchetBehaviour() {
    RandomSource& rng = systemRandom();
    {   // either side can send first
        Pair p = freshPair(rng);
        auto f = p.i.decrypt(p.r.encrypt(frameOf("first")));
        CHECK(f && *wire::decodeText(*f) == "first");
        auto g = p.r.decrypt(p.i.encrypt(frameOf("reply")));
        CHECK(g && *wire::decodeText(*g) == "reply");
    }
    {   // tampering with ANY byte is rejected and changes no state
        Pair p = freshPair(rng);
        Bytes env = p.i.encrypt(frameOf("secret"));
        Bytes before = p.r.serialize();
        int rejected = 0;
        for (std::size_t i = 0; i < env.size(); ++i) {
            Bytes t = env;
            t[i] ^= 0x01;
            if (!p.r.decrypt(t)) ++rejected;
            CHECK(p.r.serialize() == before);
        }
        CHECK_EQ(rejected, 135);
        CHECK(p.r.decrypt(env).has_value());    // the genuine one still works afterwards
        CHECK(!p.r.decrypt(env));               // and only once
    }
    {   // skip limit: a gap of exactly MaxSkip is fine, one more is refused
        Pair p = freshPair(rng);
        std::vector<Bytes> envs;
        for (int k = 0; k < 202; ++k) envs.push_back(p.i.encrypt(frameOf("x")));
        Bytes before = p.r.serialize();
        CHECK(!p.r.decrypt(envs[201]));         // gap 201
        CHECK(p.r.serialize() == before);
        CHECK(p.r.decrypt(envs[200]).has_value());   // gap 200
        CHECK(p.r.decrypt(envs[0]).has_value());     // skipped key still usable
        CHECK(!p.r.decrypt(envs[0]));
    }
    {   // state survives a save and restore
        Pair p = freshPair(rng);
        auto a = p.r.decrypt(p.i.encrypt(frameOf("one")));
        CHECK(a.has_value());
        Bytes saved = p.r.serialize();
        Ratchet restored = Ratchet::deserialize(saved, rng);
        CHECK(restored.serialize() == saved);
        auto b = restored.decrypt(p.i.encrypt(frameOf("two")));
        CHECK(b && *wire::decodeText(*b) == "two");
        auto c = p.i.decrypt(restored.encrypt(frameOf("three")));
        CHECK(c && *wire::decodeText(*c) == "three");
        for (std::size_t cut : {std::size_t(0), std::size_t(1), saved.size() / 2, saved.size() - 1}) {
            bool threw = false;
            try { Ratchet::deserialize(ByteView(saved).first(cut), rng); } catch (const StateError&) { threw = true; }
            CHECK(threw);
        }
        Bytes extra = saved; extra.push_back(0);
        bool threw = false;
        try { Ratchet::deserialize(extra, rng); } catch (const StateError&) { threw = true; }
        CHECK(threw);
    }
    {   // a long ping-pong with reordering inside each batch
        Pair p = freshPair(rng);
        for (int round = 0; round < 20; ++round) {
            std::vector<Bytes> batch;
            for (int k = 0; k < 3; ++k) batch.push_back(p.i.encrypt(frameOf("ping")));
            std::reverse(batch.begin(), batch.end());
            for (auto& e : batch) CHECK(p.r.decrypt(e).has_value());
            CHECK(p.i.decrypt(p.r.encrypt(frameOf("pong"))).has_value());
        }
    }
}

int main() {
    RUN(testPrimitives);
    RUN(testJson);
    RUN(testLimitsAndWire);
    RUN(testPairing);
    RUN(testReferenceVectors);
    RUN(testRatchetBehaviour);
    return FINISH();
}
