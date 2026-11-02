#include "tc/kdf.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <memory>

namespace tc::kdf {

Bytes sha256(std::initializer_list<ByteView> parts) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) throw CryptoError("sha256 init");
    for (auto p : parts)
        if (!p.empty() && EVP_DigestUpdate(ctx.get(), p.data(), p.size()) != 1) throw CryptoError("sha256 update");
    Bytes out(32);
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx.get(), out.data(), &len) != 1 || len != 32) throw CryptoError("sha256 final");
    return out;
}

Bytes hmac(ByteView key, std::initializer_list<ByteView> parts) {
    if (key.empty()) throw CryptoError("empty HMAC key");
    Bytes msg = concat(parts);
    Bytes out(32);
    unsigned int len = 0;
    static const std::uint8_t none = 0;
    const std::uint8_t* data = msg.empty() ? &none : msg.data();
    if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), data, msg.size(), out.data(), &len) || len != 32)
        throw CryptoError("hmac failed");
    return out;
}

Bytes hkdf(ByteView salt, ByteView ikm, ByteView info, std::size_t length) {
    constexpr std::size_t HashLen = 32;
    if (length < 1 || length > 255 * HashLen) throw CryptoError("bad HKDF length");
    Bytes zeros(HashLen);
    Bytes prk = salt.empty() ? hmac(zeros, {ikm}) : hmac(salt, {ikm});
    Bytes out;
    out.reserve(length + HashLen);
    Bytes block;
    for (std::uint8_t counter = 1; out.size() < length; ++counter) {
        const std::uint8_t c[1] = {counter};
        block = hmac(prk, {block, info, ByteView(c, 1)});
        out.insert(out.end(), block.begin(), block.end());
    }
    out.resize(length);
    return out;
}

Bytes pbkdf2(ByteView password, ByteView salt, int iterations, std::size_t keyBytes) {
    Bytes out(keyBytes);
    static const char none = 0;
    const char* pw = password.empty() ? &none : reinterpret_cast<const char*>(password.data());
    if (PKCS5_PBKDF2_HMAC(pw, static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()),
                          iterations, EVP_sha256(), static_cast<int>(keyBytes), out.data()) != 1)
        throw CryptoError("pbkdf2 failed");
    return out;
}

}  // namespace tc::kdf
