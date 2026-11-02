#include "tc/session.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "tc/pairing.hpp"
#include "tc/relay_address.hpp"
#include "tc/state.hpp"
#include "tc/wire.hpp"

namespace tc {

std::int64_t autoLockMillis(AutoLock a) {
    switch (a) {
        case AutoLock::Immediately: return 0;
        case AutoLock::Seconds30: return AutoLockShortMs;
        case AutoLock::Minutes5: return AutoLockLongMs;
    }
    return AutoLockShortMs;
}

namespace {
using Clock = std::chrono::steady_clock;
std::int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
constexpr int MaxPages = 20;

/// One RelayClient per relay address, shared by every thread that needs one.
struct ClientPool {
    std::mutex m;
    std::map<std::string, std::shared_ptr<RelayClient>> clients;
    HttpTransport* transport;
    explicit ClientPool(HttpTransport* t) : transport(t) {}
    std::shared_ptr<RelayClient> get(const std::string& endpoint) {
        std::lock_guard<std::mutex> l(m);
        auto& c = clients[endpoint];
        if (!c) c = std::make_shared<RelayClient>(endpoint, *transport);
        return c;
    }
};

const char* autoLockName(AutoLock a) { return a == AutoLock::Immediately ? "Immediately" : a == AutoLock::Seconds30 ? "Seconds30" : "Minutes5"; }
}  // namespace

struct Engine::Core : std::enable_shared_from_this<Core> {
    Deps d;
    Vault vault;
    std::filesystem::path settingsFile;
    std::shared_ptr<ClientPool> pool;

    mutable std::mutex m;  // guards everything below until "callback"
    Screen screen = Screen::Unlock;
    Notice notice = Notice::None;
    std::string code, entry, verification;
    bool joining = false, busy = false, unlockFailed = false, firstRun = false, foreground = false;
    SetupError setupError = SetupError::None;
    Connection connection = Connection::Online;
    Settings settings;
    std::optional<Bytes> dek;
    std::optional<Conversation> conv;
    std::uint64_t epoch = 0;  // bumped whenever the conversation is replaced or cleared
    int goneRounds = 0;
    std::map<std::string, std::int64_t> retryAt;
    std::uint64_t pairGen = 0;
    std::shared_ptr<PairingFlow> flow;
    std::uint64_t pollGen = 0;
    bool pollRunning = false, pollWake = false;
    std::condition_variable pollCv;

    std::mutex cbm;  // callback
    std::function<void()> cb;

    std::mutex wm;  // worker
    std::condition_variable wcv;
    std::deque<std::function<void()>> tasks;
    bool quit = false;

    explicit Core(Deps deps)
        : d(std::move(deps)), vault(d.dataDir, d.vaultIterations), settingsFile(d.dataDir / "settings.json"),
          pool(std::make_shared<ClientPool>(d.transport)) {
        loadSettings();
        if (!settings.setupDone) { screen = Screen::Setup; firstRun = true; }
        else if (!vault.exists()) screen = Screen::KeysReset;
        else screen = Screen::Unlock;
    }

