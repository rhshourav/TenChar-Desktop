#pragma once
// One buffer type for everything: a byte vector that overwrites its memory
// (with a call the compiler cannot remove) whenever it is freed. Keys, chain
// keys and plaintext all live in it, so nothing secret is released unwiped.
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tc {

void secureZero(void* p, std::size_t n) noexcept;

template <class T>
struct ZeroingAllocator {
    using value_type = T;
    ZeroingAllocator() = default;
    template <class U>
    ZeroingAllocator(const ZeroingAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void deallocate(T* p, std::size_t n) noexcept {
        secureZero(p, n * sizeof(T));
        std::allocator<T>{}.deallocate(p, n);
    }
    template <class U>
    bool operator==(const ZeroingAllocator<U>&) const noexcept { return true; }
};

using Bytes = std::vector<std::uint8_t, ZeroingAllocator<std::uint8_t>>;
using ByteView = std::span<const std::uint8_t>;

struct CryptoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Bytes concat(std::initializer_list<ByteView> parts);
ByteView asBytes(std::string_view s) noexcept;
Bytes toBytes(std::string_view s);
std::string toString(ByteView b);
/// Constant-time equality (false for different lengths).
bool constantTimeEquals(ByteView a, ByteView b) noexcept;
void wipe(Bytes& b) noexcept;

std::string hexEncode(ByteView b);
/// Strict hex (either case, even length). Empty optional on any bad digit.
std::optional<Bytes> hexDecode(std::string_view hex);

}  // namespace tc
