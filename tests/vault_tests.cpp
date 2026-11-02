#include <filesystem>

#include "tc/state.hpp"
#include "tc/vault.hpp"
#include "tc/wire.hpp"
#include "testing.hpp"

using namespace tc;
namespace fs = std::filesystem;

static Conversation sample(RandomSource& rng) {
    LocalKeys ki{KeyPair::generate(rng), KeyPair::generate(rng)}, kr{KeyPair::generate(rng), KeyPair::generate(rng)};
    Bytes sk = randomBytes(rng, 32), ad = randomBytes(rng, 64);
    Conversation c;
    c.room = hexEncode(randomBytes(rng, 16));
    c.memberId = hexEncode(randomBytes(rng, 16));
    c.memberToken = randomBytes(rng, 32);
    c.endpoints = {"https://tenchar-relay.onrender.com", "https://tenchar-relay.tenchar.workers.dev"};
    c.ratchet = Ratchet::forInitiator(sk, ad, ki.ek, kr.ek.pub, rng);
    c.verification = "4839 2011";
    c.verified = true;
    c.lastIds = {{c.endpoints[0], 41}, {c.endpoints[1], 7}};
    c.outbox.push_back({3, randomBytes(rng, 135)});
    c.messages = {{1, "hi", true, Conversation::DeliverySent},
                  {2, "\xf0\x9f\x98\x80", false, Conversation::DeliveryNone},                              // emoji: 4 bytes
                  {3, "\xe0\xa6\xac\xe0\xa6\xbe\xe0\xa6\x82\xe0\xa6\xb2\xe0\xa6\xbe", true, Conversation::DeliveryQueued}};  // Bangla
    c.nextMsgId = 4;
    return c;
}

static void testStateCodec() {
    RandomSource& rng = systemRandom();
    // strings are standard UTF-8 with a u16 length, never Java's modified UTF-8
    Bytes s;
    state::writeString(s, "\xf0\x9f\x98\x80", 64);
    CHECK_EQ(H(s), std::string("0004f09f9880"));
    Bytes nul;
    state::writeString(nul, std::string_view("\0", 1), 64);
    CHECK_EQ(H(nul), std::string("000100"));  // Java would write 0002c080
    bool threw = false;
    try { state::writeString(s, std::string(65, 'a'), 64); } catch (const state::FormatError&) { threw = true; }
    CHECK(threw);

    Conversation c = sample(rng);
    Bytes enc = state::encode(&c);
    auto back = state::decode(enc, rng);
    CHECK(back.has_value());
    CHECK_EQ(back->room, c.room);
    CHECK(back->memberToken == c.memberToken);
    CHECK_EQ(back->messages.size(), std::size_t(3));
    CHECK_EQ(back->messages[1].text, std::string("\xf0\x9f\x98\x80"));
    CHECK_EQ(back->messages[2].text, c.messages[2].text);
    CHECK_EQ(back->lastId(c.endpoints[0]), std::int64_t(41));
    CHECK(back->outbox.size() == 1 && back->outbox[0].envelope == c.outbox[0].envelope);
    CHECK(back->ratchet->serialize() == c.ratchet->serialize());
    CHECK(state::encode(&*back) == enc);          // byte-stable round trip

    Bytes none = state::encode(nullptr);
    CHECK(!state::decode(none, rng).has_value());

    auto refused = [&](ByteView data) {
        try { state::decode(data, rng); return false; } catch (const state::FormatError&) { return true; }
    };
    int cleanly = 0;
    for (std::size_t cut = 0; cut < enc.size(); ++cut) cleanly += refused(ByteView(enc).first(cut));
    CHECK_EQ(cleanly, static_cast<int>(enc.size()));   // every truncation is a clean refusal
    Bytes trailing = enc; trailing.push_back(0);
    CHECK(refused(trailing));
    Bytes badVersion = enc; badVersion[4] = 1;
    CHECK(refused(badVersion));
    Bytes badMagic = enc; badMagic[0] ^= 1;
    CHECK(refused(badMagic));
    Bytes badUtf8 = enc;   // corrupt the first message's text bytes: find "hi" after the endpoint strings
    for (std::size_t i = 0; i + 1 < badUtf8.size(); ++i)
        if (badUtf8[i] == 'h' && badUtf8[i + 1] == 'i' && i > 100) { badUtf8[i] = 0xff; break; }
    CHECK(refused(badUtf8));
    // flipping any single byte must either decode to something valid or be refused: never crash
    for (std::size_t i = 0; i < enc.size(); ++i) {
        Bytes t = enc; t[i] ^= 0x80;
        try { state::decode(t, rng); } catch (const state::FormatError&) {}
    }
    c.wipe();
}

