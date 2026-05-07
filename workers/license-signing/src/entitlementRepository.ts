import type { LicenseTier } from "./canonicalLease";
import type { AccountAuthContext } from "./accountAuth";
import { chooseEffectiveEntitlement, coreEntitlementForAccount, EntitlementPolicyError,
  type PersistedEntitlementRow } from "./entitlementPolicy";
import { storageMode } from "./accountStore";
import type { Env } from "./signing";

const TIERS = new Set<LicenseTier>(["Core", "Supporter", "Founder"]);

export type EntitlementRecord = {
  licenseId: string;
  tier: LicenseTier;
  plugins: string[];
  features: string[];
  revocationEpoch: 0;
  effectiveStatus: "core" | "active" | "grace_period";
  currentPeriodEnd?: number;
  graceUntil?: number;
};

export class EntitlementRepositoryError extends Error {
  readonly code: string;

  constructor(code: string, message: string) {
    super(message);
    this.name = "EntitlementRepositoryError";
    this.code = code;
  }
}

export type AdminEntitlementRecord = {
  id: string;
  accountId: string;
  tier: string;
  status: string;
  source: string;
  sourceRef: string | null;
  currentPeriodStart: number | null;
  currentPeriodEnd: number | null;
  graceUntil: number | null;
  revokedAt: number | null;
  metadataJson: string | null;
};

function isRecord(input: unknown): input is Record<string, unknown> {
  return typeof input === "object" && input !== null && !Array.isArray(input);
}

function readStringArray(record: Record<string, unknown>, key: string): string[] {
  const value = record[key];
  if (!Array.isArray(value) || value.some((item) => typeof item !== "string" || item.length === 0)) {
    throw new EntitlementRepositoryError("entitlement_malformed", `entitlement ${key} must be an array of strings`);
  }
  return value;
}

function parseEntitlementRecord(input: unknown): EntitlementRecord {
  if (!isRecord(input)) {
    throw new EntitlementRepositoryError("entitlement_malformed", "entitlement record must be an object");
  }

  const licenseId = input.license_id;
  const tier = input.tier;
  const revocationEpoch = input.revocation_epoch ?? 0;
  if (typeof licenseId !== "string" || licenseId.length === 0) {
    throw new EntitlementRepositoryError("entitlement_malformed", "entitlement license_id must be a non-empty string");
  }
  if (typeof tier !== "string" || !TIERS.has(tier as LicenseTier)) {
    throw new EntitlementRepositoryError("entitlement_malformed", "entitlement tier is unsupported");
  }
  if (revocationEpoch !== 0) {
    throw new EntitlementRepositoryError("entitlement_malformed", "entitlement revocation_epoch must be 0");
  }

  return {
    licenseId,
    tier: tier as LicenseTier,
    plugins: readStringArray(input, "plugins"),
    features: readStringArray(input, "features"),
    revocationEpoch: 0,
    effectiveStatus: "active",
  };
}

function requireD1(env: Env): D1Database {
  if (!env.AESTRA_LICENSE_DB) {
    throw new EntitlementRepositoryError("storage_unconfigured", "D1 entitlement storage is not configured");
  }
  return env.AESTRA_LICENSE_DB;
}

async function getD1EntitlementForAccount(auth: AccountAuthContext, env: Env, nowSeconds: number):
    Promise<EntitlementRecord> {
  const db = requireD1(env);
  const rows = await db.prepare(`
      SELECT
        id,
        account_id,
        tier,
        status,
        source,
        current_period_start,
        current_period_end,
        grace_until,
        revoked_at
      FROM account_entitlements
      WHERE account_id = ?
    `).bind(auth.accountId).all<PersistedEntitlementRow>();

  if (!rows.success) {
    throw new EntitlementRepositoryError("entitlement_query_failed", "entitlement lookup failed");
  }

  try {
    return chooseEffectiveEntitlement(auth, rows.results ?? [], nowSeconds);
  } catch (error) {
    if (error instanceof EntitlementPolicyError) {
      throw new EntitlementRepositoryError(error.code, error.message);
    }
    throw error;
  }
}

function getFixtureEntitlementForAccount(auth: AccountAuthContext, env: Env): EntitlementRecord {
  const raw = env.AESTRA_ENTITLEMENT_FIXTURES?.trim();
  if (!raw) {
    return coreEntitlementForAccount(auth);
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(raw) as unknown;
  } catch {
    throw new EntitlementRepositoryError("entitlement_fixture_malformed", "entitlement fixtures must be valid JSON");
  }
  if (!isRecord(parsed)) {
    throw new EntitlementRepositoryError("entitlement_fixture_malformed", "entitlement fixtures must be an object");
  }

  const fixtureRecord = parsed[auth.accountId];
  if (fixtureRecord === undefined) {
    return coreEntitlementForAccount(auth);
  }
  return parseEntitlementRecord(fixtureRecord);
}

export async function getEntitlementForAccount(auth: AccountAuthContext, env: Env, nowSeconds: number):
    Promise<EntitlementRecord> {
  if (storageMode(env) === "d1") {
    return getD1EntitlementForAccount(auth, env, nowSeconds);
  }
  return getFixtureEntitlementForAccount(auth, env);
}

export async function listD1EntitlementRowsForAccount(env: Env, accountId: string): Promise<AdminEntitlementRecord[]> {
  const db = requireD1(env);
  const rows = await db.prepare(`
      SELECT
        id,
        account_id,
        tier,
        status,
        source,
        source_ref,
        current_period_start,
        current_period_end,
        grace_until,
        revoked_at,
        metadata_json
      FROM account_entitlements
      WHERE account_id = ?
      ORDER BY created_at DESC, id ASC
    `).bind(accountId).all<{
      id: string;
      account_id: string;
      tier: string;
      status: string;
      source: string;
      source_ref: string | null;
      current_period_start: number | null;
      current_period_end: number | null;
      grace_until: number | null;
      revoked_at: number | null;
      metadata_json: string | null;
    }>();

  if (!rows.success) {
    throw new EntitlementRepositoryError("entitlement_query_failed", "entitlement lookup failed");
  }

  return (rows.results ?? []).map((row) => ({
    id: row.id,
    accountId: row.account_id,
    tier: row.tier,
    status: row.status,
    source: row.source,
    sourceRef: row.source_ref,
    currentPeriodStart: row.current_period_start,
    currentPeriodEnd: row.current_period_end,
    graceUntil: row.grace_until,
    revokedAt: row.revoked_at,
    metadataJson: row.metadata_json,
  }));
}
