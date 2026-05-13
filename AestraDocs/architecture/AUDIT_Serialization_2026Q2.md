# Serialization / Project Format Audit — 2026Q2

**Status:** Internal — audit findings, no code changed
**Scope:** `.aes` project format. Schema versioning, migrations, validation, roundtrip fidelity, corrupt-file recovery, atomic-write discipline, autosave + history snapshots, asset reference integrity.
**Date:** 2026-05-14
**Auditor:** Cascade
**Companion:** [`ARCHITECTURE_AUDIT_2026Q2.md`](ARCHITECTURE_AUDIT_2026Q2.md), [`AUDIT_RT_Safety_2026Q2.md`](AUDIT_RT_Safety_2026Q2.md), [`AUDIT_Threading_Concurrency_2026Q2.md`](AUDIT_Threading_Concurrency_2026Q2.md)

> **Important correction:** the architecture audit claimed "no roundtrip/fixture test exists." That claim was wrong. Aestra has substantial roundtrip coverage (see §5). The audit underrated the system because the grep didn't reach `Tests/Integration/`. This audit corrects the record and ranks the *remaining* gaps.

---

## 1. Executive summary

**Aestra's project-format discipline is the strongest of the three Tier-1 audits so far.** Atomic writes, backups, history snapshots, structural validation, bounded sizes, hardened parsing, and a real roundtrip test harness are all already in place. The team has a track record of fixing security-class bugs (SEC-001, SEC-003, SEC-RTM-003, RTM-009) before they ship.

**The risks that remain are architectural, not implementation defects:**

- **The migration framework has never been exercised.** `PROJECT_VERSION_CURRENT == 1` and zero migrations are registered. The first real v2 ship will be the first time the migration code path runs in production.
- **No canonical fixture corpus.** Roundtrip tests construct fixtures programmatically; there is no stored "real v1 file" that we can load in a v2 build to prove migration works.
- **No content integrity check.** Atomic write protects against half-writes, but disk corruption (cosmic ray, bad SSD, network filesystem) passes structural validation and loads as silently-wrong data.
- **Asset references are paths, not hashes.** If a sample is moved/renamed/edited on disk, the project loads but the audio is wrong with no warning.

**Findings:** 1 P0, 5 P1, 4 P2.

---

## 2. P0 — must fix before v1 Beta

### P0.1 — Migration framework is unproven; v1 Beta will ship into the first migration scenario blind

`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:37-38`:
```cpp
constexpr int PROJECT_VERSION_CURRENT = 1;
constexpr int PROJECT_VERSION_MIN_SUPPORTED = 1;
```

`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectMigrations.h:39-49`:
```cpp
static const std::vector<Migration>& getMigrations() {
    static std::vector<Migration> migrations = {
        // Example migration (uncomment when needed):
        // ...
    };
    return migrations;
}
```

