#include "tc/relay.hpp"

#include <stdexcept>

#include "tc/constants.hpp"

namespace tc {
namespace {
constexpr std::size_t MaxResponseBytes = 256 * 1024;

bool isHex(const std::string& s, std::size_t length) {
    if (s.size() != length) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
}  // namespace

RelayClient::RelayClient(std::string origin, HttpTransport& transport) : transport_(transport) {
    while (!origin.empty() && origin.back() == '/') origin.pop_back();
    if (origin.rfind("https://", 0) != 0) throw std::invalid_argument("relay must be https");
    origin_ = std::move(origin);
}

template <class T, class Parse>
Reply<T> RelayClient::call(const char* method, const std::string& room, const std::string& path,
                           const std::string& token, const std::string& body, Parse parse) {
    Reply<T> out;
    if (!isHex(room, 32)) return out;  // never put anything else into a URL path
    HttpRequest req{method, origin_ + "/v1/rooms/" + room + path, token, body};
    HttpResponse res = transport_.send(req);
    if (!res.transportOk || res.body.size() > MaxResponseBytes) return out;
    if (res.status >= 200 && res.status <= 299) {
        Json json;  // an empty object for 204 / empty bodies
        json.type = Json::Type::Object;
        if (res.status != 204 && !res.body.empty()) {
            auto parsed = parseJson(res.body);
            if (!parsed) return out;
            json = std::move(*parsed);
        }
        auto value = parse(json);
        if (!value) return out;
        out.kind = Reply<T>::Kind::Ok;
        out.value = std::move(*value);
        return out;
    }
    out.kind = Reply<T>::Kind::Rejected;
    out.status = res.status;
    if (auto parsed = parseJson(res.body)) out.error = parsed->optString("error").substr(0, 40);
    return out;
}

Reply<int> RelayClient::hello(const std::string& room, const std::string& codeToken, const std::string& memberId,
                              const std::string& tokenHash, const std::string& commit) {
    std::string body = "{\"memberId\":" + jsonQuote(memberId) + ",\"memberTokenHash\":" + jsonQuote(tokenHash) +
                       ",\"commit\":" + jsonQuote(commit) + "}";
    return call<int>("POST", room, "/hello", codeToken, body, [](const Json& j) -> std::optional<int> {
        auto n = j.optInt("members", -1);
        if (n < 1 || n > 2) return std::nullopt;
        return static_cast<int>(n);
    });
}

Reply<RoomState> RelayClient::state(const std::string& room, const std::string& token) {
    return call<RoomState>("GET", room, "/state", token, "", [](const Json& j) -> std::optional<RoomState> {
        const Json* arr = j.find("members");
        if (!arr || arr->type != Json::Type::Array || arr->array.size() > 2) return std::nullopt;
        RoomState st;
        st.sealed = j.optBool("sealed", false);
        for (const Json& m : arr->array) {
            if (m.type != Json::Type::Object) return std::nullopt;
            RoomMember rm;
            rm.id = m.optString("memberId");
            rm.commit = m.optString("commit");
            if (!isHex(rm.id, 32) || !isHex(rm.commit, 64)) return std::nullopt;
            for (auto [key, dst] : {std::pair<const char*, std::optional<std::string>*>{"ik", &rm.ik}, {"ek", &rm.ek}}) {
                const Json* f = m.find(key);
                if (f && !f->isNull()) {
                    if (f->type != Json::Type::String || !isHex(f->string, 64)) return std::nullopt;
                    *dst = f->string;
                }
            }
            st.members.push_back(std::move(rm));
        }
        return st;
    });
}

Reply<bool> RelayClient::reveal(const std::string& room, const std::string& token, const std::string& memberId,
                                const std::string& ik, const std::string& ek) {
    std::string body = "{\"memberId\":" + jsonQuote(memberId) + ",\"ik\":" + jsonQuote(ik) + ",\"ek\":" + jsonQuote(ek) + "}";
    return call<bool>("POST", room, "/reveal", token, body, [](const Json& j) -> std::optional<bool> { return j.optBool("sealed", false); });
}

Reply<bool> RelayClient::post(const std::string& room, const std::string& token, const std::string& envelope) {
    return call<bool>("POST", room, "/messages", token, "{\"env\":" + jsonQuote(envelope) + "}",
                      [](const Json&) -> std::optional<bool> { return true; });
}

Reply<RelayPage> RelayClient::fetch(const std::string& room, const std::string& token, std::int64_t after) {
    return call<RelayPage>("GET", room, "/messages?after=" + std::to_string(after), token, "",
                           [](const Json& j) -> std::optional<RelayPage> {
        const Json* arr = j.find("messages");
        if (!arr || arr->type != Json::Type::Array || arr->array.size() > 128) return std::nullopt;
        RelayPage page;
        for (const Json& m : arr->array) {
            if (m.type != Json::Type::Object) return std::nullopt;
            RelayMessage rm;
            rm.id = m.optInt("id", -1);
            rm.env = m.optString("env");
            if (rm.id < 0 || rm.env.size() != static_cast<std::size_t>(EnvelopeB64Chars)) return std::nullopt;
            page.messages.push_back(std::move(rm));
        }
        page.more = j.optBool("more", false);
        return page;
    });
}

Reply<bool> RelayClient::ack(const std::string& room, const std::string& token, std::int64_t upTo) {
    return call<bool>("POST", room, "/ack", token, "{\"upTo\":" + std::to_string(upTo) + "}",
                      [](const Json&) -> std::optional<bool> { return true; });
}

Reply<bool> RelayClient::del(const std::string& room, const std::string& token) {
    return call<bool>("DELETE", room, "", token, "", [](const Json&) -> std::optional<bool> { return true; });
}

}  // namespace tc
