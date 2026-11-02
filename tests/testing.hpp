#pragma once
// A very small test harness (no third-party dependency).
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "tc/bytes.hpp"
#include "tc/random.hpp"

inline int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(c)) { ++g_fail; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        ++g_checks;                                                           \
        auto va_ = (a); auto vb_ = (b);                                       \
        if (!(va_ == vb_)) {                                                  \
            ++g_fail;                                                         \
            std::ostringstream o_; o_ << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << " == " << #b << "\n   got:  " << va_ << "\n   want: " << vb_ << "\n"; \
            std::fputs(o_.str().c_str(), stderr);                             \
        }                                                                     \
    } while (0)
#define RUN(fn)                                                               \
    do { std::fprintf(stderr, "- %s\n", #fn); try { fn(); } catch (const std::exception& e_) { ++g_fail; std::fprintf(stderr, "FAIL %s threw: %s\n", #fn, e_.what()); } } while (0)
#define FINISH()                                                              \
    (std::fprintf(stderr, "%d checks, %d failed\n", g_checks, g_fail), g_fail == 0 ? 0 : 1)

inline std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

inline std::string H(tc::ByteView b) { return tc::hexEncode(b); }
inline tc::Bytes unhex(const std::string& s) {
    auto b = tc::hexDecode(s);
    if (!b) throw std::runtime_error("bad hex in test");
    return *b;
}

/// Returns queued chunks in order, and insists each request has exactly the queued size.
struct ScriptedRandom final : tc::RandomSource {
    std::deque<tc::Bytes> chunks;
    void push(tc::Bytes b) { chunks.push_back(std::move(b)); }
    void fill(std::span<std::uint8_t> out) override {
        if (chunks.empty()) throw std::runtime_error("ScriptedRandom: underflow");
        if (chunks.front().size() != out.size()) throw std::runtime_error("ScriptedRandom: size mismatch");
        std::copy(chunks.front().begin(), chunks.front().end(), out.begin());
        chunks.pop_front();
    }
    bool drained() const { return chunks.empty(); }
};
