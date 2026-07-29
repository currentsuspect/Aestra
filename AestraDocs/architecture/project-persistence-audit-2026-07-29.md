# Project Persistence Audit — 2026-07-29

## Scope and baseline

This audit evaluates the `develop` tree that started at
`ba80b6058d2e98682891f9f5d88d93483dfc965b`. It treats source, executable
tests, fixture bytes, and Git history as evidence; older audit prose is not
used as current truth.

The implementation columns below describe the audited starting tree and call
out the EOD changes in this branch separately. `PROJECT_VERSION_CURRENT` is 3
and `PROJECT_VERSION_MIN_SUPPORTED` is 1.

## Guarantee matrix

| Guarantee | Current implementation | Exact evidence | Test coverage | Remaining failure mode | EOD action | Long-term action |
| --- | --- | --- | --- | --- | --- | --- |
| 1. A crash or failed save never destroys the last valid generation. | Named saves serialize before touching disk, copy the active file to `.bak`, sync a `.tmp`, atomically replace, sync the parent directory, then write a complete history snapshot. Autosave rotates its active generation before the same sync/replace pattern. | `ProjectSerializer.cpp`: `writeAtomicallyImpl`, `save`, `writeHistorySnapshot`; `AutosaveManager.cpp`: `writeAutosaveData`, `rotateBackups`. | `ProjectRoundTripIntegrityTest`, `ProjectIntegrityCheckTest`, `ProjectHistoryCapTest`, `AutosaveManagerTest`, `SessionRecoveryTest`. | The `.bak` copy is best effort; a POSIX parent-directory sync failure is reported after rename already replaced the active file; Windows autosave removes the destination before `std::filesystem::rename`, creating a non-atomic gap. Failure injection does not cover every filesystem boundary. | Audit only; no save algorithm change in this slice. | Add platform fault-injection tests and use a replace primitive with durable atomic semantics on every platform. |
| 2. Every released format remains loadable. | Loader accepts every integer v1–v3. Explicit 1→2 and 2→3 edges exist, both deliberately report no transformation because interpretation is compatible. | `ProjectSerializer.h`: version constants; `ProjectMigrations.h`: registry; history commits `f29bbb4a` (v2) and `8510d1d3` (v3). | Before: only the hand-authored v1 corpus. EOD: v1/v2/v3 table-driven corpus and registry completeness test. | No authentic v1 serializer output is present; compatibility before this branch depended on tests that did not enumerate every supported version. | Add authentic v2 and v3 fixtures, enumerate v1–v3, and gate the full adjacent registry. | Preserve all released fixtures and old loader interpretation indefinitely; locate a genuine v1 artifact if one survives. |
| 3. Migration never modifies the only original copy. | Parsing and migration operate on an in-memory `JSON root`; load has no write to the source path. The application only writes after a later user/autosave action. | `ProjectSerializer.cpp`: `load` parses before `ProjectMigrations::runMigrations`; all writes are confined to `save`/`writeAtomically`. | Fixture migration tests load read-only inputs and re-save to temporary paths. | A later user save can intentionally replace the canonical file, subject to guarantee 1. | Preserve behavior and document it as policy. | Keep migrations pure with respect to disk and add a source-byte invariance assertion for every real transforming migration. |
| 4. The application knows when migration changed data and requires a save. | Starting tree: impossible; `LoadResult` had no migration outcome and `AestraApp::applyLoadedProject` always called `setModified(false)`. EOD: source/result versions plus `None`/`Transformed`/`Failed`; only `Transformed` marks the project modified. | Issue #662; `ProjectSerializer.h`: `LoadResult`; `ProjectMigrations.h`: outcomes; `AestraApp.cpp`: `applyLoadedProject`. | EOD: test-only real transform proves both dirty and clean decisions; source guard pins the app call site. | Production edges are both no-ops, so no shipped fixture can exercise a real transformation. `Source/` has no test-linkable application target (#666), preventing a behavioral application-lifecycle regression. Dirty state also has wider observer-authority debt (#663). | Implement issue #662 without dirtying an older version number alone. | Resolve #666 and replace the source guard with an application behavior test; resolve #663 independently. |
| 5. Missing dependencies degrade the affected object, not unrelated structure. | Missing source paths remain source records; missing patterns create placeholders; missing Arsenal unit IDs remain on MIDI notes; unresolved sends remain records; missing plugins are occupied non-RT placeholders. | `ProjectSerializer.cpp`: Phase 2 reference validation and Phase 4 commit; `EffectChain.h`: `hasMissingPlugin`/`isOccupied`; `EffectChain.cpp`: placeholder load/save. | `ProjectLoadRegressionTest`, `EffectChainMissingPluginTest`, EOD v3 project fixture. | Unknown JSON fields are not passed through. A missing binary cannot make its DSP available. Some route/object types have no permanent record identity. | Add only absent full load→save→load assertions. | Add explicit archive/runtime adapters and unknown-field preservation without weakening validation. |
| 6. Degraded objects survive another save/load. | Missing patterns and unresolved sends already round-tripped. Effect-chain plugin placeholders already preserved ID, slot, bypass, dry/wet, and opaque state. Asset paths and missing Arsenal unit IDs were implemented but not pinned through a second load. | `ProjectLoadRegressionTest`; `EffectChainMissingPluginTest`; PR #656 (`f8f907fd`). | EOD adds asset and Arsenal-unit second-load checks plus a v3 full-project missing-plugin fixture. | Plugin automation still addresses a positional slot, not a permanent plugin instance. Unknown future object fields are dropped. | Pin all currently recoverable degraded types without redesigning identity. | Permanent plugin-instance identities and identity-based automation addressing; opaque unknown-field passthrough. |
| 7. Future versions are refused non-destructively. | Version rejection occurs before Phase 4 clears models. Invalid structure and corrupt parse also return before commit. Phase 4 exceptions use a private rollback snapshot. | `ProjectSerializer.cpp`: version checks before Phase 2/4 and rollback block. | Existing corrupt-load regressions; EOD explicit v0/future probes preserve pre-existing lane/channel state. | A Phase 4 exception can still leave state damaged if private rollback creation or restoration fails. | Add the explicit v0/future non-destructive gate. | Make loading transactional at the model boundary rather than rollback-after-mutation. |
| 8. Historical fixtures are immutable evidence. | Starting tree had two v1-named fixtures, both hand-authored or later hand-edited, and no byte manifest. EOD records provenance and pins SHA-256 for every fixture and asset. | Fixture history: `f41829d6`, `7bdb65b2`, `f23e33aa`; `Tests/Fixtures/ProjectFormat/README.md`; `verify_project_fixture_manifest.cmake`. | `ProjectFixtureImmutabilityTest`. | v1 still lacks authentic writer output; a hash guard protects evidence only after this branch lands. | Add honest provenance and byte hashes; never relabel hand-authored files as serializer output. | Preserve release-built writers/artifacts and generate the new fixture before each schema bump merges. |
| 9. Every schema bump has an adjacent migration and fixture before merge. | Starting tree relied on a hand-maintained vector with no completeness enforcement. EOD validates exactly one `N→N+1` edge and one enumerated fixture for every supported integer version. | `ProjectMigrations::validateRegistry`; `ProjectCompatibilityPolicyTest`; fixture manifest. | Missing, duplicate, skipped, wrong-target, failed, and transformed migration paths are executable. | A contributor can still bypass policy by editing both production and tests dishonestly; review remains part of the control. | Add migration and fixture gates to always-on CTest. | Make the schema checklist a required PR/release gate and retain independent fixture provenance review. |
| 10. Recovery is executable in CI. | Autosave serialization, atomic write, rotated backup discovery, corrupt-primary fallback, recovery identity, and integrity behavior are executable. | `SessionRecoveryTest`, `AutosaveManagerTest`, `ProjectLifecycleInteractionTest`, `ProjectDocumentStateTest`, `ProjectIntegrityCheckTest`. | Registered headless/core tests; no hardware is required. | The UI dialog path and crash/power-loss filesystem semantics are not end-to-end tested. If the recovery dialog is unavailable, the app currently removes autosave state. | Keep existing coverage; include it in the validation gate. | Add application-level startup recovery tests after #666 and platform crash/fault harnesses. |

