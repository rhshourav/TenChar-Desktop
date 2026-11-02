#include "tc/limits.hpp"

#include "tc/constants.hpp"

namespace tc::limits {

std::optional<std::u32string> decodeUtf8(std::string_view s) {
    std::u32string out;
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char b0 = static_cast<unsigned char>(s[i]);
        int n;
        char32_t cp;
        if (b0 < 0x80) { n = 1; cp = b0; }
        else if (b0 >= 0xC2 && b0 <= 0xDF) { n = 2; cp = b0 & 0x1F; }
        else if (b0 >= 0xE0 && b0 <= 0xEF) { n = 3; cp = b0 & 0x0F; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { n = 4; cp = b0 & 0x07; }
        else return std::nullopt;
        if (i + static_cast<std::size_t>(n) > s.size()) return std::nullopt;
        for (int k = 1; k < n; ++k) {
            unsigned char b = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((b & 0xC0) != 0x80) return std::nullopt;
            cp = (cp << 6) | (b & 0x3F);
        }
        if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return std::nullopt;
        out.push_back(cp);
        i += static_cast<std::size_t>(n);
    }
    return out;
}

bool isValidUtf8(std::string_view s) { return decodeUtf8(s).has_value(); }

bool isWhitespace(char32_t c) {
    return (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x20) || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool isBlank(std::string_view s) {
    auto cps = decodeUtf8(s);
    if (!cps) return false;
    for (char32_t c : *cps)
        if (!isWhitespace(c)) return false;
    return true;
}

bool isDisplayable(std::string_view s) {
    auto cps = decodeUtf8(s);
    if (!cps) return false;
    for (char32_t c : *cps) {
        if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) return false;
        if ((c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) || c == 0x200E || c == 0x200F) return false;
    }
    return true;
}

std::string trim(std::string_view s) {
    auto cps = decodeUtf8(s);
    if (!cps) return std::string(s);
    std::size_t first = 0, last = cps->size();
    while (first < last && isWhitespace((*cps)[first])) ++first;
    while (last > first && isWhitespace((*cps)[last - 1])) --last;
    // map code point indexes back to byte offsets
    std::size_t byteFirst = 0, byteLast = 0, cp = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        bool boundary = i == s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
        if (!boundary) continue;
        if (cp == first) byteFirst = i;
        if (cp == last) { byteLast = i; break; }
        ++cp;
    }
    return std::string(s.substr(byteFirst, byteLast - byteFirst));
}

std::vector<std::size_t> codepointClusters(std::string_view s) {
    std::vector<std::size_t> ends;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (i + 1 == s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80) ends.push_back(i + 1);
    return ends;
}

std::size_t graphemeCount(std::string_view s, const Segmenter& seg) { return s.empty() ? 0 : seg(s).size(); }

bool fits(std::string_view s, const Segmenter& seg) {
    return isValidUtf8(s) && s.size() <= static_cast<std::size_t>(MaxUtf8Bytes) &&
           graphemeCount(s, seg) <= static_cast<std::size_t>(MaxChars);
}

std::string clamp(std::string_view s, const Segmenter& seg) {
    if (!isValidUtf8(s)) return {};
    std::size_t start = 0, end = 0, count = 0, bytes = 0;
    for (std::size_t boundary : seg(s)) {
        std::size_t cluster = boundary - start;
        if (count + 1 > static_cast<std::size_t>(MaxChars) || bytes + cluster > static_cast<std::size_t>(MaxUtf8Bytes)) break;
        ++count;
        bytes += cluster;
        end = boundary;
        start = boundary;
    }
    return std::string(s.substr(0, end));
}

}  // namespace tc::limits
