// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import test from 'node:test';
import worker, { parseRoute } from '../src/index.mjs';

class FakeR2 {
  constructor() { this.objects = new Map(); }
  async put(key, body) {
    const bytes = typeof body === 'string' ? Buffer.from(body) : Buffer.from(await new Response(body).arrayBuffer());
    this.objects.set(key, bytes);
  }
  async head(key) { const b = this.objects.get(key); return b ? { size: b.length, httpEtag: '"x"' } : null; }
  async get(key, opts) {
    const b = this.objects.get(key);
    if (!b) return null;
    const r = opts?.range?.get?.('range')?.match(/bytes=(\d+)-(\d*)/);
    if (r) {
      const offset = Number(r[1]);
      const end = r[2] ? Number(r[2]) : b.length - 1;
      return { size: b.length, httpEtag: '"x"', body: b.subarray(offset, end + 1), range: { offset, length: end - offset + 1 } };
    }
    return { size: b.length, httpEtag: '"x"', body: b };
  }
}
class FakeKV {
  constructor() { this.map = new Map(); }
  async get(key) { return this.map.get(key) ?? null; }
}

const FOUNDER = 'aef_0123456789abcdef0123456789abcdef';
const hash = (t) => createHash('sha256').update(t).digest('hex');

function setup() {
  const env = { RELEASES: new FakeR2(), FOUNDERS: new FakeKV(), UPLOAD_KEY: 'upload-secret-upload-secret' };
  env.FOUNDERS.map.set(hash(FOUNDER), JSON.stringify({ name: 'test' }));
  return env;
}
const req = (method, path, headers = {}, body) =>
  new Request(`https://update.aestra.studio${path}`, { method, headers: body ? { 'content-length': String(Buffer.byteLength(body)), ...headers } : headers, body });
const up = { authorization: 'Bearer upload-secret-upload-secret' };

function feed(channel, platform, name, size) {
  const payload = JSON.stringify({ schema: 1, channel, platform, version: '0.8.2', url: `https://update.aestra.studio/${channel}/${platform}/${name}`, sha256: 'a'.repeat(64), size });
  return JSON.stringify({ payload, signature: 'b'.repeat(128) });
}

test('routes are exactly channel/platform/file', () => {
  assert.deepEqual(parseRoute('/stable/windows-x64/latest.json')?.key, 'stable/windows-x64/latest.json');
  for (const bad of ['/', '/stable', '/stable/windows-x64', '/beta/windows-x64/a.zip', '/stable/amiga/a.zip', '/stable/windows-x64/../a.zip', '/stable/windows-x64/.hidden', '/stable/windows-x64/a/b', '/stable/windows-x64/' + 'x'.repeat(200)])
    assert.equal(parseRoute(bad), null, bad);
});

test('stable is public, early is founders only', async () => {
  const env = setup();
  await env.RELEASES.put('stable/windows-x64/Aestra-1.zip', 'PKzip');
  await env.RELEASES.put('early/windows-x64/Aestra-2.zip', 'PKearly');
  assert.equal((await worker.fetch(req('GET', '/stable/windows-x64/Aestra-1.zip'), env)).status, 200);
  assert.equal((await worker.fetch(req('GET', '/early/windows-x64/Aestra-2.zip'), env)).status, 401);
  assert.equal((await worker.fetch(req('GET', '/early/windows-x64/Aestra-2.zip', { authorization: 'Bearer wrong-wrong-wrong-wrong' }), env)).status, 401);
  assert.equal((await worker.fetch(req('GET', '/early/windows-x64/Aestra-2.zip', { authorization: `Bearer ${FOUNDER}` }), env)).status, 200);
  assert.equal((await worker.fetch(req('GET', '/early/windows-x64/missing.zip', { authorization: `Bearer ${FOUNDER}` }), env)).status, 404);
});

test('a revoked or removed founder loses access at once', async () => {
  const env = setup();
  await env.RELEASES.put('early/windows-x64/Aestra-2.zip', 'x');
  const h = { authorization: `Bearer ${FOUNDER}` };
  env.FOUNDERS.map.set(hash(FOUNDER), JSON.stringify({ revoked: true }));
  assert.equal((await worker.fetch(req('GET', '/early/windows-x64/Aestra-2.zip', h), env)).status, 401);
  env.FOUNDERS.map.delete(hash(FOUNDER));
  assert.equal((await worker.fetch(req('GET', '/early/windows-x64/Aestra-2.zip', h), env)).status, 401);
});

test('early is never cached, stable is', async () => {
  const env = setup();
  await env.RELEASES.put('stable/windows-x64/latest.json', '{}');
  await env.RELEASES.put('stable/windows-x64/Aestra-1.zip', 'x');
  await env.RELEASES.put('early/windows-x64/latest.json', '{}');
  assert.match((await worker.fetch(req('GET', '/stable/windows-x64/latest.json'), env)).headers.get('cache-control'), /max-age=300/);
  assert.match((await worker.fetch(req('GET', '/stable/windows-x64/Aestra-1.zip'), env)).headers.get('cache-control'), /immutable/);
  assert.match((await worker.fetch(req('GET', '/early/windows-x64/latest.json', { authorization: `Bearer ${FOUNDER}` }), env)).headers.get('cache-control'), /no-store/);
});

test('a download can resume with a Range request', async () => {
  const env = setup();
  await env.RELEASES.put('stable/windows-x64/Aestra-1.zip', '0123456789');
  const r = await worker.fetch(req('GET', '/stable/windows-x64/Aestra-1.zip', { range: 'bytes=4-' }), env);
  assert.equal(r.status, 206);
  assert.equal(r.headers.get('content-range'), 'bytes 4-9/10');
  assert.equal(await r.text(), '456789');
});

test('only CI can upload, and a feed must describe a package that is there', async () => {
  const env = setup();
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/Aestra-1.zip', {}, 'PKzip'), env)).status, 401);
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/Aestra-1.zip', { authorization: 'Bearer nope' }, 'PKzip'), env)).status, 401);
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/Aestra-1.zip', up, 'PKzip'), env)).status, 200);

  const good = feed('stable', 'windows-x64', 'Aestra-1.zip', 5);
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/latest.json', up, feed('stable', 'windows-x64', 'Aestra-1.zip', 999)), env)).status, 409, 'size differs');
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/latest.json', up, feed('stable', 'windows-x64', 'Nope.zip', 5)), env)).status, 409, 'package missing');
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/latest.json', up, feed('early', 'windows-x64', 'Aestra-1.zip', 5)), env)).status, 409, 'wrong channel');
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/latest.json', up, 'not json'), env)).status, 400);
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/latest.json', up, JSON.stringify({ payload: '{}', signature: 'short' })), env)).status, 400);
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/latest.json', up, good), env)).status, 200);
  assert.equal(await (await worker.fetch(req('GET', '/stable/windows-x64/latest.json'), env)).text(), good);
});

test('upload with no key configured is refused, odd methods are refused', async () => {
  const env = setup();
  env.UPLOAD_KEY = '';
  assert.equal((await worker.fetch(req('PUT', '/stable/windows-x64/a.zip', { authorization: 'Bearer ' }, 'x'), env)).status, 401);
  assert.equal((await worker.fetch(req('DELETE', '/stable/windows-x64/a.zip', up), setup())).status, 405);
  assert.equal((await worker.fetch(req('GET', '/robots.txt'), env)).status, 404);
});
