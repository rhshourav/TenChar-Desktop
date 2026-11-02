#include "tc/pairing.hpp"

#include <future>
#include <stdexcept>

#include "tc/constants.hpp"
#include "tc/kdf.hpp"

namespace tc::pairing {

std::string fromBytes(ByteView b) {
    if (b.size() != static_cast<std::size_t>(CodeLen)) throw std::invalid_argument("wrong number of bytes");
    static_assert(CodeAlphabet.size() == 32, "alphabet must have 32 symbols");
    std::string out(CodeLen, ' ');
    for (std::size_t i = 0; i < b.size(); ++i) out[i] = CodeAlphabet[b[i] & 0x1F];
    return out;
}

std::string generate(RandomSource& rng) {
    Bytes bytes = randomBytes(rng, CodeLen);
    return fromBytes(bytes);  // `bytes` is wiped on destruction
}

std::string normalise(std::string_view input) {
    std::string out;
    for (char c : input) {
        if (c == '-' || c == ' ' || (c >= 9 && c <= 13)) continue;
        out += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    return out;
}

static bool inAlphabet(char c) { return CodeAlphabet.find(c) != std::string_view::npos; }

bool isValid(std::string_view input) {
    std::string code = normalise(input);
    if (code.size() != static_cast<std::size_t>(CodeLen)) return false;
    for (char c : code)
        if (!inAlphabet(c)) return false;
    return true;
}

static std::string group(const std::string& code) {
    std::string out;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (i > 0 && i % CodeGroupLen == 0) out += '-';
        out += code[i];
    }
    return out;
}

std::string formatPartial(std::string_view input) {
    std::string kept;
    for (char c : normalise(input))
        if (inAlphabet(c) && kept.size() < static_cast<std::size_t>(CodeLen)) kept += c;
    return group(kept);
}

std::string format(std::string_view input) {
    if (!isValid(input)) throw std::invalid_argument("invalid pairing code");
    return group(normalise(input));
}

std::string hexPrefix(std::string_view code, std::string_view salt, std::size_t hexChars, int iterations) {
    if (hexChars < 1 || hexChars > static_cast<std::size_t>(Pbkdf2DkBytes) * 2) throw std::invalid_argument("bad prefix length");
    Bytes key = kdf::pbkdf2(asBytes(code), asBytes(salt), iterations, Pbkdf2DkBytes);
    return hexEncode(key).substr(0, hexChars);
}

Derived derive(std::string_view input, int iterations) {
    if (!isValid(input)) throw std::invalid_argument("invalid pairing code");
    const std::string code = normalise(input);
    auto room = std::async(std::launch::async, [&] { return hexPrefix(code, SaltRoom, RoomHexChars, iterations); });
    auto token = std::async(std::launch::async, [&] { return hexPrefix(code, SaltAuth, CodeTokenHexChars, iterations); });
    auto key = std::async(std::launch::async, [&] { return kdf::pbkdf2(asBytes(code), asBytes(SaltKey), iterations, Pbkdf2DkBytes); });
    Derived d{room.get(), token.get(), key.get()};
    return d;
}

}  // namespace tc::pairing
