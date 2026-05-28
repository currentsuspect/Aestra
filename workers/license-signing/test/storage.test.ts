import nacl from "tweetnacl";
import { describe, expect, it } from "vitest";
import worker from "../src/index";
import { sha256Hex } from "../src/accountStore";
import type { LeasePayload } from "../src/canonicalLease";
import type { Env } from "../src/signing";

function base64Encode(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) {
    binary += String.fromCharCode(byte);
  }
  return btoa(binary);
}

type AccountRow = {
  id: string;
  email: string;
  display_name: string | null;
  status: string;
};

type SessionRow = {
  token_hash: string;
  account_id: string;
  status: string;
  created_at?: number;
  expires_at: number;
  revoked_at: number | null;
  last_seen_at?: number | null;
  label?: string | null;
};

type EntitlementRow = {
  id: string;
  account_id: string;
  tier: string;
  status: string;
  source: string;
  source_ref?: string | null;
  current_period_start: number | null;
  current_period_end: number | null;
  grace_until: number | null;
  created_at?: number;
  updated_at?: number;
  revoked_at: number | null;
  metadata_json?: string | null;
};

type D1State = {
  accounts: AccountRow[];
  sessions: SessionRow[];
  entitlements: EntitlementRow[];
  events: Array<{
    id: string;
    account_id: string;
    entitlement_id: string | null;
    event_type: string;
    source: string;
    source_ref: string | null;
    created_at: number;
    metadata_json: string | null;
  }>;
  challenges: Array<{
    id: string;
    email: string;
    code_hash: string;
    status: string;
    attempts: number;
    created_at: number;
    expires_at: number;
    used_at: number | null;
  }>;
  seenSessionTokenHashes: string[];
};

class FakeD1Statement {
  constructor(private readonly sql: string, private readonly state: D1State, private args: unknown[] = []) {}

  bind(...args: unknown[]): FakeD1Statement {
    return new FakeD1Statement(this.sql, this.state, args);
  }

  async first<T>(): Promise<T | null> {
    if (this.sql.includes("FROM account_sessions")) {
      const tokenHash = this.args[0] as string;
      this.state.seenSessionTokenHashes.push(tokenHash);
      const session = this.state.sessions.find((row) => row.token_hash === tokenHash);
      const account = session ? this.state.accounts.find((row) => row.id === session.account_id) : undefined;
      if (!session || !account) {
        return null;
      }
      return {
        account_id: session.account_id,
        user_id: account.id,
        email: account.email,
        display_name: account.display_name,
        account_status: account.status,
        session_status: session.status,
        expires_at: session.expires_at,
        revoked_at: session.revoked_at,
      } as T;
    }
    if (this.sql.includes("FROM accounts WHERE email")) {
      const email = this.args[0] as string;
      const account = this.state.accounts.find((row) => row.email === email);
      if (!account) {
        return null;
      }
      return {
        id: account.id,
        email: account.email,
        display_name: account.display_name,
        status: account.status,
      } as T;
    }
    if (this.sql.includes("FROM account_login_challenges")) {
      const id = this.args[0] as string;
      const email = this.args[1] as string;
      return (this.state.challenges.find((row) => row.id === id && row.email === email) ?? null) as T | null;
    }
    return null;
  }

  async all<T>(): Promise<D1Result<T>> {
    if (this.sql.includes("FROM account_entitlements")) {
      const accountId = this.args[0] as string;
      return {
        success: true,
        results: this.state.entitlements.filter((row) => row.account_id === accountId).map((row) => ({
          id: row.id,
          account_id: row.account_id,
          tier: row.tier,
          status: row.status,
          source: row.source,
          source_ref: row.source_ref ?? null,
          current_period_start: row.current_period_start,
          current_period_end: row.current_period_end,
          grace_until: row.grace_until,
          revoked_at: row.revoked_at,
          metadata_json: row.metadata_json ?? null,
        })) as T[],
        meta: {} as D1Meta,
      };
    }
    return { success: true, results: [], meta: {} as D1Meta };
  }

