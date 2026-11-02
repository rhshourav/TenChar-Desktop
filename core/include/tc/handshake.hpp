#pragma once
// Key agreement between two devices that know each other's identity and
// ephemeral public keys (the relay carried them, commit-then-reveal). X3DH
// shape with four DH values and no signed prekey. The pairing code is mixed in
// as the HKDF salt, so a relay that swaps keys cannot compute the result; the
// 8-digit number still catches a swap if the code ever leaked.
#include <string>

#include "tc/bytes.hpp"
#include "tc/random.hpp"

namespace tc {

struct KeyPair {
    Bytes priv, pub;
    static KeyPair generate(RandomSource& rng);
    static KeyPair fromPrivate(ByteView priv);
};

struct LocalKeys {
    KeyPair ik, ek;
};

/// Everything one device presents to the relay while pairing (hex strings are lower case).
struct LocalMember {
    std::string memberId;     // 32 hex
    std::string memberToken;  // 64 hex, sent only as a bearer token
    std::string tokenHash;    // SHA-256(memberToken bytes), 64 hex
    LocalKeys keys;
    std::string commit;       // 64 hex
    /// Draw order: id 16, token 32, ik private 32, ek private 32.
    static LocalMember create(RandomSource& rng);
};

namespace handshake {

struct Agreement {
    Bytes sk;  // 32-byte root secret for the ratchet
    Bytes ad;  // ikI || ikR, bound into every message
    bool initiator = false;
    std::string verification;  // "dddd dddd"
};

/// SHA-256("tenchar-commit-v1" || ik || ek || memberId)
Bytes commit(ByteView ik, ByteView ek, ByteView memberId);
/// The device with the smaller member id (lower-case hex, byte-wise) is the initiator.
bool isInitiator(const std::string& localMemberId, const std::string& peerMemberId);
/// Throws CryptoError on a bad key (including a low-order peer key) or equal member ids.
Agreement agree(const LocalKeys& local, const std::string& localMemberId, ByteView peerIk, ByteView peerEk,
                const std::string& peerMemberId, ByteView codeKey);

}  // namespace handshake
}  // namespace tc
