# AestraDocs — Internal Documentation Vault

> **Built from scratch. Perfected with intention.**

This is Aestra's internal design / architecture / notes vault (an Obsidian
vault — open the `AestraDocs/` folder in Obsidian). Public, user-facing docs
live in [`../docs/`](../docs/) and are published via mkdocs; they are not
duplicated here.

## → Start at [`INDEX.md`](INDEX.md)

`INDEX.md` is the single, canonical map of content, and the only navigation
file that is maintained. This README deliberately does **not** re-list every
doc, so the two can't drift apart (they used to).

## Folder map

| Folder | Holds |
| ------ | ----- |
| _(root)_ | The six design specs cited by source code, plus `README.md` and `INDEX.md` as vault navigation |
| `architecture/` | Design rationale and invariants |
| `systems/` | Implementation-level descriptions of subsystems |
| `specs/` | System-level behavioral / interaction design specs |
| `audio/` | Audio-quality audits, validation specs, DSP task lists |
| `design/` | Visual / UI design language and pass plans |
| `guides/` | How-to and reference |
| `implementation/` | Active implementation plans |
| `product/` | Product strategy, pricing, roadmap, planning |
| `security/` | Security audits (internal, sensitive) |
| `status/` | Point-in-time project status and process governance |
| `aestra-comp-v2/` | Effort-tracked plugin build |
| `Bug Reports/` | Postmortems and triage notes |
| `images/` | Diagram / screenshot assets |

## Conventions

- **New doc?** Put it in the right folder above and add one line to `INDEX.md`.
- **Root is reserved.** Keep content docs in folders. `README.md` and `INDEX.md`
  stay at the root as navigation files; only source-cited design specs are
  additional root-level content.
- **Naming.** Prefer `kebab-case.md` for new files. Existing `SCREAMING_SNAKE`
  and `PascalCase` names are left as-is to avoid breaking inbound links.
