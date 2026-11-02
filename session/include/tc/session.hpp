#pragma once
// The one place that holds application state: a Qt-free port of AppViewModel.kt. Secrets live in the
// conversation and the data key only while unlocked; lock() overwrites them. Everything on disk goes
// through the Vault. The UI never touches a secret: it reads a Snapshot and calls the methods below.
//
// Threads: public methods may be called from any thread and return quickly. Slow work (passphrase
// stretching, pairing, polling, sending) runs on the engine's own threads. `onChange` is called from
// those threads, never while an engine lock is held.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tc/limits.hpp"
#include "tc/pairing_flow.hpp"
#include "tc/relay.hpp"
#include "tc/vault.hpp"

namespace tc {

enum class Screen { Setup, Unlock, Pair, EnterCode, Waiting, Verify, Chat, Settings, About, KeysReset };
enum class AutoLock { Immediately, Seconds30, Minutes5 };
enum class Notice { None, Mismatch, Forgotten, Timeout, Unreachable, Tampered, Full, Storage };
enum class Connection { Online, Offline, Gone };
enum class Delivery { None, Queued, Sent };
enum class SetupError { None, TooShort, Mismatch, Failed };

std::int64_t autoLockMillis(AutoLock a);

struct ChatMessage {
    std::int64_t id = 0;
    std::string text;
    bool mine = false;
    Delivery delivery = Delivery::None;
};

struct Settings {
    std::string relayUrl{DefaultRelayUrl};
    std::string fallbackUrl{FallbackRelayUrl};
    bool fallbackEnabled = true;
    AutoLock autoLock = AutoLock::Seconds30;
    bool setupDone = false;
};

struct Snapshot {
    Screen screen = Screen::Unlock;
    Notice notice = Notice::None;
    std::string code;          // formatted XXXX-XXXX-XXXX-XXXX while waiting, otherwise empty
    std::string entry;         // what has been typed on the enter-code screen
    bool joining = false;
    bool busy = false;         // stretching the passphrase
    bool unlockFailed = false; // wrong passphrase
    SetupError setupError = SetupError::None;
    bool firstRun = false;     // Setup also asks for relays and auto-lock only the first time
    Connection connection = Connection::Online;
    std::string verification;  // "dddd dddd"
    std::vector<ChatMessage> messages;
    Settings settings;
};

class Engine {
public:
    struct Deps {
        std::filesystem::path dataDir;
        HttpTransport* transport = nullptr;
        RandomSource* rng = nullptr;
        limits::Segmenter segmenter = limits::codepointClusters;
        // Everything below exists so tests can run in milliseconds; the app uses the defaults.
        int vaultIterations = Pbkdf2Iterations;
        int pairingIterations = Pbkdf2Iterations;
        PairingTimings pairingTimings{};
        std::int64_t joinTimeoutMs = 120'000;
        std::int64_t pollMs = 3'000, backoffMs = 10'000, goneRetryMs = 10 * 60'000;
    };

    explicit Engine(Deps deps);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void setOnChange(std::function<void()> callback);
    Snapshot snapshot() const;

    // setup, unlock, lock
    void finishSetup(std::string passphrase, std::string confirm);  // creates the vault, then goes to Pair
    void unlock(std::string passphrase);
    void lock();
    void pairAgain();  // after "keys reset": make a new vault
    void setForeground(bool inForeground);

    // settings (return false and change nothing if an address is not acceptable)
    bool saveRelay(const std::string& input);
    void resetRelay();
    bool saveFallback(const std::string& input);
    void resetFallback();
    void enableFallback(bool on);
    void chooseAutoLock(AutoLock value);

    // navigation
    void back();
    void open(Screen target);

    // pairing
    void startWaiting();
    void openEnterCode();
    void onEntry(const std::string& input);
    void join();
    void cancelWaiting();
    void verifyMatches();
    void verifyMismatch();
    void forgetConversation();

    // messaging
    std::string clamp(const std::string& draft) const;
    std::size_t graphemeCount(const std::string& text) const;
    void send(const std::string& text);

private:
    struct Core;
    std::shared_ptr<Core> core_;
};

}  // namespace tc
