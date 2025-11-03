Backups and temporary archives

This directory is intended for administrative backup material that was previously
placed in the repository root. Files here were moved for clarity and to keep the
repository root tidy.

Action items for maintainers:
- Inspect the original backup folder that may still exist at the project root
  (it may be named like `sensitive-backup-$(Get-Date -Format yyyyMMdd-HHmmss)`).
- If the backup is no longer required, remove it after verifying contents.
- If you want the backup preserved but not versioned, consider moving it to a
  private storage outside the repository (private repo, cloud storage, etc.).
