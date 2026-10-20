# Updates (update.aestra.studio)

The app asks `update.aestra.studio` whether a newer build exists for its channel and platform, and installs it from
Settings when you say so. The feed is signed; the app trusts the signature, not the host.

| Piece | Where |
| --- | --- |
| Feed format, signature check, version order | `AestraLicense/include/UpdateFeed.h` |
| Make a signed feed for a build | `tools/update/make_feed.py` |
| Publish a build (feed + package) | `tools/update/publish.sh` |
| The host (Cloudflare Worker + R2 bucket `aestra-releases`) | `tools/update/worker/` |
| Founder keys for the `early` channel | `tools/update/founders.py` |

## Channels
- `stable`: public. Supporters and everyone else.
- `early`: founders only. Every read needs `Authorization: Bearer <founder key>`; the Worker hashes it and looks it up in
  the `FOUNDERS` KV namespace (hashes only). `founders.py add "Name"` prints a key once; `revoke` cuts it off at once.

## Keys (none are in the repo)
- **Signing key** (Ed25519): private half is the CI secret `AESTRA_UPDATE_SIGNING_KEY`, backed up in
  `~/.config/aestra-update/signing-seed.hex` (mode 600) on the owner's machine. The public half is built into the app.
  If it leaks: make a new pair, ship an app that trusts the new one, rotate the secret.
- **Upload key**: CI secret `AESTRA_UPDATE_UPLOAD_KEY`, also the Worker's `UPLOAD_KEY` secret. Only CI can write.

## Deploying the Worker
`cd tools/update/worker && npm test && wrangler deploy` with `CLOUDFLARE_EMAIL`, `CLOUDFLARE_API_KEY` and
`CLOUDFLARE_ACCOUNT_ID` set. The custom domain creates its own DNS record. Secrets: `wrangler secret put UPLOAD_KEY`.
