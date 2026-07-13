import { normalizeEmail, requireD1, sha256Hex } from "./accountStore";
import type { Env } from "./signing";

const LOGIN_TTL_SECONDS = 10 * 60;
const MAX_ATTEMPTS = 5;
const LOGIN_START_COOLDOWN_SECONDS = 60;
const MAX_LOGIN_STARTS_PER_HOUR = 5;
const MAX_PENDING_CHALLENGES_PER_EMAIL = 3;

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

type LoginStartQuotaRow = {
  recent_count: number | string | null;
  pending_count: number | string | null;
  latest_created_at: number | string | null;
};

async function assertCanCreateLoginChallenge(env: Env, email: string, nowSeconds: number): Promise<void> {
  const normalizedEmail = normalizeEmail(email);
  const db = requireD1(env);
  const windowStart = nowSeconds - 60 * 60;
  const row = await db.prepare(`
      SELECT
        COUNT(*) AS recent_count,
        SUM(CASE WHEN status = 'pending' AND expires_at > ? THEN 1 ELSE 0 END) AS pending_count,
        MAX(created_at) AS latest_created_at
      FROM account_login_challenges
      WHERE email = ? AND created_at > ?
    `).bind(nowSeconds, normalizedEmail, windowStart).first<LoginStartQuotaRow>();

  const recentCount = Number(row?.recent_count ?? 0);
  const pendingCount = Number(row?.pending_count ?? 0);
  const latestCreatedAt = row?.latest_created_at === null || row?.latest_created_at === undefined
    ? null
    : Number(row.latest_created_at);

  if (latestCreatedAt !== null && nowSeconds - latestCreatedAt < LOGIN_START_COOLDOWN_SECONDS) {
    throw new LoginChallengeError(429, "login_start_rate_limited", "login challenge creation is rate limited");
  }
  if (pendingCount >= MAX_PENDING_CHALLENGES_PER_EMAIL || recentCount >= MAX_LOGIN_STARTS_PER_HOUR) {
    throw new LoginChallengeError(429, "login_start_rate_limited", "login challenge creation is rate limited");
  }
}

export async function createLoginChallenge(env: Env, email: string, nowSeconds: number):
    Promise<CreatedLoginChallenge> {
  const db = requireD1(env);
  await assertCanCreateLoginChallenge(env, email, nowSeconds);

  const challengeId = `lc_${randomHex(16)}`;
  const code = randomCode();
  const expiresAt = nowSeconds + LOGIN_TTL_SECONDS;
  const codeHash = await challengeHash(email, challengeId, code);

  await db.prepare(`
      INSERT INTO account_login_challenges
        (id, email, code_hash, status, attempts, created_at, expires_at, used_at, metadata_json)
      VALUES (?, ?, ?, 'pending', 0, ?, ?, NULL, NULL)
    `).bind(challengeId, normalizeEmail(email), codeHash, nowSeconds, expiresAt).run();

  return { id: challengeId, code, expiresAt };
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
