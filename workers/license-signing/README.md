# Aestra License Signing Worker

Minimal Cloudflare Worker scaffold for signing Aestra membership lease payloads.

The Worker validates a requested lease, canonicalizes it byte-for-byte like `AestraLicense::LicenseGate`, signs the canonical string with Ed25519 detached signing, and returns the canonical payload plus a hex signature suitable for local lease storage.

Out of scope for this pass:

- payment webhooks
- Stripe, Paddle, or LemonSqueezy integration
- login, OAuth, JWT, or account refresh
- D1, KV, R2, or Durable Object persistence
- `LicenseGate::refreshAsync()` client implementation

## Endpoints

### `GET /health`

Returns service metadata and the configured key id. It never returns private key material.

```json
{
  "ok": true,
  "service": "aestra-license-signing",
  "version": 1,
  "key_id": "dev"
}
```

### `POST /v1/entitlements/sign`

Internal/admin endpoint. Requires:

```text
Authorization: Bearer <AESTRA_ADMIN_API_KEY>
```

Request:

```json
{
  "license_id": "lic_123",
  "user_id": "user_123",
  "tier": "Supporter",
  "plugins": ["com.aestra.rumble"],
  "features": ["rumble"],
  "device_hash": "device_hash_v1",
  "issued_at": 1234567890,
  "expires_at": 1235172690,
  "grace_policy": "restrict",
  "revocation_epoch": 0
}
```

Response:

```json
{
  "payload": {},
  "canonical": "{\"license_id\":\"lic_123\"...}",
  "signature_hex": "<128 lowercase hex chars>",
  "key_id": "aestra-dev-2026-01",
  "format": "aestra-license-v1"
}
```

## Compatibility Contract

The signed canonical JSON uses the exact `LicenseGate.cpp` field order:

```text
license_id, user_id, tier, plugins, features, device_hash, issued_at, expires_at, grace_policy, revocation_epoch
```

Rules enforced by this Worker:

- `tier` must be exactly `Core`, `Supporter`, or `Founder`
- `expires_at` must equal `issued_at + 604800`
- `grace_policy` must equal `restrict`
- `revocation_epoch` must equal `0`
- `plugins` and `features` must be arrays of strings and preserve input order
- the signed payload does not include `kid` because the local verifier does not support it yet
- `key_id` is unsigned response metadata only

## Secrets

Do not commit `.dev.vars`, private keys, admin tokens, or generated production key material.

Configure Cloudflare secrets with:

```bash
wrangler secret put AESTRA_LICENSE_SIGNING_PRIVATE_KEY
wrangler secret put AESTRA_ADMIN_API_KEY
wrangler secret put AESTRA_SIGNING_KEY_ID
```

Private key format:

```text
AESTRA_LICENSE_SIGNING_PRIVATE_KEY = base64 raw 64-byte Ed25519 secret key
```

The DAW embeds only the raw 32-byte Ed25519 public key. It must never receive private signing material.

## Development

```bash
npm install
npm test
npm run typecheck
```

Deploy only after setting secrets in the target Cloudflare account:

```bash
npm run deploy
```
