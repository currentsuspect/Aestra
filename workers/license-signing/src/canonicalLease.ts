export type LicenseTier = "Core" | "Supporter" | "Founder";

export type LeasePayload = {
  license_id: string;
  user_id: string;
  tier: LicenseTier;
  plugins: string[];
  features: string[];
  device_hash: string;
  issued_at: number;
  expires_at: number;
  grace_policy: "restrict";
  revocation_epoch: 0;
};

function escapeLicenseString(value: string): string {
  let out = "";
  for (const ch of value) {
    switch (ch) {
      case "\\":
        out += "\\\\";
        break;
      case "\"":
        out += "\\\"";
        break;
      case "\n":
        out += "\\n";
        break;
      case "\r":
        out += "\\r";
        break;
      case "\t":
        out += "\\t";
        break;
      default:
        out += ch;
        break;
    }
  }
  return out;
}

function quoteLicenseString(value: string): string {
  return `"${escapeLicenseString(value)}"`;
}

function stringArrayToJson(values: string[]): string {
  return `[${values.map(quoteLicenseString).join(",")}]`;
}

export function canonicalizeLease(payload: LeasePayload): string {
  return "{"
    + `"license_id":${quoteLicenseString(payload.license_id)},`
    + `"user_id":${quoteLicenseString(payload.user_id)},`
    + `"tier":${quoteLicenseString(payload.tier)},`
    + `"plugins":${stringArrayToJson(payload.plugins)},`
    + `"features":${stringArrayToJson(payload.features)},`
    + `"device_hash":${quoteLicenseString(payload.device_hash)},`
    + `"issued_at":${payload.issued_at},`
    + `"expires_at":${payload.expires_at},`
    + `"grace_policy":${quoteLicenseString(payload.grace_policy)},`
    + `"revocation_epoch":${payload.revocation_epoch}}`;
}
