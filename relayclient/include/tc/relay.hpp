#pragma once
// Client for one relay (the protocol in relay/src/core.js, as used by RelayClient.kt).
// HTTPS only, no redirects, small responses only, no logging. Everything a relay says is untrusted.
//
// The HTTP transport is an interface: the app plugs in Qt Network, which uses the operating
// system's certificate store (OpenSSL on its own would not see the Windows or macOS roots).
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tc/json.hpp"

namespace tc {

struct HttpRequest {
    std::string method;  // GET, POST, DELETE
    std::string url;
    std::string bearer;  // sent as "Authorization: Bearer <bearer>"
    std::string body;    // JSON, empty for none
};
struct HttpResponse {
    bool transportOk = false;  // false: DNS, TLS, connect, timeout or oversized response
    int status = 0;
    std::string body;
};
struct HttpTransport {
    virtual ~HttpTransport() = default;
    /// Blocking and thread-safe. Must not follow redirects, must verify TLS, must cap the response size.
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

/// What a relay call came back with.
template <class T>
struct Reply {
    enum class Kind { Ok, Rejected, Unreachable };
    Kind kind = Kind::Unreachable;
    T value{};
    int status = 0;
    std::string error;
    bool ok() const { return kind == Kind::Ok; }
    bool unreachable() const { return kind == Kind::Unreachable; }
    bool rejected() const { return kind == Kind::Rejected; }
};

struct RoomMember {
    std::string id, commit;
    std::optional<std::string> ik, ek;
};
struct RoomState {
    bool sealed = false;
    std::vector<RoomMember> members;
};
struct RelayMessage {
    std::int64_t id = 0;
    std::string env;
};
struct RelayPage {
    std::vector<RelayMessage> messages;
    bool more = false;
};

class RelayClient {
public:
    /// Throws std::invalid_argument unless `origin` starts with https://
    RelayClient(std::string origin, HttpTransport& transport);

    Reply<int> hello(const std::string& room, const std::string& codeToken, const std::string& memberId,
                     const std::string& tokenHash, const std::string& commit);
    Reply<RoomState> state(const std::string& room, const std::string& token);
    Reply<bool> reveal(const std::string& room, const std::string& token, const std::string& memberId,
                       const std::string& ik, const std::string& ek);
    Reply<bool> post(const std::string& room, const std::string& token, const std::string& envelope);
    Reply<RelayPage> fetch(const std::string& room, const std::string& token, std::int64_t after);
    Reply<bool> ack(const std::string& room, const std::string& token, std::int64_t upTo);
    Reply<bool> del(const std::string& room, const std::string& token);

    const std::string& origin() const { return origin_; }

private:
    template <class T, class Parse>
    Reply<T> call(const char* method, const std::string& room, const std::string& path, const std::string& token,
                  const std::string& body, Parse parse);

    std::string origin_;
    HttpTransport& transport_;
};

}  // namespace tc
