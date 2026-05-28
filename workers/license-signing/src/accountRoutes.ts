import { AccountAuthError, authenticateAccountRequest } from "./accountAuth";
import {
  AccountStoreError,
  getOrCreateActiveAccountByEmail,
  isValidEmail,
  normalizeEmail,
  revokeSessionToken,
} from "./accountStore";
import { EntitlementRepositoryError, getEntitlementForAccount } from "./entitlementRepository";
import { LoginChallengeError, createLoginChallenge, deleteLoginChallenge, verifyLoginChallenge } from "./loginChallengeStore";
import { MailerError, assertLoginMailerConfigured, sendLoginChallenge } from "./mailer";
import { buildSignedEntitlementRefreshResponse, parseAccountRefreshRequest } from "./refreshResponse";
import { SignRequestError } from "./schema";
import { issueAccountSession } from "./sessionIssuer";
import type { Env } from "./signing";

type JsonResponder = (body: unknown, status?: number) => Response;
type ErrorResponder = (status: number, code: string, message: string, issues?: string[]) => Response;

function isRecord(input: unknown): input is Record<string, unknown> {
  return typeof input === "object" && input !== null && !Array.isArray(input);
}

function readBearerToken(request: Request): string {
  const header = request.headers.get("authorization");
  if (!header?.startsWith("Bearer ")) {
    throw new AccountAuthError(401, "unauthorized", "missing account authorization");
  }
  const token = header.slice("Bearer ".length).trim();
  if (token.length === 0) {
    throw new AccountAuthError(401, "unauthorized", "missing account authorization");
  }
  return token;
}

async function parseJson(request: Request): Promise<unknown> {
  try {
    return await request.json();
  } catch {
    throw new SignRequestError(["request body must be valid JSON"]);
  }
}

function errorFrom(error: unknown, errorResponse: ErrorResponder): Response {
  if (error instanceof AccountAuthError || error instanceof AccountStoreError || error instanceof LoginChallengeError ||
      error instanceof MailerError) {
    return errorResponse(error.status, error.code, error.message);
  }
  if (error instanceof SignRequestError) {
    return errorResponse(400, "invalid_request", error.message, error.issues);
  }
  if (error instanceof EntitlementRepositoryError) {
    return errorResponse(500, error.code, error.message);
  }
  return errorResponse(500, "account_request_failed", "account request failed");
}

function readEmail(input: unknown): string {
  if (!isRecord(input) || typeof input.email !== "string") {
    throw new SignRequestError(["email must be a string"]);
  }
  const email = normalizeEmail(input.email);
  if (!isValidEmail(email)) {
    throw new SignRequestError(["email must be valid"]);
  }
  return email;
}

function loginStartSourceKey(request: Request): string | undefined {
  return request.headers.get("cf-connecting-ip") ??
    request.headers.get("x-forwarded-for")?.split(",")[0]?.trim() ??
    undefined;
}

export async function handleLoginStart(request: Request, env: Env, jsonResponse: JsonResponder,
                                       errorResponse: ErrorResponder): Promise<Response> {
  try {
    const nowSeconds = Math.floor(Date.now() / 1000);
    const email = readEmail(await parseJson(request));
    assertLoginMailerConfigured(env);
    const challenge = await createLoginChallenge(env, email, nowSeconds, {
      sourceKey: loginStartSourceKey(request),
    });
    let delivery;
    try {
      delivery = await sendLoginChallenge(env, email, challenge);
    } catch (error) {
      await deleteLoginChallenge(env, challenge.id).catch(() => undefined);
      throw error;
    }
    return jsonResponse({
      ok: true,
      challenge_id: challenge.id,
      expires_at: challenge.expiresAt,
      ...(delivery.exposeCode ? { fixture_code: challenge.code } : {}),
    });
  } catch (error) {
    return errorFrom(error, errorResponse);
  }
}

export async function handleLoginVerify(request: Request, env: Env, jsonResponse: JsonResponder,
                                        errorResponse: ErrorResponder): Promise<Response> {
  try {
    const nowSeconds = Math.floor(Date.now() / 1000);
    const input = await parseJson(request);
    const email = readEmail(input);
    if (!isRecord(input) || typeof input.challenge_id !== "string" || typeof input.code !== "string") {
      throw new SignRequestError(["challenge_id and code must be strings"]);
    }
    if (input.challenge_id.length === 0 || input.code.length === 0) {
      throw new SignRequestError(["challenge_id and code must not be empty"]);
    }

    await verifyLoginChallenge(env, email, input.challenge_id, input.code, nowSeconds);
    const account = await getOrCreateActiveAccountByEmail(env, email, nowSeconds);
    const session = await issueAccountSession(env, account.id, nowSeconds);
    return jsonResponse({
      ok: true,
      account: {
        id: account.id,
        email: account.email,
        status: account.status,
      },
      session: {
        token: session.token,
        expires_at: session.expiresAt,
      },
    });
  } catch (error) {
    return errorFrom(error, errorResponse);
  }
}

export async function handleAccountMe(request: Request, env: Env, jsonResponse: JsonResponder,
                                      errorResponse: ErrorResponder): Promise<Response> {
  try {
    const nowSeconds = Math.floor(Date.now() / 1000);
    const auth = await authenticateAccountRequest(request, env, nowSeconds);
    const entitlement = await getEntitlementForAccount(auth, env, nowSeconds);
    return jsonResponse({
      ok: true,
      account: {
        id: auth.accountId,
        email: auth.email ?? null,
        status: "active",
      },
      entitlement: {
        tier: entitlement.tier,
        status: entitlement.effectiveStatus,
        features: entitlement.features,
      },
    });
  } catch (error) {
    return errorFrom(error, errorResponse);
  }
}

export async function handleSessionRevoke(request: Request, env: Env, jsonResponse: JsonResponder,
                                          errorResponse: ErrorResponder): Promise<Response> {
  try {
    const nowSeconds = Math.floor(Date.now() / 1000);
    const token = readBearerToken(request);
    await authenticateAccountRequest(request, env, nowSeconds);
    await revokeSessionToken(env, token, nowSeconds);
    return jsonResponse({ ok: true });
  } catch (error) {
    return errorFrom(error, errorResponse);
  }
}

export async function handleAccountRefresh(request: Request, env: Env, jsonResponse: JsonResponder,
                                           errorResponse: ErrorResponder): Promise<Response> {
  try {
    const nowSeconds = Math.floor(Date.now() / 1000);
    const auth = await authenticateAccountRequest(request, env, nowSeconds);
    const refreshRequest = parseAccountRefreshRequest(await parseJson(request), nowSeconds);
    const entitlement = await getEntitlementForAccount(auth, env, nowSeconds);
    return jsonResponse(await buildSignedEntitlementRefreshResponse(auth, entitlement, refreshRequest, env));
  } catch (error) {
    return errorFrom(error, errorResponse);
  }
}

export { parseJson as parseAccountJson };
