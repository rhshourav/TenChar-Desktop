#pragma once
// The only place protocol and policy numbers are defined. Mirrors Constants.kt.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tc {
inline constexpr int MaxChars = 10;
inline constexpr int MaxUtf8Bytes = 64;
inline constexpr int FrameBytes = 2 + MaxUtf8Bytes;                 // 66
inline constexpr int HeaderBytes = 41;
inline constexpr int NonceBytes = 12;
inline constexpr int TagBytes = 16;
inline constexpr int EnvelopeBytes = HeaderBytes + NonceBytes + FrameBytes + TagBytes;  // 135
inline constexpr int EnvelopeB64Chars = EnvelopeBytes / 3 * 4;      // 180
inline constexpr int CodeLen = 16;
inline constexpr int CodeGroupLen = 4;
inline constexpr std::string_view CodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
inline constexpr int PairingWindowHours = 24;
inline constexpr int RetentionDays = 7;
inline constexpr int MaxSkip = 200;
inline constexpr int MaxBodyBytes = 512;
inline constexpr std::string_view DefaultRelayUrl = "https://tenchar-relay.onrender.com";
inline constexpr std::string_view FallbackRelayUrl = "https://tenchar-relay.tenchar.workers.dev";
inline constexpr int Pbkdf2Iterations = 600'000;
inline constexpr int Pbkdf2DkBytes = 32;
inline constexpr std::int64_t AutoLockShortMs = 30'000;
inline constexpr std::int64_t AutoLockLongMs = 300'000;
inline constexpr std::string_view SaltRoom = "tenchar-room-v1";
inline constexpr std::string_view SaltAuth = "tenchar-auth-v1";
inline constexpr std::string_view SaltKey = "tenchar-key-v1";
inline constexpr int RoomHexChars = 32;
inline constexpr int CodeTokenHexChars = 48;
inline constexpr int MinPassphraseChars = 12;
}  // namespace tc