  async run(): Promise<D1Result> {
    if (this.sql.includes("INSERT INTO account_login_challenges")) {
      this.state.challenges.push({
        id: this.args[0] as string,
        email: this.args[1] as string,
        code_hash: this.args[2] as string,
        status: "pending",
        attempts: 0,
        created_at: this.args[3] as number,
        expires_at: this.args[4] as number,
        used_at: null,
      });
    } else if (this.sql.includes("UPDATE account_login_challenges SET attempts = attempts + 1")) {
      const id = this.args[0] as string;
      const challenge = this.state.challenges.find((row) => row.id === id);
      if (challenge) {
        challenge.attempts += 1;
      }
    } else if (this.sql.includes("UPDATE account_login_challenges") && this.sql.includes("status = 'used'")) {
      const usedAt = this.args[0] as number;
      const id = this.args[1] as string;
      const challenge = this.state.challenges.find((row) => row.id === id);
      if (challenge && challenge.status === "pending") {
        challenge.status = "used";
        challenge.used_at = usedAt;
        challenge.attempts += 1;
      }
    } else if (this.sql.includes("INSERT INTO accounts")) {
      this.state.accounts.push({
        id: this.args[0] as string,
        email: this.args[1] as string,
        display_name: null,
        status: "active",
      });
    } else if (this.sql.includes("INSERT INTO account_sessions")) {
      this.state.sessions.push({
        token_hash: this.args[0] as string,
        account_id: this.args[1] as string,
        status: "active",
        created_at: this.args[2] as number,
        expires_at: this.args[3] as number,
        revoked_at: null,
        last_seen_at: null,
        label: this.args[4] as string,
      });
    } else if (this.sql.includes("UPDATE account_sessions SET last_seen_at")) {
      const lastSeenAt = this.args[0] as number;
      const tokenHash = this.args[1] as string;
      const session = this.state.sessions.find((row) => row.token_hash === tokenHash);
      if (session) {
        session.last_seen_at = lastSeenAt;
      }
    } else if (this.sql.includes("UPDATE account_sessions") && this.sql.includes("status = 'revoked'")) {
      const revokedAt = this.args[0] as number;
      const tokenHash = this.args[1] as string;
      const session = this.state.sessions.find((row) => row.token_hash === tokenHash);
      if (session && session.revoked_at === null) {
        session.status = "revoked";
        session.revoked_at = revokedAt;
      }
    } else if (this.sql.includes("INSERT INTO account_entitlements")) {
      this.state.entitlements.push({
        id: this.args[0] as string,
        account_id: this.args[1] as string,
        tier: this.args[2] as string,
        status: this.args[3] as string,
        source: this.args[4] as string,
        source_ref: this.args[5] as string | null,
        current_period_start: this.args[6] as number | null,
        current_period_end: this.args[7] as number | null,
        grace_until: this.args[8] as number | null,
        created_at: this.args[9] as number,
        updated_at: this.args[10] as number,
        revoked_at: null,
        metadata_json: this.args[11] as string,
      });
    } else if (this.sql.includes("INSERT INTO entitlement_events")) {
      this.state.events.push({
        id: this.args[0] as string,
        account_id: this.args[1] as string,
        entitlement_id: this.args[2] as string | null,
        event_type: this.args[3] as string,
        source: "admin",
        source_ref: this.args[4] as string | null,
        created_at: this.args[5] as number,
        metadata_json: this.args[6] as string | null,
      });
    } else if (this.sql.includes("UPDATE account_entitlements") && this.sql.includes("status = 'revoked'")) {
      const revokedAt = this.args[0] as number;
      const updatedAt = this.args[1] as number;
      const entitlementId = this.args[2] as string;
      const accountId = this.args[3] as string;
      const entitlement = this.state.entitlements.find((row) => row.id === entitlementId && row.account_id === accountId);
      if (entitlement) {
        entitlement.status = "revoked";
        entitlement.revoked_at = revokedAt;
        entitlement.updated_at = updatedAt;
      }
    }
    return { success: true, meta: {} as D1Meta };
  }
}

class FakeD1Database {
  constructor(private readonly state: D1State) {}

  prepare(sql: string): FakeD1Statement {
    return new FakeD1Statement(sql, this.state);
  }
}

async function makeD1Env(state: D1State): Promise<Env> {
  const keyPair = nacl.sign.keyPair();
  return {
    AESTRA_LICENSE_SIGNING_PRIVATE_KEY: base64Encode(keyPair.secretKey),
    AESTRA_ADMIN_API_KEY: "admin",
    AESTRA_SIGNING_KEY_ID: "d1-test-key",
    AESTRA_STORAGE_MODE: "d1",
    AESTRA_LICENSE_DB: new FakeD1Database(state) as unknown as D1Database,
  };
}

async function makeLoginEnv(state: D1State): Promise<Env> {
  return {
    ...(await makeD1Env(state)),
    AESTRA_LOGIN_MAILER_MODE: "fixture",
  };
}