    void start() {
        std::thread([self = shared_from_this()] { self->workerLoop(); }).detach();
    }
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> l(wm);
                wcv.wait(l, [&] { return quit || !tasks.empty(); });
                if (quit) return;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            try { task(); } catch (...) {}
        }
    }
    void post(std::function<void()> f) {
        { std::lock_guard<std::mutex> l(wm); tasks.push_back(std::move(f)); }
        wcv.notify_one();
    }
    void fire() {
        std::function<void()> f;
        { std::lock_guard<std::mutex> l(cbm); f = cb; }
        if (f) f();
    }
    template <class F>
    void act(F f) {
        { std::lock_guard<std::mutex> l(m); f(); }
        fire();
    }

    // ---- settings ---------------------------------------------------------------------------------
    void loadSettings() {
        auto raw = readWholeFile(settingsFile, 64 * 1024);
        if (!raw) return;
        auto j = parseJson(toString(*raw));
        if (!j || j->type != Json::Type::Object) return;
        if (auto v = relay_address::normalise(j->optString("relay"))) settings.relayUrl = *v;     // checked again on load
        if (auto v = relay_address::normalise(j->optString("fallback"))) settings.fallbackUrl = *v;
        settings.fallbackEnabled = j->optBool("fallbackEnabled", true);
        std::string al = j->optString("autoLock");
        for (AutoLock a : {AutoLock::Immediately, AutoLock::Seconds30, AutoLock::Minutes5})
            if (al == autoLockName(a)) settings.autoLock = a;
        settings.setupDone = j->optBool("setupDone", false);
    }
    void saveSettingsLocked() {
        std::string text = "{\"relay\":" + jsonQuote(settings.relayUrl) + ",\"fallback\":" + jsonQuote(settings.fallbackUrl) +
                           ",\"fallbackEnabled\":" + (settings.fallbackEnabled ? "true" : "false") + ",\"autoLock\":\"" +
                           autoLockName(settings.autoLock) + "\",\"setupDone\":" + (settings.setupDone ? "true" : "false") + "}";
        try { writeFileAtomically(settingsFile, asBytes(text)); } catch (...) {}
    }
    std::vector<std::string> endpointsLocked() const {
        std::vector<std::string> out{settings.relayUrl};
        if (settings.fallbackEnabled && settings.fallbackUrl != settings.relayUrl) out.push_back(settings.fallbackUrl);
        return out;
    }

    // ---- state helpers (all "Locked" functions assume `m` is held) --------------------------------
    void gotoLocked(Screen s) { screen = s; updatePollingLocked(); }

    bool persistLocked() {
        if (!dek) return false;
        try {
            Bytes plain = state::encode(conv ? &*conv : nullptr);
            vault.save(*dek, plain, *d.rng);
            return true;
        } catch (...) {
            return false;
        }
    }

    void wipeSecretsLocked() {
        if (conv) conv->wipe();
        conv.reset();
        ++epoch;
        if (dek) wipe(*dek);
        dek.reset();
        verification.clear();
    }

    void cancelPairingLocked() {
        ++pairGen;
        joining = false;
        if (auto f = std::move(flow)) {
            flow.reset();
            f->cancel();
            std::thread([f] { f->cleanup(); }).detach();
        }
    }

    void lockLocked() {
        if (screen == Screen::Setup || screen == Screen::KeysReset || screen == Screen::Unlock) return;
        cancelPairingLocked();
        stopPollLocked();
        wipeSecretsLocked();
        code.clear();
        entry.clear();
        notice = Notice::None;
        gotoLocked(Screen::Unlock);
    }

    /// The vault cannot be opened: nothing can be recovered, by design.
    void keysLostLocked() {
        cancelPairingLocked();
        stopPollLocked();
        wipeSecretsLocked();
        vault.wipe();
        busy = false;
        gotoLocked(Screen::KeysReset);
    }

    // ---- unlock and setup -------------------------------------------------------------------------
    void doFinishSetup(std::string pass, std::string confirm) {
        {
            std::lock_guard<std::mutex> l(m);
            if (busy || screen != Screen::Setup) { secureZero(pass.data(), pass.size()); secureZero(confirm.data(), confirm.size()); return; }
            busy = true;
            setupError = SetupError::None;
        }
        fire();
        SetupError err = SetupError::None;
        Bytes newDek;
        auto cps = limits::decodeUtf8(pass);
        if (!cps || cps->size() < static_cast<std::size_t>(MinPassphraseChars)) err = SetupError::TooShort;
        else if (pass != confirm) err = SetupError::Mismatch;
        else {
            try { newDek = vault.create(pass, *d.rng); } catch (...) { err = SetupError::Failed; }
        }
        secureZero(pass.data(), pass.size());
        secureZero(confirm.data(), confirm.size());
        act([&] {
            busy = false;
            setupError = err;
            if (err != SetupError::None) return;
            dek = std::move(newDek);
            conv.reset();
            ++epoch;
            settings.setupDone = true;
            firstRun = false;
            saveSettingsLocked();
            gotoLocked(Screen::Pair);
        });
    }

    void doUnlock(std::string pass) {
        {
            std::lock_guard<std::mutex> l(m);
            if (busy || dek || screen != Screen::Unlock) { secureZero(pass.data(), pass.size()); return; }
            busy = true;
            unlockFailed = false;
        }
        fire();
        auto u = vault.unlock(pass);  // slow on purpose, so outside the lock
        secureZero(pass.data(), pass.size());
        if (u.result == Vault::Unlock::Lost) { act([&] { keysLostLocked(); }); return; }
        if (u.result == Vault::Unlock::WrongPassphrase) { act([&] { busy = false; unlockFailed = true; }); return; }
        std::optional<Conversation> loaded;
        bool lost = false;
        try {
            if (auto plain = vault.load(u.dek)) loaded = state::decode(*plain, *d.rng);
        } catch (const KeysLost&) { lost = true; } catch (const state::FormatError&) { lost = true; }
        act([&] {
            if (lost) { keysLostLocked(); return; }
            dek = std::move(u.dek);
            conv = std::move(loaded);
            ++epoch;
            verification = conv ? conv->verification : std::string();
            goneRounds = 0;
            connection = Connection::Online;
            retryAt.clear();
            busy = false;
            gotoLocked(!conv ? Screen::Pair : !conv->verified ? Screen::Verify : Screen::Chat);
        });
    }

    // ---- pairing ----------------------------------------------------------------------------------
    void beginPairingLocked(const std::string& raw, bool creator) {
        cancelPairingLocked();
        const std::uint64_t gen = ++pairGen;
        std::vector<std::string> relays = endpointsLocked();
        joining = !creator;
        std::thread([self = shared_from_this(), raw, creator, gen, relays] { self->runPairing(gen, raw, creator, relays); }).detach();
    }

    void runPairing(std::uint64_t gen, std::string raw, bool creator, std::vector<std::string> relays) {
        std::shared_ptr<PairingFlow> f;
        try {
            auto derived = pairing::derive(raw, d.pairingIterations);
            LocalMember me = LocalMember::create(*d.rng);
            auto poolCopy = pool;
            f = std::make_shared<PairingFlow>(relays, derived.room, derived.codeToken, me,
                                              [poolCopy](const std::string& ep) { return poolCopy->get(ep); },
                                              creator ? PairingWindowHours * 3'600'000LL : d.joinTimeoutMs, d.pairingTimings);
            {
                std::lock_guard<std::mutex> l(m);
                if (gen != pairGen) return;
                flow = f;
            }
            PairOutcome out = f->run();
            if (out.cancelled) return;
            if (out.success) completePairing(gen, derived, me, out, f);
            else { f->cleanup(); failPairing(gen, out.reason); }
        } catch (...) {
            if (f) f->cleanup();
            failPairing(gen, FailReason::Unreachable);
        }
    }

    void failPairing(std::uint64_t gen, FailReason reason) {
        act([&] {
            if (gen != pairGen) return;
            flow.reset();
            code.clear();
            joining = false;
            notice = reason == FailReason::Timeout ? Notice::Timeout : reason == FailReason::Unreachable ? Notice::Unreachable
                   : reason == FailReason::Tampered ? Notice::Tampered : Notice::Full;
            gotoLocked(Screen::Pair);
        });
    }

    void completePairing(std::uint64_t gen, const pairing::Derived& derived, const LocalMember& me, const PairOutcome& out,
                         const std::shared_ptr<PairingFlow>& f) {
        auto ik = hexDecode(out.peer.ik), ek = hexDecode(out.peer.ek);
        std::optional<handshake::Agreement> agreement;
        try {
            if (ik && ek) agreement = handshake::agree(me.keys, me.memberId, *ik, *ek, out.peer.memberId, derived.codeKey);
        } catch (const CryptoError&) {}  // a low-order or otherwise unusable key from the other side
        if (!agreement) { f->cleanup(); failPairing(gen, FailReason::Tampered); return; }

        Conversation c;
        c.room = derived.room;
        c.memberId = me.memberId;
        c.memberToken = *hexDecode(me.memberToken);
        c.endpoints = out.endpoints;
        c.ratchet = agreement->initiator ? Ratchet::forInitiator(agreement->sk, agreement->ad, me.keys.ek, *ek, *d.rng)
                                         : Ratchet::forResponder(agreement->sk, agreement->ad, me.keys.ek, *ek, *d.rng);
        c.verification = agreement->verification;
        bool saved = false, stale = false;
        act([&] {
            if (gen != pairGen) { stale = true; return; }
            conv = std::move(c);
            ++epoch;
            saved = persistLocked();
            flow.reset();
            code.clear();
            joining = false;
            if (!saved) {
                conv->wipe();
                conv.reset();
                ++epoch;
                notice = Notice::Storage;
                gotoLocked(Screen::Pair);
                return;
            }
            verification = conv->verification;
            gotoLocked(Screen::Verify);
        });
        if (stale) { c.wipe(); return; }
        if (!saved) f->cleanup();
    }

    void wipeConversation(Notice reason) {
        std::vector<std::string> relays;
        std::string room, token;
        {
            std::lock_guard<std::mutex> l(m);
            stopPollLocked();
            if (conv) { relays = conv->endpoints; room = conv->room; token = conv->memberTokenHex(); }
            if (conv) conv->wipe();
            conv.reset();
            ++epoch;
            verification.clear();
            code.clear();
            entry.clear();
            notice = reason;
            gotoLocked(Screen::Pair);
            persistLocked();
        }
        fire();
        if (!room.empty()) {
            auto poolCopy = pool;
            std::thread([poolCopy, relays, room, token] {  // best effort; the relay deletes idle rooms itself
                for (const auto& ep : relays) { try { poolCopy->get(ep)->del(room, token); } catch (...) {} }
            }).detach();
        }
    }

    // ---- polling ----------------------------------------------------------------------------------
    void updatePollingLocked() {
        const bool should = foreground && dek && conv && conv->verified &&
                            (screen == Screen::Chat || screen == Screen::Settings || screen == Screen::About);
        if (should && !pollRunning) {
            pollRunning = true;
            const std::uint64_t gen = ++pollGen;
            std::thread([self = shared_from_this(), gen] { self->pollLoop(gen); }).detach();
        } else if (!should && pollRunning) {
            stopPollLocked();
        }
    }
    void stopPollLocked() {
        if (!pollRunning) return;
        pollRunning = false;
        ++pollGen;
        pollCv.notify_all();
    }
    void wakePoll() {
        { std::lock_guard<std::mutex> l(m); pollWake = true; }
        pollCv.notify_all();
    }

    void pollLoop(std::uint64_t gen) {
        while (true) {
            { std::lock_guard<std::mutex> l(m); if (pollGen != gen) return; }
            bool healthy = false;
            try { healthy = cycle(); } catch (...) {}
            fire();
            std::unique_lock<std::mutex> l(m);
            if (pollGen != gen) return;
            pollWake = false;
            pollCv.wait_for(l, std::chrono::milliseconds(healthy ? d.pollMs : d.backoffMs), [&] { return pollGen != gen || pollWake; });
        }
    }

    bool inBackoffLocked(const std::string& ep) {
        auto it = retryAt.find(ep);
        return it != retryAt.end() && it->second > nowMs();
    }

    enum class Probe { Reached, Gone, Down };

    /// One round: send whatever is queued, then collect whatever is waiting. True if a relay answered.
    bool cycle() {
        std::vector<std::string> endpoints;
        std::uint64_t ep0;
        {
            std::lock_guard<std::mutex> l(m);
            if (!conv) return true;
            endpoints = conv->endpoints;
            ep0 = epoch;
        }
        flushOutbox(ep0);
        int reached = 0, gone = 0;
        for (const auto& ep : endpoints) {
            {
                std::lock_guard<std::mutex> l(m);
                if (epoch != ep0) return true;
                if (inBackoffLocked(ep)) { ++gone; continue; }
            }
            Probe p = pollEndpoint(ep0, ep);
            std::lock_guard<std::mutex> l(m);
            if (epoch != ep0) return true;
            if (p == Probe::Reached) { ++reached; retryAt.erase(ep); }
            else if (p == Probe::Gone) { ++gone; retryAt[ep] = nowMs() + d.goneRetryMs; }
        }
        std::lock_guard<std::mutex> l(m);
        if (epoch != ep0) return true;
        if (reached > 0) { goneRounds = 0; connection = Connection::Online; }
        else if (gone == static_cast<int>(endpoints.size())) { if (++goneRounds >= 3) connection = Connection::Gone; }
        else connection = Connection::Offline;
        return reached > 0;
    }

    void flushOutbox(std::uint64_t ep0) {
        while (true) {
            std::int64_t msgId;
            std::string text, room, token;
            std::vector<std::string> endpoints;
            {
                std::lock_guard<std::mutex> l(m);
                if (epoch != ep0 || !conv || conv->outbox.empty()) return;
                msgId = conv->outbox.front().msgId;
                text = wire::toBase64(conv->outbox.front().envelope);
                room = conv->room; token = conv->memberTokenHex(); endpoints = conv->endpoints;
            }
            bool sent = false;
            for (const auto& ep : endpoints) {
                { std::lock_guard<std::mutex> l(m); if (epoch != ep0) return; if (inBackoffLocked(ep)) continue; }
                auto reply = pool->get(ep)->post(room, token, text);
                if (reply.ok()) { sent = true; break; }
            }
            if (!sent) return;
            {
                std::lock_guard<std::mutex> l(m);
                if (epoch != ep0 || !conv || conv->outbox.empty() || conv->outbox.front().msgId != msgId) return;
                secureZero(conv->outbox.front().envelope.data(), conv->outbox.front().envelope.size());
                conv->outbox.erase(conv->outbox.begin());
                if (auto* sm = conv->findMessage(msgId)) sm->delivery = Conversation::DeliverySent;
                persistLocked();
            }
            fire();
        }
    }

    Probe pollEndpoint(std::uint64_t ep0, const std::string& ep) {
        auto client = pool->get(ep);
        std::string room, token;
        std::int64_t after;
        {
            std::lock_guard<std::mutex> l(m);
            if (epoch != ep0 || !conv) return Probe::Down;
            room = conv->room; token = conv->memberTokenHex(); after = conv->lastId(ep);
        }
        Probe probe = Probe::Down;
        for (int page = 0; page < MaxPages; ++page) {
            auto reply = client->fetch(room, token, after);
            if (reply.unreachable()) return probe;
            if (reply.rejected())
                return (reply.status == 404 || reply.status == 401 || reply.status == 410) ? Probe::Gone : Probe::Reached;
            probe = Probe::Reached;
            std::int64_t top = after;
            {
                std::lock_guard<std::mutex> l(m);
                if (epoch != ep0 || !conv) return probe;
                for (const auto& msg : reply.value.messages) {
                    if (msg.id <= after) continue;
                    top = std::max(top, msg.id);
                    receiveLocked(msg.env);
                }
                if (top > after) {
                    conv->setLastId(ep, top);
                    // Save the advanced ratchet first; only then let the relay forget the messages.
                    if (!persistLocked()) return probe;
                }
            }
            if (top > after) {
                fire();
                client->ack(room, token, top);
                after = top;
            }
            if (!reply.value.more) return probe;
        }
        return probe;
    }

    /// Decrypts one envelope. Anything that does not verify (forged, replayed, damaged, duplicate) is dropped.
    void receiveLocked(const std::string& envText) {
        auto env = wire::fromBase64(envText);
        if (!env) return;
        auto frame = conv->ratchet->decrypt(*env);
        if (!frame) return;
        auto text = wire::decodeText(*frame);
        if (!text) return;
        addMessageLocked({conv->nextMsgId++, *text, false, Conversation::DeliveryNone});
    }
    void addMessageLocked(StoredMessage msg) {
        conv->messages.push_back(std::move(msg));
        while (conv->messages.size() > Conversation::MaxMessages) conv->messages.erase(conv->messages.begin());
    }

    void doSend(std::string raw) {
        bool queued = false;
        {
            std::lock_guard<std::mutex> l(m);
            std::string text = limits::trim(raw);
            if (!conv || !conv->verified || text.empty() || !limits::fits(text, d.segmenter)) return;
            auto frame = wire::encodeText(text, d.segmenter);
            if (!frame) return;
            Bytes envelope;
            try { envelope = conv->ratchet->encrypt(*frame); } catch (const CryptoError&) { return; }
            std::int64_t id = conv->nextMsgId++;
            addMessageLocked({id, text, true, Conversation::DeliveryQueued});
            conv->outbox.push_back({id, std::move(envelope)});
            persistLocked();  // the ratchet has moved on: save before anything is sent
            queued = true;
        }
        if (queued) { fire(); wakePoll(); }
    }

    Snapshot snapshotNow() const {
        std::lock_guard<std::mutex> l(m);
        Snapshot s;
        s.screen = screen; s.notice = notice; s.code = code; s.entry = entry; s.joining = joining; s.busy = busy;
        s.unlockFailed = unlockFailed; s.setupError = setupError; s.firstRun = firstRun; s.connection = connection;
        s.verification = verification; s.settings = settings;
        if (conv)
            for (const auto& sm : conv->messages)
                s.messages.push_back({sm.id, sm.text, sm.mine,
                                      sm.delivery == Conversation::DeliveryQueued ? Delivery::Queued
                                      : sm.delivery == Conversation::DeliverySent ? Delivery::Sent : Delivery::None});
        return s;
    }

    ~Core() {
        { std::lock_guard<std::mutex> l(wm); quit = true; }
        wcv.notify_all();
        if (conv) conv->wipe();
        if (dek) wipe(*dek);
    }
};

