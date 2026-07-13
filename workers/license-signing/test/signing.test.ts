import nacl from "tweetnacl";
import { describe, expect, it } from "vitest";
import worker from "../src/index";
import { canonicalizeLease, type LeasePayload } from "../src/canonicalLease";
import { parseSignRequest, SignRequestError } from "../src/schema";
import { base64Decode, bytesToHex, signCanonicalLease, utf8Encode, type Env } from "../src/signing";

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

const keyPair = nacl.sign.keyPair();
const env: Env = {
  AESTRA_LICENSE_SIGNING_PRIVATE_KEY: base64Encode(keyPair.secretKey),
  AESTRA_ADMIN_API_KEY: "test-admin-key",
  AESTRA_SIGNING_KEY_ID: "aestra-test-key",
};

const accountEnv: Env = {
  ...env,
  AESTRA_ACCOUNT_FIXTURE_SESSIONS: JSON.stringify({
    "core-token": {
      account_id: "acct_core",
      user_id: "user_core",
      email: "core@example.test",
    },
    "supporter-token": {
      account_id: "acct_supporter",
      user_id: "user_supporter",
      email: "supporter@example.test",
    },
    "founder-token": {
      account_id: "acct_founder",
      user_id: "user_founder",
      email: "founder@example.test",
    },
  }),
  AESTRA_ENTITLEMENT_FIXTURES: JSON.stringify({
    acct_supporter: {
      license_id: "lic_supporter",
      tier: "Supporter",
      plugins: ["com.aestra.rumble"],
      features: ["rumble"],
      revocation_epoch: 0,
    },
    acct_founder: {
      license_id: "lic_founder",
      tier: "Founder",
      plugins: ["com.aestra.rumble"],
      features: ["rumble", "rumble_headless"],
      revocation_epoch: 0,
    },
  }),
};

const validLease: LeasePayload = {
  license_id: "lic_123",
  user_id: "user_123",
  tier: "Founder",
  plugins: ["com.aestra.rumble"],
  features: ["rumble"],
  device_hash: "device_abc",
  issued_at: 1234567890,
  expires_at: 1235172690,
  grace_policy: "restrict",
  revocation_epoch: 0,
};

const accountRefreshRequest = {
  device_hash: "device_account_1",
  issued_at: 1234567890,
  account_id: "ignored_request_account",
};

describe("parseSignRequest", () => {
  it("accepts a valid request without normalizing order or tier case", () => {
    expect(parseSignRequest(validLease)).toEqual(validLease);
  });

  it("rejects invalid tier values", () => {
    expect(() => parseSignRequest({ ...validLease, tier: "Plus" })).toThrow(SignRequestError);
    expect(() => parseSignRequest({ ...validLease, tier: "supporter" })).toThrow(SignRequestError);
  });

  it("rejects bad expiry, grace policy, revocation, missing device hash, and oversize values", () => {
    expect(() => parseSignRequest({ ...validLease, expires_at: validLease.issued_at + 1 })).toThrow(SignRequestError);
    expect(() => parseSignRequest({ ...validLease, grace_policy: "allow" })).toThrow(SignRequestError);
    expect(() => parseSignRequest({ ...validLease, revocation_epoch: 1 })).toThrow(SignRequestError);
    expect(() => parseSignRequest({ ...validLease, device_hash: "" })).toThrow(SignRequestError);
    expect(() => parseSignRequest({ ...validLease, features: Array.from({ length: 65 }, (_, i) => `f${i}`) }))
      .toThrow(SignRequestError);
  });

  it("rejects non-string plugin and feature entries", () => {
    expect(() => parseSignRequest({ ...validLease, plugins: ["ok", 1] })).toThrow(SignRequestError);
    expect(() => parseSignRequest({ ...validLease, features: ["ok", false] })).toThrow(SignRequestError);
  });

  it("rejects issue times beyond server clock skew", () => {
    expect(parseSignRequest({
      ...validLease,
      issued_at: 1300,
      expires_at: 606100,
    }, 1000).issued_at).toBe(1300);
    expect(() => parseSignRequest({
      ...validLease,
      issued_at: 1301,
      expires_at: 606101,
    }, 1000)).toThrow(SignRequestError);
  });
});

