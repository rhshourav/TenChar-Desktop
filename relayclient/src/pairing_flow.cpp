#include "tc/pairing_flow.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

namespace tc {
namespace {
using Clock = std::chrono::steady_clock;
std::int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
}  // namespace

struct PairingFlow::Shared {
    std::vector<std::string> endpoints;
    std::string room, codeToken;
    LocalMember me;
    ClientFor clientFor;
    std::int64_t timeoutMs;
    PairingTimings t;

    struct Result {
        std::string endpoint;
        bool sealed = false;
        PeerKeys peer;
        FailReason reason = FailReason::Timeout;
    };
    std::mutex m;
    std::condition_variable cv;
    bool stop = false;  // cancelled, or run() has its answer
    bool cancelled = false;
    std::vector<Result> results;
    std::set<std::string> joined;

    bool stopped() { std::lock_guard<std::mutex> l(m); return stop; }
    /// Sleeps up to `ms`, waking early when stopped. False if stopped.
    bool pause(std::int64_t ms) {
        std::unique_lock<std::mutex> l(m);
        return !cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return stop; });
    }
    void finish(Result r) {
        std::lock_guard<std::mutex> l(m);
        results.push_back(std::move(r));
        cv.notify_all();
    }
};

static void runEndpoint(const std::shared_ptr<PairingFlow::Shared>& sp, std::string endpoint, std::int64_t deadline);

PairingFlow::PairingFlow(std::vector<std::string> endpoints, std::string room, std::string codeToken, LocalMember me,
                         ClientFor clientFor, std::int64_t timeoutMs, PairingTimings timings)
    : s_(std::make_shared<Shared>()) {
    s_->endpoints = std::move(endpoints);
    s_->room = std::move(room);
    s_->codeToken = std::move(codeToken);
    s_->me = std::move(me);
    s_->clientFor = std::move(clientFor);
    s_->timeoutMs = timeoutMs;
    s_->t = timings;
}

void PairingFlow::cancel() {
    std::lock_guard<std::mutex> l(s_->m);
    s_->cancelled = true;
    s_->stop = true;
    s_->cv.notify_all();
}

PairOutcome PairingFlow::run() {
    auto s = s_;
    const std::int64_t deadline = nowMs() + s->timeoutMs;
    for (const auto& ep : s->endpoints) std::thread([s, ep, deadline] { runEndpoint(s, ep, deadline); }).detach();

    std::unique_lock<std::mutex> lk(s->m);
    std::size_t seen = 0;
    std::int64_t graceEnds = -1;
    while (s->results.size() < s->endpoints.size() && !s->stop) {
        auto ready = [&] { return s->stop || s->results.size() > seen; };
        if (graceEnds < 0) {
            s->cv.wait(lk, ready);
        } else if (!s->cv.wait_for(lk, std::chrono::milliseconds(std::max<std::int64_t>(0, graceEnds - nowMs())), ready)) {
            break;  // the grace period after the first sealed relay is over
        }
        if (s->stop) break;
        for (; seen < s->results.size(); ++seen)
            if (s->results[seen].sealed && graceEnds < 0) graceEnds = nowMs() + s->t.graceMs;
    }
    PairOutcome out;
    if (s->cancelled) {
        out.cancelled = true;
        out.reason = FailReason::Timeout;
        return out;
    }
    s->stop = true;  // tell any worker still running to give up
    s->cv.notify_all();
    auto done = s->results;
    auto joined = s->joined;
    lk.unlock();

    std::vector<const PairingFlow::Shared::Result*> sealed;
    for (const auto& r : done)
        if (r.sealed) sealed.push_back(&r);
    if (sealed.empty()) {
        for (FailReason want : {FailReason::Tampered, FailReason::Full, FailReason::Timeout, FailReason::Unreachable})
            for (const auto& r : done)
                if (r.reason == want) { out.reason = want; return out; }
        out.reason = FailReason::Timeout;
        return out;
    }
    const PeerKeys& first = sealed.front()->peer;
    for (const auto* r : sealed)
        if (r->peer.memberId != first.memberId || r->peer.ik != first.ik || r->peer.ek != first.ek) {
            out.reason = FailReason::Tampered;
            return out;
        }
    // Keep every relay we joined, sealed or not: the other device may have finished there a moment later,
    // and a message it posts there must not be missed. An unsealed room just says "not sealed" and expires.
    for (const auto& ep : s->endpoints) {
        bool carrying = joined.count(ep) > 0;
        for (const auto* r : sealed) carrying = carrying || r->endpoint == ep;
        if (carrying) out.endpoints.push_back(ep);
    }
    out.success = true;
    out.peer = first;
    return out;
}

