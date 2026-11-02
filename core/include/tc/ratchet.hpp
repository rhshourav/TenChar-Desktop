#pragma once
// The Double Ratchet (Signal specification, revision 1) with X25519,
// HKDF-SHA256, HMAC-SHA256 chain keys and AES-256-GCM. A port of Ratchet.kt.
//
// Either side can send first: the initiator uses its pairing ephemeral key as
// its first ratchet key and the responder's pairing ephemeral key as the
// remote key; the responder derives the initiator's first receiving chain at
// once and then makes its own first sending chain with a fresh ratchet key.
//
// Decrypt works on a copy and commits only on success, so a forged, damaged,
// replayed or too-far-ahead envelope changes no state.
#include <optional>
#include <stdexcept>
#include <vector>

#include "tc/bytes.hpp"
#include "tc/handshake.hpp"
#include "tc/random.hpp"

namespace tc {

struct StateError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Ratchet {
public:
    static Ratchet forInitiator(ByteView sk, ByteView ad, const KeyPair& ek, ByteView peerEk, RandomSource& rng);
    static Ratchet forResponder(ByteView sk, ByteView ad, const KeyPair& ek, ByteView peerEk, RandomSource& rng);
    /// Throws StateError for anything malformed.
    static Ratchet deserialize(ByteView bytes, RandomSource& rng);

    /// Encrypts one 66-byte frame and advances the sending chain. Persist the state BEFORE sending the result.
    Bytes encrypt(ByteView frame);
    /// The frame inside a 135-byte envelope, or nullopt if it is not a valid next, late or out-of-order
    /// message of this conversation. A nullopt result leaves the state exactly as it was.
    std::optional<Bytes> decrypt(ByteView envelope);

    Bytes serialize() const;
    void wipe();

private:
    struct Skipped {
        Bytes dh;
        std::uint32_t n;
        Bytes mk;
    };
    Ratchet() = default;

    std::optional<Bytes> decryptInner(ByteView envelope);
    void skipUntil(std::uint32_t until);
    void dhRatchet(ByteView peerPublic);

    Bytes rk_, dhsPriv_, dhsPub_;
    std::optional<Bytes> dhr_, cks_, ckr_;
    std::uint32_t ns_ = 0, nr_ = 0, pn_ = 0;
    std::vector<Skipped> skipped_;  // oldest first
    Bytes ad_;
    RandomSource* rng_ = nullptr;
};

}  // namespace tc
