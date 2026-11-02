#pragma once
// A small strict JSON reader for relay responses and test vectors. It never
// sees a secret: relay responses hold only public keys, commitments and
// ciphertext. Depth and size are capped so hostile input cannot eat memory.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tc {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    bool isInteger = false;
    std::int64_t integer = 0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;

    bool isNull() const { return type == Type::Null; }
    const Json* find(std::string_view key) const;
    std::string optString(std::string_view key, std::string def = "") const;
    std::int64_t optInt(std::string_view key, std::int64_t def) const;
    bool optBool(std::string_view key, bool def) const;
};

/// Empty optional on any syntax error, trailing text, depth over 16 or input over 1 MiB.
std::optional<Json> parseJson(std::string_view text);
/// A JSON string literal with escapes, for building request bodies.
std::string jsonQuote(std::string_view s);

}  // namespace tc
