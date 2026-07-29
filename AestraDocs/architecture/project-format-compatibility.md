# Project Format Compatibility Constitution

## Promise

"Never lose a session" means Aestra preserves recoverable creative data and
session structure across failures and upgrades. It does not mean that a
third-party binary, sample, or service will remain available forever.

Project compatibility failures block release.

## Non-negotiable rules

1. Released project fixtures are immutable. Never regenerate or edit one to
   make a new loader pass; repair the loader or add a migration.
2. Every schema bump ships an adjacent migration, a serializer-produced
   historical fixture, and CI coverage in the same change.
3. Migration runs on an in-memory parse and never overwrites the original
   project automatically.
4. Unsupported future versions are refused before the active in-memory project
   is changed.
5. Missing assets, plugins, units, patterns, and route references degrade the
   affected object rather than invalidating unrelated session structure.
6. Recoverable degraded records must survive load → save → load. Silence is an
   acceptable runtime fallback; erasing the reference is not.
7. `PROJECT_VERSION_MIN_SUPPORTED` is a product promise, not a cleanup knob.
   Lowering compatibility requires an explicit product decision and release
   plan, never a convenience edit.
8. Old integrity algorithms remain readable after newer algorithms are
   introduced. Unknown algorithms may be reported as unchecked, but an old
   project must not become unreadable merely because the writer improved.
9. Runtime refactors may not silently redefine persisted meaning. Loader
   adapters or explicit migrations preserve the old meaning first.
10. A schema version number alone does not make a project dirty. Only an actual
    in-memory transformation requires the upgraded representation to be saved.

## What counts as a transformation

Rule 10 is only enforceable if "transformation" has one meaning. It does:

> A **transformation** is any load-time work that produces an in-memory project
> whose faithful re-serialization would differ **semantically** from the bytes on
> disk, in a way the older schema cannot express.

Consequences, all binding:

- **Version advancement is not, by itself, a transformation.** Reading a v2 file
  and stamping the result as v3 changes nothing about the session. If every
  field means the same thing in both schemas, the load is `Unchanged` and the
  project stays clean.
- **Migration functions must report transformations truthfully.** A migration
  that rewrites, defaults, splits, merges, or reinterprets any field returns
  `MigrationStepResult::Transformed`. One that only inspects returns
  `Unchanged`. Returning `Transformed` "to be safe" is also wrong: it prompts
  users to save documents that did not change.
- **Loader-side compatibility interpretation is held to the same standard.**
  Aestra permits the loader to read an older shape directly instead of routing
  it through a migration function — `migrateV2ToV3` is deliberately a no-op
  because the v3 loader reads v2's lane-local mixer state natively. That is only
  legitimate when the interpretation is **representation-equivalent and
  round-trip stable**: loading the old shape and re-saving must yield a document
  the loader reads back with identical semantics, and no information the old
  file carried may be dropped or invented. A version-conditional branch in the
  loader that upgrades meaning — inventing an identity, redistributing a value,
  changing a default — **is a transformation** and must be reported as one, not
  hidden in the branch.
- **Therefore**: whoever adds version-conditional loader code owes one of two
  proofs. Either demonstrate representation-equivalence and round-trip
  stability with a fixture-backed test, or make the load report that the
  document requires saving. Silently doing neither is the failure mode this
  section exists to prevent — the user's only signal that their project was
  upgraded is the modified marker.

Contributors must not hide semantic upgrades in version-conditional loader
code. If the loader knows the document changed meaning, the application must
know too.

## Schema-bump checklist

Complete every item in the schema-bump pull request:

- [ ] Describe the persisted semantic change and why additive/default handling
      is insufficient.
- [ ] Increment `ProjectSerializer::PROJECT_VERSION_CURRENT` by exactly one.
- [ ] Add exactly one `N → N+1` entry to `ProjectMigrations::getMigrations()`.
- [ ] Make the migration report `Unchanged`, `Transformed`, or `Failed`
      truthfully.
- [ ] Keep interpretation for every older supported schema intact.
- [ ] **Precondition — resolve #670 before introducing a genuinely required new
      field.** Load validates the common structural envelope *before* migrations
      run and never re-validates after. That is safe only while every required
      field exists in all supported schemas; the first bump that adds one will
      have the pre-migration validator reject the very file the migration exists
      to repair.
- [ ] If the loader reads the older shape natively instead of routing it through
      the migration, prove representation-equivalence and round-trip stability
      with a fixture-backed test — or report the load as `Transformed`. Do not
      hide a semantic upgrade in a version-conditional branch.
- [ ] Record the new fixture's path and SHA-256 in
      `Tests/Guards/verify_project_fixture_manifest.cmake`. The manifest is
      bidirectional: an unlisted fixture on disk fails the guard.
- [ ] Produce the new historical fixture with the serializer that writes
      version `N+1`; record the source commit and generation command.
- [ ] Do not edit any earlier fixture.
- [ ] Add semantic assertions for the field or relationship that distinguishes
      the new version.
- [ ] Prove the fixture loads, reaches the current schema, re-saves as current,
      reloads, and preserves stable identities and important values.
- [ ] Prove degraded dependencies in the changed domain survive
      load → save → load.
- [ ] Prove v0 and a future version still fail without changing the active
      project.
- [ ] Prove the migration registry has no missing, duplicate, or skipped edge.
- [ ] Prove a transformed load remains modified and a current/no-op load remains
      clean.
- [ ] Run the project roundtrip, value-fidelity, identity, fixture, integrity,
      load-regression, recovery, and plugin-project tests that are available in
      the build configuration.
- [ ] Record rollback risk and confirm the original file is never rewritten by
      load.

## Fixture provenance

Each fixture location must record:

- schema version;
- producing serializer commit, or an explicit statement that the fixture is a
  labelled hand-authored compatibility sample;
- the generation command or reason authentic generation was impossible;
- the semantic feature the fixture pins;
- a checksum used by the fixture guard.

Hand-authored fixtures may supplement authentic files, but must never be
presented as evidence of exact historical writer output.