async function stateFor(token: string, overrides?: {
  accountId?: string;
  accountStatus?: string;
  sessionStatus?: string;
  expiresAt?: number;
  revokedAt?: number | null;
  entitlements?: EntitlementRow[];
}): Promise<D1State> {
  const accountId = overrides?.accountId ?? "acct_a";
  return {
    accounts: [{
      id: accountId,
      email: `${accountId}@example.test`,
      display_name: null,
      status: overrides?.accountStatus ?? "active",
    }],
    sessions: [{
      token_hash: await sha256Hex(token),
      account_id: accountId,
      status: overrides?.sessionStatus ?? "active",
      expires_at: overrides?.expiresAt ?? 4102444800,
      revoked_at: overrides?.revokedAt ?? null,
    }],
    entitlements: overrides?.entitlements ?? [],
    events: [],
    challenges: [],
    seenSessionTokenHashes: [],
  };
}

function emptyState(): D1State {
  return {
    accounts: [],
    sessions: [],
    entitlements: [],
    events: [],
    challenges: [],
    seenSessionTokenHashes: [],
  };
}

function entitlement(id: string, tier: string, status: string, accountId = "acct_a", extra?: Partial<EntitlementRow>):
    EntitlementRow {
  return {
    id,
    account_id: accountId,
    tier,
    status,
    source: "fixture_d1",
    current_period_start: 1234560000,
    current_period_end: 1235172690,
    grace_until: null,
    revoked_at: null,
    ...extra,
  };
}

async function refresh(env: Env, token: string, body: Record<string, unknown> = {}) {
  return worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
    method: "POST",
    headers: {
      authorization: `Bearer ${token}`,
      "content-type": "application/json",
    },
    body: JSON.stringify({
      device_hash: "device_d1",
      issued_at: 1234567890,
      ...body,
    }),
  }), env);
}

async function loginStart(env: Env, email: string) {
  return worker.fetch(new Request("https://example.test/v1/account/login/start", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ email }),
  }), env);
}

async function loginVerify(env: Env, email: string, challengeId: string, code: string) {
  return worker.fetch(new Request("https://example.test/v1/account/login/verify", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ email, challenge_id: challengeId, code }),
  }), env);
}

async function me(env: Env, token: string) {
  return worker.fetch(new Request("https://example.test/v1/account/me", {
    method: "GET",
    headers: { authorization: `Bearer ${token}` },
  }), env);
}

async function revoke(env: Env, token: string) {
  return worker.fetch(new Request("https://example.test/v1/account/session/revoke", {
    method: "POST",
    headers: { authorization: `Bearer ${token}` },
  }), env);
}

async function adminGrant(env: Env, body: Record<string, unknown>, adminToken = "admin") {
  return worker.fetch(new Request("https://example.test/v1/admin/entitlements/grant", {
    method: "POST",
    headers: {
      authorization: `Bearer ${adminToken}`,
      "content-type": "application/json",
    },
    body: JSON.stringify(body),
  }), env);
}

async function adminRevoke(env: Env, body: Record<string, unknown>, adminToken = "admin") {
  return worker.fetch(new Request("https://example.test/v1/admin/entitlements/revoke", {
    method: "POST",
    headers: {
      authorization: `Bearer ${adminToken}`,
      "content-type": "application/json",
    },
    body: JSON.stringify(body),
  }), env);
}

async function adminLookup(env: Env, email: string, adminToken = "admin") {
  return worker.fetch(new Request(`https://example.test/v1/admin/accounts/lookup?email=${encodeURIComponent(email)}`, {
    method: "GET",
    headers: {
      authorization: `Bearer ${adminToken}`,
    },
  }), env);
}

