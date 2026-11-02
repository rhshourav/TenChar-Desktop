# TenChar Desktop

A native C++20 / Qt 6 client for the same two-person, 10-character messenger as the Android app. Same protocol, same
relays, same look (dark, aurora, glass plates). Windows and macOS are the targets; **everything here was built and
tested on Linux only** (see "What has not been tested").

> **Do not rely on this for anything where a flaw could cause harm.** The handshake and ratchet (in Kotlin and in this
> C++ port) have had no independent review. That review is the one thing this repository cannot supply.

## Layout

| Target | What it is | Qt? |
|---|---|---|
| `core/` | pairing code, limits, wire format, X25519, HKDF, AES-GCM, handshake, Double Ratchet, strict JSON | no |
| `vault/` | state codec and the passphrase vault (passphrase → PBKDF2 → key wrapping a random data key → encrypted state) | no |
| `relayclient/` | relay HTTP client (transport is an interface), multi-relay pairing flow, relay-address rules | no |
| `session/` | the application logic (port of `AppViewModel.kt`): unlock, pairing, send, poll, lock, settings | no |
| `app/` | Qt Quick interface (QML), Qt Network transport, controller, tray, platform calls | yes |
| `vectors/` | independent reference vectors (`gen_vectors.py` writes `vectors.json`, written from the spec, not from either core) | |
| `tests/` | all tests, the fake relay, the contrast test, the fuzzer | |

Only `core`, `vault` and `session` touch secrets. Every byte buffer is a `tc::Bytes`, a vector that overwrites its
memory when freed.

## Build and test (Linux, as verified)

```
sudo apt install cmake ninja-build g++ libssl-dev qt6-base-dev qt6-declarative-dev \
     qml6-module-qtquick qml6-module-qtquick-shapes qml6-module-qtquick-window qml6-module-qtqml-workerscript python3
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build && cmake --build build && ctest --test-dir build
# sanitisers:  -DTC_SANITIZE=ON      developer screenshots with sample data:  -DTC_DEV_TOOLS=ON
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software build/app/tenchar --preview chat --grab chat.png
```

`TC_BUILD_APP=OFF` builds and tests only the Qt-free targets.

## What was verified

| Area | Evidence |
|---|---|
| Crypto primitives | RFC 7748 (incl. the 1,000-round iterated vector), RFC 5869, GCM known answers, PBKDF2: expected values computed by Python's `cryptography`/`hashlib`, not by this code |
| Protocol vs the independent vectors | pairing derivations, commitments, handshake from both sides, and a 5-message ratchet transcript (out of order, DH steps, replay) match `vectors.json` **byte for byte** |
| Tests can fail | deliberately broken handshake label and skip limit were both caught, then reverted |
| Ratchet | either side first; every one of 135 byte positions tampered; skip limit boundaries; save and restore; transactional (a rejected envelope changes no state) |
| State and vault | emoji and Bangla round trip; every possible truncation refused cleanly; wrong passphrase; damaged and swapped files; atomic writes; 0600/0700 permissions |
| Relay client and pairing | against an in-process **fake** relay: two relays, one down, one slow, one man-in-the-middle, third device refused, cancel, timeout, cleanup |
| Session engine | two engines pair, verify and chat both ways; lock wipes the screen; restart from disk keeps the ratchet in step; offline queue survives a restart; failover to the backup relay; "relays forgot the conversation"; damaged state → "Keys were reset" |
| Threads | session and relay suites clean under ThreadSanitizer; everything clean under ASan + UBSan |
| Fuzzing | 60,000 mutations per parser (JSON, relay responses, envelope, frame, state file, ratchet decrypt) under ASan + UBSan: no crash, no state change on rejection |
| HTTPS transport | against a local TLS server: bearer header, no redirect following, 256 KiB cap, self-signed certificate rejected, plain http refused, 8 threads at once |
| Interface | every screen rendered (English and Bangla, narrow and wide); Qt's accessibility tree dumped: every control named, codes read digit by digit, messages in conversation order |
| Strings | English and Bangla have identical keys and placeholders; every key the QML uses exists |
| Contrast | measured per use on the real aurora (see below) |

## Findings you should know about

1. **The plan's contrast claims were wrong, and so are the phone's.** "Ink above 12:1, muted above 6:1" only holds on
   the darkest background. Measured on the lightest part of the aurora behind a glass plate, with the phone's tokens:
   ink 4.1 on my bubbles, muted 3.9, faint 3.0, peach text 3.0 (minimum 4.5). The desktop fixes this with a dark
   scrim under every plate (`GlassPlate.scrim = 0.45`), a lighter peach for *text* (`peachText #FFCAAA`), and by using the
   faint ink for non-text glyphs only. `tests/contrast.py` reads the real token values from the QML and fails CI if any
   use drops under 4.5:1 (text) or 3:1 (non-text). The Android app needs the same fix.
2. **A `ListView` scrambled the screen-reader order** of messages. The chat list is a `Flickable` + `Repeater`.
3. **Receiver and grapheme counting.** The receiver here does *not* enforce the 10-cluster limit (only the 64-byte
   frame, valid UTF-8, no controls or bidi marks). Otherwise a message that already advanced the ratchet could be
   dropped just because Qt and Android's ICU count a Bangla conjunct differently. The sender still enforces 10.
4. **State file format** is the phone's `StateCodec` *after* the version-2 change in `fix-prompt-kotlin-core.md`:
   strings are a `u16` length plus standard UTF-8, not Java's modified UTF-8. The header's chain counters are big-endian.
5. Reduced transparency, motion and high contrast are **in-app switches only** (no OS detection yet).

## What has NOT been tested (be honest with yourself about these)

- **Windows and macOS builds.** Not compiled. `platform_mac.mm` (capture exclusion), the Windows branches of
  `platform.cpp` and `vault.cpp` (display affinity, clipboard-history exclusion, `MoveFileExW`) were written carefully
  but never run. MSVC `/W4 /WX` may need small fixes.
- **A real relay.** `relay/src/core.js` was not available. The relay client follows `RelayClient.kt`; the fake relay
  in `tests/fake_relay.hpp` is *my reading of that protocol*, so agreement with it proves consistency, not correctness.
  Live phone↔desktop interop is still to do (Gate C).
- **Screen readers.** Only Qt's accessibility tree was inspected. Narrator, VoiceOver and NVDA have not been used.
- **Real Bangla input** through an operating-system IME, and the 10-character counter against the phone on a shared
  list of test strings (Qt 6.4's Unicode tables may differ from ICU for conjuncts).
- **GPU performance.** Screenshots used the software renderer. The 60 fps / idle-CPU targets are unmeasured.
- Qt **6.4** was what the sandbox had; the plan wants the current LTS. Nothing here needs more than 6.4.
- Installers, signing, notarisation, checksummed dependency pins, reproducible builds: not started.
- Start at login, locking on OS screen lock, Argon2id (PBKDF2 used, awaiting your decision), changing the passphrase
  from the UI (the vault supports it), `mlock` of secret pages, Windows DPAPI / macOS Keychain wrapping.
- Passphrase text passes through Qt/JavaScript strings before it reaches the engine; those copies cannot be wiped.
- The Bangla strings are mine and need a native speaker. The logo is two drawn letters: the phone's image was not available.
- The independent cryptography review.

## Decisions I assumed (change them if you disagree)

OpenSSL 3 for the C++ crypto (decision 7); PBKDF2, no Argon2id (4); no fetching while locked (8); injectable
randomness in both cores (9); one pairing per device (1); Windows and macOS first (3).
