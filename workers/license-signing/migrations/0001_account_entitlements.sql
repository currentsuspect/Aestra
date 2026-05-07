CREATE TABLE IF NOT EXISTS accounts (
  id TEXT PRIMARY KEY,
  email TEXT UNIQUE,
  display_name TEXT NULL,
  status TEXT NOT NULL CHECK (status IN ('active', 'disabled')),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS account_sessions (
  token_hash TEXT PRIMARY KEY,
  account_id TEXT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('active', 'revoked', 'expired')),
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  revoked_at INTEGER NULL,
  last_seen_at INTEGER NULL,
  label TEXT NULL,
  FOREIGN KEY (account_id) REFERENCES accounts(id)
);

CREATE TABLE IF NOT EXISTS account_entitlements (
  id TEXT PRIMARY KEY,
  account_id TEXT NOT NULL,
  tier TEXT NOT NULL CHECK (tier IN ('core', 'supporter', 'founder')),
  status TEXT NOT NULL CHECK (status IN ('active', 'trialing', 'grace_period', 'canceled', 'expired', 'revoked')),
  source TEXT NOT NULL,
  source_ref TEXT NULL,
  current_period_start INTEGER NULL,
  current_period_end INTEGER NULL,
  grace_until INTEGER NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  revoked_at INTEGER NULL,
  metadata_json TEXT NULL,
  FOREIGN KEY (account_id) REFERENCES accounts(id)
);

CREATE TABLE IF NOT EXISTS entitlement_events (
  id TEXT PRIMARY KEY,
  account_id TEXT NOT NULL,
  entitlement_id TEXT NULL,
  event_type TEXT NOT NULL,
  source TEXT NOT NULL,
  source_ref TEXT NULL,
  created_at INTEGER NOT NULL,
  metadata_json TEXT NULL,
  FOREIGN KEY (account_id) REFERENCES accounts(id),
  FOREIGN KEY (entitlement_id) REFERENCES account_entitlements(id)
);

CREATE INDEX IF NOT EXISTS idx_account_sessions_account_id ON account_sessions(account_id);
CREATE INDEX IF NOT EXISTS idx_account_entitlements_account_id ON account_entitlements(account_id);
CREATE INDEX IF NOT EXISTS idx_entitlement_events_account_id ON entitlement_events(account_id);

CREATE TABLE IF NOT EXISTS account_login_challenges (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL,
  code_hash TEXT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('pending', 'used', 'expired')),
  attempts INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  used_at INTEGER NULL,
  metadata_json TEXT NULL
);

CREATE TABLE IF NOT EXISTS account_login_events (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL,
  account_id TEXT NULL,
  event_type TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  metadata_json TEXT NULL,
  FOREIGN KEY (account_id) REFERENCES accounts(id)
);

CREATE INDEX IF NOT EXISTS idx_account_login_challenges_email ON account_login_challenges(email);
CREATE INDEX IF NOT EXISTS idx_account_login_events_email ON account_login_events(email);
