import { canonicalizeLease } from "./canonicalLease";
import { parseSignRequest, SignRequestError } from "./schema";
import { signCanonicalLease, type Env } from "./signing";

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

function keyId(env: Env): string {
  return env.AESTRA_SIGNING_KEY_ID?.trim() || "dev";
}

function isAuthorized(request: Request, env: Env): boolean {
  const expected = env.AESTRA_ADMIN_API_KEY;
  if (!expected) {
    return false;
  }
  return request.headers.get("authorization") === `Bearer ${expected}`;
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
      if (!isAuthorized(request, env)) {
        return errorResponse(401, "unauthorized", "missing or invalid admin authorization");
      }

      try {
        const payload = parseSignRequest(await parseJson(request));
        const canonical = canonicalizeLease(payload);
        const signatureHex = await signCanonicalLease(canonical, env);
        return jsonResponse({
          payload,
          canonical,
          signature_hex: signatureHex,
          key_id: keyId(env),
          format: "aestra-license-v1",
        });
      } catch (error) {
        if (error instanceof SignRequestError) {
          return errorResponse(400, "invalid_request", error.message, error.issues);
        }
        return errorResponse(500, "signing_failed", "failed to sign entitlement lease");
      }
    }

    return errorResponse(404, "not_found", "route not found");
  },
};

export default worker;
