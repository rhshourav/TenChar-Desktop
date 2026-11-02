#include "tc/relay_address.hpp"

#include <algorithm>
#include <cctype>

namespace tc::relay_address {
namespace {
constexpr std::size_t MaxInput = 255, MaxHost = 253, MaxLabel = 63;

bool plausibleLabel(std::string_view l) {
    if (l.empty() || l.size() > MaxLabel || l.front() == '-' || l.back() == '-') return false;
    return std::all_of(l.begin(), l.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}
bool plausibleHost(const std::string& host) {
    if (host.empty() || host.size() > MaxHost) return false;
    if (host.front() == '[') {  // bracketed IPv6 literal: only hex digits, colons and dots
        if (host.back() != ']' || host.size() <= 2) return false;
        return std::all_of(host.begin() + 1, host.end() - 1, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.'; });
    }
    if (host.find('.') == std::string::npos) return false;
    std::size_t start = 0;
    while (true) {
        std::size_t dot = host.find('.', start);
        if (!plausibleLabel(std::string_view(host).substr(start, dot == std::string::npos ? std::string::npos : dot - start))) return false;
        if (dot == std::string::npos) return true;
        start = dot + 1;
    }
}
}  // namespace

std::optional<std::string> normalise(std::string_view input) {
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) input.remove_suffix(1);
    if (input.empty() || input.size() > MaxInput) return std::nullopt;
    for (unsigned char c : input)
        if (c <= 0x20 || c == 0x7F) return std::nullopt;  // white space and control characters
    std::string text(input);
    if (text.find("://") == std::string::npos) text = "https://" + text;

    std::string scheme = text.substr(0, text.find("://"));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return std::tolower(c); });
    if (scheme != "https") return std::nullopt;
    std::string rest = text.substr(text.find("://") + 3);
    if (rest.find('?') != std::string::npos || rest.find('#') != std::string::npos) return std::nullopt;
    std::size_t slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    if (slash != std::string::npos && rest.substr(slash) != "/") return std::nullopt;  // a path
    if (authority.find('@') != std::string::npos) return std::nullopt;                  // credentials

    std::string host = authority;
    int port = -1;
    std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']', colon) == std::string::npos) {
        host = authority.substr(0, colon);
        std::string p = authority.substr(colon + 1);
        if (p.empty() || p.size() > 5 || !std::all_of(p.begin(), p.end(), [](unsigned char c) { return std::isdigit(c); })) return std::nullopt;
        port = std::stoi(p);
        if (port < 1 || port > 65535) return std::nullopt;
    }
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return c < 0x80 ? std::tolower(c) : c; });
    if (!plausibleHost(host)) return std::nullopt;
    std::string out = "https://" + host;
    if (port != -1 && port != 443) out += ":" + std::to_string(port);
    return out;
}

}  // namespace tc::relay_address
