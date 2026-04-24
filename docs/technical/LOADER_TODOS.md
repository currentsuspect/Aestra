# Project Loader Hardening — Expansion TODOs

Future improvements for the `.aes` project loader.

## Formal Load Plan / Report Types

Replace inline validation and reporting with structured `ProjectLoadPlan` / `ProjectLoadReport` types. Ensure every warning/error path is queryable by tests and UI. Currently only orphan clip/unit IDs trigger structured reporting.

## Non-Mutating Orphan Handling

Stop mutating clip names with `[MISSING PATTERN]` prefix. Preserve original name and add explicit unresolved/missing-pattern metadata. Let the UI render warning badges/prefixes visually.

## Atomic Temporary Loading

Long-term: Load all project data into a temporary `ProjectState`, validate/rebind fully, then swap into live engine only after complete success. This protects against failures occurring after PHASE 4 (first state mutation).

## Asset Relinking Workflow

Add user-facing missing audio relink support. Preserve missing asset metadata and allow recovery without destructive project edits.

## Malformed Project Tests

Add fixtures for:
- Corrupted/malformed JSON
- Invalid plugin IDs
- Bad routing/send targets
- Invalid automation targets
- Migration compatibility

## Loader Fuzz Testing

Generate randomized `.aes` files with missing/invalid references. Confirm loader never crashes, silently drops user data, or mutates live state before commit.