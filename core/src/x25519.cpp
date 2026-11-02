#include "tc/x25519.hpp"

#include <openssl/evp.h>

#include <memory>

namespace tc::x25519 {
namespace {
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using KCtx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
}  // namespace

Bytes generatePrivate(RandomSource& rng) { return randomBytes(rng, KeyBytes); }

Bytes publicKey(ByteView priv) {
    if (priv.size() != KeyBytes) throw CryptoError("keys are 32 bytes");
    Key k(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv.data(), priv.size()), &EVP_PKEY_free);
    if (!k) throw CryptoError("bad x25519 private key");
    Bytes out(KeyBytes);
    std::size_t len = out.size();
    if (EVP_PKEY_get_raw_public_key(k.get(), out.data(), &len) != 1 || len != KeyBytes) throw CryptoError("x25519 public");
    return out;
}

Bytes sharedSecret(ByteView priv, ByteView peer) {
    if (priv.size() != KeyBytes || peer.size() != KeyBytes) throw CryptoError("keys are 32 bytes");
    Key mine(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv.data(), priv.size()), &EVP_PKEY_free);
    Key theirs(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer.data(), peer.size()), &EVP_PKEY_free);
    if (!mine || !theirs) throw CryptoError("bad x25519 key");
    KCtx ctx(EVP_PKEY_CTX_new(mine.get(), nullptr), &EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), theirs.get()) != 1)
        throw CryptoError("x25519 derive");
    Bytes out(KeyBytes);
    std::size_t len = out.size();
    if (EVP_PKEY_derive(ctx.get(), out.data(), &len) != 1 || len != KeyBytes) throw CryptoError("low-order public key");
    std::uint8_t acc = 0;
    for (auto b : out) acc |= b;  // no early exit
    if (acc == 0) throw CryptoError("low-order public key");
    return out;
}

}  // namespace tc::x25519
