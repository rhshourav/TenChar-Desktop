#include "tc/random.hpp"

#include <openssl/rand.h>

namespace tc {
namespace {
struct SystemRandom final : RandomSource {
    void fill(std::span<std::uint8_t> out) override {
        if (out.empty()) return;
        if (RAND_bytes(out.data(), static_cast<int>(out.size())) != 1) throw CryptoError("random source failed");
    }
};
}  // namespace

RandomSource& systemRandom() {
    static SystemRandom instance;
    return instance;
}

Bytes randomBytes(RandomSource& rng, std::size_t n) {
    Bytes out(n);
    rng.fill(out);
    return out;
}
}  // namespace tc
