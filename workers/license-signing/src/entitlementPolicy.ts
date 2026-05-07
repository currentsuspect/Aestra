import type { LicenseTier } from "./canonicalLease";
import type { AccountAuthContext } from "./accountAuth";
import type { EntitlementRecord } from "./entitlementRepository";

export type PersistedEntitlementRow = {
  id: string;
  account_id: string;
  tier: string;
  status: string;
  source: string;
  current_period_start: number | null;
  current_period_end: number | null;
  grace_until: number | null;
  revoked_at: number | null;
};

export class EntitlementPolicyError extends Error {
  readonly code: string;

  constructor(code: string, message: string) {
    super(message);
    this.name = "EntitlementPolicyError";
    this.code = code;
  }
}

const TIER_RANK: Record<LicenseTier, number> = {
  Core: 0,
  Supporter: 1,
  Founder: 2,
};

function tierFromStorage(value: string): LicenseTier {
  switch (value) {
    case "core":
      return "Core";
    case "supporter":
      return "Supporter";
    case "founder":
      return "Founder";
    default:
      throw new EntitlementPolicyError("entitlement_unknown_tier", "entitlement tier is unsupported");
  }
}

function pluginsForTier(tier: LicenseTier): string[] {
  return tier === "Core" ? [] : ["com.aestra.rumble"];
}

function featuresForTier(tier: LicenseTier): string[] {
  switch (tier) {
    case "Founder":
      return ["rumble", "rumble_headless"];
    case "Supporter":
      return ["rumble"];
    case "Core":
    default:
      return [];
  }
}

export function coreEntitlementForAccount(auth: AccountAuthContext): EntitlementRecord {
  return {
    licenseId: `core_${auth.accountId}`,
    tier: "Core",
    plugins: [],
    features: [],
    revocationEpoch: 0,
    effectiveStatus: "core",
  };
}

function effectivePriority(row: PersistedEntitlementRow, nowSeconds: number): number {
  switch (row.status) {
    case "active":
    case "trialing":
      return row.revoked_at === null ? 2 : 0;
    case "grace_period":
      return row.revoked_at === null && (row.grace_until === null || row.grace_until > nowSeconds) ? 1 : 0;
    case "canceled":
    case "expired":
    case "revoked":
      return 0;
    default:
      throw new EntitlementPolicyError("entitlement_unknown_status", "entitlement status is unsupported");
  }
}

function rowToEntitlement(row: PersistedEntitlementRow, priority: number): EntitlementRecord {
  const tier = tierFromStorage(row.tier);
  return {
    licenseId: row.id,
    tier,
    plugins: pluginsForTier(tier),
    features: featuresForTier(tier),
    revocationEpoch: 0,
    effectiveStatus: priority === 1 ? "grace_period" : "active",
    ...(row.current_period_end !== null ? { currentPeriodEnd: row.current_period_end } : {}),
    ...(row.grace_until !== null ? { graceUntil: row.grace_until } : {}),
  };
}

export function chooseEffectiveEntitlement(auth: AccountAuthContext, rows: PersistedEntitlementRow[],
                                           nowSeconds: number): EntitlementRecord {
  let best: { row: PersistedEntitlementRow; priority: number; tier: LicenseTier } | null = null;

  for (const row of rows) {
    if (row.account_id !== auth.accountId) {
      throw new EntitlementPolicyError("entitlement_account_mismatch", "entitlement row account does not match");
    }
    if (row.id.length === 0 || row.source.length === 0) {
      throw new EntitlementPolicyError("entitlement_malformed", "entitlement row is malformed");
    }
    const tier = tierFromStorage(row.tier);
    const priority = effectivePriority(row, nowSeconds);
    if (priority === 0) {
      continue;
    }
    if (!best || priority > best.priority || (priority === best.priority && TIER_RANK[tier] > TIER_RANK[best.tier]) ||
        (priority === best.priority && TIER_RANK[tier] === TIER_RANK[best.tier] && row.id < best.row.id)) {
      best = { row, priority, tier };
    }
  }

  return best ? rowToEntitlement(best.row, best.priority) : coreEntitlementForAccount(auth);
}
