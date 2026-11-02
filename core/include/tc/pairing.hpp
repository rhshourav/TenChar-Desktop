#pragma once
// The pairing code is a one-time rendezvous secret, not an encryption key: it
// lets two devices find the same relay room and authenticate to it until both
// have joined. room and codeToken go to the relay; codeKey never leaves the device.
#include <string>
#include <string_view>

#include "tc/bytes.hpp"
#include "tc/random.hpp"

namespace tc::pairing {

struct Derived {
    std::string room;       // 32 hex chars
    std::string codeToken;  // 48 hex chars
    Bytes codeKey;          // 32 bytes, never sent
};

/// 16 symbols, no separators. One random byte's low 5 bits per symbol (no modulo bias).
std::string generate(RandomSource& rng);
std::string fromBytes(ByteView sixteenBytes);

/// Upper-cases ASCII letters and drops hyphens and white space. Does not check validity.
std::string normalise(std::string_view input);
bool isValid(std::string_view input);
/// What has been typed so far as XXXX-XXXX-..., dropping anything that can never be part of a code.
std::string formatPartial(std::string_view input);
/// XXXX-XXXX-XXXX-XXXX. Throws std::invalid_argument for an invalid code.
std::string format(std::string_view input);

/// Three PBKDF2 runs in parallel (slow on purpose, ~0.3 s each). `iterations` exists for tests only.
Derived derive(std::string_view input, int iterations = 600'000);
std::string hexPrefix(std::string_view normalisedCode, std::string_view salt, std::size_t hexChars, int iterations);

}  // namespace tc::pairing
