#pragma once
// Rules for a relay address typed by the person (RelayAddress.kt). HTTPS only; only a bare origin is accepted:
// no credentials, path, query or fragment. Non-ASCII host names are refused so a look-alike domain cannot be
// typed in by mistake.
#include <optional>
#include <string>
#include <string_view>

namespace tc::relay_address {

/// "https://host" or "https://host:port", or nullopt if not acceptable.
std::optional<std::string> normalise(std::string_view input);
inline bool isValid(std::string_view input) { return normalise(input).has_value(); }

}  // namespace tc::relay_address
