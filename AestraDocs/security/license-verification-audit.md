# License Verification Architecture Audit

## 1. Executive summary

Aestra currently has **two separate license/tier paths** in this repository:

1. **`Source/license/LicenseVerifier.*` + `Source/ui/InfoTab.cpp`** (UI profile card path): this is a **stub** and is trivially bypassable with `signature == "MOCK-VALID"`.
2. **`AestraLicense/LicenseGate.*`** (lease verification path): this contains **real cryptographic verification code** (Ed25519/libsodium on non-Windows), but is only consumed by premium plugin gating paths (not the Info tab path).

The known concern is correct: editing `~/.Aestra/user_info.json` and setting `"signature": "MOCK-VALID"` can mark the Info tab profile as verified. This is not protected by real crypto in `Source/license/LicenseVerifier.cpp`.

## 2. Files inspected

- `Source/license/LicenseVerifier.h`
- `Source/license/LicenseVerifier.cpp`
- `Source/license/user_info.json`
- `Source/ui/InfoTab.cpp`
- `Source/CMakeLists.txt`
- `aestra-core/include/Aestra/License.h`
- `aestra-core/src/LicenseStub.cpp`
- `AestraLicense/include/LicenseGate.h`
- `AestraLicense/include/LicenseTier.h`
- `AestraLicense/src/LicenseGate.cpp`
- `AestraLicense/CMakeLists.txt`
- `AestraAudio/CMakeLists.txt`
- `AestraAudio/src/Plugin/BuiltInPlugins.cpp`
- `AestraAudio/src/Plugin/PluginManager.cpp`
- `AestraAudio/src/Plugin/InternalPluginRegistry.cpp`
- `AestraPlugins/CMakeLists.txt`
- `AestraPlugins/AestraRumble/CMakeLists.txt`
- `AestraPlugins/AestraRumble/src/RumblePluginRegistration.cpp`
- `AestraPlugins/AestraRumble/src/RumbleInstance.cpp`
- `CMakeLists.txt`
- `aestra-core/cmake/AestraCoreMode.cmake`
- `.github/workflows/public-ci.yml`
- `.github/workflows/private-release.yml`

## 3. Current license flow

### 3.1 Profile/InfoTab path (stub path)

1. `InfoTab` calls `loadProfile()` then `verifyLicense(g_profile)` (`Source/ui/InfoTab.cpp`).
2. `loadProfile()` reads from `~/.Aestra/user_info.json` (`Source/license/LicenseVerifier.cpp`).
3. Parsed fields: `username`, `tier`, `serial`, `signature`. `verified` is not read from disk.
4. `verifyLicense(UserProfile&)` computes `payload = username + serial + tier` but does not verify it cryptographically.
5. Verification is currently: `ok = (profile.signature == "MOCK-VALID")`.
6. If not ok, `tier` is forced to `"Aestra Core"`; if ok, provided tier string is retained.
7. Info tab displays `username`, `tier`, `serial`, badge, and card asset based on tier.

### 3.2 Lease/feature-gate path (AestraLicense path)

1. `AestraLicense` loads a stored lease blob from platform secret storage (DPAPI/Keychain/libsecret) with encrypted backup fallback (`lease.bin`) on non-Windows.
2. Blob format is `payload + "\n" + signature_hex`.
3. Payload is parsed into strict fields (`license_id`, `user_id`, `tier`, `plugins`, `features`, `device_hash`, `issued_at`, `expires_at`, `grace_policy`, `revocation_epoch`).
4. Payload is canonicalized in fixed JSON field order.
5. Signature is verified with `crypto_sign_verify_detached(..., AESTRA_LICENSE_PUBKEY)` on non-Windows.
6. Additional checks: device hash drift tolerance (<=1 field mismatch), `grace_policy == "restrict"`, `revocation_epoch == 0`, expiry windows.
7. `LicenseGate::canAccess()` maps tier to features (`RUMBLE`, `RUMBLE_HEADLESS`).
8. Current in-repo consumers are premium Rumble plugin registration/processing paths.

## 4. Current trust boundaries

- **Untrusted local user profile file:** `~/.Aestra/user_info.json` is user-editable and treated as trusted by the Info tab stub path.
- **Secret-storage lease blob:** trust anchored in `AESTRA_LICENSE_PUBKEY` for signature verification (AestraLicense path).
- **Build-time boundary:** premium plugin code is only compiled when `AESTRA_ENABLE_PREMIUM_PLUGINS=ON`; public CI runs core/headless defaults.
- **UI vs gating split:** Info tab verification state is separate from AestraLicense feature gating.

## 5. Confirmed bypasses

