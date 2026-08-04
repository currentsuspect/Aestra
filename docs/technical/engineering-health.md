# Aestra Engineering Health

**Status:** durable engineering contract
**Audience:** anyone writing or reviewing a change to Aestra

This document is not a plan and not a status report. It is the set of properties
Aestra's architecture is expected to hold, written so a reviewer can point at one
and say *"this change violates it"*.

Weekly targets are tracked internally and expire. Nothing in this file
should ever describe work in flight — if a principle here is satisfied, it stays
here anyway, because it is what stops the problem from coming back.

## The north-star questions

Every substantial change should be able to answer both:

> **What became the single source of truth?**
>
> **What now prevents that truth from drifting again?**

If neither has an answer, the change is probably maintenance rather than
architecture — which is fine, but it should not be described as the latter.

---

## 1. One source of truth per concept

A concept has exactly one authority. Everything else derives from it or asks it.

**What drift looks like:** the same value computed independently in several
places; a "cache" that is really a second writable copy; a persisted field
shadowing a live runtime owner that never reads it.

**What to look for in review:** if a change adds a second place that decides the
same thing, the change should say why, and what keeps the two in agreement. A
constant repeated thirty times agrees only by luck.

**Precedent:** `gridStartX = controlAreaWidth + 5` existed 37 times across seven
translation units (#550). `Preferences` looked like the settings model while
seven of its fields had no readers at all (#630) — a setting that persists and
governs nothing is worse than a missing setting, because it reports success.

---

## 2. Audit before implementing a stale issue

**Old issues are hypotheses, not specifications.** Before implementing one,
reproduce the current failure and identify the authority currently responsible
for it.

**What to look for in review:** for any change that cites an issue older than the
architecture it touches, the description should say whether the issue's premise
still holds. When it does not, the right output is a re-scope or a new issue —
not an implementation of the stale text.

**Precedent:** three issues in one day described real historical problems and
none described the present root cause. #247 asked for Linux plugin crash
protection; that objective was already met by out-of-process hosting, while the
live gap was the inverse (Windows cannot start the helper at all) and became
#633. #551's "some clip operations forgot `markModified()`" was true, but
patching those call sites would have preserved the design that made forgetting
possible.

**Corollary — see also principle 8.** Closing an issue as "already satisfied" is
a claim that needs the same evidence as a fix.

---

## 3. User cancellation is not history

An operation the user abandoned leaves the undo stack exactly as it found it.

**What drift looks like:** a cancel path that "reverts using a command so it's
undoable". A revert command captures the *current* state as its original, and at
cancel time the current state is the abandoned one — so undo re-applies exactly
what the user rejected. Pushing it also clears the redo stack, discarding
history that had nothing to do with the cancelled gesture.

**What to look for in review:** cancel paths restore state directly. If a live
gesture never entered the history (because it mutates the model directly for
smoothness), its cancel has nothing to undo.

---

## 4. Failed operations do not enter history

An operation that did not change the project is not a project change: no undo
entry, no dirty flag, no leftover scaffolding.

**What drift looks like:** `construct → discover failure → clean up hopefully`.
Structure created before validation and removed on failure by a rollback path
that every future failure branch must remember to call. Or a success reported
unconditionally because the work happened in a `void` helper.

**What to look for in review:** validate what is cheap to validate before
creating anything; make what must be created part of the same transaction as the
thing it exists for; and return the real outcome rather than assuming it. A
handler that cannot fail is usually a handler that cannot report failure.

---

## 5. Stable identity survives undo and redo

An entity re-created by a redo keeps the identity it had before the undo.

**What drift looks like:** "the ID will be different on redo since we can't reuse
the old ID". Anything grouped in the same undo step still refers to the old
identity, so replay silently attaches those members to something that no longer
exists.

**What to look for in review:** any command that creates an identified entity
must restore that identity on re-execute, not only in `redo()` — batching
mechanisms may replay members through `execute()`. Identity restoration should
fall back to a fresh ID when the old one has been taken, never assume it is free.

---

## 6. Headless, live and export paths share authorities

Where the same concept exists in more than one execution path, the paths share
the implementation rather than each having their own.

**What drift looks like:** an offline render that differs from playback in
master-stage processing; a project that can only be loaded, validated or
migrated by the desktop application; two rendering systems where there should be
one system with two configurations.

**What to look for in review:** a new path that reimplements an existing one
should say why the existing authority could not be used, and what keeps the two
in agreement when one changes.

---

## 7. Narrowing CI must fail broad, never fail open

Anything that reduces what CI runs may only do so when it is certain, and every
uncertainty must resolve toward running more.

**Non-negotiables:**

- Unknown, unrecognised or new paths → run everything.
- A change set too large to trust, or one whose file list disagrees with the
  authoritative count → run everything.
- Changes targeting `main` → run everything.
- Every required check always reports a conclusion. A required context that never
  reports does not fail; it hangs every pull request forever.
- Pushes to `develop` always run the complete suite. Selective CI is a
  pull-request optimisation, never a definition of repository truth.

**Test selection has a specific trap:** running a labelled subset silently drops
unlabelled tests, which redefines "green" without anyone deciding to. Selective
test execution stays blocked until labelling is trustworthy, with
*unlabelled → always runs* as the standing rule.

**Liveness is part of the contract.** A classifier that has silently died answers
"broad" to everything and passes every safety assertion while doing nothing. Any
narrowing mechanism needs at least one test that fails when the narrowing stops
working, not only tests that fail when it becomes unsafe.

---

## 8. "Fixed" requires reproduction, or an explicit obsolescence argument

A change that claims to fix something demonstrates the failure it removes —
ideally as a test that fails before the change and passes after. Where the
architecture has made the old failure impossible, say so explicitly and show
what makes it impossible.

**What to look for in review:** "added a test" is not the same as "the test
rejects the bad state". Verify the gate by reverting the fix and watching it
fail. Where a fix cannot be tested at the layer it lives in — UI gestures have no
headless harness — say that plainly and pin the shape of the change one layer
down, rather than implying coverage that does not exist.

### The verification ladder

These four are different claims, and they get blurred precisely when a change is
hard to test:

| rung | means | typical evidence |
|---|---|---|
| **compiled** | the code is legal and links | CI build lane |
| **code path executed** | the branch actually ran | a log line, a counter, a coverage run |
| **behaviour observed** | the change did the thing, once | screenshot, trace, driven gesture |
| **behaviour regression-tested** | the covered scenario is protected against silent regression | a test that fails when the change is reverted |

A change may legitimately ship at any rung. What is not legitimate is claiming a
higher one than was reached, or leaving the rung unstated so a reader assumes the
top. Say which rung each part of a change is on.

The top rung is not absolute either: a test protects the scenario it covers and
nothing more, and it can still be skipped, flaky or incomplete. "Regression-tested"
means *this* behaviour is pinned — not that the feature is safe. Naming the
covered scenario is part of the claim.

**Rung and extent are separate axes.** The rung says what *kind* of evidence
exists; the named scenario says how far it reaches. Two changes can both be
"regression-tested" while one pins a single reproduction and the other pins an
entire semantic input space — the same rung, wildly different extent. State both:
`safeClampFloat` is regression-tested over finite/NaN/±inf against normal,
inverted, degenerate and non-finite bounds (#640), which is a very different
claim from a test that reproduces one reported bug, even though both sit on the
top rung.

**Why the second rung is not free.** A fix can compile, read correctly, and never
run. #638 shipped a menu-toggle fix whose guard sat in a handler that a mouse
click never reached with the state it tested for — an earlier handler had already
cleared it. The code was locally correct and globally inert. One log line settled
in a single run what re-reading the file could not.

**Observe before constructing a causal story.** Not because reasoning is bad, but
because a false premise about control flow yields an impeccable explanation and a
useless fix. This matters most for composition bugs, where every function is
individually correct and the defect exists only in their ordering — those hide
from single-file reading by construction.

---

## 9. Behaviour-preserving refactors stop where equivalence cannot be demonstrated

A refactor described as behaviour-preserving must actually be. When part of it
would change behaviour, that part stops and becomes its own change with its own
justification and its own validation.

**What drift looks like:** unifying call sites that were *nearly* identical, and
absorbing the difference into the new abstraction. The refactor lands green, the
behavioural change ships unannounced, and the commit message says the opposite.

**What to look for in review:** where a unification has to pick between variants
that were not equivalent, the pick is a behavioural decision. Leaving the seam
visible and documented is better engineering than a prettier abstraction that
silently resolves it.

---

## Working rules

**Instrument before theorising about runtime behaviour.** For anything about
what the running system does — input, ordering, timing, "it does X when I do Y" —
add the trace and observe first. The existing logs are often enough. This is not
a substitute for reading the code; it is what stops you reading the wrong file
confidently. (Pure logic and compile errors are exempt: there the code *is* the
evidence.)

**A running build owns the working tree until it exits.** Do not switch branches,
rebase, or check out files while a build is running against the tree — the build
will either die or, worse, produce a binary from a mixture of two revisions.

**One pull request defends one primary invariant.** Do not combine unrelated
changes to save review capacity. When review is the constraint, prepare
independent branches rather than larger ones — but avoid parallel branches that
modify the same authority before the preceding architectural change lands.

**An open issue describes a problem that still exists.** When an issue is
overtaken by merged work, close it with the evidence, or rewrite its premise to
match the system that exists now. Preserve the historical truth; do not mutate an
old title into a new problem.

---

## Related

- `docs/technical/command_model.md` — command, transaction and history semantics
  (principles 3, 4, 5)
- `docs/technical/muse_state_ownership.md` — per-domain state authorities
  (principle 1)
- `docs/technical/THREADING_MODEL.md` — thread ownership
- Internal status notes — weekly targets and point-in-time status; expires