describe("D1 account refresh storage boundary", () => {
  it("resolves a valid bearer session by hashed token and returns Core when no entitlement exists", async () => {
    const state = await stateFor("session-a");
    const env = await makeD1Env(state);
    const response = await refresh(env, "session-a");
    expect(response.status).toBe(200);
    const body = await response.json() as { payload: LeasePayload; lease_blob: string };
    expect(body.payload.tier).toBe("Core");
    expect(body.payload.user_id).toBe("acct_a");
    expect(body.payload.license_id).toBe("core_acct_a");
    expect(body.lease_blob).toBeTruthy();
    expect(state.seenSessionTokenHashes[0]).toHaveLength(64);
    expect(state.seenSessionTokenHashes[0]).not.toBe("session-a");
  });

  it("rejects future-issued refresh requests", async () => {
    const state = await stateFor("session-a");
    const env = await makeD1Env(state);
    const response = await refresh(env, "session-a", { issued_at: Math.floor(Date.now() / 1000) + 3600 });
    expect(response.status).toBe(400);
    expect(await response.json()).toMatchObject({ error: { code: "invalid_request" } });
  });

  it("rejects missing, invalid, expired, and revoked sessions", async () => {
    const validState = await stateFor("session-a");
    const validEnv = await makeD1Env(validState);
    const missing = await worker.fetch(new Request("https://example.test/v1/account/entitlements/refresh", {
      method: "POST",
      body: JSON.stringify({ device_hash: "device_d1", issued_at: 1234567890 }),
    }), validEnv);
    expect(missing.status).toBe(401);
    expect((await refresh(validEnv, "wrong-token")).status).toBe(401);
    expect((await refresh(await makeD1Env(await stateFor("expired", { expiresAt: 1 })), "expired")).status).toBe(401);
    expect((await refresh(await makeD1Env(await stateFor("revoked-status", {
      sessionStatus: "revoked",
    })), "revoked-status")).status).toBe(401);
    expect((await refresh(await makeD1Env(await stateFor("revoked", { revokedAt: 123 })), "revoked")).status)
      .toBe(401);
  });

  it("fails explicitly when D1 storage mode is requested without a D1 binding", async () => {
    const keyPair = nacl.sign.keyPair();
    const env: Env = {
      AESTRA_LICENSE_SIGNING_PRIVATE_KEY: base64Encode(keyPair.secretKey),
      AESTRA_ADMIN_API_KEY: "admin",
      AESTRA_STORAGE_MODE: "d1",
    };
    const response = await refresh(env, "session-a");
    expect(response.status).toBe(500);
    expect(await response.json()).toMatchObject({ error: { code: "storage_unconfigured" } });
  });

  it("returns active Supporter and Founder entitlements from server-side rows", async () => {
    const supporter = await refresh(await makeD1Env(await stateFor("supporter", {
      entitlements: [entitlement("lic_supporter", "supporter", "active")],
    })), "supporter");
    expect((await supporter.json() as { payload: LeasePayload }).payload.tier).toBe("Supporter");

    const founder = await refresh(await makeD1Env(await stateFor("founder", {
      entitlements: [entitlement("lic_founder", "founder", "active")],
    })), "founder");
    const founderBody = await founder.json() as { payload: LeasePayload };
    expect(founderBody.payload.tier).toBe("Founder");
    expect(founderBody.payload.features).toEqual(["rumble", "rumble_headless"]);
  });

  it("does not return paid leases for expired, canceled, or revoked paid entitlements", async () => {
    for (const status of ["expired", "canceled", "revoked"]) {
      const response = await refresh(await makeD1Env(await stateFor(`token-${status}`, {
        entitlements: [entitlement(`lic_${status}`, "founder", status)],
      })), `token-${status}`);
      expect(response.status).toBe(200);
      expect((await response.json() as { payload: LeasePayload }).payload.tier).toBe("Core");
    }
  });

  it("returns grace entitlements with unsigned grace metadata", async () => {
    const response = await refresh(await makeD1Env(await stateFor("grace", {
      entitlements: [entitlement("lic_grace", "supporter", "grace_period", "acct_a", { grace_until: 4102444800 })],
    })), "grace");
    expect(response.status).toBe(200);
    const body = await response.json() as { payload: LeasePayload; entitlement_status: string; grace_until: number };
    expect(body.payload.tier).toBe("Supporter");
    expect(body.entitlement_status).toBe("grace_period");
    expect(body.grace_until).toBe(4102444800);
  });

  it("fails loudly for unknown tier and status rows", async () => {
    const badTier = await refresh(await makeD1Env(await stateFor("bad-tier", {
      entitlements: [entitlement("lic_bad_tier", "plus", "active")],
    })), "bad-tier");
    expect(badTier.status).toBe(500);
    expect(await badTier.json()).toMatchObject({ error: { code: "entitlement_unknown_tier" } });

    const badStatus = await refresh(await makeD1Env(await stateFor("bad-status", {
      entitlements: [entitlement("lic_bad_status", "supporter", "paused")],
    })), "bad-status");
    expect(badStatus.status).toBe(500);
    expect(await badStatus.json()).toMatchObject({ error: { code: "entitlement_unknown_status" } });
  });

  it("fails loudly for malformed entitlement rows", async () => {
    const response = await refresh(await makeD1Env(await stateFor("malformed", {
      entitlements: [entitlement("", "supporter", "active")],
    })), "malformed");
    expect(response.status).toBe(500);
    expect(await response.json()).toMatchObject({ error: { code: "entitlement_malformed" } });
  });

  it("chooses the highest active tier deterministically across multiple rows", async () => {
    const response = await refresh(await makeD1Env(await stateFor("multi", {
      entitlements: [
        entitlement("lic_supporter", "supporter", "active"),
        entitlement("lic_founder", "founder", "active"),
      ],
    })), "multi");
    expect((await response.json() as { payload: LeasePayload }).payload.license_id).toBe("lic_founder");
  });

  it("prevents account_id spoofing and cross-account entitlement access", async () => {
    const response = await refresh(await makeD1Env(await stateFor("acct-a-token", {
      entitlements: [entitlement("lic_other", "founder", "active", "acct_b")],
    })), "acct-a-token", { account_id: "acct_b" });
    expect(response.status).toBe(200);
    const body = await response.json() as { payload: LeasePayload };
    expect(body.payload.user_id).toBe("acct_a");
    expect(body.payload.tier).toBe("Core");
  });

  it("does not use fixture paid entitlements when D1 storage mode is active", async () => {
    const env = await makeD1Env(await stateFor("session-a"));
    env.AESTRA_ENTITLEMENT_FIXTURES = JSON.stringify({
      acct_a: { license_id: "lic_fixture_founder", tier: "Founder", plugins: [], features: [], revocation_epoch: 0 },
    });
    const response = await refresh(env, "session-a");
    expect((await response.json() as { payload: LeasePayload }).payload.tier).toBe("Core");
  });
});