static void testVault() {
    RandomSource& rng = systemRandom();
    fs::path dir = fs::temp_directory_path() / ("tc-vault-test-" + hexEncode(randomBytes(rng, 6)));
    Vault v(dir, 2000);
    CHECK(!v.exists());
    Bytes dek = v.create("correct horse battery staple", rng);
    CHECK(v.exists() && dek.size() == 32);

    CHECK(v.unlock("correct horse battery staple").result == Vault::Unlock::Ok);
    CHECK(v.unlock("correct horse battery stapl").result == Vault::Unlock::WrongPassphrase);
    CHECK(v.unlock("").result == Vault::Unlock::WrongPassphrase);
    auto ok = v.unlock("correct horse battery staple");
    CHECK(ok.dek == dek);

    CHECK(!v.load(dek).has_value());                  // nothing saved yet
    Bytes plain = toBytes("the whole conversation");
    v.save(dek, plain, rng);
    auto first = readWholeFile(dir / "tc_state.bin", 1 << 20);
    v.save(dek, plain, rng);
    auto second = readWholeFile(dir / "tc_state.bin", 1 << 20);
    CHECK(first && second && *first != *second);      // a fresh nonce on every write
    CHECK(*v.load(dek) == plain);
    CHECK(!fs::exists(dir / "tc_state.bin.tmp"));     // the temporary file never lingers

    // a wrong data key, a flipped byte, a truncated file, and a swapped-in file all refuse cleanly
    Bytes other = randomBytes(rng, 32);
    bool threw = false;
    try { v.load(other); } catch (const KeysLost&) { threw = true; }
    CHECK(threw);
    for (std::size_t cut : {std::size_t(0), std::size_t(5), second->size() - 1}) {
        writeFileAtomically(dir / "tc_state.bin", ByteView(*second).first(cut));
        threw = false;
        try { v.load(dek); } catch (const KeysLost&) { threw = true; }
        CHECK(threw);
    }
    Bytes flipped = *second; flipped[20] ^= 1;
    writeFileAtomically(dir / "tc_state.bin", flipped);
    threw = false;
    try { v.load(dek); } catch (const KeysLost&) { threw = true; }
    CHECK(threw);
    writeFileAtomically(dir / "tc_state.bin", *second);
    CHECK(*v.load(dek) == plain);

    // changing the passphrase re-wraps the key; the state file is untouched
    CHECK(!v.changePassphrase("wrong wrong wrong", "new new new new", rng));
    CHECK(v.changePassphrase("correct horse battery staple", "another long passphrase", rng));
    CHECK(v.unlock("correct horse battery staple").result == Vault::Unlock::WrongPassphrase);
    auto u = v.unlock("another long passphrase");
    CHECK(u.result == Vault::Unlock::Ok && u.dek == dek && *v.load(u.dek) == plain);

    // a damaged wrapped-key file
    auto dekFile = readWholeFile(dir / "tc_dek.bin", 4096);
    Bytes bad = *dekFile; bad[40] ^= 1;               // inside the authenticated header
    writeFileAtomically(dir / "tc_dek.bin", bad);
    CHECK(v.unlock("another long passphrase").result != Vault::Unlock::Ok);
    writeFileAtomically(dir / "tc_dek.bin", ByteView(*dekFile).first(50));
    CHECK(v.unlock("another long passphrase").result == Vault::Unlock::Lost);
    Bytes huge = *dekFile; huge[33] = 0x7f;           // absurd iteration count: refused before any work
    writeFileAtomically(dir / "tc_dek.bin", huge);
    CHECK(v.unlock("another long passphrase").result == Vault::Unlock::Lost);

#ifndef _WIN32
    CHECK((fs::status(dir / "tc_state.bin").permissions() & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
    CHECK((fs::status(dir).permissions() & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    v.wipe();
    CHECK(!v.exists() && !fs::exists(dir / "tc_state.bin"));
    fs::remove_all(dir);
}

int main() {
    RUN(testStateCodec);
    RUN(testVault);
    return FINISH();
}