**The framework is correctly built** — `runMigrations` walks gaps, fails cleanly when no path exists, updates the version field on success. But it has **never been run with real input**. When the first post-Beta release ships with `PROJECT_VERSION_CURRENT == 2`:
- Every Beta user's `.aes` files get migrated on first open.
- The migration code path runs in production for the first time, on millions of files we've never seen.
- If a migration has an edge case (a Beta user did something the migration author didn't anticipate), data loss or load failure is the symptom — and users will not be quiet about it.

**Why this is P0 for Beta** (not "post-Beta"):
- Beta IS the input format for v1.0. Every saved Beta project becomes a migration test case six months later. Beta's job is to surface every shape of project before we commit to a schema we have to migrate.
- Mid-Beta, we *will* discover schema changes we need. (Routing, PDC v2's `LatencyDomain`, Arsenal bridge metadata, Muse data structures.) Each one bumps `PROJECT_VERSION_CURRENT`. We need migrations from "every released Beta version" to current.

**Remediation:**
1. **Fixture corpus** — every time `PROJECT_VERSION_CURRENT` is bumped, drop a `.aes` file saved by the previous version into `Tests/Fixtures/projects/v<N>/`. Test that it loads cleanly under the current version.
2. **First real migration as a forcing function** — even before we *need* a v2, bump the schema to v2 with a trivial no-op migration (`migrateV1ToV2: noop`). This exercises the framework end-to-end *now*, not under pressure later. The cost is one bumped field and one test.
3. **Beta schema lock** — declare a date (e.g. October 2026) after which schema changes require an explicit migration + fixture, no exceptions. Until that date, we can iterate freely.
4. **Smoke test for unsupported-version error paths** — assert that loading a v0 file is rejected, loading a v999 file is rejected, and the user-facing error message is correct in both directions.

**Effort:** 1 day (no-op v2 migration + fixture infrastructure), then a discipline-level commitment.

---

## 3. P1 — fix before v1 Beta but not blockers

### P1.1 — No content-integrity check; corrupt files load silently

`ProjectSerializer.cpp` validates structure but does not compute or verify any hash. So:

1. Bit-rot on disk (cosmic ray, fading SSD, bad SD card) corrupts a few bytes inside the JSON.
2. Atomic write protected the file at save time; the rename was clean.
3. On load, JSON parse succeeds (corruption was in a string value, not a structural byte).
4. `validateProjectStructure` accepts the file because it doesn't know what the values *should* be.
5. Project loads with silently-wrong values — a clip at the wrong beat, a pan value flipped, a route pointing to the wrong target.

**Remediation:**
1. **Add a top-level `"contentHash"` field** containing SHA-256 of the JSON body with the `"contentHash"` field stripped. Compute on save; verify on load. Mismatch → user-facing "this file appears corrupt; load anyway?" dialog.
2. **Roll the hash into the autosave / history snapshot path** so we can pick the most recent valid file from the history dir if the active save is corrupt.
3. **Document the policy.** If a user edits a `.aes` by hand (a feature — text format invites this), they need to know to strip the hash or the load will reject it. Or: provide a CLI tool that recomputes the hash post-edit.

**Effort:** half a day for the hash, half a day for the recovery UI.

---

### P1.2 — Asset references are file paths, not content hashes

The serializer stores source assets as paths (`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:1041` accesses `sj[i]["path"]`). If the sample file is moved, renamed, or replaced with different content of the same name, the project loads but the audio is wrong — silently.

The MiniAudio decoder (`@/home/currentsuspect/Dev/Aestra/Source/Core/MiniAudioDecoder.h`) is called during load to validate sources; it catches "file no longer exists" and "file is not decodable" but not "file content has changed."

**Remediation:**
1. **Per-source content hash.** When a source is first imported, hash the audio file (SHA-256 of decoded PCM frames at the project sample rate, or simply SHA-256 of the file bytes).
2. **Store both** `"path"` and `"contentHash"` in each source entry.
3. **On load, validate the hash.** If mismatch:
   - Soft mismatch (file exists, hash differs): warn user, offer to keep using the file or to mark the source as missing.
   - Missing file: existing behavior (offer relink).
4. **Relink-by-hash** — if the path is broken but the hash is known, scan the user's recent project assets / a configured asset library for matching content. Smart relink.

**Effort:** 2 days. The hashing-during-import is straightforward; the UX of the warning/relink dialog is the bulk.

---

### P1.3 — Roundtrip tests assert semantic equality but not field-level fidelity

`@/home/currentsuspect/Dev/Aestra/Tests/Integration/ProjectRoundTripIntegrityTest.cpp:93` (`compareProjectSemantic`) compares counts of lanes/clips/notes/units and lane names. **It does not compare:**

- Numeric values: tempo, BPM after roundtrip, clip start/duration in beats, note start times, automation point positions/values, pan, gain, volume.
- Routing topology: send target IDs, send gains, send pan, sidechain flags, route mode/bridge mode strings.
- Arsenal unit parameters: plugin state, parameter values, route assignments.
- UI state: dialog positions, panel layouts.

So the test confirms "the right *shape* survives" but not "the right *content* survives." A serializer bug that swapped clip start/duration would not be caught.

**Remediation:**
1. Extend `compareProjectSemantic` to walk the JSON trees and compare every leaf numeric value with epsilon, every string value exactly, every array element-by-element.
2. Optionally: use the canonicalization step that already exists (`normalizeJson`) and just compare normalized strings — this catches everything but produces less helpful failure messages.

**Effort:** half a day.

---

### P1.4 — `ProjectSerializer` lives in `Source/` (UI tier) but is project-format-critical

Already documented in the architecture audit (§3.3). Re-flagged here because it's the canonical example of "this code's location implies UI-tier criticality but its function is engine-tier."

Consequences:
- Anything in `aestra-core` build mode that wants to load a project must duplicate the parsing logic or pull in the UI tier.
- The headless test runner has to compile `ProjectSerializer.cpp` separately (twice — once for the main app, once for `HeadlessOfflineRenderer` at `@/home/currentsuspect/Dev/Aestra/Tests/Headless/HeadlessOfflineRenderer.cpp:6` which includes `"Core/ProjectSerializer.h"` — wait, that's a different ProjectSerializer.h path?).
- Migrations that need engine-level types (e.g. plugin state migrations) require backward includes.

**Remediation:**
1. Move `Source/Core/ProjectSerializer.{h,cpp}` and `Source/Core/ProjectMigrations.h` into `AestraAudio/include/IO/` and `AestraAudio/src/IO/`.
2. Keep the UI-thin wrapper (the part that uses `UIState`) in `Source/`.
3. Verify both compile paths resolve to the same object file — no more duplicate compilation.

**Effort:** 1 day. Cross-cutting but mechanical.

---

### P1.5 — `compareProjectSemantic` shells out to programmatic fixtures; no canonical `.aes` corpus exists

`@/home/currentsuspect/Dev/Aestra/Tests/Headless/dummy.aes` is the only stored `.aes` file in the test tree. Every other test constructs its fixtures programmatically (e.g. `@/home/currentsuspect/Dev/Aestra/Tests/Integration/ProjectRoundTripIntegrityTest.cpp:275` builds a JSON string inline).

This is fine for the current tests' goals. But it means:
- There's no "real-world saved project" we can re-load in every future build.
- Reproducing a user-reported load failure requires the user's exact file, which they may not be able to share.
- The migration framework (see P0.1) has no input to migrate.

**Remediation:**
1. Stand up `Tests/Fixtures/projects/` with at least 5 canonical projects: empty, single-track-with-clips, multi-track-with-routing, arsenal-heavy, automation-heavy.
2. Add `FixtureLoadRoundtripTest` that loads each, roundtrips it, asserts content-hash equality.
3. Hand-author at least one fixture file with deliberately weird-but-valid data: very long lane names, max-supported sources count, deeply nested routing, etc.

**Effort:** 1 day to write the fixtures + harness.

---

## 4. P2 — quality-of-life, not blockers

### P2.1 — No schema documentation; the source code IS the schema

There is no `AestraDocs/Project-Format-Schema.md` (or similar) describing what fields exist, what their semantics are, what's required vs optional, what the validation rules are. The code is the spec. For an internal-only format pre-Beta, this is acceptable; for a post-Beta format that third parties may want to read/write (Cloud Takes sync, third-party tooling, debug inspection), it's a gap.

**Remediation:** generate a schema doc from the serializer code, or hand-write one. Either is fine; the goal is one document third parties can read.

**Effort:** 1 day.

---

### P2.2 — `PROJECT_HISTORY_MAX_ENTRIES = 50` is unconfigurable; no quota on disk usage

`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:39` caps history to 50 entries per project but doesn't cap total disk usage. A user with a 200-MB project and 50 history entries silently consumes 10 GB on disk in a hidden history dir. No retention policy beyond entry count.

**Remediation:**
1. Add a `kHistoryMaxBytes = 1 GB` cap.
2. Evict oldest entries when over the byte cap.
3. Surface the history-dir size in the Project Settings UI.

**Effort:** half a day.

---

### P2.3 — `MiniAudioDecoder` validation on load is the only defense for source files

The serializer calls into `MiniAudioDecoder` to verify source files are decodable during load (the SEC-003 fix landed because `numChannels == 0` was a div-by-zero in this path). This is correct defense, but the decoder is a third-party single-source file — if any new defect ships there, every project load is vulnerable.

**Remediation:**
1. Track which version of MiniAudio is vendored (`labs/` or `External/`).
2. Subscribe to its upstream security advisories.
3. Consider replacing `MiniAudioDecoder` for the *validation* phase with a smaller bespoke header-only verifier — RIFF/WAVE structural check, no decode.

**Effort:** 1-2 days; defer if no decoder CVEs surface.

---

### P2.4 — `PROJECT_VERSION_MIN_SUPPORTED == PROJECT_VERSION_CURRENT == 1`

When we bump to v2, the policy choice is binary:
- **Bump both** → drop v1 support entirely. Forces all users to re-save, but no migration code needed.
- **Bump only CURRENT, leave MIN_SUPPORTED at 1** → keep loading v1, run migration to v2 in-process.

Today's value is consistent (1==1) but the intent of the framework is the second option. **Make sure when we ship v2, we bump CURRENT only.**

**Remediation:** add a comment at the constants explaining the policy; add a `static_assert(PROJECT_VERSION_MIN_SUPPORTED <= PROJECT_VERSION_CURRENT)`.

**Effort:** 15 minutes.

---

## 5. What's already excellent

Genuinely strong. Don't break these:

| Item | Where |
|------|-------|
| Atomic write via tmp + rename | `@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:427-470` and replicated in `AutosaveManager.cpp:410`, `UIState.cpp:74`, `Preferences.cpp:120, 144` |
| `.bak` backup before overwrite | `@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:797-807` |
| History-snapshot directory (50 deep) | `@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:560-568` |
| Bounded sizes everywhere (64MB file, 10000 sources, 100 history, max strings) | Constants at top of `ProjectSerializer.cpp` |
| Structural validation runs before any destructive state change | `validateProjectStructure` (`@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:276`) |
| `try/catch` around `std::stoul` parsing (SEC-001, RTM-009 retrofits) | Lane color, clip color, unit color all now guarded |
| Real roundtrip test harness with multiple scenarios | `Tests/Integration/ProjectRoundTripIntegrityTest.cpp` — empty, sources/lanes/clips/patterns, arsenal, missing-pattern non-compounding |
| Specific Arsenal roundtrip tests for routeMode / bridgeMode | `Tests/AestraAudio/ArsenalRouteModeRoundTripTest.cpp`, `ArsenalBridgeModeRoundTripTest.cpp` |
| Legacy compatibility tests (`routeId`-only files still load) | `verifyLegacyRouteIdOnlyProjectLoad` in `ArsenalRouteModeRoundTripTest.cpp:155` |
| Red-team test directory exists with PoCs for each fix | `tests/redteam/poc_*.py` — discipline of building the exploit alongside the fix |
| Session-recovery test exists | `Tests/Integration/SessionRecoveryTest.cpp` |
| Future-version files are rejected with a clear user-facing message | `@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:897-901` |
| Past-version files trigger the migration path | `@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.cpp:913-925` (path exists; just not yet exercised) |

---

## 6. Remediation roadmap

| Sprint | Item | Effort | Risk reduction |
|--------|------|--------|----------------|
| **This week** | P0.1: stand up no-op v1→v2 migration + first fixture | 1d | Forces the migration code path to run in CI; eliminates the "first migration is in production" risk |
| Week 2 | P1.3: deepen `compareProjectSemantic` to walk all leaf values | 0.5d | Catches field-level serialization regressions |
| Week 2 | P1.5: stand up `Tests/Fixtures/projects/` corpus | 1d | Reproducibility floor; migration testbed |
| Week 3 | P1.1: content-hash field + corrupt-file recovery dialog | 1d | Survives bit-rot |
| Week 3 | P1.2: per-source content hash + relink-by-hash | 2d | Survives moved/changed assets |
| Week 4 | P1.4: move ProjectSerializer to AestraAudio (arch audit) | 1d | Removes layering violation |
| Week 4 | P2.x polish | 1d | History quota, schema doc, min-version assertion |

**Total: ~1.5 weeks**, easily parallelizable with PDC v2 since this is purely off-RT, file-format code.

---

## 7. Tests to add

Forms `ProjectFormatRegressionSuite`:

1. **`MigrationV1ToV2NoopTest`** — load a v1 fixture, assert it becomes v2 after roundtrip, assert content-hash equality on the semantic payload. **Lands with P0.1.**
2. **`FixtureCorpusRoundtripTest`** — for each file in `Tests/Fixtures/projects/`, load → roundtrip → save → reload → assert byte-equal canonical form.
3. **`FieldFidelityRoundtripTest`** — programmatically build a project that exercises every persisted type (numeric, string, nested), roundtrip, assert every leaf value equal.
4. **`ContentHashCorruptionDetectionTest`** — flip one bit in the middle of a `.aes` file, assert load fails with the corruption error path (post-P1.1).
5. **`AssetHashMismatchDetectionTest`** — load a project, modify the referenced WAV (rewrite with different content), reload, assert the source is flagged as content-mismatch (post-P1.2).
6. **`MinSupportedVersionInvariantTest`** — `static_assert(PROJECT_VERSION_MIN_SUPPORTED <= PROJECT_VERSION_CURRENT)`.
7. **`UnsupportedVersionErrorMessageTest`** — load a synthetic v0 file, assert the user-facing error mentions "too old"; load a synthetic v999 file, assert the message mentions "update AESTRA."

---

## 8. Open questions

1. **Plugin state versioning.** Each plugin (`AestraComp`, `MasterSafetyLimiter`, etc.) serializes its own state with a per-plugin magic + version (e.g. `kStateMagicV2 = 0x434D5002` in `AestraComp.h:24`). When a plugin version bumps, the project's stored plugin state may need migration too. Is there a per-plugin migration framework, or are plugins expected to handle backward compat internally? (Spot-check suggests the latter — fine, but worth documenting as a project-format invariant.)
2. **Autosave file naming.** `AutosaveManager` stores backups in a separate directory. Need to confirm: if autosave detects an unsaved-since-crash session, does it use the same atomic-write + backup pattern as `ProjectSerializer::save`? (Quick grep says yes — `AutosaveManager.cpp:410` does `fs::rename`. Good.)
3. **Cross-platform path handling.** Source paths stored in `.aes` files: are they absolute, relative-to-project, relative-to-asset-library? A project saved on macOS with `/Users/dylan/...` paths and opened on Linux will have all-broken asset references. Cloud Takes (post-Beta) makes this much worse.
4. **Encryption.** Project files are plaintext JSON. For studio collaborators sharing files, plaintext is fine. For Cloud Takes (post-Beta), at-rest encryption may be a requirement. Out of scope for v1 Beta but a v1.2 design call.