describe("D1 admin entitlement grant boundary", () => {
  it("requires admin authorization and rejects normal account sessions", async () => {
    const state = await stateFor("account-token");
    const env = await makeD1Env(state);
    const grantBody = {
      email: "owner@example.test",
      tier: "founder",
      status: "active",
      source: "manual_grant",
      source_ref: "admin:dylan",
      current_period_end: null,
      grace_until: null,
      reason: "founder account",
    };
    expect((await worker.fetch(new Request("https://example.test/v1/admin/entitlements/grant", {
      method: "POST",
      body: JSON.stringify(grantBody),
    }), env)).status).toBe(401);
    expect((await adminGrant(env, grantBody, "bad-admin")).status).toBe(401);
    expect((await adminGrant(env, grantBody, "account-token")).status).toBe(401);
    expect((await adminGrant(env, grantBody)).status).toBe(200);
  });

  it("grants supporter by new email, creates account, persists period end, and writes audit event", async () => {
    const state = emptyState();
    const env = await makeD1Env(state);
    const response = await adminGrant(env, {
      email: " Supporter@Example.TEST ",
      tier: "supporter",
      status: "active",
      source: "manual_grant",
      source_ref: "admin:dylan",
      current_period_end: 4102444800,
      grace_until: null,
      reason: "manual supporter",
      account_id: "ignored",
    });
    expect(response.status).toBe(200);
    const body = await response.json() as {
      account: { id: string; email: string };
      entitlement_id: string;
      effective_entitlement: { tier: string; status: string };
    };
    expect(body.account.email).toBe("supporter@example.test");
    expect(body.effective_entitlement.tier).toBe("Supporter");
    expect(state.accounts).toHaveLength(1);
    expect(state.entitlements[0]).toMatchObject({
      id: body.entitlement_id,
      account_id: body.account.id,
      tier: "supporter",
      status: "active",
      current_period_end: 4102444800,
    });
    expect(state.events[0]).toMatchObject({
      account_id: body.account.id,
      entitlement_id: body.entitlement_id,
      event_type: "manual_grant",
      source: "admin",
      source_ref: "admin:dylan",
    });
  });

  it("reuses existing account, supports lifetime founder grants, and refresh returns signed Founder lease", async () => {
    const state = emptyState();
    state.accounts.push({ id: "acct_founder_manual", email: "founder@example.test", display_name: null, status: "active" });
    state.sessions.push({
      token_hash: await sha256Hex("founder-session"),
      account_id: "acct_founder_manual",
      status: "active",
      expires_at: 4102444800,
      revoked_at: null,
    });
    const env = await makeD1Env(state);
    const response = await adminGrant(env, {
      email: "founder@example.test",
      tier: "founder",
      status: "active",
      source: "manual_grant",
      source_ref: "admin:dylan",
      current_period_end: null,
      grace_until: null,
      reason: "owner/internal dogfood",
    });
    expect(response.status).toBe(200);
    expect(state.accounts).toHaveLength(1);
    expect(state.entitlements[0]?.current_period_end).toBeNull();
    const refreshBody = await (await refresh(env, "founder-session")).json() as { payload: LeasePayload };
    expect(refreshBody.payload.tier).toBe("Founder");
    expect(refreshBody.payload.license_id).toBe(state.entitlements[0]?.id);
  });

  it("rejects malformed grant requests and unknown tier/status", async () => {
    const env = await makeD1Env(emptyState());
    const base = {
      email: "user@example.test",
      tier: "supporter",
      status: "active",
      source: "manual_grant",
    };
    expect((await adminGrant(env, { ...base, email: "bad-email" })).status).toBe(400);
    expect((await adminGrant(env, { ...base, tier: "internal" })).status).toBe(400);
    expect((await adminGrant(env, { ...base, status: "paused" })).status).toBe(400);
  });

  it("revokes an active entitlement, writes audit event, and refresh falls back to Core", async () => {
    const state = emptyState();
    state.accounts.push({ id: "acct_revoke", email: "revoke@example.test", display_name: null, status: "active" });
    state.sessions.push({
      token_hash: await sha256Hex("revoke-session"),
      account_id: "acct_revoke",
      status: "active",
      expires_at: 4102444800,
      revoked_at: null,
    });
    state.entitlements.push(entitlement("ent_revoke", "founder", "active", "acct_revoke"));
    const env = await makeD1Env(state);
    const response = await adminRevoke(env, {
      email: "revoke@example.test",
      entitlement_id: "ent_revoke",
      reason: "manual revoke",
    });
    expect(response.status).toBe(200);
    expect(state.entitlements[0]?.status).toBe("revoked");
    expect(state.events[0]).toMatchObject({
      account_id: "acct_revoke",
      entitlement_id: "ent_revoke",
      event_type: "manual_revoke",
    });
    const refreshBody = await (await refresh(env, "revoke-session")).json() as { payload: LeasePayload };
    expect(refreshBody.payload.tier).toBe("Core");
  });

  it("fails cleanly when revoking an unknown entitlement", async () => {
    const state = emptyState();
    state.accounts.push({ id: "acct_missing_ent", email: "missing-ent@example.test", display_name: null, status: "active" });
    const env = await makeD1Env(state);
    const response = await adminRevoke(env, {
      email: "missing-ent@example.test",
      entitlement_id: "does_not_exist",
      reason: "manual revoke",
    });
    expect(response.status).toBe(404);
  });

  it("keeps multiple entitlement policy deterministic after manual grants", async () => {
    const state = emptyState();
    const env = await makeD1Env(state);
    await adminGrant(env, {
      email: "multi-admin@example.test",
      tier: "supporter",
      status: "active",
      source: "manual_grant",
    });
    await adminGrant(env, {
      email: "multi-admin@example.test",
      tier: "founder",
      status: "active",
      source: "manual_grant",
    });
    const accountId = state.accounts[0]?.id ?? "";
    state.sessions.push({
      token_hash: await sha256Hex("multi-admin-session"),
      account_id: accountId,
      status: "active",
      expires_at: 4102444800,
      revoked_at: null,
    });
    const refreshBody = await (await refresh(env, "multi-admin-session")).json() as { payload: LeasePayload };
    expect(refreshBody.payload.tier).toBe("Founder");
  });

  it("admin lookup returns account and entitlement summary without raw tokens", async () => {
    const state = emptyState();
    state.accounts.push({ id: "acct_lookup", email: "lookup@example.test", display_name: null, status: "active" });
    state.sessions.push({
      token_hash: await sha256Hex("lookup-session"),
      account_id: "acct_lookup",
      status: "active",
      expires_at: 4102444800,
      revoked_at: null,
    });
    state.entitlements.push(entitlement("ent_lookup", "supporter", "active", "acct_lookup"));
    const env = await makeD1Env(state);
    const response = await adminLookup(env, "lookup@example.test");
    expect(response.status).toBe(200);
    const body = await response.json() as {
      account: { id: string; email: string };
      entitlements: Array<{ id: string }>;
      sessions: { raw_tokens_exposed: boolean; token?: string; token_hash?: string };
      effective_entitlement: { tier: string };
    };
    expect(body.account.id).toBe("acct_lookup");
    expect(body.entitlements[0]?.id).toBe("ent_lookup");
    expect(body.effective_entitlement.tier).toBe("Supporter");
    expect(body.sessions.raw_tokens_exposed).toBe(false);
    expect(body.sessions.token).toBeUndefined();
    expect(body.sessions.token_hash).toBeUndefined();
    expect((await adminLookup(env, "missing@example.test")).status).toBe(404);
  });
});

