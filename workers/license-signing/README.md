# Aestra License Signing Worker

Minimal Cloudflare Worker scaffold for signing Aestra membership lease payloads.

The Worker validates a requested lease, canonicalizes it byte-for-byte like `AestraLicense::LicenseGate`, signs the canonical string with Ed25519 detached signing, and returns the canonical payload plus a hex signature suitable for local lease storage.

Out of scope for this pass:

- payment webhooks
- Stripe, Paddle, or LemonSqueezy integration
- `LicenseGate::refreshAsync()` client implementation
- production login, OAuth, JWT, or session issuance

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

### `POST /v1/entitlements/refresh`

Internal/admin scaffold endpoint. Requires the same bearer token as `/v1/entitlements/sign`.

This endpoint intentionally does not perform user authentication, lookup entitlement storage, or talk to a payment
provider. For now it accepts the same validated lease request shape as `/v1/entitlements/sign`, signs it, and returns a
storage-ready lease blob for DAW refresh-client compatibility work.

Response:

```json
{
  "payload": {},
  "canonical": "{\"license_id\":\"lic_123\"...}",
  "signature_hex": "<128 lowercase hex chars>",
  "lease_blob": "<canonical JSON>\n<signature hex>",
  "key_id": "aestra-dev-2026-01",
  "format": "aestra-license-v1"
}
```

### `POST /v1/account/entitlements/refresh`

Account-facing refresh boundary for future DAW clients. Requires account authorization:

```text
Authorization: Bearer <account session token>
```

Current implementation status:

- authentication can use D1 session lookup or explicit fixture mode
- entitlement lookup can use D1 account entitlement rows or explicit fixture mode
- the request body must provide `device_hash`
- `issued_at` may be provided for deterministic tests; otherwise the Worker uses current server time
- `account_id` in the request body is ignored and never used to choose the account
- missing entitlement records return a signed Core lease
- Supporter and Founder leases are only minted from server-side entitlement records
- malformed storage/auth state fails with structured errors

Request:

```json
{
  "device_hash": "device_hash_v1",
  "issued_at": 1234567890
}
```

Response shape matches the native refresh parser:

```json
{
  "payload": {},
  "canonical": "{\"license_id\":\"lic_123\"...}",
  "signature_hex": "<128 lowercase hex chars>",
  "lease_blob": "<canonical JSON>\n<signature hex>",
  "key_id": "aestra-dev-2026-01",
  "format": "aestra-license-v1",
  "entitlement_status": "core|active|grace_period"
}
```

### `POST /v1/account/login/start`

Starts an email one-time-code login challenge.

Request:

```json
{
  "email": "user@example.com"
}
```

Behavior:

- normalizes email before storage and lookup
- does not reveal whether the account already exists
- stores only a challenge code hash
- returns `fixture_code` only when `AESTRA_LOGIN_MAILER_MODE=fixture`
- fails with `mailer_unconfigured` when no production mailer mode is configured

### `POST /v1/account/login/verify`

Verifies a one-time login code and issues an account session.

Request:

```json
{
  "email": "user@example.com",
  "challenge_id": "lc_...",
  "code": "123456"
}
```

Behavior:

- rejects expired, reused, malformed, or over-attempt challenges
- creates a Core account if the normalized email is new
- resolves an existing active account if present
- stores only the SHA-256 session token hash in D1
- returns the raw session token exactly once
- does not create Supporter or Founder entitlements

### `GET /v1/account/me`

Requires `Authorization: Bearer <account session token>`.

Returns account identity plus the current effective entitlement summary. It does not return raw session tokens, token
hashes, private signing material, or signed lease blobs.

### `POST /v1/account/session/revoke`

Requires `Authorization: Bearer <account session token>`.

Revokes the current session by marking the D1 session row revoked. Future `/me` and entitlement refresh calls with the
same token return `401`.

### `POST /v1/admin/entitlements/grant`

Admin-only route. Requires:

```text
Authorization: Bearer <AESTRA_ADMIN_API_KEY>
```

Creates or resolves an account by normalized email, inserts a manual entitlement row, writes an `entitlement_events`
audit record, and returns the effective entitlement summary. It does not sign lease material and does not return session
tokens.

Manual founder/developer access should use this route with `tier: "founder"` and `source: "manual_grant"`. The DAW must
still receive access through `/v1/account/entitlements/refresh` and local signature verification.

### `POST /v1/admin/entitlements/revoke`

Admin-only route. Marks a matching entitlement row revoked, writes a `manual_revoke` audit event, and returns the new
effective entitlement summary. Future refresh calls fall back to Core unless another valid entitlement remains.

