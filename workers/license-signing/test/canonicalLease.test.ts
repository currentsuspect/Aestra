import { describe, expect, it } from "vitest";
import { canonicalizeLease, type LeasePayload } from "../src/canonicalLease";

const baseLease: LeasePayload = {
  license_id: "lic_1",
  user_id: "user_1",
  tier: "Supporter",
  plugins: [],
  features: [],
  device_hash: "device_1",
  issued_at: 0,
  expires_at: 604800,
  grace_policy: "restrict",
  revocation_epoch: 0,
};

describe("canonicalizeLease", () => {
  it("emits the LicenseGate-compatible golden string", () => {
    expect(canonicalizeLease(baseLease)).toBe(
      "{\"license_id\":\"lic_1\",\"user_id\":\"user_1\",\"tier\":\"Supporter\",\"plugins\":[],\"features\":[],\"device_hash\":\"device_1\",\"issued_at\":0,\"expires_at\":604800,\"grace_policy\":\"restrict\",\"revocation_epoch\":0}",
    );
  });

  it("preserves plugin and feature order", () => {
    expect(canonicalizeLease({
      ...baseLease,
      plugins: ["com.aestra.one", "com.aestra.two", "*"],
      features: ["rumble", "cloud_sync", "early_access"],
    })).toContain("\"plugins\":[\"com.aestra.one\",\"com.aestra.two\",\"*\"]");
    expect(canonicalizeLease({
      ...baseLease,
      plugins: ["com.aestra.one"],
      features: ["rumble", "cloud_sync", "early_access"],
    })).toContain("\"features\":[\"rumble\",\"cloud_sync\",\"early_access\"]");
  });

  it("escapes strings like LicenseGate escapeJson", () => {
    expect(canonicalizeLease({
      ...baseLease,
      license_id: "lic_\"quote\"",
      user_id: "user\\slash",
      plugins: ["line\nbreak", "tab\tvalue", "carriage\rreturn"],
    })).toBe(
      "{\"license_id\":\"lic_\\\"quote\\\"\",\"user_id\":\"user\\\\slash\",\"tier\":\"Supporter\",\"plugins\":[\"line\\nbreak\",\"tab\\tvalue\",\"carriage\\rreturn\"],\"features\":[],\"device_hash\":\"device_1\",\"issued_at\":0,\"expires_at\":604800,\"grace_policy\":\"restrict\",\"revocation_epoch\":0}",
    );
  });
});
