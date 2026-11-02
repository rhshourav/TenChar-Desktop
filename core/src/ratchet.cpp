#include "tc/ratchet.hpp"

#include <algorithm>
#include <limits>

#include "tc/aead.hpp"
#include "tc/constants.hpp"
#include "tc/kdf.hpp"
#include "tc/wire.hpp"
#include "tc/x25519.hpp"

namespace tc {
namespace {
constexpr std::uint8_t StateVersion = 1;
constexpr std::uint32_t IntMax = std::numeric_limits<std::int32_t>::max();

std::pair<Bytes, Bytes> kdfRk(ByteView rk, ByteView dhOut) {
    Bytes out = kdf::hkdf(rk, dhOut, asBytes("tenchar-ratchet-v1"), 64);
    return {Bytes(out.begin(), out.begin() + 32), Bytes(out.begin() + 32, out.end())};
}
/// (next chain key, message key)
std::pair<Bytes, Bytes> kdfCk(ByteView ck) {
    const std::uint8_t two[1] = {2}, one[1] = {1};
    return {kdf::hmac(ck, {ByteView(two, 1)}), kdf::hmac(ck, {ByteView(one, 1)})};
}
Bytes messageKey(ByteView mk) { return kdf::hkdf({}, mk, asBytes("tenchar-message-v1"), 32); }

void putU32(Bytes& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}
std::uint32_t getU32(ByteView b) { return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | b[3]; }

Bytes makeHeader(ByteView pub, std::uint32_t pn, std::uint32_t n) {
    Bytes h;
    h.reserve(HeaderBytes);
    h.push_back(wire::Version);
    h.insert(h.end(), pub.begin(), pub.end());
    putU32(h, pn);
    putU32(h, n);
    return h;
}

struct Reader {
    ByteView b;
    std::size_t i = 0;
    ByteView take(std::size_t n) {
        if (b.size() - i < n) throw StateError("truncated ratchet state");
        ByteView v = b.subspan(i, n);
        i += n;
        return v;
    }
    std::uint32_t u32() { return getU32(take(4)); }
    std::uint8_t u8() { return take(1)[0]; }
    Bytes key() { auto v = take(32); return Bytes(v.begin(), v.end()); }
    std::optional<Bytes> optional() { return u8() != 0 ? std::optional<Bytes>(key()) : std::nullopt; }
};
}  // namespace

Ratchet Ratchet::forInitiator(ByteView sk, ByteView ad, const KeyPair& ek, ByteView peerEk, RandomSource& rng) {
    auto [rk, cks] = kdfRk(sk, x25519::sharedSecret(ek.priv, peerEk));
    Ratchet r;
    r.rk_ = std::move(rk);
    r.dhsPriv_ = ek.priv;
    r.dhsPub_ = ek.pub;
    r.dhr_ = Bytes(peerEk.begin(), peerEk.end());
    r.cks_ = std::move(cks);
    r.ad_ = Bytes(ad.begin(), ad.end());
    r.rng_ = &rng;
    return r;
}

Ratchet Ratchet::forResponder(ByteView sk, ByteView ad, const KeyPair& ek, ByteView peerEk, RandomSource& rng) {
    auto [rk1, ckr] = kdfRk(sk, x25519::sharedSecret(ek.priv, peerEk));
    Bytes priv = x25519::generatePrivate(rng);
    Bytes pub = x25519::publicKey(priv);
    auto [rk2, cks] = kdfRk(rk1, x25519::sharedSecret(priv, peerEk));
    Ratchet r;
    r.rk_ = std::move(rk2);
    r.dhsPriv_ = std::move(priv);
    r.dhsPub_ = std::move(pub);
    r.dhr_ = Bytes(peerEk.begin(), peerEk.end());
    r.cks_ = std::move(cks);
    r.ckr_ = std::move(ckr);
    r.ad_ = Bytes(ad.begin(), ad.end());
    r.rng_ = &rng;
    return r;
}

Bytes Ratchet::encrypt(ByteView frame) {
    if (frame.size() != static_cast<std::size_t>(FrameBytes)) throw CryptoError("bad frame size");
    if (!cks_) throw CryptoError("no sending chain");
    if (ns_ >= IntMax) throw CryptoError("chain exhausted");
    auto [nextCk, mk] = kdfCk(*cks_);
    Bytes header = makeHeader(dhsPub_, pn_, ns_);
    cks_ = std::move(nextCk);
    ++ns_;
    Bytes nonce = randomBytes(*rng_, NonceBytes);
    Bytes key = messageKey(mk);
    Bytes sealed = aead::seal(key, nonce, concat({header, ad_}), frame);
    return concat({header, nonce, sealed});
}

std::optional<Bytes> Ratchet::decrypt(ByteView envelope) {
    if (envelope.size() != static_cast<std::size_t>(EnvelopeBytes) || envelope[0] != wire::Version) return std::nullopt;
    Ratchet work = *this;
    std::optional<Bytes> frame;
    try {
        frame = work.decryptInner(envelope);
    } catch (const CryptoError&) {
        frame.reset();
    } catch (const StateError&) {
        frame.reset();
    }
    if (!frame) return std::nullopt;
    *this = std::move(work);
    return frame;
}

std::optional<Bytes> Ratchet::decryptInner(ByteView env) {
    ByteView dh = env.subspan(1, 32);
    const std::uint32_t pnHeader = getU32(env.subspan(33, 4));
    const std::uint32_t n = getU32(env.subspan(37, 4));
    if (pnHeader > IntMax || n > IntMax) return std::nullopt;
    ByteView nonce = env.subspan(HeaderBytes, NonceBytes);
    ByteView sealed = env.subspan(HeaderBytes + NonceBytes);
    Bytes aad = concat({env.first(HeaderBytes), ad_});

    for (auto it = skipped_.begin(); it != skipped_.end(); ++it) {
        if (it->n == n && constantTimeEquals(it->dh, dh)) {
            Bytes key = messageKey(it->mk);
            auto plain = aead::open(key, nonce, aad, sealed);
            if (!plain) return std::nullopt;
            skipped_.erase(it);
            return plain;
        }
    }

    if (!dhr_ || !constantTimeEquals(*dhr_, dh)) {
        skipUntil(pnHeader);
        dhRatchet(dh);
    }
    if (n < nr_) return std::nullopt;  // an old message whose key is already used: a replay
    skipUntil(n);
    if (!ckr_) return std::nullopt;
    auto [nextCk, mk] = kdfCk(*ckr_);
    Bytes key = messageKey(mk);
    auto plain = aead::open(key, nonce, aad, sealed);
    if (!plain) return std::nullopt;
    ckr_ = std::move(nextCk);
    ++nr_;
    return plain;
}

void Ratchet::skipUntil(std::uint32_t until) {
    if (!ckr_ || !dhr_) return;
    if (static_cast<std::int64_t>(until) - static_cast<std::int64_t>(nr_) > MaxSkip) throw StateError("too many skipped messages");
    Bytes ck = *ckr_;
    while (nr_ < until) {
        auto [next, mk] = kdfCk(ck);
        skipped_.push_back({*dhr_, nr_, std::move(mk)});
        ck = std::move(next);
        ++nr_;
    }
    ckr_ = std::move(ck);
    while (skipped_.size() > static_cast<std::size_t>(MaxSkip)) skipped_.erase(skipped_.begin());
}

void Ratchet::dhRatchet(ByteView peerPublic) {
    pn_ = ns_;
    ns_ = 0;
    nr_ = 0;
    dhr_ = Bytes(peerPublic.begin(), peerPublic.end());
    auto [rk1, ckr1] = kdfRk(rk_, x25519::sharedSecret(dhsPriv_, peerPublic));
    Bytes newPriv = x25519::generatePrivate(*rng_);
    Bytes newPub = x25519::publicKey(newPriv);
    auto [rk2, cks2] = kdfRk(rk1, x25519::sharedSecret(newPriv, peerPublic));
    rk_ = std::move(rk2);
    ckr_ = std::move(ckr1);
    cks_ = std::move(cks2);
    dhsPriv_ = std::move(newPriv);
    dhsPub_ = std::move(newPub);
}

Bytes Ratchet::serialize() const {
    Bytes out;
    out.push_back(StateVersion);
    out.insert(out.end(), rk_.begin(), rk_.end());
    out.insert(out.end(), dhsPriv_.begin(), dhsPriv_.end());
    out.insert(out.end(), dhsPub_.begin(), dhsPub_.end());
    for (const auto* opt : {&dhr_, &cks_, &ckr_}) {
        out.push_back(opt->has_value() ? 1 : 0);
        if (opt->has_value()) out.insert(out.end(), (*opt)->begin(), (*opt)->end());
    }
    putU32(out, ns_);
    putU32(out, nr_);
    putU32(out, pn_);
    putU32(out, static_cast<std::uint32_t>(skipped_.size()));
    for (const auto& s : skipped_) {
        out.insert(out.end(), s.dh.begin(), s.dh.end());
        putU32(out, s.n);
        out.insert(out.end(), s.mk.begin(), s.mk.end());
    }
    putU32(out, static_cast<std::uint32_t>(ad_.size()));
    out.insert(out.end(), ad_.begin(), ad_.end());
    return out;
}

Ratchet Ratchet::deserialize(ByteView bytes, RandomSource& rng) {
    Reader in{bytes};
    if (in.u8() != StateVersion) throw StateError("unknown ratchet state version");
    Ratchet r;
    r.rk_ = in.key();
    r.dhsPriv_ = in.key();
    r.dhsPub_ = in.key();
    r.dhr_ = in.optional();
    r.cks_ = in.optional();
    r.ckr_ = in.optional();
    r.ns_ = in.u32();
    r.nr_ = in.u32();
    r.pn_ = in.u32();
    const std::uint32_t count = in.u32();
    if (r.ns_ > IntMax || r.nr_ > IntMax || r.pn_ > IntMax || count > static_cast<std::uint32_t>(MaxSkip))
        throw StateError("bad ratchet state");
    for (std::uint32_t i = 0; i < count; ++i) {
        Bytes dh = in.key();
        std::uint32_t n = in.u32();
        if (n > IntMax) throw StateError("bad ratchet state");
        r.skipped_.push_back({std::move(dh), n, in.key()});
    }
    if (in.u32() != 64) throw StateError("bad associated data");
    auto ad = in.take(64);
    r.ad_ = Bytes(ad.begin(), ad.end());
    if (in.i != bytes.size()) throw StateError("trailing bytes");
    r.rng_ = &rng;
    return r;
}

void Ratchet::wipe() {
    secureZero(rk_.data(), rk_.size());
    secureZero(dhsPriv_.data(), dhsPriv_.size());
    for (auto* o : {&cks_, &ckr_})
        if (o->has_value()) secureZero((*o)->data(), (*o)->size());
    for (auto& s : skipped_) secureZero(s.mk.data(), s.mk.size());
    skipped_.clear();
}

}  // namespace tc
