import type { Env } from "./signing";
import { AccountStoreError, lookupAccountBySessionToken, storageMode } from "./accountStore";

export type AccountAuthContext = {
  accountId: string;
  userId: string;
  email?: string;
};

export class AccountAuthError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = "AccountAuthError";
    this.status = status;
    this.code = code;
  }
}

function isRecord(input: unknown): input is Record<string, unknown> {
  return typeof input === "object" && input !== null && !Array.isArray(input);
}

function readFixtureSessions(env: Env): Record<string, AccountAuthContext> {
  const raw = env.AESTRA_ACCOUNT_FIXTURE_SESSIONS?.trim();
  if (!raw) {
    return {};
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(raw) as unknown;
  } catch {
    throw new AccountAuthError(500, "auth_fixture_malformed", "account auth fixture sessions must be valid JSON");
  }
  if (!isRecord(parsed)) {
    throw new AccountAuthError(500, "auth_fixture_malformed", "account auth fixture sessions must be an object");
  }

  const sessions: Record<string, AccountAuthContext> = {};
  for (const [token, value] of Object.entries(parsed)) {
    if (!isRecord(value) || typeof value.account_id !== "string" || typeof value.user_id !== "string") {
      throw new AccountAuthError(500, "auth_fixture_malformed", "account auth fixture session is malformed");
    }
    if (token.length === 0 || value.account_id.length === 0 || value.user_id.length === 0) {
      throw new AccountAuthError(500, "auth_fixture_malformed", "account auth fixture session has empty identity");
    }
    sessions[token] = {
      accountId: value.account_id,
      userId: value.user_id,
      ...(typeof value.email === "string" && value.email.length > 0 ? { email: value.email } : {}),
    };
  }
  return sessions;
}

export async function authenticateAccountRequest(request: Request, env: Env, nowSeconds: number):
    Promise<AccountAuthContext> {
  const header = request.headers.get("authorization");
  if (!header?.startsWith("Bearer ")) {
    throw new AccountAuthError(401, "unauthorized", "missing account authorization");
  }

  const token = header.slice("Bearer ".length).trim();
  if (token.length === 0) {
    throw new AccountAuthError(401, "unauthorized", "missing account authorization");
  }

  if (storageMode(env) === "d1") {
    try {
      const account = await lookupAccountBySessionToken(env, token, nowSeconds);
      if (!account) {
        throw new AccountAuthError(401, "unauthorized", "invalid account authorization");
      }
      return account;
    } catch (error) {
      if (error instanceof AccountStoreError) {
        throw new AccountAuthError(error.status, error.code, error.message);
      }
      throw error;
    }
  }

  const sessions = readFixtureSessions(env);
  const session = sessions[token];
  if (!session) {
    throw new AccountAuthError(401, "unauthorized", "invalid account authorization");
  }
  return session;
}
