import { canonicalizeLease, type LeasePayload } from "./canonicalLease";
import type { AccountAuthContext } from "./accountAuth";
import type { EntitlementRecord } from "./entitlementRepository";
import { MAX_CLIENT_CLOCK_SKEW_SECONDS } from "./constants";
import { parseSignRequest, SignRequestError } from "./schema";
import { signCanonicalLease, type Env } from "./signing";

const LEASE_PERIOD_SECONDS = 604800;

export type AccountRefreshRequest = {
  deviceHash: string;
  issuedAt: number;
};

function isRecord(input: unknown): input is Record<string, unknown> {
  return typeof input === "object" && input !== null && !Array.isArray(input);
}

export function keyId(env: Env): string {
  return env.AESTRA_SIGNING_KEY_ID?.trim() || "dev";
}

export function parseAccountRefreshRequest(input: unknown, nowSeconds: number): AccountRefreshRequest {
  const issues: string[] = [];
  if (!isRecord(input)) {
    throw new SignRequestError(["request body must be a JSON object"]);
  }

  const deviceHash = input.device_hash;
  if (typeof deviceHash !== "string" || deviceHash.length === 0) {
    issues.push("device_hash must be a non-empty string");
  }
  if (typeof deviceHash === "string" && deviceHash.length > 512) {
    issues.push("device_hash exceeds 512 characters");
  }

  const issuedAtValue = input.issued_at;
  let issuedAt = nowSeconds;
  if (issuedAtValue !== undefined) {
    if (typeof issuedAtValue !== "number" || !Number.isSafeInteger(issuedAtValue) || issuedAtValue < 0) {
      issues.push("issued_at must be a non-negative safe integer when present");
    } else if (issuedAtValue > nowSeconds + MAX_CLIENT_CLOCK_SKEW_SECONDS) {
      issues.push("issued_at must not be in the future");
    } else {
      issuedAt = issuedAtValue;
    }
  }

  if (issues.length > 0) {
    throw new SignRequestError(issues);
  }

  return { deviceHash: deviceHash as string, issuedAt };
}

export async function buildSignedLeaseResponse(payload: LeasePayload, env: Env, includeLeaseBlob: boolean,
                                               nowSeconds?: number):
    Promise<Record<string, unknown>> {
  const validatedPayload = parseSignRequest(payload, nowSeconds);
  const canonical = canonicalizeLease(validatedPayload);
  const signatureHex = await signCanonicalLease(canonical, env);
  return {
    payload: validatedPayload,
    canonical,
    signature_hex: signatureHex,
    ...(includeLeaseBlob ? { lease_blob: `${canonical}\n${signatureHex}` } : {}),
    key_id: keyId(env),
    format: "aestra-license-v1",
  };
}

export async function buildSignedEntitlementRefreshResponse(auth: AccountAuthContext, entitlement: EntitlementRecord,
                                                           request: AccountRefreshRequest, env: Env):
    Promise<Record<string, unknown>> {
  const payload: LeasePayload = {
    license_id: entitlement.licenseId,
    user_id: auth.userId,
    tier: entitlement.tier,
    plugins: entitlement.plugins,
    features: entitlement.features,
    device_hash: request.deviceHash,
    issued_at: request.issuedAt,
    expires_at: request.issuedAt + LEASE_PERIOD_SECONDS,
    grace_policy: "restrict",
    revocation_epoch: entitlement.revocationEpoch,
  };

  const body = await buildSignedLeaseResponse(payload, env, true);
  return {
    ...body,
    entitlement_status: entitlement.effectiveStatus,
    ...(entitlement.currentPeriodEnd !== undefined ? { current_period_end: entitlement.currentPeriodEnd } : {}),
    ...(entitlement.graceUntil !== undefined ? { grace_until: entitlement.graceUntil } : {}),
  };
}
