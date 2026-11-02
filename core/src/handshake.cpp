#include "tc/handshake.hpp"

#include "tc/constants.hpp"
#include "tc/kdf.hpp"
#include "tc/x25519.hpp"

namespace tc {

KeyPair KeyPair::generate(RandomSource& rng) {
    Bytes priv = x25519::generatePrivate(rng);
    Bytes pub = x25519::publicKey(priv);
    return {std::move(priv), std::move(pub)};
}
KeyPair KeyPair::fromPrivate(ByteView priv) { return {Bytes(priv.begin(), priv.end()), x25519::publicKey(priv)}; }

LocalMember LocalMember::create(RandomSource& rng) {
    Bytes id = randomBytes(rng, 16);
    Bytes token = randomBytes(rng, 32);
    LocalKeys keys{KeyPair::generate(rng), KeyPair::generate(rng)};
    Bytes c = handshake::commit(keys.ik.pub, keys.ek.pub, id);
    return {hexEncode(id), hexEncode(token), hexEncode(kdf::sha256({token})), std::move(keys), hexEncode(c)};
}

namespace handshake {

Bytes commit(ByteView ik, ByteView ek, ByteView memberId) {
    return kdf::sha256({asBytes("tenchar-commit-v1"), ik, ek, memberId});
}

bool isInitiator(const std::string& local, const std::string& peer) {
    if (local == peer) throw CryptoError("member ids must differ");
    return local < peer;
}

Agreement agree(const LocalKeys& local, const std::string& localId, ByteView peerIk, ByteView peerEk,
                const std::string& peerId, ByteView codeKey) {
    if (peerIk.size() != 32 || peerEk.size() != 32 || codeKey.size() != static_cast<std::size_t>(Pbkdf2DkBytes))
        throw CryptoError("bad handshake input");
    const bool initiator = isInitiator(localId, peerId);

    Bytes dh1, dh2, dh3, dh4;
    if (initiator) {
        dh1 = x25519::sharedSecret(local.ik.priv, peerEk);
        dh2 = x25519::sharedSecret(local.ek.priv, peerIk);
        dh3 = x25519::sharedSecret(local.ek.priv, peerEk);
        dh4 = x25519::sharedSecret(local.ik.priv, peerIk);
    } else {
        dh1 = x25519::sharedSecret(local.ek.priv, peerIk);
        dh2 = x25519::sharedSecret(local.ik.priv, peerEk);
        dh3 = x25519::sharedSecret(local.ek.priv, peerEk);
        dh4 = x25519::sharedSecret(local.ik.priv, peerIk);
    }
    ByteView ikI = initiator ? ByteView(local.ik.pub) : peerIk;
    ByteView ekI = initiator ? ByteView(local.ek.pub) : peerEk;
    ByteView ikR = initiator ? peerIk : ByteView(local.ik.pub);
    ByteView ekR = initiator ? peerEk : ByteView(local.ek.pub);
    auto idI = hexDecode(initiator ? localId : peerId);
    auto idR = hexDecode(initiator ? peerId : localId);
    if (!idI || !idR) throw CryptoError("bad member id");

    Bytes ff(32, 0xFF);
    Bytes ikm = concat({ff, dh1, dh2, dh3, dh4});
    Bytes info = concat({asBytes("tenchar-x3dh-v1"), ikI, ekI, ikR, ekR, *idI, *idR});
    Bytes sk = kdf::hkdf(codeKey, ikm, info, 32);

    Bytes verifyInfo = concat({asBytes("tenchar-verify-v1"), info});
    Bytes digits = kdf::hkdf({}, sk, verifyInfo, 7);
    std::uint64_t v = 0;
    for (auto b : digits) v = (v << 8) | b;
    std::string text = std::to_string(v % 100'000'000ULL);
    text.insert(0, 8 - text.size(), '0');

    Agreement a;
    a.sk = std::move(sk);
    a.ad = concat({ikI, ikR});
    a.initiator = initiator;
    a.verification = text.substr(0, 4) + " " + text.substr(4);
    return a;
}

}  // namespace handshake
}  // namespace tc