describe("D1 account login and session issuance boundary", () => {
  it("starts login, normalizes email, and exposes fixture code only in fixture mailer mode", async () => {
    const state = emptyState();
    const env = await makeLoginEnv(state);
    const response = await loginStart(env, "  USER@Example.TEST ");
    expect(response.status).toBe(200);
    const body = await response.json() as { ok: boolean; challenge_id: string; expires_at: number; fixture_code: string };
    expect(body.ok).toBe(true);
    expect(body.challenge_id).toMatch(/^lc_/);
    expect(body.fixture_code).toHaveLength(6);
    expect(state.challenges).toHaveLength(1);
    expect(state.challenges[0]?.email).toBe("user@example.test");
    expect(state.challenges[0]?.code_hash).not.toBe(body.fixture_code);
  });

  it("rejects malformed email and fails loudly without production mailer config", async () => {
    const fixtureEnv = await makeLoginEnv(emptyState());
    expect((await loginStart(fixtureEnv, "not-an-email")).status).toBe(400);

    const state = emptyState();
    const prodLikeEnv = await makeD1Env(state);
    const response = await loginStart(prodLikeEnv, "user@example.test");
    expect(response.status).toBe(500);
    expect(await response.json()).toMatchObject({ error: { code: "mailer_unconfigured" } });
  });

  it("does not return fixture login codes in configured mailer mode", async () => {
    const state = emptyState();
    const env = await makeD1Env(state);
    env.AESTRA_LOGIN_MAILER_MODE = "configured";
    const response = await loginStart(env, "configured@example.test");
    expect(response.status).toBe(500);
    const body = await response.json() as { ok: boolean; error?: { code: string } };
    expect(body.error?.code).toBe("resend_key_missing");
  });

  it("does not reveal whether an account exists when starting login", async () => {
    const state = emptyState();
    state.accounts.push({ id: "acct_existing", email: "existing@example.test", display_name: null, status: "active" });
    const env = await makeLoginEnv(state);
    const existing = await loginStart(env, "existing@example.test");
    const missing = await loginStart(env, "missing@example.test");
    expect(existing.status).toBe(200);
    expect(missing.status).toBe(200);
    const existingBody = await existing.json() as { ok: boolean; fixture_code: string };
    const missingBody = await missing.json() as { ok: boolean; fixture_code: string };
    expect(existingBody.ok).toBe(true);
    expect(missingBody.ok).toBe(true);
    expect(existingBody.fixture_code).toHaveLength(6);
    expect(missingBody.fixture_code).toHaveLength(6);
  });

  it("verifies a challenge, creates a Core account, returns a raw session once, and stores only token hash", async () => {
    const state = emptyState();
    const env = await makeLoginEnv(state);
    const startBody = await (await loginStart(env, "new@example.test")).json() as {
      challenge_id: string;
      fixture_code: string;
    };
    const response = await loginVerify(env, "NEW@example.test", startBody.challenge_id, startBody.fixture_code);
    expect(response.status).toBe(200);
    const body = await response.json() as {
      account: { id: string; email: string; status: string };
      session: { token: string; expires_at: number };
    };
    expect(body.account.email).toBe("new@example.test");
    expect(body.account.status).toBe("active");
    expect(body.session.token).toMatch(/^as_/);
    expect(state.accounts).toHaveLength(1);
    expect(state.sessions).toHaveLength(1);
    expect(state.sessions[0]?.token_hash).toHaveLength(64);
    expect(state.sessions[0]?.token_hash).not.toBe(body.session.token);
    expect(state.challenges[0]?.status).toBe("used");

    const refreshResponse = await refresh(env, body.session.token);
    expect(refreshResponse.status).toBe(200);
    expect((await refreshResponse.json() as { payload: LeasePayload }).payload.tier).toBe("Core");
  });

  it("resolves an existing account and preserves existing paid entitlement records", async () => {
    const state = emptyState();
    state.accounts.push({ id: "acct_existing", email: "paid@example.test", display_name: null, status: "active" });
    state.entitlements.push(entitlement("lic_supporter", "supporter", "active", "acct_existing"));
    const env = await makeLoginEnv(state);
    const startBody = await (await loginStart(env, "paid@example.test")).json() as {
      challenge_id: string;
      fixture_code: string;
    };
    const verifyBody = await (await loginVerify(env, "paid@example.test", startBody.challenge_id,
      startBody.fixture_code)).json() as { session: { token: string } };
    expect(state.accounts).toHaveLength(1);
    const refreshBody = await (await refresh(env, verifyBody.session.token)).json() as { payload: LeasePayload };
    expect(refreshBody.payload.tier).toBe("Supporter");
    expect(refreshBody.payload.license_id).toBe("lic_supporter");
  });

  it("rejects reused, expired, wrong-code, max-attempt, and malformed challenge verification", async () => {
    const state = emptyState();
    const env = await makeLoginEnv(state);
    const startBody = await (await loginStart(env, "user@example.test")).json() as {
      challenge_id: string;
      fixture_code: string;
    };
    expect((await loginVerify(env, "user@example.test", startBody.challenge_id, "000000")).status).toBe(401);
    expect(state.challenges[0]?.attempts).toBe(1);
    expect((await loginVerify(env, "user@example.test", startBody.challenge_id, startBody.fixture_code)).status)
      .toBe(200);
    expect((await loginVerify(env, "user@example.test", startBody.challenge_id, startBody.fixture_code)).status)
      .toBe(401);

    const expiredBody = await (await loginStart(env, "expired@example.test")).json() as {
      challenge_id: string;
      fixture_code: string;
    };
    const expired = state.challenges.find((row) => row.id === expiredBody.challenge_id);
    if (expired) {
      expired.expires_at = 1;
    }
    expect((await loginVerify(env, "expired@example.test", expiredBody.challenge_id, expiredBody.fixture_code)).status)
      .toBe(401);

    const attemptsBody = await (await loginStart(env, "attempts@example.test")).json() as {
      challenge_id: string;
    };
    for (let i = 0; i < 5; i += 1) {
      expect((await loginVerify(env, "attempts@example.test", attemptsBody.challenge_id, "111111")).status).toBe(401);
    }
    expect((await loginVerify(env, "attempts@example.test", attemptsBody.challenge_id, "111111")).status).toBe(429);

    const malformed = await worker.fetch(new Request("https://example.test/v1/account/login/verify", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ email: "user@example.test", challenge_id: "", code: "" }),
    }), env);
    expect(malformed.status).toBe(400);
  });

  it("issued sessions work with account me and revoke prevents future refresh", async () => {
    const state = emptyState();
    const env = await makeLoginEnv(state);
    const startBody = await (await loginStart(env, "me@example.test")).json() as {
      challenge_id: string;
      fixture_code: string;
    };
    const verifyBody = await (await loginVerify(env, "me@example.test", startBody.challenge_id,
      startBody.fixture_code)).json() as { session: { token: string } };
    const token = verifyBody.session.token;

    const meResponse = await me(env, token);
    expect(meResponse.status).toBe(200);
    expect(await meResponse.json()).toMatchObject({
      account: { email: "me@example.test", status: "active" },
      entitlement: { tier: "Core" },
    });

    expect((await revoke(env, token)).status).toBe(200);
    expect((await refresh(env, token)).status).toBe(401);
    expect((await me(env, token)).status).toBe(401);
  });

  it("login-created account can receive Founder only from pre-existing entitlement rows", async () => {
    const state = emptyState();
    state.accounts.push({ id: "acct_founder_login", email: "founder@example.test", display_name: null, status: "active" });
    state.entitlements.push(entitlement("lic_founder_login", "founder", "active", "acct_founder_login"));
    const env = await makeLoginEnv(state);
    const startBody = await (await loginStart(env, "founder@example.test")).json() as {
      challenge_id: string;
      fixture_code: string;
    };
    const verifyBody = await (await loginVerify(env, "founder@example.test", startBody.challenge_id,
      startBody.fixture_code)).json() as { session: { token: string } };
    const refreshBody = await (await refresh(env, verifyBody.session.token)).json() as { payload: LeasePayload };
    expect(refreshBody.payload.tier).toBe("Founder");
    expect(state.entitlements).toHaveLength(1);
  });
});
