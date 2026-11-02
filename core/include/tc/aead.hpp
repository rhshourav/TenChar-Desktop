#pragma once
// AES-256-GCM, 96-bit nonce, 128-bit tag. Every key used with this on the
// messaging path is a one-time message key, so a random nonce is safe.
#include "tc/bytes.hpp"

namespace tc::aead {

/// Ciphertext followed by the 16-byte tag.
Bytes seal(ByteView key, ByteView nonce, ByteView aad, ByteView plaintext);
/// Plaintext, or nullopt if the tag does not verify.
std::optional<Bytes> open(ByteView key, ByteView nonce, ByteView aad, ByteView sealed);

}  // namespace tc::aead