// ---- Engine: thin forwarding layer ------------------------------------------------------------------

Engine::Engine(Deps deps) : core_(std::make_shared<Core>(std::move(deps))) { core_->start(); }

Engine::~Engine() {
    auto& c = *core_;
    { std::lock_guard<std::mutex> l(c.cbm); c.cb = nullptr; }
    {
        std::lock_guard<std::mutex> l(c.m);
        c.cancelPairingLocked();
        c.stopPollLocked();
        c.wipeSecretsLocked();
    }
    { std::lock_guard<std::mutex> l(c.wm); c.quit = true; }
    c.wcv.notify_all();
}

void Engine::setOnChange(std::function<void()> callback) { std::lock_guard<std::mutex> l(core_->cbm); core_->cb = std::move(callback); }
Snapshot Engine::snapshot() const { return core_->snapshotNow(); }

void Engine::finishSetup(std::string p, std::string c) {
    auto core = core_;
    core->post([core, p = std::move(p), c = std::move(c)]() mutable { core->doFinishSetup(std::move(p), std::move(c)); });
}
void Engine::unlock(std::string p) {
    auto core = core_;
    core->post([core, p = std::move(p)]() mutable { core->doUnlock(std::move(p)); });
}
void Engine::lock() { core_->act([&] { core_->lockLocked(); }); }
void Engine::pairAgain() {
    core_->act([&] {
        if (core_->screen != Screen::KeysReset) return;
        core_->notice = Notice::None;
        core_->setupError = SetupError::None;
        core_->firstRun = !core_->settings.setupDone;
        core_->gotoLocked(Screen::Setup);
    });
}
void Engine::setForeground(bool on) { core_->act([&] { core_->foreground = on; core_->updatePollingLocked(); }); }

