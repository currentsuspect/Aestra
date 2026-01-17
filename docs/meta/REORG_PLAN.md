# Aestra repository reorganization plan

Goal: split the current repository into three logical repos with clear public/private boundaries:

- `Aestra-core` (public): Core engine, UI, platform abstraction, tests, docs, small mock assets. Buildable by contributors without private assets.
- `Aestra-premium` (private): AI models, trained weights, premium plugins, large sample libraries, and other paid assets.
- `Aestra-build` (private): Signing keys and build+sign packaging pipeline (scripts, certificates, installer packaging assets, release automation).

High-level mapping (what to move)

- Aestra-core (public)
  - AestraCore/
  - AestraPlat/
  - AestraUI/
  - AestraAudio/ (core audio engine; keep only non-premium code)
  - AestraSDK/ (public headers/interface only; move private plugins elsewhere)
  - docs/, AestraDocs/
  - build scripts that don't embed secrets, `cmake/`, `assets_mock/`, `.github/workflows/public-ci.yml`

- Aestra-premium (private)
  - AestraMuse/ (models, research, trained weights)
  - AestraAssets/ (fonts, icons, shaders, sounds) — if these contain paid assets
  - Any large sample libraries and exclusive plugins

- Aestra-build (private)
  - build_and_sign.bat, build.ps1 (signed/cert usage only)
  - installer.iss (if it packages private assets)
  - Signing certs/keystores (never in git; store in secret vault)
  - Private-release GitHub Actions and self-hosted runner configs

Recommended split process (safe, high level)

1) Back up: create a mirror backup of the current repo:

   git clone --mirror /path/to/Aestra Aestra-backup.git

2) Create a working clone for public-core extraction (non-destructive):

   git clone --branch develop /path/to/Aestra Aestra-working
   cd Aestra-working

3) Extract `Aestra-core` history using `git subtree split` or `git filter-repo`:

   # subtree (keeps history for the prefix)
   git subtree split --prefix=AestraCore -b Aestra-core-only

   # Or git-filter-repo approach to remove private paths (recommended for removing blobs)
   See docs/HISTORY_REWRITE.md for filter-repo steps.

4) Create new remote and push the extracted branch to `Aestra-core` repo (do not force-push to existing remotes until verified).

5) Extract private directories into their own repositories (AestraMuse, AestraAssets) similarly.

6) Clean history where necessary: use `git-filter-repo` on a mirrored clone to permanently remove large blobs or secrets. Verify with `gitleaks` and largest object listing before pushing cleaned repos.

7) Decide integration approach: submodule vs subtree vs package distribution
   - Submodules: keep private repos as git submodules under AestraCore for devs with access. Pros: straightforward, preserves separate ACLs. Cons: extra git complexity for contributors.
   - Subtrees: embed private code into core via subtree merges at release time. Pros: simpler for consumers. Cons: risk of accidental public push if not careful.
   - Binary distribution: keep premium models as separate artifact storage (S3, cloud storage) and fetch at build or runtime.

CI/CD & secrets

- Public repo (`Aestra-core`) should have a slim `public-ci.yml` that builds only core and runs secret scans (gitleaks). It must not contain steps that require private secrets.
- Private repo (`Aestra-build`) will host the signing pipeline and run on a self-hosted runner with access to signing keys and hardware.
- Use GitHub Secrets / Azure Key Vault / HashiCorp Vault for signing certs; never check .pfx/.p12 into git.

Developer experience

- Provide `assets_mock/` and `cmake/AestraPremiumFallback.cmake` (already added) so contributors can build core without premium assets.
- Document submodule setup in `docs/CONTRIBUTING_CORE.md`.

Checklist before public push

- Run `.\tools\run_gitleaks.ps1` (or `gitleaks detect`) and clear any findings.
- Ensure no private cert/key files exist in the working tree or history (use `scan_history.ps1` to list large objects and `gitleaks`).
- Prepare the cleaned public repo mirror and verify that it builds locally in `Aestra_CORE_ONLY` mode.
