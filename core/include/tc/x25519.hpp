#pragma once
// X25519 (RFC 7748) on OpenSSL 3. Refuses an all-zero shared secret.
#include "tc/bytes.hpp"
#include "tc/random.hpp"

namespace tc::x25519 {

inline constexpr std::size_t KeyBytes = 32;

Bytes generatePrivate(RandomSource& rng);
Bytes publicKey(ByteView priv);
/// Throws CryptoError for a low-order peer key (an attack, never an honest peer).
Bytes sharedSecret(ByteView priv, ByteView peerPublic);

}  // namespace tc::x25519