## Explicit audit answers

### 1. Which versions have authentic immutable fixtures?

- v1 has two representative fixtures, but neither is authentic serializer
  output.
- v2 now has authentic output from `b4af8de2`, the last serializer revision
  before the v3 bump.
- v3 now has authentic output from the EOD starting tree at `ba80b605`.

The EOD manifest makes all five fixture/asset files immutable from this point
forward.

### 2. Were the fixtures produced by the claimed serializer?

`v1_minimal.aes` was added at `f41829d6` while v1 was current but was
hand-authored and later edited at `7bdb65b2` after v2 existed.
`v1_rich.aes` was explicitly hand-authored at `f23e33aa` after v2. The new v2
and v3 fixtures were emitted by their respective serializers; their exact
provenance and generation method are recorded beside them.

### 3. Is every adjacent migration explicit?

Yes: 1→2 and 2→3 are registry entries. Both are no-ops. The v2→v3 no-op is
deliberate: the v3 loader directly interprets v2 lane-local mixer state after
restoring stable channel identities. Before the EOD gate, nothing proved the
registry remained complete or adjacent.

### 4. Is interpretation confined to `ProjectMigrations`?

No. `ProjectMigrations` only advances schema edges. `ProjectSerializer::load`
contains legacy/default interpretation, including lane-local mixer state,
source/channel route fallback, reference placeholders, and old integrity
handling. The effective migration boundary is therefore split between the
registry and loader.

### 5. Can a caller distinguish unchanged and migrated loads?

Not at the starting commit. The EOD `LoadResult` exposes source schema,
resulting schema, and outcome. It intentionally distinguishes “schema stamp
advanced through compatible no-op edges” from “in-memory data transformed.”
Only the latter requires a save.

### 6. Does load overwrite the source automatically?

No. `load` reads and mutates only the parsed JSON and target model. It creates a
private rollback file for the pre-load in-memory model before Phase 4, but
never writes the source project. Canonical replacement happens only through an
explicit later save.