bool Engine::saveRelay(const std::string& input) {
    auto v = relay_address::normalise(input);
    if (!v) return false;
    core_->act([&] { core_->settings.relayUrl = *v; core_->saveSettingsLocked(); });
    return true;
}
void Engine::resetRelay() { core_->act([&] { core_->settings.relayUrl = std::string(DefaultRelayUrl); core_->saveSettingsLocked(); }); }
bool Engine::saveFallback(const std::string& input) {
    auto v = relay_address::normalise(input);
    if (!v) return false;
    core_->act([&] { core_->settings.fallbackUrl = *v; core_->saveSettingsLocked(); });
    return true;
}
void Engine::resetFallback() { core_->act([&] { core_->settings.fallbackUrl = std::string(FallbackRelayUrl); core_->saveSettingsLocked(); }); }
void Engine::enableFallback(bool on) { core_->act([&] { core_->settings.fallbackEnabled = on; core_->saveSettingsLocked(); }); }
void Engine::chooseAutoLock(AutoLock v) { core_->act([&] { core_->settings.autoLock = v; core_->saveSettingsLocked(); }); }

void Engine::back() {
    core_->act([&] {
        auto& c = *core_;
        switch (c.screen) {
            case Screen::Settings: c.gotoLocked(Screen::Chat); break;
            case Screen::About: c.gotoLocked(Screen::Settings); break;
            case Screen::EnterCode: c.gotoLocked(Screen::Pair); break;
            case Screen::Waiting: c.cancelPairingLocked(); c.code.clear(); c.gotoLocked(Screen::Pair); break;
            default: break;
        }
    });
}
void Engine::open(Screen target) {
    core_->act([&] {
        // only the screens a person can open from the chat
        if (target == Screen::Settings || target == Screen::About || target == Screen::Chat) core_->gotoLocked(target);
    });
}

