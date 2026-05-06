import type { LeasePayload, LicenseTier } from "./canonicalLease";

const LEASE_PERIOD_SECONDS = 604800;
const MAX_STRING_LENGTH = 512;
const MAX_ARRAY_LENGTH = 64;
const MAX_ISSUE_COUNT = 32;
const TIERS = new Set<LicenseTier>(["Core", "Supporter", "Founder"]);

export class SignRequestError extends Error {
  readonly issues: string[];

  constructor(issues: string[]) {
    super("Invalid entitlement signing request");
    this.name = "SignRequestError";
    this.issues = issues.slice(0, MAX_ISSUE_COUNT);
  }
}

function isRecord(input: unknown): input is Record<string, unknown> {
  return typeof input === "object" && input !== null && !Array.isArray(input);
}

function hasUnsafeControlCharacter(value: string): boolean {
  for (let i = 0; i < value.length; i += 1) {
    const code = value.charCodeAt(i);
    if (code < 0x20 && code !== 0x09 && code !== 0x0a && code !== 0x0d) {
      return true;
    }
  }
  return false;
}

function readString(record: Record<string, unknown>, key: string, issues: string[]): string {
  const value = record[key];
  if (typeof value !== "string") {
    issues.push(`${key} must be a string`);
    return "";
  }
  if (value.length === 0) {
    issues.push(`${key} must not be empty`);
  }
  if (value.length > MAX_STRING_LENGTH) {
    issues.push(`${key} exceeds ${MAX_STRING_LENGTH} characters`);
  }
  if (hasUnsafeControlCharacter(value)) {
    issues.push(`${key} contains unsupported control characters`);
  }
  return value;
}

function readInteger(record: Record<string, unknown>, key: string, issues: string[]): number {
  const value = record[key];
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value < 0) {
    issues.push(`${key} must be a non-negative safe integer`);
    return 0;
  }
  return value;
}

function readStringArray(record: Record<string, unknown>, key: string, issues: string[]): string[] {
  const value = record[key];
  if (!Array.isArray(value)) {
    issues.push(`${key} must be an array`);
    return [];
  }
  if (value.length > MAX_ARRAY_LENGTH) {
    issues.push(`${key} exceeds ${MAX_ARRAY_LENGTH} entries`);
  }

  const out: string[] = [];
  for (let i = 0; i < value.length; i += 1) {
    const item = value[i];
    if (typeof item !== "string") {
      issues.push(`${key}[${i}] must be a string`);
      continue;
    }
    if (item.length === 0) {
      issues.push(`${key}[${i}] must not be empty`);
    }
    if (item.length > MAX_STRING_LENGTH) {
      issues.push(`${key}[${i}] exceeds ${MAX_STRING_LENGTH} characters`);
    }
    if (hasUnsafeControlCharacter(item)) {
      issues.push(`${key}[${i}] contains unsupported control characters`);
    }
    out.push(item);
  }
  return out;
}

export function parseSignRequest(input: unknown): LeasePayload {
  const issues: string[] = [];
  if (!isRecord(input)) {
    throw new SignRequestError(["request body must be a JSON object"]);
  }

  const licenseId = readString(input, "license_id", issues);
  const userId = readString(input, "user_id", issues);
  const tierValue = readString(input, "tier", issues);
  if (!TIERS.has(tierValue as LicenseTier)) {
    issues.push("tier must be exactly Core, Supporter, or Founder");
  }
  const plugins = readStringArray(input, "plugins", issues);
  const features = readStringArray(input, "features", issues);
  const deviceHash = readString(input, "device_hash", issues);
  const issuedAt = readInteger(input, "issued_at", issues);
  const expiresAt = readInteger(input, "expires_at", issues);
  const gracePolicy = readString(input, "grace_policy", issues);
  if (gracePolicy !== "restrict") {
    issues.push("grace_policy must be restrict");
  }
  const revocationEpoch = readInteger(input, "revocation_epoch", issues);
  if (revocationEpoch !== 0) {
    issues.push("revocation_epoch must be 0");
  }
  if (expiresAt !== issuedAt + LEASE_PERIOD_SECONDS) {
    issues.push(`expires_at must equal issued_at + ${LEASE_PERIOD_SECONDS}`);
  }

  if (issues.length > 0) {
    throw new SignRequestError(issues);
  }

  return {
    license_id: licenseId,
    user_id: userId,
    tier: tierValue as LicenseTier,
    plugins,
    features,
    device_hash: deviceHash,
    issued_at: issuedAt,
    expires_at: expiresAt,
    grace_policy: "restrict",
    revocation_epoch: 0,
  };
}
