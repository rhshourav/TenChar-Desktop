#pragma once
// Every random draw in the core goes through a RandomSource, so tests can feed
// fixed values and compare byte for byte with the independent vectors.
#include "tc/bytes.hpp"

namespace tc {

struct RandomSource {
    virtual ~RandomSource() = default;
    virtual void fill(std::span<std::uint8_t> out) = 0;
};

RandomSource& systemRandom();
Bytes randomBytes(RandomSource& rng, std::size_t n);

}  // namespace tc
