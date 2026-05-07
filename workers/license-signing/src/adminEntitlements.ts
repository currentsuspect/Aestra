import { getOrCreateActiveAccountByEmail, isValidEmail, lookupAccountByEmail, normalizeEmail, requireD1 } from "./accountStore";
import { chooseEffectiveEntitlement, EntitlementPolicyError, type PersistedEntitlementRow } from "./entitlementPolicy";
import { listD1EntitlementRowsForAccount } from "./entitlementRepository";
import type { Env } from "./signing";

const ALLOWED_TIERS = new Set(["core", "supporter", "founder"]);
const ALLOWED_STATUSES = new Set(["active", "trialing", "grace_period", "canceled", "expired", "revoked"]);
const ALLOWED_SOURCES = new Set(["manual_grant"]);

export class AdminEntitlementError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = "AdminEntitlementError";
    this.status = status;
    this.code = code;
  }
}

export type AdminGrantRequest = {
  email: string;
  tier: string;
  status: string;
  source: string;
  sourceRef: string | null;
  currentPeriodEnd: number | null;
  graceUntil: number | null;
  reason: string | null;
};

export type AdminRevokeRequest = {
  email: string;
  entitlementId: string;
  reason: string | null;
};

function isRecord(input: unknown): input is Record<string, unknown> {
  return typeof input === "object" && input !== null && !Array.isArray(input);
}

function randomId(prefix: string): string {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  const hex = Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
  return `${prefix}_${hex}`;
}

function readNullableInteger(input: Record<string, unknown>, key: string): number | null {
  const value = input[key];
  if (value === undefined || value === null) {
    return null;
  }
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value < 0) {
    throw new AdminEntitlementError(400, "invalid_request", `${key} must be null or a non-negative integer`);
  }
  return value;
}

function readNullableString(input: Record<string, unknown>, key: string): string | null {
  const value = input[key];
  if (value === undefined || value === null) {
    return null;
  }
  if (typeof value !== "string") {
    throw new AdminEntitlementError(400, "invalid_request", `${key} must be null or a string`);
  }
  return value.trim().length > 0 ? value.trim() : null;
}

function readEmail(input: Record<string, unknown>): string {
  if (typeof input.email !== "string") {
    throw new AdminEntitlementError(400, "invalid_request", "email must be a string");
  }
  const email = normalizeEmail(input.email);
  if (!isValidEmail(email)) {
    throw new AdminEntitlementError(400, "invalid_request", "email must be valid");
  }
  return email;
}

export function parseAdminGrantRequest(input: unknown): AdminGrantRequest {
  if (!isRecord(input)) {
    throw new AdminEntitlementError(400, "invalid_request", "request body must be a JSON object");
  }
  const email = readEmail(input);
  const tier = typeof input.tier === "string" ? input.tier.trim().toLowerCase() : "";
  const status = typeof input.status === "string" ? input.status.trim().toLowerCase() : "";
  const source = typeof input.source === "string" ? input.source.trim() : "";
  if (!ALLOWED_TIERS.has(tier)) {
    throw new AdminEntitlementError(400, "entitlement_unknown_tier", "entitlement tier is unsupported");
  }
  if (!ALLOWED_STATUSES.has(status)) {
    throw new AdminEntitlementError(400, "entitlement_unknown_status", "entitlement status is unsupported");
  }
  if (!ALLOWED_SOURCES.has(source)) {
    throw new AdminEntitlementError(400, "invalid_source", "manual admin grants must use manual_grant source");
  }

  return {
    email,
    tier,
    status,
    source,
    sourceRef: readNullableString(input, "source_ref"),
    currentPeriodEnd: readNullableInteger(input, "current_period_end"),
    graceUntil: readNullableInteger(input, "grace_until"),
    reason: readNullableString(input, "reason"),
  };
}

export function parseAdminRevokeRequest(input: unknown): AdminRevokeRequest {
  if (!isRecord(input)) {
    throw new AdminEntitlementError(400, "invalid_request", "request body must be a JSON object");
  }
  const email = readEmail(input);
  if (typeof input.entitlement_id !== "string" || input.entitlement_id.trim().length === 0) {
    throw new AdminEntitlementError(400, "invalid_request", "entitlement_id must be a non-empty string");
  }
  return {
    email,
    entitlementId: input.entitlement_id.trim(),
    reason: readNullableString(input, "reason"),
  };
}