1. **Manual JSON editing (confirmed):** set `~/.Aestra/user_info.json` signature to `"MOCK-VALID"` to get `verified=true` in Info tab path.
2. **Tier string spoofing (confirmed):** with `"MOCK-VALID"`, arbitrary `tier` string is accepted and displayed (no whitelist in `LicenseVerifier`).
3. **Mock validation in production source tree (confirmed):** `LicenseVerifier` comment and logic explicitly indicate stub acceptance.
4. **Non-cryptographic payload check (confirmed):** payload is constructed then unused.
5. **UI trust boundary mismatch (confirmed):** `verified/tier` display logic is not tied to `LicenseGate` cryptographic decision.
6. **Windows crypto gap in AestraLicense (confirmed):** `verifyLeaseSignature` returns `false` on Windows in current source, forcing Core for that gate path.
7. **Potential test-only key leakage to runtime (confirmed):** `AESTRA_LICENSE_PUBKEY` is annotated `AESTRA_TEST_KEY_V1` in a public runtime header.

## 6. Whether real crypto exists

**Yes, but only in the AestraLicense path.**

- **Algorithm in code:** Ed25519 detached signature verification via libsodium (`crypto_sign_verify_detached`) on non-Windows.
- **Bundled public key:** `AESTRA_LICENSE_PUBKEY[32]` embedded in `AestraLicense/include/LicenseGate.h`.
- **Canonicalization:** explicit canonical JSON builder (`canonicalizeLeasePayload`).
- **Payload construction/parsing:** strict required fields and type checks.
- **Expiry support:** yes (`issued_at`, `expires_at`) with fixed lease period assumptions.
- **Key ID support:** **no** key id/key rotation metadata in current payload/API.
- **Offline verification:** yes (stored lease + local verification).
- **Online refresh/check:** **no active implementation** (`LicenseGate::refreshAsync()` is empty).

## 7. Bitwarden/key storage recommendation

### Private signing key (secret)

- Keep **only outside repo**.
- Store in backend KMS/HSM or secret manager; Bitwarden acceptable only for tightly controlled operational storage/rotation workflows.
- Never ship private key in client binary, source, tests, CI logs, or artifacts.

### Public verification key (safe to embed)

- Commit only public verification key material used by clients to verify signatures.
- Include key version/key-id metadata in signed payload to support rotation.

### Test keypair (disposable)

- Allowed for local/unit tests only.
- Keep clearly namespaced as test-only and never used in production signing.
- Ensure production binaries do not rely on test key identifiers.

## 8. Recommended implementation plan

1. **Unify trust source:** make all UI/profile verified/tier status derive from the cryptographic license gate result, not `MOCK-VALID` profile logic.
2. **Remove stub semantics from runtime path:** eliminate `"MOCK-VALID"` acceptance in production codepaths.
3. **Define signed token schema:** include `sub/user`, `tier`, `entitlements`, `iat`, `exp`, `kid`, optional `nbf`, device policy, and revocation epoch.
4. **Implement key rotation:** support multiple embedded public keys selected by `kid`.
5. **Implement refresh path:** `refreshAsync()` should fetch/refresh lease (authenticated transport), store signed lease, and preserve offline grace behavior.
6. **Harden platform parity:** implement real signature verification on Windows too (same cryptographic guarantees as non-Windows).
7. **Separate display from assertions:** UI should render tier badge based on trusted gate state only.
8. **Constrain local profile file usage:** keep `user_info.json` as cache/display metadata only, never as authoritative proof.

## 9. Test plan for the future implementation

1. **Signature correctness tests:** valid token passes; tampered payload/signature fails.
2. **Key rotation tests:** old/new `kid` validation behavior and deprecation windows.
3. **Expiry/grace tests:** `nbf/exp` boundaries, offline grace transitions.
4. **Revocation tests:** revoked epoch/token rejected after refresh.
5. **Cross-platform parity tests:** Windows/macOS/Linux verification outcomes match for same vectors.
6. **Bypass regression tests:** local edits to profile JSON cannot elevate tier or verified state.
7. **Feature-gate tests:** Supporter/Founder entitlements enforce expected plugin availability and processing behavior.
8. **Cold-start/offline tests:** cached valid lease works offline within policy; invalid/expired drops to Core.

## 10. Open questions

1. Should tier taxonomy be unified (`Aestra Core/Plus/Founder/Campus` vs `Core/Supporter/Founder`)?
2. Should device binding be mandatory, optional, or tier-dependent?
3. What is the required offline grace duration and revocation SLA?
4. Should all premium entitlement checks be centralized in `AestraLicense` only?
5. Is `user_info.json` intended to remain user-editable metadata, or should it be replaced by signed cache data?
6. What backend service will issue leases and manage key rotation/revocation?
