// Exercises QtTransport against tests/transport_server.py (a local HTTPS server with a self-signed certificate).
// Usage: transport_test CERT.pem PORT
#include <QCoreApplication>
#include <QFile>
#include <atomic>
#include <thread>
#include <vector>

#include "../app/src/qt_transport.hpp"
#include "tc/json.hpp"
#include "testing.hpp"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) return 2;
    QFile f(argv[1]);
    f.open(QIODevice::ReadOnly);
    const QByteArray pem = f.readAll();
    const std::string base = std::string("https://localhost:") + argv[2] + "/v1/rooms/" + std::string(32, 'a');
    QtTransport trusting("TenChar-test/1", pem);

    auto get = trusting.send({"GET", base + "/state", "tok123", ""});
    CHECK(get.transportOk && get.status == 200);
    auto j = tc::parseJson(get.body);
    CHECK(j.has_value());
    if (j) {
        CHECK_EQ(j->optString("method"), std::string("GET"));
        CHECK_EQ(j->optString("auth"), std::string("Bearer tok123"));      // the bearer token goes in the header
        CHECK_EQ(j->optString("ua"), std::string("TenChar-test/1"));
    }
    auto post = trusting.send({"POST", base + "/hello", "tok", "{\"a\":1}"});
    auto pj = tc::parseJson(post.body);
    CHECK(post.transportOk && pj && pj->optString("body") == "{\"a\":1}" && pj->optString("ctype") == "application/json");
    auto del = trusting.send({"DELETE", base, "tok", ""});
    CHECK(del.transportOk && tc::parseJson(del.body)->optString("method") == "DELETE");

    auto missing = trusting.send({"GET", base + "/missing", "t", ""});
    CHECK(missing.transportOk && missing.status == 404);                  // an error status is an answer, not a failure
    CHECK_EQ(tc::parseJson(missing.body)->optString("error"), std::string("not found"));

    auto redirect = trusting.send({"GET", base + "/redirect", "t", ""});
    CHECK(redirect.transportOk && redirect.status == 302);                // redirects are never followed

    auto big = trusting.send({"GET", base + "/big", "t", ""});
    CHECK(!big.transportOk);                                              // a response over 256 KiB is refused

    CHECK(!trusting.send({"GET", "http://localhost:" + std::string(argv[2]) + "/x", "t", ""}).transportOk);   // plain http is refused
    CHECK(!trusting.send({"PUT", base, "t", ""}).transportOk);                                             // unsupported method
    CHECK(!trusting.send({"GET", "https://localhost:1/v1/rooms/" + std::string(32, 'a') + "/state", "t", ""}).transportOk);  // closed port

    QtTransport strict("TenChar-test/1");                                 // no extra CA: the self-signed certificate must be rejected
    CHECK(!strict.send({"GET", base + "/state", "t", ""}).transportOk);

    // many requests at once from several threads
    std::vector<std::thread> threads;
    std::atomic<int> okCount{0};
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] { for (int k = 0; k < 5; ++k) if (trusting.send({"GET", base + "/state", "t", ""}).status == 200) ++okCount; });
    for (auto& t : threads) t.join();
    CHECK_EQ(okCount.load(), 40);
    return FINISH();
}
