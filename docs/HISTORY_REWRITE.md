# Safe history rewrite checklist

Follow these steps locally — DO NOT attempt to force-push to origin until you have a verified mirror backup.

1) Create a mirror backup of the repository

   git clone --mirror "$(pwd)" ../nomad-backup.git

2) Identify files/paths to remove from history

   Common candidates in this repo:
   - NomadMuse/models/**
   - NomadAssets/**  (if assets are private)
   - Any *.p12, *.pfx, *.pem files accidentally committed

3) Use git-filter-repo to remove paths (recommended)

   # install git-filter-repo (follow upstream docs)
   git filter-repo --path NomadMuse/models --invert-paths --force

   # remove multiple paths
   git filter-repo --path NomadMuse/models --path NomadAssets --invert-paths --force

4) Verify the cleaned repo locally:
   - Inspect file history: git log --stat -- path
   - Run gitleaks detect --source . --report-path gitleaks-post-clean.json

5) When satisfied: force-push cleaned branches to the new public remote

   # push main (replace origin-public with your target remote)
   git remote add public git@github.com:YourOrg/nomad-core.git
   git push public --force --all
   git push public --force --tags

6) Coordinate with collaborators — rewriting history requires everyone to re-clone or run `git fetch` + reset.

Notes & alternatives:
- If you prefer BFG for large-file removal, follow BFG instructions; BFG is simpler for removing large blobs but requires some git plumbing.
- Always keep the mirror backup (`nomad-backup.git`) until you are 100% confident.