void PairingFlow::cleanup() {
    std::set<std::string> joined;
    { std::lock_guard<std::mutex> l(s_->m); joined = s_->joined; }
    for (const auto& ep : joined) {
        try { s_->clientFor(ep)->del(s_->room, s_->me.memberToken); } catch (...) {}
    }
}

static void runEndpoint(const std::shared_ptr<PairingFlow::Shared>& sp, std::string endpoint, std::int64_t deadline) {
    PairingFlow::Shared& s = *sp;
    auto fail = [&](FailReason why) { s.finish({endpoint, false, {}, why}); };
    std::shared_ptr<RelayClient> client;
    try { client = s.clientFor(endpoint); } catch (...) { return fail(FailReason::Unreachable); }
    const LocalMember& me = s.me;

    // 1. hello, tolerating a cold-starting relay for a while.
    const std::int64_t helloDeadline = std::min(deadline, nowMs() + s.t.helloBudgetMs);
    while (true) {
        if (s.stopped()) return fail(FailReason::Timeout);
        auto r = client->hello(s.room, s.codeToken, me.memberId, me.tokenHash, me.commit);
        if (r.ok()) break;
        if (r.rejected()) return fail((r.status == 409 || r.status == 410) ? FailReason::Full : FailReason::Unreachable);
        if (nowMs() >= helloDeadline) return fail(FailReason::Unreachable);
        if (!s.pause(s.t.retryMs)) return fail(FailReason::Timeout);
    }
    { std::lock_guard<std::mutex> l(s.m); s.joined.insert(endpoint); }

    // 2-4. wait for the other device, reveal, wait for its reveal.
    bool didReveal = false;
    while (nowMs() < deadline && !s.stopped()) {
        auto st = client->state(s.room, me.memberToken);
        if (st.ok()) {
            const RoomMember* peer = nullptr;
            for (const auto& m : st.value.members)
                if (m.id != me.memberId) { peer = &m; break; }
            if (peer && st.value.members.size() == 2) {
                if (!didReveal) {
                    auto r = client->reveal(s.room, me.memberToken, me.memberId, hexEncode(me.keys.ik.pub), hexEncode(me.keys.ek.pub));
                    if (r.ok()) didReveal = true;
                    else if (r.rejected() && r.status == 400) return fail(FailReason::Tampered);
                }
                if (didReveal && peer->ik && peer->ek) {
                    auto ik = hexDecode(*peer->ik), ek = hexDecode(*peer->ek), id = hexDecode(peer->id);
                    if (!ik || !ek || !id) return fail(FailReason::Tampered);
                    if (hexEncode(handshake::commit(*ik, *ek, *id)) != peer->commit) return fail(FailReason::Tampered);
                    s.finish({endpoint, true, PeerKeys{peer->id, *peer->ik, *peer->ek}, FailReason::Timeout});
                    return;
                }
            }
        } else if (st.rejected() && (st.status == 404 || st.status == 401)) {
            return fail(FailReason::Unreachable);
        }
        if (!s.pause(s.t.pollMs)) break;
    }
    fail(FailReason::Timeout);
}

}  // namespace tc
