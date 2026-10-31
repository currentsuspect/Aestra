// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// update.aestra.studio. Serves the signed update feed and the packages it points at, from an R2 bucket.
//
//   GET|HEAD /<channel>/<platform>/latest.json        the signed feed (the app verifies the signature, not this Worker)
//   GET|HEAD /<channel>/<platform>/<package file>     a package (supports Range, so a download can resume)
//   PUT      /<channel>/<platform>/<file>             CI uploads a build (Authorization: Bearer <UPLOAD_KEY>)
//
// Channels: `stable` is public. `early` is for founders: every read needs `Authorization: Bearer <founder key>`; the key
// is hashed here and looked up in the FOUNDERS KV namespace (only hashes are stored, so reading the namespace does not
// reveal anyone's key). A founder who is removed loses access on the next request; nothing is cached for `early`.

const CHANNELS = new Set(['stable', 'early']);
const PLATFORMS = new Set(['windows-x64', 'linux-x64', 'macos-arm64']);
const FILE = /^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/;
const MAX_UPLOAD_BYTES = 200 * 1024 * 1024;

const json = (body, status = 200) =>
  new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });

export function parseRoute(pathname) {
  const parts = pathname.split('/').filter(Boolean);
  if (parts.length !== 3) return null;
  const [channel, platform, file] = parts;
  if (!CHANNELS.has(channel) || !PLATFORMS.has(platform) || !FILE.test(file) || file.includes('..')) return null;
  return { channel, platform, file, key: `${channel}/${platform}/${file}` };
}

function bearer(req) {
  const h = req.headers.get('authorization') ?? '';
  return h.startsWith('Bearer ') ? h.slice(7).trim() : '';
}

function sameBytes(a, b) {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

async function sha256Hex(text) {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

export async function isFounder(req, env) {
  const token = bearer(req);
  if (token.length < 16 || token.length > 128 || !env.FOUNDERS) return false;
  const record = await env.FOUNDERS.get(await sha256Hex(token));
  if (!record) return false;
  try {
    return JSON.parse(record).revoked !== true;
  } catch {
    return false;
  }
}

function isUploader(req, env) {
  return Boolean(env.UPLOAD_KEY) && sameBytes(bearer(req), env.UPLOAD_KEY);
}

async function serve(req, env, route) {
  const isFeed = route.file === 'latest.json';
  const range = req.headers.get('range') ? req.headers : undefined;
  const object = req.method === 'HEAD' ? await env.RELEASES.head(route.key) : await env.RELEASES.get(route.key, range ? { range } : undefined);
  if (!object) return json({ error: 'none' }, 404);

  const headers = new Headers();
  headers.set('content-type', isFeed ? 'application/json' : 'application/octet-stream');
  headers.set('accept-ranges', 'bytes');
  headers.set('etag', object.httpEtag ?? '');
  if (route.channel === 'early') headers.set('cache-control', 'private, no-store');
  else headers.set('cache-control', isFeed ? 'public, max-age=300' : 'public, max-age=31536000, immutable');
  headers.set('content-length', String(object.size));
  if (req.method === 'HEAD') return new Response(null, { headers });

  let status = 200;
  if (range && object.range && typeof object.range.offset === 'number') {
    const offset = object.range.offset;
    const length = object.range.length ?? object.size - offset;
    status = 206;
    headers.set('content-range', `bytes ${offset}-${offset + length - 1}/${object.size}`);
    headers.set('content-length', String(length));
  }
  return new Response(object.body, { status, headers });
}

export default {
  async fetch(req, env) {
    const route = parseRoute(new URL(req.url).pathname);
    if (!route) return json({ error: 'none' }, 404);

    if (req.method === 'GET' || req.method === 'HEAD') {
      if (route.channel === 'early' && !(await isFounder(req, env))) return json({ error: 'founders only' }, 401);
      return serve(req, env, route);
    }

    if (req.method === 'PUT') {
      if (!isUploader(req, env)) return json({ error: 'auth' }, 401);
      const length = Number(req.headers.get('content-length') ?? 0);
      if (!req.body || !(length > 0)) return json({ error: 'body' }, 400);
      if (length > MAX_UPLOAD_BYTES) return json({ error: 'too large' }, 413);
      if (route.file === 'latest.json') {
        // The feed is small: check it is the envelope the app expects before it can replace the live one.
        const text = await req.text();
        let doc;
        try {
          doc = JSON.parse(text);
        } catch {
          return json({ error: 'json' }, 400);
        }
        if (typeof doc?.payload !== 'string' || !/^[0-9a-f]{128}$/.test(doc?.signature ?? '')) return json({ error: 'shape' }, 400);
        let payload;
        try {
          payload = JSON.parse(doc.payload);
        } catch {
          return json({ error: 'payload' }, 400);
        }
        if (payload.channel !== route.channel || payload.platform !== route.platform) return json({ error: 'wrong place' }, 409);
        // The package it names must already be here with the size it claims, so the feed never points at nothing.
        const name = decodeURIComponent(String(payload.url ?? '').split('/').pop() ?? '');
        const pkg = FILE.test(name) ? await env.RELEASES.head(`${route.channel}/${route.platform}/${name}`) : null;
        if (!pkg || pkg.size !== payload.size) return json({ error: 'package missing or size differs' }, 409);
        await env.RELEASES.put(route.key, text, { httpMetadata: { contentType: 'application/json' } });
        return json({ ok: true });
      }
      await env.RELEASES.put(route.key, req.body, { httpMetadata: { contentType: 'application/octet-stream' } });
      return json({ ok: true });
    }

    return json({ error: 'method' }, 405);
  },
};
