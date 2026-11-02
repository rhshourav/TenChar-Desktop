#include "tc/vault.hpp"

#include <cstdio>
#include <fstream>

#include "tc/aead.hpp"
#include "tc/kdf.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tc {
namespace {
namespace fs = std::filesystem;

constexpr std::uint8_t DekVersion = 1, StateVersion = 1;
constexpr std::size_t SaltLen = 32, DekLen = 32, NonceLen = 12, TagLen = 16;
constexpr std::size_t DekHeader = 1 + SaltLen + 4;
constexpr std::size_t DekFileLen = DekHeader + NonceLen + DekLen + TagLen;
constexpr std::size_t MaxStateFile = 4 * 1024 * 1024;
constexpr int MinIterations = 1000, MaxIterations = 20'000'000;

Bytes dekAad(ByteView header) { return concat({asBytes("tenchar-dek-v1"), header}); }
const Bytes& stateAad() { static const Bytes a = toBytes("tenchar-state-v1"); return a; }

void putU32(Bytes& o, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<std::uint8_t>(v >> s)); }

Bytes wrapKey(ByteView dek, std::string_view pass, int iterations, RandomSource& rng) {
    Bytes header;
    header.push_back(DekVersion);
    Bytes salt = randomBytes(rng, SaltLen);
    header.insert(header.end(), salt.begin(), salt.end());
    putU32(header, static_cast<std::uint32_t>(iterations));
    Bytes kek = kdf::pbkdf2(asBytes(pass), salt, iterations, 32);
    Bytes nonce = randomBytes(rng, NonceLen);
    Bytes sealed = aead::seal(kek, nonce, dekAad(header), dek);
    return concat({header, nonce, sealed});
}
}  // namespace

void writeFileAtomically(const fs::path& target, ByteView data) {
    fs::path tmp = target;
    tmp += ".tmp";
#ifdef _WIN32
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create temporary file");
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        throw std::runtime_error("cannot write file");
    }
#else
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("cannot create temporary file");
    std::size_t off = 0;
    bool ok = true;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0) { ok = false; break; }
        off += static_cast<std::size_t>(n);
    }
    ok = ok && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || ::rename(tmp.c_str(), target.c_str()) != 0) {
        ::unlink(tmp.c_str());
        throw std::runtime_error("cannot write file");
    }
    int dirFd = ::open(target.parent_path().c_str(), O_RDONLY | O_CLOEXEC);  // make the rename durable, best effort
    if (dirFd >= 0) { ::fsync(dirFd); ::close(dirFd); }
#endif
}

std::optional<Bytes> readWholeFile(const fs::path& path, std::size_t maxBytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    Bytes out;
    char buf[4096];
    while (f.read(buf, sizeof buf) || f.gcount() > 0) {
        out.insert(out.end(), buf, buf + f.gcount());
        if (out.size() > maxBytes) return std::nullopt;
    }
    secureZero(buf, sizeof buf);
    return out;
}

Vault::Vault(fs::path dir, int iterations)
    : dekFile_(dir / "tc_dek.bin"), stateFile_(dir / "tc_state.bin"), iterations_(iterations) {
    std::error_code ec;
    fs::create_directories(dir, ec);
#ifndef _WIN32
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);  // 0700
#endif
}

bool Vault::exists() const { return fs::exists(dekFile_); }

Bytes Vault::create(std::string_view passphrase, RandomSource& rng) {
    wipe();
    Bytes dek = randomBytes(rng, DekLen);
    writeFileAtomically(dekFile_, wrapKey(dek, passphrase, iterations_, rng));
    return dek;
}

Vault::Unlocked Vault::unlock(std::string_view passphrase) const {
    auto blob = readWholeFile(dekFile_, 4096);
    if (!blob || blob->size() != DekFileLen || (*blob)[0] != DekVersion) return {Unlock::Lost, {}};
    ByteView b(*blob);
    ByteView header = b.first(DekHeader), salt = b.subspan(1, SaltLen), nonce = b.subspan(DekHeader, NonceLen);
    std::uint32_t iters = (std::uint32_t(b[1 + SaltLen]) << 24) | (std::uint32_t(b[2 + SaltLen]) << 16) |
                          (std::uint32_t(b[3 + SaltLen]) << 8) | b[4 + SaltLen];
    if (iters < static_cast<std::uint32_t>(MinIterations) || iters > static_cast<std::uint32_t>(MaxIterations)) return {Unlock::Lost, {}};
    Bytes kek = kdf::pbkdf2(asBytes(passphrase), salt, static_cast<int>(iters), 32);
    auto dek = aead::open(kek, nonce, dekAad(header), b.subspan(DekHeader + NonceLen));
    if (!dek || dek->size() != DekLen) return {Unlock::WrongPassphrase, {}};
    return {Unlock::Ok, std::move(*dek)};
}

bool Vault::changePassphrase(std::string_view oldPass, std::string_view newPass, RandomSource& rng) {
    auto u = unlock(oldPass);
    if (u.result != Unlock::Ok) return false;
    writeFileAtomically(dekFile_, wrapKey(u.dek, newPass, iterations_, rng));
    return true;
}

std::optional<Bytes> Vault::load(ByteView dek) const {
    if (!fs::exists(stateFile_)) return std::nullopt;
    auto blob = readWholeFile(stateFile_, MaxStateFile);
    if (!blob || blob->size() < 1 + NonceLen + TagLen || (*blob)[0] != StateVersion) throw KeysLost("state file unreadable");
    ByteView b(*blob);
    auto plain = aead::open(dek, b.subspan(1, NonceLen), stateAad(), b.subspan(1 + NonceLen));
    if (!plain) throw KeysLost("state file does not authenticate");
    return plain;
}

void Vault::save(ByteView dek, ByteView plain, RandomSource& rng) const {
    Bytes nonce = randomBytes(rng, NonceLen);
    Bytes sealed = aead::seal(dek, nonce, stateAad(), plain);
    const std::uint8_t ver[1] = {StateVersion};
    writeFileAtomically(stateFile_, concat({ByteView(ver, 1), nonce, sealed}));
}

void Vault::wipe() const {
    std::error_code ec;
    fs::remove(dekFile_, ec);
    fs::remove(stateFile_, ec);
}

}  // namespace tc
