#include "tc/bytes.hpp"

#include <openssl/crypto.h>

namespace tc {

void secureZero(void* p, std::size_t n) noexcept {
    if (p != nullptr && n != 0) OPENSSL_cleanse(p, n);
}

Bytes concat(std::initializer_list<ByteView> parts) {
    std::size_t n = 0;
    for (auto p : parts) n += p.size();
    Bytes out;
    out.reserve(n);
    for (auto p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

ByteView asBytes(std::string_view s) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}
Bytes toBytes(std::string_view s) {
    auto v = asBytes(s);
    return Bytes(v.begin(), v.end());
}
std::string toString(ByteView b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

bool constantTimeEquals(ByteView a, ByteView b) noexcept {
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

void wipe(Bytes& b) noexcept { secureZero(b.data(), b.size()); }

std::string hexEncode(ByteView b) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(b.size() * 2, '0');
    for (std::size_t i = 0; i < b.size(); ++i) {
        out[i * 2] = digits[b[i] >> 4];
        out[i * 2 + 1] = digits[b[i] & 0x0F];
    }
    return out;
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;  // ASCII only
}

std::optional<Bytes> hexDecode(std::string_view hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    Bytes out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        int hi = hexDigit(hex[i * 2]), lo = hexDigit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

}  // namespace tc
