#pragma once
// Everything the app remembers about the one conversation it can hold, and the
// plaintext file format the vault encrypts. The format is the phone's StateCodec
// after its version-2 change: big-endian integers, and strings as a u16 byte
// length followed by STANDARD UTF-8 (not Java's modified UTF-8).
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tc/bytes.hpp"
#include "tc/ratchet.hpp"

namespace tc {

struct StoredMessage {
    std::int64_t id = 0;
    std::string text;  // UTF-8
    bool mine = false;
    int delivery = 0;  // Conversation::DeliveryNone / Queued / Sent
};

/// An envelope that is already encrypted (the ratchet has moved on) but not yet accepted by a relay.
struct OutboxItem {
    std::int64_t msgId = 0;
    Bytes envelope;  // 135 bytes
};

struct Conversation {
    static constexpr int DeliveryNone = 0, DeliveryQueued = 1, DeliverySent = 2;
    static constexpr std::size_t MaxMessages = 1000, MaxEndpoints = 4, MaxOutbox = 2000;

    std::string room;      // 32 hex
    std::string memberId;  // 32 hex
    Bytes memberToken;     // 32 raw bytes; the relay only ever sees its hash until the first request
    std::vector<std::string> endpoints;  // relays that carry it, in order of preference
    std::optional<Ratchet> ratchet;
    std::string verification;  // "dddd dddd"
    bool verified = false;
    std::vector<std::pair<std::string, std::int64_t>> lastIds;  // highest relay message id handled, per relay
    std::vector<OutboxItem> outbox;
    std::vector<StoredMessage> messages;
    std::int64_t nextMsgId = 1;

    std::string memberTokenHex() const { return hexEncode(memberToken); }
    std::int64_t lastId(const std::string& endpoint) const;
    void setLastId(const std::string& endpoint, std::int64_t id);
    StoredMessage* findMessage(std::int64_t id);
    void wipe();
};

namespace state {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// A null conversation encodes "no conversation yet".
Bytes encode(const Conversation* conversation);
/// Empty optional means "no conversation yet". Throws FormatError for anything malformed.
std::optional<Conversation> decode(ByteView data, RandomSource& rng);

// Exposed for tests.
void writeString(Bytes& out, std::string_view s, std::size_t maxBytes);

}  // namespace state
}  // namespace tc
