import type { AccountAuthContext } from "./accountAuth";
import type { Env } from "./signing";

export class AccountStoreError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = "AccountStoreError";
    this.status = status;
    this.code = code;
  }
}

type AccountSessionRow = {
  account_id: string;
  user_id: string;
  email: string | null;
  display_name: string | null;
  account_status: string;
  session_status: string;
  expires_at: number;
  revoked_at: number | null;
};

export type AccountRecord = {
  id: string;
  email: string;
  displayName?: string;
  status: "active" | "disabled";
};

export function storageMode(env: Env): "fixture" | "d1" {
  if (env.AESTRA_STORAGE_MODE === "d1" || env.AESTRA_STORAGE_MODE === "fixture") {
    return env.AESTRA_STORAGE_MODE;
  }
  return env.AESTRA_LICENSE_DB ? "d1" : "fixture";
}

export function normalizeEmail(email: string): string {
  return email.trim().toLowerCase();
}

export function isValidEmail(email: string): boolean {
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email) && email.length <= 254;
}

export async function sha256Hex(value: string): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value));
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export function requireD1(env: Env): D1Database {
  if (!env.AESTRA_LICENSE_DB) {
    throw new AccountStoreError(500, "storage_unconfigured", "D1 account storage is not configured");
  }
  return env.AESTRA_LICENSE_DB;
}

export async function lookupAccountBySessionToken(env: Env, sessionToken: string, nowSeconds: number):
    Promise<AccountAuthContext | null> {
  const db = requireD1(env);
  const tokenHash = await sha256Hex(sessionToken);
  const row = await db.prepare(`
      SELECT
        s.account_id AS account_id,
        a.id AS user_id,
        a.email AS email,
        a.display_name AS display_name,
        a.status AS account_status,
        s.status AS session_status,
        s.expires_at AS expires_at,
        s.revoked_at AS revoked_at
      FROM account_sessions s
      JOIN accounts a ON a.id = s.account_id
      WHERE s.token_hash = ?
      LIMIT 1
    `).bind(tokenHash).first<AccountSessionRow>();

  if (!row) {
    return null;
  }
  if (row.account_status !== "active" || row.session_status !== "active") {
    return null;
  }
  if (row.revoked_at !== null || row.expires_at <= nowSeconds) {
    return null;
  }

  await db.prepare("UPDATE account_sessions SET last_seen_at = ? WHERE token_hash = ?").bind(nowSeconds, tokenHash)
    .run();

  return {
    accountId: row.account_id,
    userId: row.user_id,
    ...(row.email ? { email: row.email } : {}),
  };
}

function randomHex(byteLength: number): string {
  const bytes = new Uint8Array(byteLength);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function getOrCreateActiveAccountByEmail(env: Env, email: string, nowSeconds: number):
    Promise<AccountRecord> {
  const db = requireD1(env);
  const normalizedEmail = normalizeEmail(email);
  const existing = await db.prepare("SELECT id, email, display_name, status FROM accounts WHERE email = ? LIMIT 1")
    .bind(normalizedEmail).first<{ id: string; email: string; display_name: string | null; status: string }>();

  if (existing) {
    if (existing.status !== "active") {
      throw new AccountStoreError(403, "account_disabled", "account is not active");
    }
    return {
      id: existing.id,
      email: existing.email,
      ...(existing.display_name ? { displayName: existing.display_name } : {}),
      status: "active",
    };
  }

  const accountId = `acct_${randomHex(16)}`;
  await db.prepare(`
      INSERT INTO accounts (id, email, display_name, status, created_at, updated_at)
      VALUES (?, ?, NULL, 'active', ?, ?)
    `).bind(accountId, normalizedEmail, nowSeconds, nowSeconds).run();

  return {
    id: accountId,
    email: normalizedEmail,
    status: "active",
  };
}

export async function lookupAccountByEmail(env: Env, email: string): Promise<AccountRecord | null> {
  const db = requireD1(env);
  const normalizedEmail = normalizeEmail(email);
  const existing = await db.prepare("SELECT id, email, display_name, status FROM accounts WHERE email = ? LIMIT 1")
    .bind(normalizedEmail).first<{ id: string; email: string; display_name: string | null; status: string }>();

  if (!existing) {
    return null;
  }
  return {
    id: existing.id,
    email: existing.email,
    ...(existing.display_name ? { displayName: existing.display_name } : {}),
    status: existing.status === "active" ? "active" : "disabled",
  };
}

export async function createSession(env: Env, accountId: string, rawSessionToken: string, nowSeconds: number,
                                    expiresAt: number, label: string): Promise<void> {
  const db = requireD1(env);
  const tokenHash = await sha256Hex(rawSessionToken);
  await db.prepare(`
      INSERT INTO account_sessions
        (token_hash, account_id, status, created_at, expires_at, revoked_at, last_seen_at, label)
      VALUES (?, ?, 'active', ?, ?, NULL, NULL, ?)
    `).bind(tokenHash, accountId, nowSeconds, expiresAt, label).run();
}

export async function revokeSessionToken(env: Env, rawSessionToken: string, nowSeconds: number): Promise<void> {
  const db = requireD1(env);
  const tokenHash = await sha256Hex(rawSessionToken);
  await db.prepare(`
      UPDATE account_sessions
      SET status = 'revoked', revoked_at = ?
      WHERE token_hash = ? AND revoked_at IS NULL
    `).bind(nowSeconds, tokenHash).run();
}
