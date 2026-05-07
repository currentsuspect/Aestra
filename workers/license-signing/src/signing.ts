import nacl from "tweetnacl";

export type Env = {
  AESTRA_LICENSE_SIGNING_PRIVATE_KEY: string;
  AESTRA_ADMIN_API_KEY: string;
  AESTRA_SIGNING_KEY_ID?: string;
  AESTRA_ACCOUNT_FIXTURE_SESSIONS?: string;
  AESTRA_ENTITLEMENT_FIXTURES?: string;
  AESTRA_STORAGE_MODE?: "fixture" | "d1";
  AESTRA_LICENSE_DB?: D1Database;
  AESTRA_LOGIN_MAILER_MODE?: "fixture" | "configured";
};

export function utf8Encode(value: string): Uint8Array {
  return new TextEncoder().encode(value);
}

export function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export function base64Decode(value: string): Uint8Array {
  const trimmed = value.trim();
  if (trimmed.length === 0) {
    throw new Error("missing signing private key");
  }

  const binary = atob(trimmed);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

export async function signCanonicalLease(canonical: string, env: Env): Promise<string> {
  const secretKey = base64Decode(env.AESTRA_LICENSE_SIGNING_PRIVATE_KEY);
  if (secretKey.length !== nacl.sign.secretKeyLength) {
    throw new Error("signing private key must decode to 64 bytes");
  }

  const signature = nacl.sign.detached(utf8Encode(canonical), secretKey);
  return bytesToHex(signature);
}