### `GET /v1/admin/accounts/lookup?email=user@example.com`

Admin-only route. Returns account identity, entitlement rows, and effective entitlement summary. It intentionally does
not expose raw session tokens or token hashes.

## Storage

The Worker supports two storage modes:

```text
AESTRA_STORAGE_MODE = fixture | d1
```

If `AESTRA_STORAGE_MODE` is unset, the Worker uses D1 when `AESTRA_LICENSE_DB` is bound and fixture mode otherwise.
Setting `AESTRA_STORAGE_MODE=d1` without an `AESTRA_LICENSE_DB` binding fails explicitly with
`storage_unconfigured`.

D1 schema lives in `migrations/0001_account_entitlements.sql` and defines:

- `accounts`
- `account_sessions`
- `account_entitlements`
- `entitlement_events`
- `account_login_challenges`
- `account_login_events`

Session lookup behavior:

- bearer tokens are hashed with SHA-256
- `account_sessions.token_hash` stores only the hash, never the raw token
- session rows must be `active`, unexpired, and not revoked
- joined account rows must be `active`
- failed/missing/expired/revoked sessions return `401`

Entitlement behavior:

- missing entitlement rows return a signed Core lease
- active/trialing paid rows return signed paid leases
- grace-period rows return signed paid leases with unsigned grace metadata
- expired/canceled/revoked paid rows fall back to signed Core
- unknown tiers or statuses fail loudly
- multiple active rows resolve deterministically by highest tier, then stable id order
- fixture entitlement data is ignored in D1 mode

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

Fixture-only account refresh bindings for tests/dev:

```text
AESTRA_ACCOUNT_FIXTURE_SESSIONS = {"test-token":{"account_id":"acct_1","user_id":"user_1"}}
AESTRA_ENTITLEMENT_FIXTURES = {"acct_1":{"license_id":"lic_1","tier":"Supporter","plugins":[],"features":["rumble"],"revocation_epoch":0}}
```

Do not use these fixture bindings as production account auth or entitlement storage.

Fixture-only login delivery for tests/dev:

```text
AESTRA_LOGIN_MAILER_MODE = fixture
```

Production-like deployments must configure a real mail delivery path before using login start. `configured` currently
means the mailer boundary is present but no provider integration is implemented in this repository pass.

Configure the D1 binding in `wrangler.toml` after creating the Cloudflare database:

```toml
[[d1_databases]]
binding = "AESTRA_LICENSE_DB"
database_name = "aestra-license"
database_id = "<cloudflare-d1-id>"
```

## Local Worker/D1 Native Smoke

The native dev public key is `AESTRA_DEV_TEST_PUBKEY_V1`:

```text
03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8
```

`scripts/dev-smoke.sh` generates ignored local Wrangler config and `.dev.vars.local-smoke` values for this dev/test
key. The generated signing value is fixture material derived from the cross-language Worker test seed; it is for local
smoke only and must not be reused for production. Production signing remains Cloudflare-secret based.

Run the local smoke flow in two terminals:

```bash
cd workers/license-signing
npm install
scripts/dev-smoke.sh worker
```

Then, from the repo root:

```bash
cmake --build build-release --target SecAccountEndToEndSmoke -j2
workers/license-signing/scripts/dev-smoke.sh native
```

The smoke target is skipped by default. It runs only when `AESTRA_SMOKE_EXPECT_LIVE_WORKER=1` is present.

Expected flow:

```text
login/start fixture code
-> login/verify issued session
-> local session cache write
-> account/me bearer request
-> signed Core refresh verifies in LicenseGate
-> admin manual Founder/Supporter grant
-> signed paid refresh verifies in LicenseGate
-> MembershipViewModel observes Aestra Founder or Aestra Supporter
-> session revoke
-> old bearer session becomes unauthorized
```

Useful environment overrides:

```text
AESTRA_SMOKE_PORT=8787
AESTRA_SMOKE_EMAIL=smoke+aestra@example.test
AESTRA_SMOKE_ADMIN_KEY=local-smoke-admin
AESTRA_SMOKE_TIER=founder | supporter
AESTRA_SMOKE_D1_STATE=workers/license-signing/.wrangler/local-smoke-state
```

Known limitations before production:

- fixture mailer mode returns the one-time code to the caller
- manual admin grants are dev/internal tooling, not billing
- production D1 database IDs and Cloudflare secrets are not stored in this repository
- key rotation and signed `kid` verification are still future work

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
