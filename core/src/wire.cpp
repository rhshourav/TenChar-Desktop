#include "tc/wire.hpp"

#include "tc/constants.hpp"

namespace tc::wire {

std::optional<Bytes> encodeText(std::string_view text, const limits::Segmenter& seg) {
    if (limits::isBlank(text) || !limits::fits(text, seg) || !limits::isDisplayable(text)) return std::nullopt;
    Bytes frame(FrameBytes);
    frame[0] = TypeText;
    frame[1] = static_cast<std::uint8_t>(text.size());
    std::copy(text.begin(), text.end(), frame.begin() + 2);
    return frame;
}

std::optional<std::string> decodeText(ByteView frame) {
    if (frame.size() != static_cast<std::size_t>(FrameBytes) || frame[0] != TypeText) return std::nullopt;
    const std::size_t len = frame[1];
    if (len < 1 || len > static_cast<std::size_t>(MaxUtf8Bytes)) return std::nullopt;
    for (std::size_t i = 2 + len; i < frame.size(); ++i)
        if (frame[i] != 0) return std::nullopt;
    std::string text(reinterpret_cast<const char*>(frame.data()) + 2, len);
    if (!limits::isValidUtf8(text) || limits::isBlank(text) || !limits::isDisplayable(text)) return std::nullopt;
    return text;
}

static constexpr char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(ByteView e) {
    if (e.size() != static_cast<std::size_t>(EnvelopeBytes)) throw CryptoError("bad envelope size");
    std::string out;
    out.reserve(EnvelopeB64Chars);
    for (std::size_t i = 0; i < e.size(); i += 3) {  // 135 is a multiple of 3: no padding
        std::uint32_t v = (e[i] << 16) | (e[i + 1] << 8) | e[i + 2];
        out += B64[(v >> 18) & 63]; out += B64[(v >> 12) & 63]; out += B64[(v >> 6) & 63]; out += B64[v & 63];
    }
    return out;
}

std::optional<Bytes> fromBase64(std::string_view t) {
    if (t.size() != static_cast<std::size_t>(EnvelopeB64Chars)) return std::nullopt;
    Bytes out;
    out.reserve(EnvelopeBytes);
    for (std::size_t i = 0; i < t.size(); i += 4) {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const char* p = std::char_traits<char>::find(B64, 64, t[i + static_cast<std::size_t>(k)]);
            if (!p || t[i + static_cast<std::size_t>(k)] == '\0') return std::nullopt;
            v = (v << 6) | static_cast<std::uint32_t>(p - B64);
        }
        out.push_back(static_cast<std::uint8_t>(v >> 16)); out.push_back(static_cast<std::uint8_t>(v >> 8));
        out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

}  // namespace tc::wire
