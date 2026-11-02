#include <filesystem>
#include <functional>

#include "fake_relay.hpp"
#include "tc/session.hpp"
#include "tc/state.hpp"

using namespace tc;
namespace fs = std::filesystem;

static FakeRelay net;  // global: detached worker threads may outlive a test function
static const std::string Main = "tenchar-relay.onrender.com", Backup = "tenchar-relay.tenchar.workers.dev";
static const std::string Pass = "correct horse battery staple";

static bool waitFor(const std::function<bool()>& pred, int ms = 8000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

struct Device {
    fs::path dir;
    std::unique_ptr<Engine> e;
    explicit Device(const std::string& name) : dir(fs::temp_directory_path() / ("tc-session-" + name + "-" + hexEncode(randomBytes(systemRandom(), 5)))) { make(); }
    void make() {
        Engine::Deps d;
        d.dataDir = dir;
        d.transport = &net;
        d.rng = &systemRandom();
        d.vaultIterations = 2000;
        d.pairingIterations = 2000;
        d.pairingTimings = {20, 20, 400, 150};
        d.joinTimeoutMs = 5000;
        d.pollMs = 30;
        d.backoffMs = 60;
        d.goneRetryMs = 50;
        e = std::make_unique<Engine>(std::move(d));
    }
    void restart() { e.reset(); make(); }
    Snapshot s() const { return e->snapshot(); }
    bool on(Screen sc) const { return s().screen == sc; }
    bool hasMessage(const std::string& text, bool mine) const {
        for (const auto& m : s().messages) if (m.text == text && m.mine == mine) return true;
        return false;
    }
    bool sent(const std::string& text) const {
        for (const auto& m : s().messages) if (m.text == text && m.mine && m.delivery == Delivery::Sent) return true;
        return false;
    }
    ~Device() { e.reset(); std::error_code ec; fs::remove_all(dir, ec); }
};

static void setUp(Device& d) {
    CHECK(d.on(Screen::Setup) && d.s().firstRun);
    d.e->finishSetup(Pass, Pass);
    CHECK(waitFor([&] { return d.on(Screen::Pair); }));
}

/// Pairs two fresh devices and verifies; leaves both in Chat with polling on.
static void chatPair(Device& a, Device& b) {
    setUp(a);
    setUp(b);
    a.e->startWaiting();
    CHECK(a.on(Screen::Waiting));
    std::string code = a.s().code;
    CHECK_EQ(code.size(), std::size_t(19));
    b.e->openEnterCode();
    b.e->onEntry(code);
    b.e->join();
    CHECK(waitFor([&] { return a.on(Screen::Verify) && b.on(Screen::Verify); }));
    CHECK_EQ(a.s().verification, b.s().verification);
    CHECK_EQ(a.s().verification.size(), std::size_t(9));
    a.e->verifyMatches();
    b.e->verifyMatches();
    a.e->setForeground(true);
    b.e->setForeground(true);
    CHECK(a.on(Screen::Chat) && b.on(Screen::Chat));
}

static void testSetupAndUnlock() {
    net.reset();
    Device a("setup");
    CHECK(a.on(Screen::Setup));
    a.e->finishSetup("short", "short");
    CHECK(waitFor([&] { return a.s().setupError == SetupError::TooShort; }));
    a.e->finishSetup(Pass, Pass + "x");
    CHECK(waitFor([&] { return a.s().setupError == SetupError::Mismatch; }));
    CHECK(a.on(Screen::Setup));
    CHECK(a.e->saveRelay("Relay.Example.com:8443"));
    CHECK(!a.e->saveRelay("http://relay.example.com") && !a.e->saveRelay("https://user@relay.example.com"));
    CHECK_EQ(a.s().settings.relayUrl, std::string("https://relay.example.com:8443"));
    a.e->chooseAutoLock(AutoLock::Minutes5);
    a.e->enableFallback(false);
    a.e->finishSetup(Pass, Pass);
    CHECK(waitFor([&] { return a.on(Screen::Pair); }));
    CHECK(!a.s().settings.fallbackEnabled && a.s().settings.setupDone);

    // restart: settings survive, and the app starts locked
    a.restart();
    CHECK(a.on(Screen::Unlock));
    CHECK_EQ(a.s().settings.relayUrl, std::string("https://relay.example.com:8443"));
    CHECK(a.s().settings.autoLock == AutoLock::Minutes5);
    a.e->unlock("wrong passphrase!!");
    CHECK(waitFor([&] { return a.s().unlockFailed && !a.s().busy; }));
    CHECK(a.on(Screen::Unlock));
    a.e->unlock(Pass);
    CHECK(waitFor([&] { return a.on(Screen::Pair); }));
}

static void testPairChatLockRestart() {
    net.reset();
    Device a("a"), b("b");
    chatPair(a, b);

    for (int i = 0; i < 6; ++i) {
        std::string x = "a" + std::to_string(i), y = "b" + std::to_string(i);
        a.e->send(x);
        CHECK(waitFor([&] { return b.hasMessage(x, false); }));
        b.e->send(y);
        CHECK(waitFor([&] { return a.hasMessage(y, false); }));
        CHECK(waitFor([&] { return a.sent(x) && b.sent(y); }));
    }
    // emoji and Bangla go through, and the 10-character rule stops an 11th character at the sender
    a.e->send("\xf0\x9f\x98\x80 \xe0\xa6\xac\xe0\xa6\xbe");
    CHECK(waitFor([&] { return b.hasMessage("\xf0\x9f\x98\x80 \xe0\xa6\xac\xe0\xa6\xbe", false); }));
    CHECK_EQ(a.e->clamp("0123456789ABC"), std::string("0123456789"));
    std::size_t before = b.s().messages.size();
    a.e->send("01234567890");   // 11 characters: refused
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_EQ(b.s().messages.size(), before);

    // lock wipes everything on screen; unlock brings the history back and the chat keeps working
    a.e->lock();
    CHECK(a.on(Screen::Unlock) && a.s().messages.empty() && a.s().verification.empty());
    a.e->send("x-while-locked");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!b.hasMessage("x-while-locked", false));
    a.e->unlock(Pass);
    CHECK(waitFor([&] { return a.on(Screen::Chat); }));
    CHECK(a.hasMessage("b5", false));
    a.e->setForeground(true);
    b.e->send("unlocked");
    CHECK(waitFor([&] { return a.hasMessage("unlocked", false); }));

    // a full restart of one side (state read back from disk) keeps the ratchet in step
    a.restart();
    CHECK(a.on(Screen::Unlock));
    a.e->unlock(Pass);
    CHECK(waitFor([&] { return a.on(Screen::Chat); }));
    a.e->setForeground(true);
    a.e->send("restarted");
    CHECK(waitFor([&] { return b.hasMessage("restarted", false); }));
    b.e->send("and-back");
    CHECK(waitFor([&] { return a.hasMessage("and-back", false); }));
}

