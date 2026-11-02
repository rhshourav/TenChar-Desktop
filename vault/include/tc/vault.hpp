#pragma once
// Key hierarchy (the phone's, with a passphrase in place of the Android Keystore):
//
//   passphrase -> PBKDF2-HMAC-SHA256 (600,000 rounds, random 32-byte salt) -> key-encryption key
//   wraps   -> data key (32 random bytes, stored wrapped in tc_dek.bin, AES-256-GCM)
//   encrypts-> tc_state.bin (AES-256-GCM, fresh random nonce per write, AAD "tenchar-state-v1")
//
// A wrong passphrase is an authentication failure. There is no recovery and no
// "forgot passphrase": the data is gone by design. Files are replaced atomically.
#include <filesystem>
#include <string_view>

#include "tc/bytes.hpp"
#include "tc/constants.hpp"
#include "tc/random.hpp"

namespace tc {

/// The state file cannot be opened with the data key: damaged, truncated or replaced.
struct KeysLost : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Vault {
public:
    /// `iterations` is a parameter so tests can be quick; the app always uses the default.
    explicit Vault(std::filesystem::path dir, int iterations = Pbkdf2Iterations);

    bool exists() const;

    /// Makes a new vault for `passphrase` (any old one is discarded) and returns the data key.
    Bytes create(std::string_view passphrase, RandomSource& rng);

    enum class Unlock { Ok, WrongPassphrase, Lost };
    struct Unlocked {
        Unlock result = Unlock::Lost;
        Bytes dek;
    };
    /// Lost: the wrapped-key file is missing or malformed. WrongPassphrase also covers a damaged file
    /// that still parses, because authentication failure cannot tell the two apart.
    Unlocked unlock(std::string_view passphrase) const;

    /// Re-wraps the data key (32 bytes); the state file is not re-encrypted.
    bool changePassphrase(std::string_view oldPass, std::string_view newPass, RandomSource& rng);

    /// The saved plaintext state, or nullopt if nothing was saved. Throws KeysLost if it cannot be opened.
    std::optional<Bytes> load(ByteView dek) const;
    /// Encrypts under a fresh nonce and replaces the state file atomically. Throws std::runtime_error on I/O failure.
    void save(ByteView dek, ByteView plain, RandomSource& rng) const;

    /// Deletes both files.
    void wipe() const;

private:
    std::filesystem::path dekFile_, stateFile_;
    int iterations_;
};

/// Writes `data` to `target` through a temporary file that is flushed to disk and then renamed over it.
/// The file is readable only by the current user. Throws std::runtime_error on failure.
void writeFileAtomically(const std::filesystem::path& target, ByteView data);
std::optional<Bytes> readWholeFile(const std::filesystem::path& path, std::size_t maxBytes);

}  // namespace tc
