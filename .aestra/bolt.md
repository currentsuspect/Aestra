# Bolt Performance Journal

## 2025-01-03 — Initial Setup

**Learning:** Bolt journal created.

**Action:** Use this to track performance discoveries.

## 2025-01-03 - [Docs Audit: Doxygen + READMEs]

**Learning:** Doxygen warnings were primarily missing brief descriptions in exported headers (ASIO, AudioRT, Core Assert/Config). `README.md` contained several broken links to the documentation site which hasn't been fully deployed or has changed structure.
**Action:**
- Enforced header doc presence (`@brief`) for public headers in `AestraAudio`, `AestraCore`, and `AestraUI`.
- Added `scripts/docs-check.sh` to CI to validate docs (links, doxygen, spelling).
- Fixed broken links in `README.md`.
- Removed references to "FL Studio" throughout documentation and source comments to align with professional branding.
- Consolidated disparate documentation (`AestraAudio/docs`, `AestraUI/docs`, root `*.md`) into a structured `docs/` hierarchy (`technical/`, `getting-started/`, `developer/`) to improve discoverability and organization.
