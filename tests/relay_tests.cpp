#include <future>

#include "fake_relay.hpp"
#include "tc/constants.hpp"
#include "tc/pairing.hpp"
#include "tc/pairing_flow.hpp"
#include "tc/wire.hpp"

using namespace tc;

static const PairingTimings fast{20, 20, 400, 150};
static const std::string RA = "https://relay-a.test", RB = "https://relay-b.test";

static ClientFor clientsOn(FakeRelay& net) {
    auto cache = std::make_shared<std::map<std::string, std::shared_ptr<RelayClient>>>();
    auto mu = std::make_shared<std::mutex>();
    return [&net, cache, mu](const std::string& ep) {
        std::lock_guard<std::mutex> l(*mu);
        auto& c = (*cache)[ep];
        if (!c) c = std::make_shared<RelayClient>(ep, net);
        return c;
    };
}

struct Side {
    LocalMember me = LocalMember::create(systemRandom());
    std::unique_ptr<PairingFlow> flow;
};

static const std::string Code = "ABCDEFGHJKLMNPQR";

static std::pair<PairOutcome, PairOutcome> pairBoth(FakeRelay& net, std::vector<std::string> endpoints, std::int64_t timeoutMs = 5000,
                                                    Side* a = nullptr, Side* b = nullptr) {
    Side sa, sb;
    if (!a) a = &sa;
    if (!b) b = &sb;
    auto d = pairing::derive(Code, 2000);
    a->flow = std::make_unique<PairingFlow>(endpoints, d.room, d.codeToken, a->me, clientsOn(net), timeoutMs, fast);
    b->flow = std::make_unique<PairingFlow>(endpoints, d.room, d.codeToken, b->me, clientsOn(net), timeoutMs, fast);
    auto fa = std::async(std::launch::async, [&] { return a->flow->run(); });
    auto fb = std::async(std::launch::async, [&] { return b->flow->run(); });
    return {fa.get(), fb.get()};
}