describe("signCanonicalLease", () => {
  it("returns a 64-byte detached Ed25519 signature as 128 hex chars", async () => {
    const canonical = canonicalizeLease(validLease);
    const signatureHex = await signCanonicalLease(canonical, env);
    expect(signatureHex).toHaveLength(128);

    const signature = hexToBytes(signatureHex);
    expect(nacl.sign.detached.verify(utf8Encode(canonical), signature, keyPair.publicKey)).toBe(true);
    expect(nacl.sign.detached.verify(utf8Encode(`${canonical}x`), signature, keyPair.publicKey)).toBe(false);
  });

  it("hex helper emits lowercase fixed-width bytes", () => {
    expect(bytesToHex(new Uint8Array([0, 1, 15, 16, 255]))).toBe("00010f10ff");
  });
});

describe("worker endpoints", () => {
  it("returns health without exposing private key material", async () => {
    const response = await worker.fetch(new Request("https://example.test/health"), env);
    expect(response.status).toBe(200);
    await expect(response.json()).resolves.toEqual({
      ok: true,
      service: "aestra-license-signing",
      version: 1,
      key_id: "aestra-test-key",
    });
  });

  it("rejects sign requests without admin authorization", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/entitlements/sign", {
      method: "POST",
      body: JSON.stringify(validLease),
    }), env);
    expect(response.status).toBe(401);
  });

  it("signs a valid lease request with admin authorization", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/entitlements/sign", {
      method: "POST",
      headers: {
        authorization: "Bearer test-admin-key",
        "content-type": "application/json",
      },
      body: JSON.stringify(validLease),
    }), env);
    expect(response.status).toBe(200);
    const body = await response.json() as {
      payload: LeasePayload;
      canonical: string;
      signature_hex: string;
      key_id: string;
      format: string;
    };
    expect(body.payload).toEqual(validLease);
    expect(body.canonical).toBe(canonicalizeLease(validLease));
    expect(body.signature_hex).toHaveLength(128);
    expect(body.key_id).toBe("aestra-test-key");
    expect(body.format).toBe("aestra-license-v1");
  });

  it("refreshes a valid lease request with a storage-ready lease blob", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer test-admin-key",
        "content-type": "application/json",
      },
      body: JSON.stringify(validLease),
    }), env);
    expect(response.status).toBe(200);
    const body = await response.json() as {
      payload: LeasePayload;
      canonical: string;
      signature_hex: string;
      lease_blob: string;
      key_id: string;
      format: string;
    };
    expect(body.payload).toEqual(validLease);
    expect(body.canonical).toBe(canonicalizeLease(validLease));
    expect(body.signature_hex).toHaveLength(128);
    expect(body.lease_blob).toBe(`${body.canonical}\n${body.signature_hex}`);
    expect(body.key_id).toBe("aestra-test-key");
    expect(body.format).toBe("aestra-license-v1");
  });

  it("rejects refresh requests without admin authorization", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/entitlements/refresh", {
      method: "POST",
      body: JSON.stringify(validLease),
    }), env);
    expect(response.status).toBe(401);
  });

  it("returns 400 for invalid request payloads", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/entitlements/sign", {
      method: "POST",
      headers: {
        authorization: "Bearer test-admin-key",
        "content-type": "application/json",
      },
      body: JSON.stringify({ ...validLease, tier: "supporter" }),
    }), env);
    expect(response.status).toBe(400);
  });

  it("returns 400 for future-issued admin sign payloads", async () => {
    const issuedAt = Math.floor(Date.now() / 1000) + 3600;
    const response = await worker.fetch(new Request("https://example.test/v1/entitlements/sign", {
      method: "POST",
      headers: {
        authorization: "Bearer test-admin-key",
        "content-type": "application/json",
      },
      body: JSON.stringify({ ...validLease, issued_at: issuedAt, expires_at: issuedAt + 604800 }),
    }), env);
    expect(response.status).toBe(400);
  });

  it("returns 400 for invalid refresh payloads", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer test-admin-key",
        "content-type": "application/json",
      },
      body: JSON.stringify({ ...validLease, expires_at: validLease.issued_at + 1 }),
    }), env);
    expect(response.status).toBe(400);
  });

  it("rejects account refresh without account authorization", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      body: JSON.stringify(accountRefreshRequest),
    }), accountEnv);
    expect(response.status).toBe(401);
  });

  it("rejects account refresh with invalid account authorization", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer bad-token",
        "content-type": "application/json",
      },
      body: JSON.stringify(accountRefreshRequest),
    }), accountEnv);
    expect(response.status).toBe(401);
  });

  it("refreshes an authenticated account with a Core fallback lease when no entitlement exists", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer core-token",
        "content-type": "application/json",
      },
      body: JSON.stringify(accountRefreshRequest),
    }), accountEnv);
    expect(response.status).toBe(200);
    const body = await response.json() as {
      payload: LeasePayload;
      canonical: string;
      signature_hex: string;
      lease_blob: string;
      key_id: string;
      format: string;
    };
    expect(body.payload).toEqual({
      license_id: "core_acct_core",
      user_id: "user_core",
      tier: "Core",
      plugins: [],
      features: [],
      device_hash: "device_account_1",
      issued_at: 1234567890,
      expires_at: 1235172690,
      grace_policy: "restrict",
      revocation_epoch: 0,
    });
    expect(body.canonical).toBe(canonicalizeLease(body.payload));
    expect(body.signature_hex).toHaveLength(128);
    expect(body.lease_blob).toBe(`${body.canonical}\n${body.signature_hex}`);
    expect(body.key_id).toBe("aestra-test-key");
    expect(body.format).toBe("aestra-license-v1");
  });

  it("refreshes an authenticated account with a server-side Supporter entitlement fixture", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer supporter-token",
        "content-type": "application/json",
      },
      body: JSON.stringify(accountRefreshRequest),
    }), accountEnv);
    expect(response.status).toBe(200);
    const body = await response.json() as { payload: LeasePayload; canonical: string; signature_hex: string };
    expect(body.payload.user_id).toBe("user_supporter");
    expect(body.payload.license_id).toBe("lic_supporter");
    expect(body.payload.tier).toBe("Supporter");
    expect(body.payload.plugins).toEqual(["com.aestra.rumble"]);
    expect(body.payload.features).toEqual(["rumble"]);
    expect(body.canonical).toBe(canonicalizeLease(body.payload));
    expect(body.signature_hex).toHaveLength(128);
  });

  it("refreshes an authenticated account with a server-side Founder entitlement fixture", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer founder-token",
        "content-type": "application/json",
      },
      body: JSON.stringify(accountRefreshRequest),
    }), accountEnv);
    expect(response.status).toBe(200);
    const body = await response.json() as { payload: LeasePayload; canonical: string; signature_hex: string };
    expect(body.payload.user_id).toBe("user_founder");
    expect(body.payload.license_id).toBe("lic_founder");
    expect(body.payload.tier).toBe("Founder");
    expect(body.payload.features).toEqual(["rumble", "rumble_headless"]);
    expect(body.canonical).toBe(canonicalizeLease(body.payload));
    expect(body.signature_hex).toHaveLength(128);
  });

  it("does not trust account_id from the account refresh request body", async () => {
    const response = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer core-token",
        "content-type": "application/json",
      },
      body: JSON.stringify({ ...accountRefreshRequest, account_id: "acct_founder" }),
    }), accountEnv);
    expect(response.status).toBe(200);
    const body = await response.json() as { payload: LeasePayload };
    expect(body.payload.user_id).toBe("user_core");
    expect(body.payload.license_id).toBe("core_acct_core");
    expect(body.payload.tier).toBe("Core");
  });

  it("fails loudly for malformed server-side entitlement records", async () => {
    const malformedEnv: Env = {
      ...accountEnv,
      AESTRA_ENTITLEMENT_FIXTURES: JSON.stringify({
        acct_supporter: {
          license_id: "lic_bad",
          tier: "Supporter",
          plugins: ["com.aestra.rumble"],
          features: "rumble",
          revocation_epoch: 0,
        },
      }),
    };
    const response = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      headers: {
        authorization: "Bearer supporter-token",
        "content-type": "application/json",
      },
      body: JSON.stringify(accountRefreshRequest),
    }), malformedEnv);
    expect(response.status).toBe(500);
    await expect(response.json()).resolves.toMatchObject({
      error: {
        code: "entitlement_malformed",
      },
    });
  });
});