void Engine::startWaiting() {
    core_->act([&] {
        auto& c = *core_;
        if (!c.dek || c.screen != Screen::Pair) return;
        c.notice = Notice::None;
        std::string raw = pairing::generate(*c.d.rng);
        c.code = pairing::format(raw);
        c.gotoLocked(Screen::Waiting);
        c.beginPairingLocked(raw, true);
        c.joining = false;
    });
}
void Engine::openEnterCode() {
    core_->act([&] {
        if (core_->screen != Screen::Pair) return;
        core_->notice = Notice::None;
        core_->entry.clear();
        core_->gotoLocked(Screen::EnterCode);
    });
}
void Engine::onEntry(const std::string& input) { core_->act([&] { core_->entry = pairing::formatPartial(input); }); }
void Engine::join() {
    core_->act([&] {
        auto& c = *core_;
        if (!c.dek || c.screen != Screen::EnterCode || !pairing::isValid(c.entry)) return;
        std::string raw = c.entry;
        c.entry.clear();
        c.notice = Notice::None;
        c.code.clear();
        c.gotoLocked(Screen::Waiting);
        c.beginPairingLocked(raw, false);
    });
}
void Engine::cancelWaiting() {
    core_->act([&] {
        if (core_->screen != Screen::Waiting) return;
        core_->cancelPairingLocked();
        core_->code.clear();
        core_->gotoLocked(Screen::Pair);
    });
}
void Engine::verifyMatches() {
    auto core = core_;
    core->act([&] {
        if (!core->conv || core->screen != Screen::Verify) return;
        core->conv->verified = true;
        core->gotoLocked(Screen::Chat);
        core->persistLocked();
    });
}
void Engine::verifyMismatch() { if (snapshot().screen == Screen::Verify) core_->wipeConversation(Notice::Mismatch); }
void Engine::forgetConversation() { core_->wipeConversation(Notice::Forgotten); }

std::string Engine::clamp(const std::string& draft) const { return limits::clamp(draft, core_->d.segmenter); }
std::size_t Engine::graphemeCount(const std::string& text) const { return limits::graphemeCount(text, core_->d.segmenter); }
void Engine::send(const std::string& text) {
    auto core = core_;
    core->post([core, text] { core->doSend(text); });
}

}  // namespace tc
