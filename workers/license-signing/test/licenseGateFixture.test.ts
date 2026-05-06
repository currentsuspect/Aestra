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
const expectedPublicKeyHex = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const expectedSignatureHex =
  "6570b63ace3cd4848ce82aa5afe4aa37120a86e6c31f44eef5dfaab350fe29d165476c64f1bfb1741b4ff4b2db1d44c9866edb96e1cf974be24c7b424c575506";

describe("LicenseGate cross-language fixture", () => {
  it("generates the exact canonical lease and signature verified by the C++ fixture test", async () => {
    const seed = Uint8Array.from(Array.from({ length: 32 }, (_, i) => i));
    const keyPair = nacl.sign.keyPair.fromSeed(seed);
    const env: Env = {
      AESTRA_LICENSE_SIGNING_PRIVATE_KEY: base64Encode(keyPair.secretKey),
      AESTRA_ADMIN_API_KEY: "fixture-admin-key",
      AESTRA_SIGNING_KEY_ID: "aestra-worker-fixture-v1",
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
