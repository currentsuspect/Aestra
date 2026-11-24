# Welcome to Aestra

Aestra is a C++17 digital audio workstation built with care for the people who make music.

Our philosophy is simple: make capable music-making accessible without sacrificing craft, quality, or respect for the producer.

Build with that in mind. Consult `philosophy.md` when a task involves product direction or an unresolved design decision.
Founder decisions are recorded as `FD-NN`. If you can't find one, ask the user or check [CONTRIBUTING.md](CONTRIBUTING.md).

## How We Work

- Understand the existing implementation before changing it. Prefer existing patterns over new abstractions.
- Make small, focused, reviewable changes, one scoped change at a time.
- Verify your changes; never claim tests or checks you haven't run. Never fabricate results, SHAs, or numbers.
- A search that finds nothing is not proof that something is missing. Trace callers, `*/External/`, merged PRs and the git log first.
- Explain meaningful tradeoffs rather than silently making decisions.
- Prefer compounding gains: when a concrete opportunity exists, improve the shared tool, test, or pattern that makes future work easier. Avoid speculative abstractions.
- Optimize only what you measured, and verify the improvement. Correctness and maintainability come first.
- **Test first** for bug fixes and for engine, DSP and real-time changes: write the test, watch it fail on unchanged code, then fix. When a test fails, read why before changing code; when it passes, ask whether it would have failed without your change.
- **UI changes need eyes.** Screenshot the result and compare it with the approved design. Report what you could not reach as unchecked.
- The repo is usually checked out in several worktrees. A branch checked out elsewhere can't be switched to; confirm `git branch --show-current` before any `reset --hard` or forced rebase.
- Never `pkill -f` or `pgrep -f` a pattern that appears in your own command line. Find processes by port, PID file or exact name.

## Boundaries

- Never modify `main` or `develop` directly. Branch from `develop`; flow is `feature/*` → `develop` → `main`.
- Never commit, push, merge, or rewrite history without authorization. Where the owner has authorized merging (FD-61), merge only when every required check is green.
- Never discard existing user changes.
- Never introduce secrets, private assets, premium binaries, model weights, or credentials.
- Never change licensing, pricing, release policy, or public product commitments without authorization.
- Never weaken tests, CI, or security checks, or silence a reviewer, compiler, sanitizer, or CI finding, without a clear technical reason. Never hide a failure behind a fake fallback or a misleading success path.
- Public docs describe only what is implemented and validated.
- Never edit tracked runtime state files (`audio_settings.conf`, `browser_settings.json`, `autosave.aes`, logs) unless asked; revert them if testing changes them.
- Never add global AVX/AVX2/AVX512 flags. Use runtime dispatch or per-file targeting.

## Critical Areas

- **Real-time audio:** No allocations, locks, blocking calls, logging, file I/O, sleeps, exceptions, JSON, or plugin discovery in audio callbacks. Use preallocated buffers, bounded work, and lock-free queues, with NaN/Inf protection and safe bypass.
- **DSP:** Preserve bypass parity, latency, and parameter IDs. Avoid clicks, denormals, and loudness jumps. Check silence, impulses, and other sample rates.
- **Project persistence:** Treat serialization, migration, and compatibility as high-risk. New fields get defaults or migration, unknown fields are not dropped silently, corrupt critical state fails loudly, and roundtrip tests are updated.
- **Security:** Treat plugins, project files, and external input as untrusted.
- **Public builds:** Preserve compatibility with the public/core build; UI dependencies must not leak into headless builds.
- **Design:** Don't make product design decisions before presenting them to the user.

If a task touches one of these areas, investigate its existing contracts and tests first. The durable ones are [engineering health](docs/technical/engineering-health.md), [audio engine ownership](docs/technical/audio/AUDIO_ENGINE_OWNERSHIP.md), and the [pan law policy](docs/technical/audio/PAN_LAW_POLICY.md).

## Building and Testing

- Follow `.clang-format` and `.clang-tidy`. [BUILD.md](BUILD.md) and [testing and CI](docs/technical/testing_ci.md) are the two docs you hit first.
- Build the affected target, run the narrowest relevant test, then broaden for shared systems.
- **A headless build proves nothing about `Source/` or `AestraUI/`.** For those, configure a UI build with every flag explicit (they are cached): `-DAESTRA_CI=OFF -DAESTRA_HEADLESS_ONLY=OFF -DAESTRA_ENABLE_UI=ON -DAESTRA_ENABLE_TESTS=ON -DAESTRA_ALLOW_LICENSE_GATE_OFF=OFF`, then `cmake --build build-ui --target Aestra`. `AESTRA_CI=ON` silently force-disables the UI.
- Runtime/device tests stay behind `AESTRA_ENABLE_RUNTIME_TESTS`, experimental ones behind `AESTRA_ENABLE_EXPERIMENTAL_TESTS`. UI and manual checks are reported as manual, not automated proof.
- RtAudio and RtMidi sources compile only into the platform backend targets, never into core or headless libraries. Don't remove GNU ld linker groups without validating a Linux build.
- New tests carry their `contract:*` labels from the start. Run every guard CI runs before pushing.

## Compounding Rules (FD-81)

Every slice makes the next one easier:

1. Behaviour lives in a tested model; the screen only draws it.
2. Solve an interaction once, in the kit. Never write a third hand-built copy.
3. One source of truth, and one named path per capability.
4. Small slices, merged the same day; at most two PRs stacked.
5. Recipes for launching, driving, or screenshotting the app belong in `tools/`.
6. Measure each slice; when one costs more than the last, build the missing reusable piece.

## Pull Requests and Review

- Use [the PR template](.github/pull_request_template.md). It requires the **decision citation** (`FD-NN @ <vault SHA>`, checked by CI), a summary, why, testing, risk, and a **producer note**.
- The producer note feeds the public changelog. Write one sentence a producer would understand ("Hitting pause no longer rewinds you"), or `None`. Most PRs are `None`.
- UI, editor, and Muse changes include a flow check (FD-67): does it ask the producer for anything it could already tell?
- Request CodeRabbit with `@coderabbitai review` (about one review an hour); `@coderabbitai approve` files its approval once findings are dispositioned.
- Answer findings by fixing them, explaining a false positive with evidence, adding a targeted suppression with a comment, or opening a follow-up issue. Never suppress a whole category or remove the reviewed code unread.
- Stacked PRs lose approval when the PR below merges; expect to re-request it.

## When You're Done

Report:
- What changed.
- Which files changed.
- What you tested.
- What you couldn't test.
- Any remaining risks.

Be factual. Don't claim more than you verified.
