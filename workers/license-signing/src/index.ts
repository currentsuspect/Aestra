import {
  handleAccountMe,
  handleAccountRefresh,
  handleLoginStart,
  handleLoginVerify,
  handleSessionRevoke,
  parseAccountJson,
} from "./accountRoutes";
import {
  AdminEntitlementError,
  grantManualEntitlement,
  lookupAdminAccount,
  parseAdminGrantRequest,
  parseAdminRevokeRequest,
  revokeManualEntitlement,
} from "./adminEntitlements";
import {
  buildSignedLeaseResponse,
  keyId,
} from "./refreshResponse";
import { parseSignRequest, SignRequestError } from "./schema";
import type { Env } from "./signing";

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
    },
  });
}

function errorResponse(status: number, code: string, message: string, issues?: string[]): Response {
  return jsonResponse({ error: { code, message, ...(issues ? { issues } : {}) } }, status);
}

function isAuthorized(request: Request, env: Env): boolean {
  const expected = env.AESTRA_ADMIN_API_KEY;
  if (!expected) {
    return false;
  }
  return request.headers.get("authorization") === `Bearer ${expected}`;
}

function requireAdmin(request: Request, env: Env): Response | null {
  return isAuthorized(request, env) ? null : errorResponse(401, "unauthorized", "missing or invalid admin authorization");
}

async function parseJson(request: Request): Promise<unknown> {
  try {
    return await request.json();
  } catch {
    throw new SignRequestError(["request body must be valid JSON"]);
  }
}

const worker = {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/health") {
      if (request.method !== "GET") {
        return errorResponse(405, "method_not_allowed", "GET is required for /health");
      }
      return jsonResponse({
        ok: true,
        service: "aestra-license-signing",
        version: 1,
        key_id: keyId(env),
      });
    }

    if (url.pathname === "/v1/entitlements/sign") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/entitlements/sign");
      }
      const adminError = requireAdmin(request, env);
      if (adminError) {
        return adminError;
      }

      try {
        const nowSeconds = Math.floor(Date.now() / 1000);
        return jsonResponse(await buildSignedLeaseResponse(parseSignRequest(await parseJson(request), nowSeconds), env,
          false, nowSeconds));
      } catch (error) {
        if (error instanceof SignRequestError) {
          return errorResponse(400, "invalid_request", error.message, error.issues);
        }
        return errorResponse(500, "signing_failed", "failed to sign entitlement lease");
      }
    }

    if (url.pathname === "/v1/entitlements/refresh") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/entitlements/refresh");
      }
      const adminError = requireAdmin(request, env);
      if (adminError) {
        return adminError;
      }

      try {
        const nowSeconds = Math.floor(Date.now() / 1000);
        return jsonResponse(await buildSignedLeaseResponse(parseSignRequest(await parseJson(request), nowSeconds), env,
          true, nowSeconds));
      } catch (error) {
        if (error instanceof SignRequestError) {
          return errorResponse(400, "invalid_request", error.message, error.issues);
        }
        return errorResponse(500, "signing_failed", "failed to refresh entitlement lease");
      }
    }

    if (url.pathname === "/v1/account/entitlements/refresh") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/account/entitlements/refresh");
      }

      return handleAccountRefresh(request, env, jsonResponse, errorResponse);
    }

    if (url.pathname === "/v1/account/login/start") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/account/login/start");
      }
      return handleLoginStart(request, env, jsonResponse, errorResponse);
    }

    if (url.pathname === "/v1/account/login/verify") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/account/login/verify");
      }
      return handleLoginVerify(request, env, jsonResponse, errorResponse);
    }

    if (url.pathname === "/v1/account/me") {
      if (request.method !== "GET") {
        return errorResponse(405, "method_not_allowed", "GET is required for /v1/account/me");
      }
      return handleAccountMe(request, env, jsonResponse, errorResponse);
    }

    if (url.pathname === "/v1/account/session/revoke") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/account/session/revoke");
      }
      return handleSessionRevoke(request, env, jsonResponse, errorResponse);
    }

    if (url.pathname === "/v1/admin/entitlements/grant") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/admin/entitlements/grant");
      }
      const adminError = requireAdmin(request, env);
      if (adminError) {
        return adminError;
      }
      try {
        const nowSeconds = Math.floor(Date.now() / 1000);
        const result = await grantManualEntitlement(env, parseAdminGrantRequest(await parseAccountJson(request)),
          nowSeconds);
        return jsonResponse({ ok: true, ...result });
      } catch (error) {
        if (error instanceof AdminEntitlementError) {
          return errorResponse(error.status, error.code, error.message);
        }
        return errorResponse(500, "admin_grant_failed", "failed to grant entitlement");
      }
    }

    if (url.pathname === "/v1/admin/entitlements/revoke") {
      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "POST is required for /v1/admin/entitlements/revoke");
      }
      const adminError = requireAdmin(request, env);
      if (adminError) {
        return adminError;
      }
      try {
        const nowSeconds = Math.floor(Date.now() / 1000);
        const result = await revokeManualEntitlement(env, parseAdminRevokeRequest(await parseAccountJson(request)),
          nowSeconds);
        return jsonResponse({ ok: true, ...result });
      } catch (error) {
        if (error instanceof AdminEntitlementError) {
          return errorResponse(error.status, error.code, error.message);
        }
        return errorResponse(500, "admin_revoke_failed", "failed to revoke entitlement");
      }
    }

    if (url.pathname === "/v1/admin/accounts/lookup") {
      if (request.method !== "GET") {
        return errorResponse(405, "method_not_allowed", "GET is required for /v1/admin/accounts/lookup");
      }
      const adminError = requireAdmin(request, env);
      if (adminError) {
        return adminError;
      }
      try {
        const nowSeconds = Math.floor(Date.now() / 1000);
        const result = await lookupAdminAccount(env, url.searchParams.get("email") ?? "", nowSeconds);
        return jsonResponse({ ok: true, ...result });
      } catch (error) {
        if (error instanceof AdminEntitlementError) {
          return errorResponse(error.status, error.code, error.message);
        }
        return errorResponse(500, "admin_lookup_failed", "failed to look up account");
      }
    }

    return errorResponse(404, "not_found", "route not found");
  },
};

export default worker;
