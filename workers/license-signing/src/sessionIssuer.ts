import { createSession } from "./accountStore";
import type { Env } from "./signing";

const SESSION_TOKEN_BYTES = 32;
const SESSION_TTL_SECONDS = 60 * 60 * 24 * 30;

function randomHex(byteLength: number): string {
  const bytes = new Uint8Array(byteLength);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export type IssuedSession = {
  token: string;
  expiresAt: number;
};

export async function issueAccountSession(env: Env, accountId: string, nowSeconds: number): Promise<IssuedSession> {
  const token = `as_${randomHex(SESSION_TOKEN_BYTES)}`;
  const expiresAt = nowSeconds + SESSION_TTL_SECONDS;
  await createSession(env, accountId, token, nowSeconds, expiresAt, "account-login");
  return { token, expiresAt };
}
