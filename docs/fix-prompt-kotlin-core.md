# Task: make the TenChar Android core testable against independent vectors, and fix four defects

You are working on the TenChar Android app: Kotlin, Jetpack Compose, package `com.rhshourav.tenchar`, minSdk 30. It is a two-person messenger with 10-character messages, end-to-end encrypted with a hand-composed X3DH-style handshake and Double Ratchet. A C++ desktop client is being written in parallel and must be byte-compatible with this code, so every change below has to keep the production wire behaviour identical unless a task says otherwise.

Files you will touch (all already in the repo): `Constants.kt`, `Wire.kt`, `MessageLimits.kt`, `Ratchet.kt`, `Handshake.kt`, `X25519.kt` (only `generatePrivate`), `Pairing.kt` (only `generate`), `StateCodec.kt`, `AppViewModel.kt`, `Screens.kt` (Composer and SendButton), the string resources, and the unit tests.

Two extra files are provided next to this prompt:

- `vectors/gen_vectors.py` is a Python script written from the written protocol spec, **not** from this Kotlin code. It uses `cryptography` and `hashlib`, and fixed "random" inputs.
- `vectors/vectors.json` is its output. Canary values to confirm you have the right file: pairing `room` = `f1b914add3fb600eb619488f2d3e622c`, handshake `verification` = `9617 7894`, `SK` starts `aa8a8c3f8c457161`, ratchet has 5 steps.

## Ground rules

1. **No new dependencies.** The project pins every dependency. In particular, do not add `org.json` or any JSON library to the unit-test classpath (the Android stub returns empty values). Write a minimal parser in test code for the JSON subset used (objects, arrays, strings, integers); about 40 lines is enough.
2. **Never edit `vectors.json` or `gen_vectors.py` to make a test pass.** The vectors are the independent referee. If a Kotlin result differs, work out which side contradicts the spec, then stop and report both values and your reasoning. Do not "fix" either side silently.
3. Do not change the X25519 arithmetic, any derivation label, any wire layout, or any constant, except where a task says so.
4. Match the existing style: plain comments that explain why, no TODO litter, no unrelated refactors.
5. Work in the order below. Run `./gradlew testDebugUnitTest` after each task and make one commit per task with a clear message.
6. New UI strings go in both `values/strings.xml` and the Bangla resource file. You may write the Bangla, but list every new Bangla string in your report as "needs native-speaker review".

## Task 1: injectable randomness

Today the code takes `java.security.SecureRandom` directly, which makes byte-for-byte tests impossible.

Add in package `...crypto`:

```kotlin
fun interface RandomSource { fun nextBytes(out: ByteArray) }
object SystemRandom : RandomSource {
    private val sr = java.security.SecureRandom()
    override fun nextBytes(out: ByteArray) = sr.nextBytes(out)
}
```

Replace `SecureRandom` parameters and fields with `RandomSource` in: `X25519.generatePrivate`, `KeyPair.generate`, `LocalMember.create`, `Ratchet` (constructor field, `forInitiator`, `forResponder`, `deserialize`), `Pairing.generate`, `StateCodec.decode`, and `AppViewModel.random`. Default everywhere in production is `SystemRandom`. Leave `Vault` on its own `SecureRandom`; it is not part of the vectors.

Rules: every draw must request exactly the bytes it uses (32 for a private key, 12 for a nonce, `CODE_LEN` for a pairing code), because the test source checks sizes. Do not add any extra draws or reorder existing ones. Update existing tests that passed `SecureRandom()`.

Also add a test-only `ScriptedRandom : RandomSource` that is given a list of byte arrays, returns them in order, throws if a request's size differs from the next chunk, throws on underflow, and has `assertDrained()`.

Done when: all existing tests pass and the app behaves exactly as before.

## Task 2: the vector tests (this is the gate)

Copy `vectors.json` to `app/src/test/resources/`. Write `ReferenceVectorTest` (follow the existing test layout) asserting, with exact hex equality:

1. **Pairing.** For `pairing.code`, `Pairing.hexPrefix(code, SALT_ROOM, ROOM_HEX_CHARS)` equals `room`, the `SALT_AUTH` / `CODE_TOKEN_HEX_CHARS` one equals `codeToken`, and `pbkdf2Sha256(..., SALT_KEY, ...)` as hex equals `codeKey`.
2. **Members.** For A and B: `X25519.publicKey(ik)` = `ikp`, `X25519.publicKey(ek)` = `ekp`, and `Handshake.commit(ikp, ekp, id)` = `commit`. Build `LocalKeys` directly from the private keys in the file, with no randomness.
3. **Handshake.** `Handshake.agree` from A's side and from B's side both give `SK`, `AD` and `verification` from the file. A is the initiator and B is not.
4. **Ratchet transcript.** Use two `ScriptedRandom` sources, one per side, loaded in exactly this order:
   - Side I (initiator A): `nonces[0]`, `I_ratchet2_priv`, `nonces[2]`, `nonces[3]`, `I_ratchet4_priv`
   - Side R (responder B): `R_first_ratchet_priv`, `nonces[1]`, `R_ratchet3_priv`, `nonces[4]`

   (`nonces` is `ratchet.injected.nonces_in_send_order`.) Then run the steps in `ratchet.delivery_order` (1, 2, 4, 3, 5), where step *k* is `ratchet.steps[k-1]`:
   - the sender encrypts `Wire.encodeText(text)`; the envelope must equal `envelope_hex` byte for byte;
   - the receiver decrypts it and `Wire.decodeText` returns the text;
   - step 3 is delivered after step 4, so it is served from the skipped-key store.
