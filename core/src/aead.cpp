#include "tc/aead.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <memory>

namespace tc::aead {
namespace {
using Ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
constexpr int TagLen = 16;
}  // namespace

Bytes seal(ByteView key, ByteView nonce, ByteView aad, ByteView plaintext) {
    if (key.size() != 32 || nonce.size() != 12) throw CryptoError("bad key or nonce size");
    Ctx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1)
        throw CryptoError("gcm init");
    int len = 0;
    if (!aad.empty() && EVP_EncryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1)
        throw CryptoError("gcm aad");
    Bytes out(plaintext.size() + TagLen);
    int total = 0;
    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(ctx.get(), out.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1)
            throw CryptoError("gcm update");
        total = len;
    }
    if (EVP_EncryptFinal_ex(ctx.get(), out.data() + total, &len) != 1) throw CryptoError("gcm final");
    total += len;
    if (total != static_cast<int>(plaintext.size()) ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TagLen, out.data() + total) != 1)
        throw CryptoError("gcm tag");
    return out;
}

std::optional<Bytes> open(ByteView key, ByteView nonce, ByteView aad, ByteView sealed) {
    if (key.size() != 32 || nonce.size() != 12) throw CryptoError("bad key or nonce size");
    if (sealed.size() < static_cast<std::size_t>(TagLen)) return std::nullopt;
    const std::size_t ctLen = sealed.size() - TagLen;
    Ctx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1)
        return std::nullopt;
    int len = 0;
    if (!aad.empty() && EVP_DecryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1)
        return std::nullopt;
    Bytes out(ctLen);
    int total = 0;
    if (ctLen != 0) {
        if (EVP_DecryptUpdate(ctx.get(), out.data(), &len, sealed.data(), static_cast<int>(ctLen)) != 1) return std::nullopt;
        total = len;
    }
    std::uint8_t tag[TagLen];
    std::copy(sealed.end() - TagLen, sealed.end(), tag);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TagLen, tag) != 1) return std::nullopt;
    if (EVP_DecryptFinal_ex(ctx.get(), out.data() + total, &len) <= 0) return std::nullopt;
    return out;
}

}  // namespace tc::aead
