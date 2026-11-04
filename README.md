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

## Build on Windows (automated; the scripts have not been run on Windows yet: report the first error you hit)

**One command, locally.** Double-click `build-windows.bat` (the first time, right-click → *Run as administrator*), or:

```
powershell -ExecutionPolicy Bypass -File windows\build.ps1 -InstallPrereqs
```

It finds or installs Visual Studio Build Tools (winget), Git and Python (winget), Qt 6.8 (aqtinstall) and OpenSSL 3 (vcpkg,
linked statically), then configures, builds, runs all tests, collects the Qt runtime with `windeployqt`, and writes:

- `dist\TenChar-win64\tenchar.exe` (run it from there), with `THIRD-PARTY-NOTICES.txt` and font licences
- `dist\TenChar-win64.zip` and `dist\TenChar-win64.zip.sha256`
- `dist\BUILD-INFO.txt` (commit, compiler, Qt, vcpkg, hash)

Useful switches: `-QtDir C:\Qt\6.8.3\msvc2022_64` (use a Qt you already have), `-Smoke` (launch the real app headless and
screenshot seven screens, plus the accessibility tree, into `dist\smoke`), `-Clean`, `-SkipTests`,
`-Triplet x64-windows` (OpenSSL as a DLL instead of static), `-SignThumbprint <sha1>` (sign `tenchar.exe` with a certificate).

**In the cloud.** Push the repository to GitHub: `.github/workflows/windows.yml` builds on a clean Windows 2022 machine for every push
and pull request, runs the tests and the smoke test, and uploads the zip, checksum and screenshots as artifacts. Push a tag
such as `v0.1.0` and the zip and its checksum are attached to a GitHub release. Qt and the compiled OpenSSL are cached.

**By hand** (if you prefer): install Visual Studio 2022 with *Desktop development with C++*, a Qt 6 *MSVC 2022 64-bit* kit and
vcpkg; then, in an *x64 Native Tools Command Prompt*:
`cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DCMAKE_PREFIX_PATH=<Qt>\msvc2022_64 -S . -B build`,
`cmake --build build`, `ctest --test-dir build`, then `windeployqt --qmldir app\qml build\app\tenchar.exe`.

`transport_test` is skipped on Windows (it needs `sh` and the `openssl` tool). Data is kept in `%LOCALAPPDATA%\TenChar`.
Not done: an MSI installer, and pinning vcpkg and the GitHub actions to exact commits (the build records what it used in `BUILD-INFO.txt`).

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