5. After the transcript, decrypting step 3's envelope again returns null, and `serialize()` is identical before and after that failed call.
6. Both `ScriptedRandom` sources report drained.

If anything fails, follow ground rule 2.

## Task 3: stop losing messages the receiver counts differently

`Wire.decodeText` rejects a frame if `MessageLimits.fits` is false. `AppViewModel.receive` then drops it, and `pollEndpoint` acknowledges it to the relay anyway, because the ratchet already advanced. So if two platforms count a Bangla string differently (Android ICU vs the desktop's Qt, for example 10 vs 11 clusters), the receiver silently loses a valid message.

Fix: the 10-grapheme limit is enforced when **sending** (`encodeText`, composer) only. `decodeText` must still require: type 1, `len` in 1..64, zero padding, strict UTF-8, not blank, no control or bidirectional-override characters. Remove only the grapheme-count condition (the 64-byte frame already bounds the length). Update the comments in `Wire.kt` to say why.

Tests: a 64-byte frame holding 12 ASCII letters is accepted by `decodeText` but `encodeText` of the same text returns null; Bangla conjunct strings round-trip; bidi and control characters are still rejected. Check that `MessageRow` wraps a long received message inside the 300 dp bubble and does not overflow.

## Task 4: state file strings must be standard UTF-8

`StateCodec` uses `DataOutputStream.writeUTF` / `readUTF`, which is Java's modified UTF-8: U+0000 becomes `C0 80`, and an emoji such as U+1F600 is written as two 3-byte surrogates (6 bytes) instead of `F0 9F 98 80`. A second implementation would have to reproduce this quirk.

Fix: add internal helpers `writeStr(out, s, maxBytes)` and `readStr(input, maxBytes)` that write a big-endian `u16` byte length followed by standard UTF-8, and read with a decoder set to REPORT on malformed input. Use them for the endpoints (max 255 bytes), the verification string (must be exactly 9 bytes), the `lastIds` keys (max 255), and message text (max 64 bytes). Bump `StateCodec.VERSION` to 2 and make `decode` reject any other version with `IOException`, which already leads to the "Keys were reset" screen. Do not write a v1 migration: assume no released build exists. If you find evidence that one does, stop and say so.

Tests: round-trip a conversation containing an emoji message and a Bangla message; assert that `writeStr("😀")` produces exactly `00 04 F0 9F 98 80`; assert that a truncated file, trailing bytes, an over-long string and invalid UTF-8 are each refused cleanly with `IOException`.

## Task 5: show when the byte limit binds

A Bangla consonant-virama-consonant cluster is 9 UTF-8 bytes, so the 64-byte cap is reached after about 7 such clusters while the counter still reads under 10. Today the composer just shakes and the person cannot tell why.

In `Composer` and `SendButton`:

- Compute `bytes = MessageLimits.utf8Length(draft)`. The ring fraction becomes `max(count / 10f, bytes / 64f)` and the colour thresholds use that fraction (mint below 0.8, lavender from 0.8, peach at 1.0). Pass the fraction into `SendButton` instead of `count`.
- In the `onValueChange` rejection path, if `graphemeCount(raw) <= MAX_CHARS` the byte limit caused the rejection. Set a `byteLimitHit` flag, show a one-line hint under the composer in `labelMedium` peach inside a polite live region, and turn the counter peach. Clear the flag on the next accepted edit. If the character limit caused the rejection, behave exactly as today.
- New string `chat_limit_bytes`, English: "No more room. Some letters take extra space." Add the Bangla version. Keep the shake and the haptic as they are.

## Task 6: measure the contrast claims

The design states ink on glass is "above 12 to 1" and muted text "above 6 to 1", but nothing measures it. Write a pure-JVM `ContrastTest` that alpha-composites in sRGB and computes WCAG 2.x ratios.

Surfaces to test, each as the full stack from the back: `Space`, then the aurora glows at their **maximum** alpha over their centre (Plum 1.0, Indigo 0.55, Lavender 0.22, Peach 0.14, Mint 0.12; also test the overlap Plum + Indigo + Lavender), then the glass fill at its lightest (white 0.13), then each tint (none, Peach 0.14, Lavender 0.14), plus the "mine" bubble (Indigo 0.70 to Lavender 0.34, both ends) and the "theirs" bubble (Peach 0.07).

Text colours composite onto the surface first. Required minimums:

| Foreground | Minimum |
|---|---|
| `Ink`, `InkMuted` | 4.5 : 1 |
| `InkFaint` (used for 13 sp notes and placeholders) | 4.5 : 1 |
| `Peach` text on the peach-tinted glass | 4.5 : 1 |
| `Space` text on the `Brand` gradient (both ends) | 4.5 : 1 |
| `Mint` focus ring and `InkFaint` chevrons (non-text) | 3 : 1 |

Do not lower any threshold. If something fails, raise the offending token's alpha or the surface tint by the smallest amount that passes, update `Theme.kt`, and report the old and new values (the desktop theme must copy them). Put the full results table in your report.

## Report back with

1. Per task: files changed, tests added, test run result.
2. Anything in `vectors.json` that did not match, with the Kotlin value, the vector value and which one you believe contradicts the spec, quoting the spec line.
3. Contrast results table and any token changes.
4. New Bangla strings needing review.
5. The following spec facts, which the C++ port will rely on. Confirm each matches the code, or say how it differs:
   - `previous chain length` and `n` in the envelope header are **big-endian** 4-byte integers;
   - the receiver enforces no grapheme limit (Task 3);
   - the state file uses standard UTF-8 with a `u16` length (Task 4).
