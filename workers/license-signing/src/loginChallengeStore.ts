import { normalizeEmail, requireD1, sha256Hex } from "./accountStore";
import type { Env } from "./signing";

const LOGIN_TTL_SECONDS = 10 * 60;
const MAX_ATTEMPTS = 5;
const MAX_CHALLENGES_PER_SOURCE_HOUR = 20;

export class LoginChallengeError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = "LoginChallengeError";
    this.status = status;
    this.code = code;
  }
}

export type CreatedLoginChallenge = {
  id: string;
  code: string;
  expiresAt: number;
};

export type LoginChallengeCreateOptions = {
  sourceKey?: string;
};

function randomHex(byteLength: number): string {
  const bytes = new Uint8Array(byteLength);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function randomCode(): string {
  const bytes = new Uint8Array(4);
  crypto.getRandomValues(bytes);
  const value = new DataView(bytes.buffer).getUint32(0) % 1000000;
  return value.toString().padStart(6, "0");
}

function challengeHash(email: string, challengeId: string, code: string): Promise<string> {
  return sha256Hex(`${normalizeEmail(email)}:${challengeId}:${code}`);
}

type SourceQuotaRow = {
  recent_count: number | string | null;
};

function normalizeSourceKey(sourceKey: string | undefined): string | null {
  const normalized = sourceKey?.trim();
  if (!normalized) {
    return null;
  }
  return normalized.slice(0, 128);
}

async function assertSourceQuota(env: Env, sourceKey: string | null, nowSeconds: number): Promise<void> {
  if (!sourceKey) {
    return;
  }

  const db = requireD1(env);
  const windowStart = nowSeconds - 60 * 60;
  const row = await db.prepare(`
      SELECT COUNT(*) AS recent_count
      FROM account_login_challenges
      WHERE JSON_EXTRACT(metadata_json, '$.source') = ? AND created_at > ?
    `).bind(sourceKey, windowStart).first<SourceQuotaRow>();

  if (Number(row?.recent_count ?? 0) >= MAX_CHALLENGES_PER_SOURCE_HOUR) {
    throw new LoginChallengeError(429, "login_start_rate_limited", "login challenge creation is rate limited");
  }
}

export async function createLoginChallenge(env: Env, email: string, nowSeconds: number):
    Promise<CreatedLoginChallenge>;
export async function createLoginChallenge(env: Env, email: string, nowSeconds: number,
                                           options?: LoginChallengeCreateOptions):
    Promise<CreatedLoginChallenge> {
  const db = requireD1(env);
  const sourceKey = normalizeSourceKey(options?.sourceKey);
  await assertSourceQuota(env, sourceKey, nowSeconds);

  const challengeId = `lc_${randomHex(16)}`;
  const code = randomCode();
  const expiresAt = nowSeconds + LOGIN_TTL_SECONDS;
  const codeHash = await challengeHash(email, challengeId, code);
  const metadataJson = sourceKey ? JSON.stringify({ source: sourceKey }) : null;

  await db.prepare(`
      INSERT INTO account_login_challenges
        (id, email, code_hash, status, attempts, created_at, expires_at, used_at, metadata_json)
      VALUES (?, ?, ?, 'pending', 0, ?, ?, NULL, ?)
    `).bind(challengeId, normalizeEmail(email), codeHash, nowSeconds, expiresAt, metadataJson).run();

  return { id: challengeId, code, expiresAt };
}

export async function deleteLoginChallenge(env: Env, challengeId: string): Promise<void> {
  const db = requireD1(env);
  await db.prepare("DELETE FROM account_login_challenges WHERE id = ? AND status = 'pending'")
    .bind(challengeId).run();
}

type ChallengeRow = {
  id: string;
  email: string;
  code_hash: string;
  status: string;
  attempts: number;
  expires_at: number;
  used_at: number | null;
};

export async function verifyLoginChallenge(env: Env, email: string, challengeId: string, code: string,
                                           nowSeconds: number): Promise<void> {
  const db = requireD1(env);
  const normalizedEmail = normalizeEmail(email);
  const row = await db.prepare(`
      SELECT id, email, code_hash, status, attempts, expires_at, used_at
      FROM account_login_challenges
      WHERE id = ? AND email = ?
      LIMIT 1
    `).bind(challengeId, normalizedEmail).first<ChallengeRow>();

  if (!row || row.status !== "pending" || row.used_at !== null) {
    throw new LoginChallengeError(401, "invalid_challenge", "login challenge is invalid");
  }
  if (row.expires_at <= nowSeconds) {
    throw new LoginChallengeError(401, "challenge_expired", "login challenge has expired");
  }
  if (row.attempts >= MAX_ATTEMPTS) {
    throw new LoginChallengeError(429, "too_many_attempts", "login challenge attempts exceeded");
  }

  const submittedHash = await challengeHash(normalizedEmail, challengeId, code);
  if (submittedHash !== row.code_hash) {
    await db.prepare("UPDATE account_login_challenges SET attempts = attempts + 1 WHERE id = ?").bind(challengeId)
      .run();
    throw new LoginChallengeError(401, "invalid_code", "login code is invalid");
  }

  await db.prepare(`
      UPDATE account_login_challenges
      SET status = 'used', used_at = ?, attempts = attempts + 1
      WHERE id = ? AND status = 'pending'
    `).bind(nowSeconds, challengeId).run();
}
