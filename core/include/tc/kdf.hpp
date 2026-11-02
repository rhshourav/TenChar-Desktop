#pragma once
// SHA-256, HMAC-SHA256, HKDF-SHA256 (RFC 5869) and PBKDF2-HMAC-SHA256 on OpenSSL 3.
#include "tc/bytes.hpp"

namespace tc::kdf {

Bytes sha256(std::initializer_list<ByteView> parts);
Bytes hmac(ByteView key, std::initializer_list<ByteView> parts);
/// An empty salt means 32 zero bytes, as the RFC says.
Bytes hkdf(ByteView salt, ByteView ikm, ByteView info, std::size_t length);
Bytes pbkdf2(ByteView password, ByteView salt, int iterations, std::size_t keyBytes);

}  // namespace tc::kdf
