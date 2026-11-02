#pragma once
// Message length rules: at most MaxChars user-perceived characters and
// MaxUtf8Bytes bytes. The core has no Unicode tables, so the caller supplies
// a Segmenter (the Qt app uses QTextBoundaryFinder). It returns the byte
// offset at which each grapheme cluster ends.
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tc::limits {

using Segmenter = std::function<std::vector<std::size_t>(std::string_view utf8)>;

/// Fallback and test segmenter: one cluster per code point.
std::vector<std::size_t> codepointClusters(std::string_view utf8);

std::optional<std::u32string> decodeUtf8(std::string_view s);  // strict, empty optional if malformed
bool isValidUtf8(std::string_view s);
/// Kotlin's String.isBlank(): empty, or only Unicode white space.
bool isBlank(std::string_view s);
/// Kotlin's Char.isWhitespace() for one code point.
bool isWhitespace(char32_t c);
/// Removes leading and trailing white space (invalid UTF-8 is returned unchanged).
std::string trim(std::string_view s);
/// No ISO control characters and no bidirectional overrides or marks.
bool isDisplayable(std::string_view s);

std::size_t graphemeCount(std::string_view s, const Segmenter& seg);
/// Sender-side rule: valid UTF-8, <= MaxChars clusters, <= MaxUtf8Bytes bytes.
bool fits(std::string_view s, const Segmenter& seg);
/// Longest prefix that fits, cut only at cluster boundaries.
std::string clamp(std::string_view s, const Segmenter& seg);

}  // namespace tc::limits
