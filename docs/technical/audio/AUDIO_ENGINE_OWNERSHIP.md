# AudioEngine Ownership Contract (R2)

`AudioEngine` has **no process-wide instance**. Every engine is explicitly owned by
whoever constructs it, and every other holder borrows a non-owning pointer or
reference that must not outlive the owner.

This document records the contract as it actually holds in the code, established by
the T-3 call-site audit (2026-09-05). It is descriptive, not aspirational — where the
code diverges from the doctrine, that divergence is recorded in
[Known residuals](#known-residuals) rather than written out of the contract.

## Why there is no singleton

The removed mechanism was `AudioEngine::getInstance()` plus a file-static
registration pointer written from the constructor. It shipped a crash: the accessor
aborted when no engine existed, and registration was last-constructor-wins, so two
live engines silently corrupted the global.

Both halves of that failure are ownership failures, not accessor failures — which is
why the fix was explicit ownership rather than a safer accessor.

## The contract

1. **One owner, by value or `unique_ptr`.** The owner's lifetime bounds the engine's.
2. **Everyone else borrows.** `AudioEngine*` or `AudioEngine&`, injected through a
   constructor or a setter. No holder extends the engine's lifetime.
3. **No static, global, or thread-local engine** — object, pointer, reference, class
   member, or function-local static.
4. **Dependencies thread through the owner.** A type that needs the engine takes it
   as a parameter; it does not reach for an ambient one.
5. **Multiple live engines are supported.** Construction, destruction, and
   interleaved lifetimes are independent in any order.

`AudioEngine` is neither copyable nor movable: `unique_ptr` members implicitly delete
the copy operations, and the user-declared destructor suppresses the move operations.
This is relied upon but not spelled out in the class — see
[Known residuals](#known-residuals).

## Who owns an engine

| Context | Owner | Lifetime |
|---|---|---|
| Application | `AestraAudioController::m_audioEngine` (`std::unique_ptr`) | Controller |
| Headless render / export | stack local in `HeadlessMain`, `HeadlessExportMain` | Function scope |
| Muse REPL | stack local in `MuseReplMain` | Function scope |
| Tests | stack local or `unique_ptr` per test | Test scope |

The application process holds **exactly one** engine. `AudioExporter` takes
`AudioEngine&` and renders through the engine it is given, so export does not create a
second one. Headless tools are separate processes.

## Who borrows one

All non-owning, all injected:

`AestraContent` · `AestraApp::getAudioEngine()` (accessor over the controller's engine)
· `MuseService` · `CommandContext` / `CommandRegistry` · `PlaybackGraphController` ·
`AudioSettingsPage` · `ExportDialog` · `PianoRollPanel` · `PerformanceHUD` ·
`UnifiedHUD` · `UnifiedProfiler` · `AudioExporter` · `AudioRenderer`

**A borrower must not outlive the owner of the engine it borrows.** The rule is stated
against the owner rather than against any one owner, because the same type can borrow
from different owners: `AudioExporter` takes the controller's engine when the
application exports, and a stack-local engine when a headless tool does.

In the application this holds structurally — `AestraAudioController` is owned by
`AestraApp` and outlives the UI and service objects that borrow from it. In headless
tools it holds by scope: the borrower is created and destroyed inside the frame that
owns the engine.

## Enforcement

Two independent mechanisms, both in CI:

- **`NoAudioEngineSingletonGuard`** (`Tests/Guards/no_audioengine_singleton.cmake`) — a
  zero-tolerance source scan for the legacy accessor, the legacy registration global,
  and the structural shapes a renamed reintroduction would take. **No exemptions**;
  the last one (`CommandRegistry`) was retired by #559. It has its own self-test.
- **`AudioEngineOwnershipTest`** — two live engines are independent, destroying either
  order leaves the other intact, and interleaved lifetimes all render.

**These two prove different things, and the difference matters.**

The guard is a textual tripwire, not a static analyzer. What it establishes is the
absence of the old singleton *vocabulary* — the legacy accessor, the legacy global,
and the structural shapes a renamed reintroduction would take. It cannot establish
that ownership is semantically correct. An engine pointer parked inside an unrelated
named struct passes it cleanly.

What establishes the actual model is `AudioEngineOwnershipTest` plus the call-site
audit behind this document: every construction site identified, every borrower
identified, and each one checked against the contract. A green guard on its own would
be consistent with a codebase that had merely renamed its way around the ban.

The T-3 audit is the demonstration. The guard was green, and the audit still found a
one-engine-per-process assumption surviving inside the class — see
[Known residuals](#known-residuals). Absence of the old vocabulary was never proof
that the assumption was gone.

## Known residuals

**RT-misuse accounting is process-wide, not per-engine.**
`AudioEngine::performNonRealtimeMaintenance()` — an instance member — reads and drains
file-scope counters (`g_rtMisuseCount`, `g_rtMisuseReportedCount`, `g_rtMisuseLastApi`
in `AudioEngine.cpp`), and `installRealtimeMisuseHandler()` installs the handler once
per process behind a function-local static.

With two live engines this misattributes: a violation committed under engine A is
logged by whichever engine drains it first, and draining it silences the other. The
counters survive an engine's destruction, so a fresh engine can report a violation
that a previous one caused.

This is diagnostics only — no audio path depends on it — and it is unreachable in the
application, which runs a single engine. It is nonetheless a one-engine-per-process
assumption inside the class whose contract says otherwise, in exactly the
configuration `AudioEngineOwnershipTest` blesses. The singleton guard does not catch
it because the state is counters, not an engine instance.

Tracked as [#885](https://github.com/currentsuspect/Aestra/issues/885). It shares a
root with [#883](https://github.com/currentsuspect/Aestra/issues/883) — those same
counters are never published, so the violations they record are discarded. Moving
RT-misuse accounting onto `AudioTelemetry` (already a per-engine member) closes both
in one change. Update this section when it does.

**Copy and move are deleted implicitly, not explicitly.** The suppression is a
consequence of member types rather than a stated intent, so a future refactor that
removes the last `unique_ptr` member would silently make the engine copyable, and the
compiler error a bad copy produces today points at a member rather than at the
contract.

## References

- FD-13 — v0.7.1 trust sprint scope (`Aestra-Internals`, `11 Decisions and Tradeoffs`)
- #559 — retired the last singleton-guard exemption (`CommandRegistry`)
- [THREADING_MODEL.md](../THREADING_MODEL.md) — thread ownership and RT constraints