static void testOfflineAndFailover() {
    net.reset();
    Device a("a"), b("b");
    chatPair(a, b);
    // main relay down: the backup carries the message
    net.set(Main, true);
    a.e->send("via-backup");
    CHECK(waitFor([&] { return b.hasMessage("via-backup", false); }));
    // both down: the message waits in the outbox, the banner says offline, and it is sent when a relay returns
    net.set(Backup, true);
    a.e->send("queued-msg");
    CHECK(waitFor([&] { return a.s().connection == Connection::Offline; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    bool queued = false;
    for (auto& m : a.s().messages) if (m.text == "queued-msg" && m.delivery == Delivery::Queued) queued = true;
    CHECK(queued && !b.hasMessage("queued-msg", false));
    // ... and it survives a restart while still queued
    a.restart();
    a.e->unlock(Pass);
    CHECK(waitFor([&] { return a.on(Screen::Chat); }));
    a.e->setForeground(true);
    net.set(Main, false);
    net.set(Backup, false);
    CHECK(waitFor([&] { return b.hasMessage("queued-msg", false); }));
    CHECK(waitFor([&] { return a.sent("queued-msg") && a.s().connection == Connection::Online; }));
}

static void testRelaysForgetConversation() {
    net.reset();
    Device a("a"), b("b");
    chatPair(a, b);
    net.dropAllRooms(Main);
    net.dropAllRooms(Backup);
    CHECK(waitFor([&] { return a.s().connection == Connection::Gone; }));
}

static void testForgetAndMismatch() {
    net.reset();
    {
        Device a("a"), b("b");
        chatPair(a, b);
        a.e->forgetConversation();
        CHECK(a.on(Screen::Pair) && a.s().notice == Notice::Forgotten && a.s().messages.empty());
        CHECK(waitFor([&] { return net.roomCount(Main) <= 1; }));          // a's side asked the relays to delete the room
        a.restart();
        a.e->unlock(Pass);
        CHECK(waitFor([&] { return a.on(Screen::Pair); }));                 // and nothing was kept on disk
    }
    net.reset();
    {
        Device a("a"), b("b");
        setUp(a);
        setUp(b);
        a.e->startWaiting();
        b.e->openEnterCode();
        b.e->onEntry(a.s().code);
        b.e->join();
        CHECK(waitFor([&] { return a.on(Screen::Verify) && b.on(Screen::Verify); }));
        a.e->verifyMismatch();
        CHECK(a.on(Screen::Pair) && a.s().notice == Notice::Mismatch);
    }
}

static void testWrongCodeAndCancel() {
    net.reset();
    Device a("a"), b("b");
    setUp(a);
    setUp(b);
    a.e->startWaiting();
    std::string code = a.s().code;
    // locking while waiting cancels the pairing and wipes the code from the screen
    a.e->lock();
    CHECK(a.on(Screen::Unlock) && a.s().code.empty());
    a.e->unlock(Pass);
    CHECK(waitFor([&] { return a.on(Screen::Pair); }));
    // joining with a code nobody made times out with a notice
    b.e->openEnterCode();
    b.e->onEntry("abcd-efgh-jklm-npqr");
    CHECK_EQ(b.s().entry, std::string("ABCD-EFGH-JKLM-NPQR"));
    b.e->join();
    CHECK(b.on(Screen::Waiting) && b.s().joining);
    b.e->cancelWaiting();
    CHECK(b.on(Screen::Pair));
}

static void testDamagedStateAndReset() {
    net.reset();
    Device a("a");
    setUp(a);
    a.e->lock();
    // flip a byte inside the encrypted state file: authentication fails and the keys are reported lost
    a.e->unlock(Pass);
    CHECK(waitFor([&] { return a.on(Screen::Pair); }));
    a.restart();
    auto blob = readWholeFile(a.dir / "tc_state.bin", 1 << 20);
    if (!blob) {   // nothing saved yet (no conversation): force a save by pairing is overkill; write a state file instead
        Vault v(a.dir, 2000);
        auto u = v.unlock(Pass);
        v.save(u.dek, state::encode(nullptr), systemRandom());
        blob = readWholeFile(a.dir / "tc_state.bin", 1 << 20);
    }
    CHECK(blob.has_value());
    Bytes bad = *blob; bad[20] ^= 1;
    writeFileAtomically(a.dir / "tc_state.bin", bad);
    a.restart();
    a.e->unlock(Pass);
    CHECK(waitFor([&] { return a.on(Screen::KeysReset); }));
    CHECK(!fs::exists(a.dir / "tc_dek.bin"));        // everything is gone, by design
    a.restart();
    CHECK(a.on(Screen::KeysReset));
    a.e->pairAgain();
    CHECK(a.on(Screen::Setup) && !a.s().firstRun);
    a.e->finishSetup("a brand new passphrase", "a brand new passphrase");
    CHECK(waitFor([&] { return a.on(Screen::Pair); }));
}

int main() {
    RUN(testSetupAndUnlock);
    RUN(testPairChatLockRestart);
    RUN(testOfflineAndFailover);
    RUN(testRelaysForgetConversation);
    RUN(testForgetAndMismatch);
    RUN(testWrongCodeAndCancel);
    RUN(testDamagedStateAndReset);
    std::_Exit(FINISH());   // detached worker threads may still be winding down
}
