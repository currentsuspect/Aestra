import nacl from "tweetnacl";
import { describe, expect, it } from "vitest";
import { canonicalizeLease, type LeasePayload } from "../src/canonicalLease";
import { bytesToHex, signCanonicalLease, utf8Encode, type Env } from "../src/signing";

function base64Encode(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) {
    binary += String.fromCharCode(byte);
  }
  return btoa(binary);
}

function hexToBytes(hex: string): Uint8Array {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i += 1) {
    out[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

const fixtureLease: LeasePayload = {
  license_id: "lic_worker_fixture_1",
  user_id: "user_worker_fixture_1",
  tier: "Supporter",
  plugins: ["com.Aestrastudios.rumble", "com.Aestrastudios.experimental"],
  features: ["rumble", "cloud_sync"],
  device_hash:
    "v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:cccccccccccccccccccccccccccccccc:dddddddddddddddddddddddddddddddd",
  issued_at: 1700000000,
  expires_at: 1700604800,
  grace_policy: "restrict",
  revocation_epoch: 0,
};

const expectedCanonical =
  "{\"license_id\":\"lic_worker_fixture_1\",\"user_id\":\"user_worker_fixture_1\",\"tier\":\"Supporter\",\"plugins\":[\"com.Aestrastudios.rumble\",\"com.Aestrastudios.experimental\"],\"features\":[\"rumble\",\"cloud_sync\"],\"device_hash\":\"v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:cccccccccccccccccccccccccccccccc:dddddddddddddddddddddddddddddddd\",\"issued_at\":1700000000,\"expires_at\":1700604800,\"grace_policy\":\"restrict\",\"revocation_epoch\":0}";
const expectedPublicKeyHex = "bf30bfb9e66ff349bb96922b26e92fb860272adc2413f15b4052bc8b56800f58";
const expectedSignatureHex =
  "1976d12e7dc5e399f472ef0af739a33280c3df0624d093c2c2ba4506b56860b7b2e6108ece9b0e0fd6723d91003dd360fe2436e0163dace0328bf2dc0ca61908";

describe("LicenseGate cross-language fixture", () => {
  it("generates the exact canonical lease and signature verified by the C++ fixture test", async () => {
    const secret_b64 = process.env.AESTRA_LICENSE_GATE_FIXTURE_PRIVATE_KEY;
    if (!secret_b64) {
      throw new Error("AESTRA_LICENSE_GATE_FIXTURE_PRIVATE_KEY is required");
    }
    const secret = Uint8Array.from(atob(secret_b64), (c: string) => c.charCodeAt(0));
    const keyPair = nacl.sign.keyPair.fromSecretKey(secret);
    const env: Env = {
      AESTRA_LICENSE_SIGNING_PRIVATE_KEY: base64Encode(secret),
      AESTRA_ADMIN_API_KEY: "fixture-admin-key",
      AESTRA_SIGNING_KEY_ID: "AESTRA_DEV_TEST_PUBKEY_V3",
    };

    const canonical = canonicalizeLease(fixtureLease);
    const signatureHex = await signCanonicalLease(canonical, env);
    const signature = hexToBytes(signatureHex);

    expect(canonical).toBe(expectedCanonical);
    expect(bytesToHex(keyPair.publicKey)).toBe(expectedPublicKeyHex);
    expect(signatureHex).toBe(expectedSignatureHex);
    expect(nacl.sign.detached.verify(utf8Encode(canonical), signature, keyPair.publicKey)).toBe(true);
    expect(nacl.sign.detached.verify(utf8Encode(`${canonical}x`), signature, keyPair.publicKey)).toBe(false);
  });
});
