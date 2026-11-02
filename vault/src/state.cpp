#include "tc/state.hpp"

#include "tc/constants.hpp"
#include "tc/limits.hpp"

namespace tc {

std::int64_t Conversation::lastId(const std::string& ep) const {
    for (const auto& [k, v] : lastIds)
        if (k == ep) return v;
    return 0;
}
void Conversation::setLastId(const std::string& ep, std::int64_t id) {
    for (auto& [k, v] : lastIds)
        if (k == ep) { v = id; return; }
    lastIds.emplace_back(ep, id);
}
StoredMessage* Conversation::findMessage(std::int64_t id) {
    for (auto& m : messages)
        if (m.id == id) return &m;
    return nullptr;
}
void Conversation::wipe() {
    if (ratchet) ratchet->wipe();
    for (auto& o : outbox) secureZero(o.envelope.data(), o.envelope.size());
    secureZero(memberToken.data(), memberToken.size());
}

namespace state {
namespace {
constexpr std::uint32_t Magic = 0x54435331;  // "TCS1"
constexpr std::uint8_t Version = 2;
constexpr std::size_t MaxRatchetBytes = 64 * 1024;
constexpr std::size_t MaxEndpointBytes = 255;

void u8(Bytes& o, std::uint8_t v) { o.push_back(v); }
void u16(Bytes& o, std::uint32_t v) { o.push_back(static_cast<std::uint8_t>(v >> 8)); o.push_back(static_cast<std::uint8_t>(v)); }
void u32(Bytes& o, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<std::uint8_t>(v >> s)); }
void i64(Bytes& o, std::int64_t v) { for (int s = 56; s >= 0; s -= 8) o.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) >> s)); }
void raw(Bytes& o, ByteView v) { o.insert(o.end(), v.begin(), v.end()); }

struct Reader {
    ByteView b;
    std::size_t i = 0;
    ByteView take(std::size_t n) {
        if (b.size() - i < n) throw FormatError("truncated state");
        ByteView v = b.subspan(i, n);
        i += n;
        return v;
    }
    std::uint8_t u8() { return take(1)[0]; }
    std::uint32_t u16() { auto v = take(2); return (std::uint32_t(v[0]) << 8) | v[1]; }
    std::uint32_t u32() { auto v = take(4); return (std::uint32_t(v[0]) << 24) | (std::uint32_t(v[1]) << 16) | (std::uint32_t(v[2]) << 8) | v[3]; }
    std::int64_t i64() { auto v = take(8); std::uint64_t x = 0; for (auto c : v) x = (x << 8) | c; return static_cast<std::int64_t>(x); }
    std::string str(std::size_t maxBytes) {
        std::size_t n = u16();
        if (n > maxBytes) throw FormatError("string too long");
        auto v = take(n);
        std::string s = toString(v);
        if (!limits::isValidUtf8(s)) throw FormatError("invalid UTF-8");
        return s;
    }
    bool boolean() { std::uint8_t v = u8(); if (v > 1) throw FormatError("bad boolean"); return v == 1; }
    /// A count that must fit in a signed 32-bit int and not exceed `max`.
    std::size_t count(std::size_t max, const char* what) {
        std::uint32_t n = u32();
        if (n > max) throw FormatError(what);
        return n;
    }
};

bool isVerification(const std::string& s) {
    if (s.size() != 9 || s[4] != ' ') return false;
    for (std::size_t i = 0; i < 9; ++i)
        if (i != 4 && (s[i] < '0' || s[i] > '9')) return false;
    return true;
}
}  // namespace

void writeString(Bytes& out, std::string_view s, std::size_t maxBytes) {
    if (s.size() > maxBytes || s.size() > 0xFFFF) throw FormatError("string too long");
    u16(out, static_cast<std::uint32_t>(s.size()));
    raw(out, asBytes(s));
}