async function writeEntitlementEvent(env: Env, accountId: string, entitlementId: string | null, eventType: string,
                                     sourceRef: string | null, reason: string | null, nowSeconds: number):
    Promise<void> {
  const db = requireD1(env);
  await db.prepare(`
      INSERT INTO entitlement_events
        (id, account_id, entitlement_id, event_type, source, source_ref, created_at, metadata_json)
      VALUES (?, ?, ?, ?, 'admin', ?, ?, ?)
    `).bind(randomId("evt"), accountId, entitlementId, eventType, sourceRef, nowSeconds,
      JSON.stringify({ reason })).run();
}

function effectiveSummary(accountId: string, rows: PersistedEntitlementRow[], nowSeconds: number) {
  try {
    const entitlement = chooseEffectiveEntitlement({ accountId, userId: accountId }, rows, nowSeconds);
    return {
      tier: entitlement.tier,
      status: entitlement.effectiveStatus,
      features: entitlement.features,
    };
  } catch (error) {
    if (error instanceof EntitlementPolicyError) {
      throw new AdminEntitlementError(500, error.code, error.message);
    }
    throw error;
  }
}

async function persistedRows(env: Env, accountId: string): Promise<PersistedEntitlementRow[]> {
  return (await listD1EntitlementRowsForAccount(env, accountId)).map((row) => ({
    id: row.id,
    account_id: row.accountId,
    tier: row.tier,
    status: row.status,
    source: row.source,
    current_period_start: row.currentPeriodStart,
    current_period_end: row.currentPeriodEnd,
    grace_until: row.graceUntil,
    revoked_at: row.revokedAt,
  }));
}

export async function grantManualEntitlement(env: Env, request: AdminGrantRequest, nowSeconds: number) {
  const db = requireD1(env);
  const account = await getOrCreateActiveAccountByEmail(env, request.email, nowSeconds);
  const entitlementId = randomId("ent");
  const metadata = JSON.stringify({
    reason: request.reason,
    internal: request.sourceRef?.includes("admin:") ?? false,
  });

  await db.prepare(`
      INSERT INTO account_entitlements
        (id, account_id, tier, status, source, source_ref, current_period_start, current_period_end, grace_until,
         created_at, updated_at, revoked_at, metadata_json)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?)
    `).bind(entitlementId, account.id, request.tier, request.status, request.source, request.sourceRef, nowSeconds,
      request.currentPeriodEnd, request.graceUntil, nowSeconds, nowSeconds, metadata).run();
  await writeEntitlementEvent(env, account.id, entitlementId, "manual_grant", request.sourceRef, request.reason,
    nowSeconds);

  return {
    account,
    entitlement_id: entitlementId,
    effective_entitlement: effectiveSummary(account.id, await persistedRows(env, account.id), nowSeconds),
  };
}

export async function revokeManualEntitlement(env: Env, request: AdminRevokeRequest, nowSeconds: number) {
  const account = await lookupAccountByEmail(env, request.email);
  if (!account) {
    throw new AdminEntitlementError(404, "not_found", "account not found");
  }

  const rows = await listD1EntitlementRowsForAccount(env, account.id);
  const entitlement = rows.find((row) => row.id === request.entitlementId);
  if (!entitlement) {
    throw new AdminEntitlementError(404, "not_found", "entitlement not found");
  }

  const db = requireD1(env);
  await db.prepare(`
      UPDATE account_entitlements
      SET status = 'revoked', revoked_at = ?, updated_at = ?
      WHERE id = ? AND account_id = ?
    `).bind(nowSeconds, nowSeconds, request.entitlementId, account.id).run();
  await writeEntitlementEvent(env, account.id, request.entitlementId, "manual_revoke", null, request.reason,
    nowSeconds);

  return {
    account,
    entitlement_id: request.entitlementId,
    effective_entitlement: effectiveSummary(account.id, await persistedRows(env, account.id), nowSeconds),
  };
}

export async function lookupAdminAccount(env: Env, email: string, nowSeconds: number) {
  const normalizedEmail = normalizeEmail(email);
  if (!isValidEmail(normalizedEmail)) {
    throw new AdminEntitlementError(400, "invalid_request", "email must be valid");
  }

  const account = await lookupAccountByEmail(env, normalizedEmail);
  if (!account) {
    throw new AdminEntitlementError(404, "not_found", "account not found");
  }

  const entitlements = await listD1EntitlementRowsForAccount(env, account.id);
  return {
    account,
    sessions: {
      raw_tokens_exposed: false,
    },
    entitlements,
    effective_entitlement: effectiveSummary(account.id, await persistedRows(env, account.id), nowSeconds),
  };
}