### 7. Are failed old, future, or corrupt loads non-destructive?

Old/future rejection, parse failure, integrity rejection, and structural
validation occur before the active model is cleared and are non-destructive.
The EOD v0/future probes prove this. Exceptions after Phase 4 begins depend on
a best-effort rollback file; if rollback creation or restoration also fails,
the guarantee is not absolute.

### 8. Do missing-plugin placeholders survive load → save → load?

Yes after PR #656. `EffectChain::loadState` retains plugin ID, exact slot,
bypass, dry/wet, and opaque state when instance creation fails; `saveState`
emits that record in the same slot. `isEmpty()` remains the real-time
`plugin == nullptr` predicate and `isOccupied()` is the non-RT occupancy
predicate. Existing effect-layer tests cover repeated roundtrips; the EOD v3
project fixture additionally proves project load, missing-plugin reporting,
project save, and second project load.

### 9. Are plugin automation targets stable identities?

No. Mixer automation carries a stable `mixerChannelId`, and the parameter uses
a stable plugin `paramId`, but the plugin instance is addressed by positional
`effectSlot`. There is no permanent plugin-instance ID, so slot reorder can
retarget automation. This is explicitly deferred from the EOD slice.

### 10. Does CI fail when a supported version lacks a migration or fixture?

Not on the starting tree. The EOD policy test iterates every integer from
minimum through current for fixtures, validates exactly one adjacent edge below
current, and self-tests missing, duplicate, skipped, failed, transformed, and
wrong-target behavior. The byte manifest fails if an existing fixture changes.

### 11. Are history generations independent?

Each `.history` entry is a complete, independently parseable project JSON file,
not an alias to the active file. It contains the newly serialized generation
written during that save, so it may be byte-identical to the active file at
that moment; successive entries diverge only when serialized state changes.
The `.bak` file is the previous active generation at the time a named save
begins.

### 12. What persisted data mirrors runtime architecture?

The format directly exposes source-manager numeric IDs and paths,
`PatternSource` variants and payloads, playlist lane/clip layout, mixer channel
objects and route vectors, effect-chain binary blobs, Arsenal `UnitManager`
JSON, automation curve types plus positional effect slots, and selected UI
panel state. Those representations couple the archive to current manager and
container shapes; future refactors need adapters or explicit migrations rather
than silently reinterpreting the same fields.

### 13. What is honest today?

At the starting commit, Aestra could honestly say it uses durable temp-file
replacement, preserves a previous named-save backup, keeps bounded autosave and
history generations, rejects unsupported input before normal commit, recovers
from a corrupt active autosave when a rotated backup is valid, and loads
structurally valid v1–v3 files covered by the available corpus.

“Never lose a session” was still misleading as an unconditional marketing
claim: migration transformations could be silently marked clean; v2/v3 had no
historical fixture; v1 fixtures were not authentic writer output; compatibility
completeness was not a merge gate; Phase 4 rollback and cross-platform atomic
replacement are not absolute; assets have paths but no content hashes (#264);
plugin automation is positional; unknown fields are not preserved; and the
application lifecycle cannot yet be behaviorally tested without #666.

The EOD work makes compatibility enforceable from this point forward. It does
not retroactively manufacture authentic v1 evidence or solve the deferred
archive, asset, and plugin-identity limitations.

## Starting-state EOD score

The requested foundation was **20% already present (4/20 points)** at
`ba80b605`, using this rubric:

| Foundation area | Possible | Present at start | Basis |
| --- | ---: | ---: | --- |
| Observable migration metadata and lifecycle | 4 | 0 | No outcome metadata; unconditional clean. |
| Complete, authentic, immutable fixture matrix | 5 | 1 | v1 corpus loaded/re-saved, but no v2/v3 fixture, authentic v1, or byte manifest. |
| Migration completeness gates | 4 | 0 | Edges existed but no missing/duplicate/skip/target gate. |
| Degraded-session persistence regressions | 5 | 3 | Missing pattern, unresolved plugin at effect-chain level, and corrupt-primary recovery were already pinned; missing asset and Arsenal-unit second loads were not. |
| Compatibility constitution | 2 | 0 | No mechanical schema-bump contract. |
| **Total** | **20** | **4** | **20%** |

## Issues and plan corrections

- Reused #662 for observable migration state and the “actual transformation
  only” dirty rule.
- Reused #663 as dirty-observer authority background; not expanded here.
- Reused #666 as the application-layer testability constraint; not solved here.
- Reused #264 for asset hashes/smart relinking and #266 for serializer
  layering; both remain deferred.
- Created #668 for the confirmed Windows autosave remove-before-rename gap;
  this platform commit-boundary change is intentionally separate from the
  project-format foundation.
- PR #656 falsified the original assumption that missing-plugin preservation
  remained broadly unsolved. The implementation was already correct at the
  effect-chain layer; this slice only adds full-project evidence.
- The focused regression exposed duplicate `missingAssets` entries from
  preflight and commit validation. Cardinality is therefore not a stable
  public contract; this slice asserts that the preserved path remains reported
  and does not expand into unrelated result cleanup.
- No issue was duplicated.
