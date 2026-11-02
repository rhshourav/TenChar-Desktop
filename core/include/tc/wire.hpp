#pragma once
// The two fixed-size layouts that cross the wire.
//   Frame (encrypted part, always 66 bytes): [type 1][len 1][payload 64, zero padded]
//   Envelope (what the relay stores, 135 bytes = 180 base64 chars):
//     header 41: [version 1][ratchet public key 32][previous chain length 4 BE][n 4 BE]
//     [nonce 12][AES-GCM ciphertext of the frame 66][tag 16]
#include <optional>
#include <string>
#include <string_view>

#include "tc/bytes.hpp"
#include "tc/limits.hpp"

namespace tc::wire {

inline constexpr std::uint8_t Version = 1;
inline constexpr std::uint8_t TypeText = 1;

/// Sender rule: not blank, <= 10 clusters, <= 64 bytes, displayable. Empty optional otherwise.
std::optional<Bytes> encodeText(std::string_view text, const limits::Segmenter& seg);

/// Receiver rule. The frame must be exactly what a sender would produce, except that the
/// 10-cluster limit is NOT checked here: platforms count Bangla clusters differently, and
/// a message that already decrypted (and advanced the ratchet) must never be dropped over a
/// counting difference. The 64-byte frame already bounds the length.
std::optional<std::string> decodeText(ByteView frame);

std::string toBase64(ByteView envelope);  // throws CryptoError unless exactly EnvelopeBytes
std::optional<Bytes> fromBase64(std::string_view text);

}  // namespace tc::wire
