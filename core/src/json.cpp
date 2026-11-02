#include "tc/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>

namespace tc {

const Json* Json::find(std::string_view key) const {
    for (const auto& [k, v] : object)
        if (k == key) return &v;
    return nullptr;
}
std::string Json::optString(std::string_view key, std::string def) const {
    const Json* v = find(key);
    return (v && v->type == Type::String) ? v->string : def;
}
std::int64_t Json::optInt(std::string_view key, std::int64_t def) const {
    const Json* v = find(key);
    return (v && v->type == Type::Number && v->isInteger) ? v->integer : def;
}
bool Json::optBool(std::string_view key, bool def) const {
    const Json* v = find(key);
    return (v && v->type == Type::Bool) ? v->boolean : def;
}

namespace {
constexpr std::size_t MaxInput = 1 << 20;
constexpr int MaxDepth = 16;

struct Parser {
    std::string_view s;
    std::size_t i = 0;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool eat(char c) {
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }
    bool literal(std::string_view lit) {
        if (s.substr(i, lit.size()) == lit) { i += lit.size(); return true; }
        return false;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(std::uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i + k];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + static_cast<std::uint32_t>(d);
        }
        i += 4;
        return true;
    }
    bool string(std::string& out) {
        if (!eat('"')) return false;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        std::uint32_t lo;
                        if (!literal("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool number(Json& j) {
        std::size_t start = i;
        bool integral = true;
        eat('-');
        if (i >= s.size()) return false;
        if (s[i] == '0') ++i;
        else if (s[i] >= '1' && s[i] <= '9') { while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i; }
        else return false;
        if (eat('.')) {
            integral = false;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            integral = false;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        std::string text(s.substr(start, i - start));
        j.type = Json::Type::Number;
        j.number = std::strtod(text.c_str(), nullptr);
        if (integral) {
            std::int64_t v = 0;
            auto r = std::from_chars(text.data(), text.data() + text.size(), v);
            if (r.ec == std::errc() && r.ptr == text.data() + text.size()) { j.isInteger = true; j.integer = v; }
        }
        return true;
    }
    bool value(Json& j, int depth) {
        if (depth > MaxDepth) return false;
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') {
            ++i; j.type = Json::Type::Object; ws();
            if (eat('}')) return true;
            while (true) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (!eat(':')) return false;
                Json v;
                if (!value(v, depth + 1)) return false;
                j.object.emplace_back(std::move(key), std::move(v));
                ws();
                if (eat(',')) continue;
                return eat('}');
            }
        }
        if (c == '[') {
            ++i; j.type = Json::Type::Array; ws();
            if (eat(']')) return true;
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                j.array.push_back(std::move(v));
                ws();
                if (eat(',')) continue;
                return eat(']');
            }
        }
        if (c == '"') { j.type = Json::Type::String; return string(j.string); }
        if (literal("true")) { j.type = Json::Type::Bool; j.boolean = true; return true; }
        if (literal("false")) { j.type = Json::Type::Bool; j.boolean = false; return true; }
        if (literal("null")) { j.type = Json::Type::Null; return true; }
        return number(j);
    }
};
}  // namespace

std::optional<Json> parseJson(std::string_view text) {
    if (text.size() > MaxInput) return std::nullopt;
    Parser p{text};
    Json root;
    if (!p.value(root, 0)) return std::nullopt;
    p.ws();
    if (p.i != text.size()) return std::nullopt;
    return root;
}

std::string jsonQuote(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* d = "0123456789abcdef";
                    out += "\\u00"; out += d[c >> 4]; out += d[c & 15];
                } else out += static_cast<char>(c);
        }
    }
    return out + '"';
}

}  // namespace tc
