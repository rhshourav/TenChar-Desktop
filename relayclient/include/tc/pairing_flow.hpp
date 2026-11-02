#pragma once
// The relay side of pairing, run on every configured relay at once (a port of PairingFlow.kt).
//
//  1. hello: join the room with a commitment to this device's public keys.
//  2. wait until the other device's commitment is there too (two members).
//  3. reveal this device's keys. The relay refuses a reveal before both commitments exist.
//  4. wait for the other device's keys and check them against its commitment.
//
// A relay is not trusted. If two relays both complete they must agree on the other
// device's keys, so one dishonest relay cannot swap them unnoticed.
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tc/handshake.hpp"
#include "tc/relay.hpp"

namespace tc {

struct PeerKeys {
    std::string memberId, ik, ek;  // hex
};
enum class FailReason { Timeout, Unreachable, Tampered, Full };

struct PairOutcome {
    bool success = false;
    bool cancelled = false;
    PeerKeys peer;                      // success only
    std::vector<std::string> endpoints; // the relays that carry the conversation: every relay this device joined
    FailReason reason = FailReason::Timeout;  // failure only
};

struct PairingTimings {
    std::int64_t pollMs = 2000, retryMs = 3000, helloBudgetMs = 60000, graceMs = 15000;
};

using ClientFor = std::function<std::shared_ptr<RelayClient>(const std::string& endpoint)>;

class PairingFlow {
public:
    PairingFlow(std::vector<std::string> endpoints, std::string room, std::string codeToken, LocalMember me,
                ClientFor clientFor, std::int64_t timeoutMs, PairingTimings timings = {});

    /// Blocks until pairing succeeds, fails, times out or is cancelled. A slow relay never holds up the
    /// result: its worker is left to finish on its own.
    PairOutcome run();
    void cancel();
    /// Deletes the rooms this device joined. Best effort; the relay deletes unused rooms after 24 hours anyway.
    void cleanup();

    struct Shared;  // implementation detail, public only so the worker function can name it

private:
    std::shared_ptr<Shared> s_;
};

}  // namespace tc
