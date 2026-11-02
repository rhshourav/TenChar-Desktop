// A deterministic mutation fuzzer for every parser of untrusted bytes: the relay's JSON, the relay-client response
// parsing, the 180-character envelope, the decrypted frame, the ratchet's decrypt, and the state file. It runs
// under the address and undefined-behaviour sanitisers in CI. Usage: fuzz_tests [iterations]
#include <random>

#include "fake_relay.hpp"
#include "tc/pairing.hpp"
#include "tc/ratchet.hpp"
#include "tc/state.hpp"
#include "tc/wire.hpp"

using namespace tc;

static std::mt19937_64 rng(0xC0FFEE);
static Bytes mutate(Bytes b, int maxEdits = 6) {
    int edits = 1 + static_cast<int>(rng() % maxEdits);
    for (int i = 0; i < edits; ++i) {
        switch (rng() % 5) {
            case 0: if (!b.empty()) b[rng() % b.size()] ^= static_cast<std::uint8_t>(1u << (rng() % 8)); break;
            case 1: if (!b.empty()) b[rng() % b.size()] = static_cast<std::uint8_t>(rng()); break;
            case 2: if (!b.empty()) b.erase(b.begin() + static_cast<std::ptrdiff_t>(rng() % b.size())); break;
            case 3: b.insert(b.begin() + static_cast<std::ptrdiff_t>(rng() % (b.size() + 1)), static_cast<std::uint8_t>(rng())); break;
            case 4: if (!b.empty()) b.resize(rng() % b.size()); break;
        }
    }
    return b;
}
static std::string asString(const Bytes& b) { return std::string(b.begin(), b.end()); }

/// A transport that answers every request with a fixed (mutated) body, to fuzz RelayClient's response parsing.
struct CannedTransport final : HttpTransport {
    std::string body;
    int status = 200;
    HttpResponse send(const HttpRequest&) override { return {true, status, body}; }
};

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 20000;
    RandomSource& sys = systemRandom();
    int parsed = 0, accepted = 0;

    // JSON and relay responses
    const std::vector<std::string> seeds = {
        R"({"members":[{"memberId":")" + std::string(32, 'a') + R"(","commit":")" + std::string(64, 'b') + R"(","ik":null,"ek":null}],"sealed":false})",
        R"({"messages":[{"id":1,"env":")" + std::string(180, 'A') + R"("}],"more":true})", R"({"members":2})", R"({"sealed":true})", "{}"};
    CannedTransport canned;
    RelayClient client("https://fuzz.test", canned);
    const std::string room(32, 'c'), token(64, 'd');
    for (int i = 0; i < iterations; ++i) {
        canned.body = asString(mutate(toBytes(seeds[rng() % seeds.size()])));
        canned.status = (rng() % 4 == 0) ? 500 : 200;
        if (parseJson(canned.body)) ++parsed;
        (void)client.hello(room, "t", std::string(32, 'a'), std::string(64, 'b'), std::string(64, 'c'));
        (void)client.state(room, token);
        (void)client.fetch(room, token, 0);
        (void)client.reveal(room, token, std::string(32, 'a'), std::string(64, 'b'), std::string(64, 'c'));
    }

    // base64 envelopes and frames
    Bytes env(135);
    for (int i = 0; i < iterations; ++i) {
        std::string text = wire::toBase64(env);
        Bytes m = mutate(toBytes(text));
        if (auto d = wire::fromBase64(asString(m))) ++accepted;
        Bytes frame = mutate(Bytes(66, 0), 8);
        if (!frame.empty()) frame[0] = (rng() % 3) ? 1 : static_cast<std::uint8_t>(rng());
        (void)wire::decodeText(frame);
        (void)limits::decodeUtf8(asString(frame));
    }

    // ratchet decrypt of mutated genuine envelopes: must never crash and never change state on failure
    LocalKeys ki{KeyPair::generate(sys), KeyPair::generate(sys)}, kr{KeyPair::generate(sys), KeyPair::generate(sys)};
    Bytes sk = randomBytes(sys, 32), ad = randomBytes(sys, 64);
    Ratchet a = Ratchet::forInitiator(sk, ad, ki.ek, kr.ek.pub, sys), b = Ratchet::forResponder(sk, ad, kr.ek, ki.ek.pub, sys);
    Bytes good = a.encrypt(*wire::encodeText("hi", limits::codepointClusters));
    Bytes before = b.serialize();
    int forged = 0;
    for (int i = 0; i < iterations / 4; ++i) {
        Bytes m = mutate(good, 3);
        if (m == good) continue;
        if (b.decrypt(m)) ++forged;
        if (b.serialize() != before) { std::fprintf(stderr, "STATE CHANGED by a rejected envelope\n"); return 1; }
    }
    if (forged) { std::fprintf(stderr, "a mutated envelope was accepted (%d)\n", forged); return 1; }
    if (!b.decrypt(good)) { std::fprintf(stderr, "genuine envelope rejected after fuzzing\n"); return 1; }

    // state file
    Conversation c;
    c.room = hexEncode(randomBytes(sys, 16)); c.memberId = hexEncode(randomBytes(sys, 16)); c.memberToken = randomBytes(sys, 32);
    c.endpoints = {"https://a.example.com"}; c.ratchet = Ratchet::forInitiator(sk, ad, ki.ek, kr.ek.pub, sys);
    c.verification = "1234 5678"; c.outbox.push_back({1, randomBytes(sys, 135)});
    c.messages = {{1, "hi", true, 1}, {2, "\xf0\x9f\x98\x80", false, 0}};
    Bytes enc = state::encode(&c);
    int decoded = 0;
    for (int i = 0; i < iterations / 4; ++i) {
        try { if (state::decode(mutate(enc, 4), sys)) ++decoded; } catch (const state::FormatError&) {}
    }
    std::fprintf(stderr, "fuzz: %d iterations per parser; json parsed %d, base64 accepted %d, state decoded %d (all without a crash)\n",
                 iterations, parsed, accepted, decoded);
    return 0;
}