static void testClientParsing() {
    FakeRelay net;
    RelayClient c(RA + "/", net);
    CHECK_EQ(c.origin(), RA);
    bool threw = false;
    try { RelayClient bad("http://relay.test", net); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // a room id that is not 32 lower-case hex digits never reaches the network
    CHECK(c.state("../../etc/passwd", "t").unreachable());
    CHECK_EQ(net.requests("relay-a.test"), 0);
    // unknown room: rejected with the relay's own status
    auto r = c.state(std::string(32, 'a'), std::string(64, 'b'));
    CHECK(r.rejected() && r.status == 404 && r.error == "not found");
    net.set("relay-a.test", true);
    CHECK(c.state(std::string(32, 'a'), "x").unreachable());
}

static void testTwoRelaysPair() {
    FakeRelay net;
    Side a, b;
    auto [oa, ob] = pairBoth(net, {RA, RB}, 5000, &a, &b);
    CHECK(oa.success && ob.success);
    CHECK(oa.endpoints == (std::vector<std::string>{RA, RB}) && ob.endpoints == oa.endpoints);
    CHECK_EQ(oa.peer.memberId, b.me.memberId);
    CHECK_EQ(oa.peer.ik, hexEncode(b.me.keys.ik.pub));
    CHECK_EQ(ob.peer.ek, hexEncode(a.me.keys.ek.pub));
    // and the keys they agree on give both the same secret and number
    auto d = pairing::derive(Code, 2000);
    auto xa = handshake::agree(a.me.keys, a.me.memberId, *hexDecode(oa.peer.ik), *hexDecode(oa.peer.ek), oa.peer.memberId, d.codeKey);
    auto xb = handshake::agree(b.me.keys, b.me.memberId, *hexDecode(ob.peer.ik), *hexDecode(ob.peer.ek), ob.peer.memberId, d.codeKey);
    CHECK(xa.sk == xb.sk && xa.verification == xb.verification && xa.initiator != xb.initiator);
}

static void testOneRelayDownAndSlow() {
    {   // one relay down for the whole pairing: finishes on the other, and only that relay carries the chat
        FakeRelay net;
        net.set("relay-b.test", true);
        auto [oa, ob] = pairBoth(net, {RA, RB});
        CHECK(oa.success && ob.success);
        CHECK(oa.endpoints == std::vector<std::string>{RA});
    }
    {   // one slow relay does not hold up the result
        FakeRelay net;
        net.set("relay-b.test", false, false, 3000);
        auto t0 = std::chrono::steady_clock::now();
        auto [oa, ob] = pairBoth(net, {RA, RB});
        auto took = std::chrono::steady_clock::now() - t0;
        CHECK(oa.success && ob.success);
        CHECK(took < std::chrono::milliseconds(2500));
    }
    {   // every relay down: Unreachable
        FakeRelay net;
        net.set("relay-a.test", true);
        net.set("relay-b.test", true);
        auto [oa, ob] = pairBoth(net, {RA, RB}, 5000);
        CHECK(!oa.success && oa.reason == FailReason::Unreachable);
    }
}

static void testMaliciousRelay() {
    {   // one relay swaps the other member's keys (consistently, with its own commitment): the honest relay disagrees
        FakeRelay net;
        net.set("relay-b.test", false, /*mitm=*/true);
        auto [oa, ob] = pairBoth(net, {RA, RB});
        CHECK(!oa.success && oa.reason == FailReason::Tampered);
        CHECK(!ob.success && ob.reason == FailReason::Tampered);
    }
    {   // The only relay is the attacker. It can hand each side its own forged keys, so the relay cross-check has
        // nothing to compare against and pairing "succeeds". What protects this case is the pairing code mixed into
        // the key derivation plus the spoken 8-digit number: the two sides end with different secrets and numbers.
        FakeRelay net;
        net.set("relay-a.test", false, true);
        Side a, b;
        auto [oa, ob] = pairBoth(net, {RA}, 5000, &a, &b);
        CHECK(oa.success && ob.success);
        CHECK_EQ(oa.peer.ik, hexEncode(net.atkIk.pub));
        CHECK_EQ(ob.peer.ik, hexEncode(net.atkIk.pub));
        auto d = pairing::derive(Code, 2000);
        auto xa = handshake::agree(a.me.keys, a.me.memberId, *hexDecode(oa.peer.ik), *hexDecode(oa.peer.ek), oa.peer.memberId, d.codeKey);
        auto xb = handshake::agree(b.me.keys, b.me.memberId, *hexDecode(ob.peer.ik), *hexDecode(ob.peer.ek), ob.peer.memberId, d.codeKey);
        CHECK(xa.sk != xb.sk);
        CHECK(xa.verification != xb.verification);
        // and an attacker who does not know the code cannot compute either side's secret
        Bytes guess = d.codeKey; guess[5] ^= 1;
        CHECK(handshake::agree(net.atkIk.priv.empty() ? a.me.keys : LocalKeys{net.atkIk, net.atkEk}, a.me.memberId,
                               *hexDecode(oa.peer.ik), *hexDecode(oa.peer.ek), oa.peer.memberId, guess).sk != xa.sk);
    }
    {   // a third device with the same code is refused
        FakeRelay net;
        auto d = pairing::derive(Code, 2000);
        RelayClient c(RA, net);
        auto m1 = LocalMember::create(systemRandom()), m2 = LocalMember::create(systemRandom()), m3 = LocalMember::create(systemRandom());
        CHECK(c.hello(d.room, d.codeToken, m1.memberId, m1.tokenHash, m1.commit).ok());
        CHECK(c.hello(d.room, d.codeToken, m2.memberId, m2.tokenHash, m2.commit).ok());
        auto r = c.hello(d.room, d.codeToken, m3.memberId, m3.tokenHash, m3.commit);
        CHECK(r.rejected() && r.status == 409);
    }
}

static void testCancelAndTimeout() {
    {   // only one device ever joins: Timeout
        FakeRelay net;
        auto d = pairing::derive(Code, 2000);
        Side a;
        PairingFlow f({RA}, d.room, d.codeToken, a.me, clientsOn(net), 300, fast);
        auto o = f.run();
        CHECK(!o.success && !o.cancelled && o.reason == FailReason::Timeout);
        f.cleanup();
        CHECK(!net.hasRoom("relay-a.test", d.room));   // the abandoned room is deleted
    }
    {   // cancel returns promptly even with a 24 hour window
        FakeRelay net;
        auto d = pairing::derive(Code, 2000);
        Side a;
        PairingFlow f({RA, RB}, d.room, d.codeToken, a.me, clientsOn(net), 24LL * 3600 * 1000, fast);
        auto fut = std::async(std::launch::async, [&] { return f.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto t0 = std::chrono::steady_clock::now();
        f.cancel();
        auto o = fut.get();
        CHECK(o.cancelled && !o.success);
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500));
    }
}

static void testMessagesThroughRelay() {
    FakeRelay net;
    auto d = pairing::derive(Code, 2000);
    Side a, b;
    auto [oa, ob] = pairBoth(net, {RA}, 5000, &a, &b);
    CHECK(oa.success && ob.success);
    RelayClient c(RA, net);
    std::string env(180, 'A');
    CHECK(c.post(d.room, a.me.memberToken, env).ok());
    CHECK(c.post(d.room, a.me.memberToken, env).ok());
    CHECK(c.post(d.room, a.me.memberToken, "short").rejected());
    auto page = c.fetch(d.room, b.me.memberToken, 0);
    CHECK(page.ok() && page.value.messages.size() == 2 && page.value.messages[1].id == 2 && !page.value.more);
    CHECK(c.fetch(d.room, a.me.memberToken, 0).value.messages.empty());   // never your own messages
    CHECK(c.fetch(d.room, "00" + b.me.memberToken.substr(2), 0).rejected());   // wrong token
    CHECK(c.ack(d.room, b.me.memberToken, 1).ok());
    CHECK_EQ(c.fetch(d.room, b.me.memberToken, 0).value.messages.size(), std::size_t(1));
    CHECK(c.del(d.room, b.me.memberToken).ok());
    CHECK(c.state(d.room, b.me.memberToken).rejected());
}

int main() {
    RUN(testClientParsing);
    RUN(testTwoRelaysPair);
    RUN(testOneRelayDownAndSlow);
    RUN(testMaliciousRelay);
    RUN(testCancelAndTimeout);
    RUN(testMessagesThroughRelay);
    return FINISH();
}
