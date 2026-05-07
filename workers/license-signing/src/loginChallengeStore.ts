import { normalizeEmail, requireD1, sha256Hex } from "./accountStore";
import type { Env } from "./signing";

const LOGIN_TTL_SECONDS = 10 * 60;
const MAX_ATTEMPTS = 5;

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

export async function createLoginChallenge(env: Env, email: string, nowSeconds: number):
    Promise<CreatedLoginChallenge> {
  const db = requireD1(env);
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