Bytes encode(const Conversation* c) {
    Bytes out;
    u32(out, Magic);
    u8(out, Version);
    u8(out, c ? 1 : 0);
    if (!c) return out;
    auto room = hexDecode(c->room), id = hexDecode(c->memberId);
    if (!room || room->size() != 16 || !id || id->size() != 16 || c->memberToken.size() != 32 || !c->ratchet)
        throw FormatError("incomplete conversation");
    raw(out, *room);
    raw(out, *id);
    raw(out, c->memberToken);
    u8(out, static_cast<std::uint8_t>(c->endpoints.size()));
    for (const auto& e : c->endpoints) writeString(out, e, MaxEndpointBytes);
    u8(out, c->verified ? 1 : 0);
    writeString(out, c->verification, 9);
    Bytes ratchet = c->ratchet->serialize();
    u32(out, static_cast<std::uint32_t>(ratchet.size()));
    raw(out, ratchet);
    u32(out, static_cast<std::uint32_t>(c->lastIds.size()));
    for (const auto& [k, v] : c->lastIds) { writeString(out, k, MaxEndpointBytes); i64(out, v); }
    u32(out, static_cast<std::uint32_t>(c->outbox.size()));
    for (const auto& o : c->outbox) {
        if (o.envelope.size() != static_cast<std::size_t>(EnvelopeBytes)) throw FormatError("bad outbox envelope");
        i64(out, o.msgId);
        raw(out, o.envelope);
    }
    u32(out, static_cast<std::uint32_t>(c->messages.size()));
    for (const auto& m : c->messages) {
        i64(out, m.id);
        writeString(out, m.text, MaxUtf8Bytes);
        u8(out, m.mine ? 1 : 0);
        u8(out, static_cast<std::uint8_t>(m.delivery));
    }
    i64(out, c->nextMsgId);
    return out;
}

std::optional<Conversation> decode(ByteView data, RandomSource& rng) {
    Reader in{data};
    if (in.u32() != Magic) throw FormatError("not a TenChar state file");
    if (in.u8() != Version) throw FormatError("unknown state version");
    if (!in.boolean()) {
        if (in.i != data.size()) throw FormatError("trailing bytes");
        return std::nullopt;
    }
    Conversation c;
    c.room = hexEncode(in.take(16));
    c.memberId = hexEncode(in.take(16));
    auto tok = in.take(32);
    c.memberToken = Bytes(tok.begin(), tok.end());
    std::size_t endpoints = in.u8();
    if (endpoints < 1 || endpoints > Conversation::MaxEndpoints) throw FormatError("bad endpoint count");
    for (std::size_t i = 0; i < endpoints; ++i) c.endpoints.push_back(in.str(MaxEndpointBytes));
    c.verified = in.boolean();
    c.verification = in.str(9);
    if (!isVerification(c.verification)) throw FormatError("bad verification number");
    std::size_t ratchetSize = in.u32();
    if (ratchetSize < 1 || ratchetSize > MaxRatchetBytes) throw FormatError("bad ratchet size");
    try {
        c.ratchet = Ratchet::deserialize(in.take(ratchetSize), rng);
    } catch (const StateError& e) {
        throw FormatError(e.what());
    }
    std::size_t ids = in.count(Conversation::MaxEndpoints, "bad id count");
    for (std::size_t i = 0; i < ids; ++i) { std::string k = in.str(MaxEndpointBytes); c.lastIds.emplace_back(std::move(k), in.i64()); }
    std::size_t outbox = in.count(Conversation::MaxOutbox, "bad outbox count");
    for (std::size_t i = 0; i < outbox; ++i) {
        OutboxItem o;
        o.msgId = in.i64();
        auto e = in.take(EnvelopeBytes);
        o.envelope = Bytes(e.begin(), e.end());
        c.outbox.push_back(std::move(o));
    }
    std::size_t messages = in.count(Conversation::MaxMessages, "bad message count");
    for (std::size_t i = 0; i < messages; ++i) {
        StoredMessage m;
        m.id = in.i64();
        m.text = in.str(MaxUtf8Bytes);
        m.mine = in.boolean();
        m.delivery = in.u8();
        if (m.delivery > Conversation::DeliverySent) throw FormatError("bad delivery state");
        c.messages.push_back(std::move(m));
    }
    c.nextMsgId = in.i64();
    if (in.i != data.size()) throw FormatError("trailing bytes");
    return c;
}

}  // namespace state
}  // namespace tc
