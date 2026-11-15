import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { authenticatedFetch, tokenFor } from '../client-auth.mjs';

test('client credentials are scoped, reloaded after rotation, and fail closed', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'atpet-auth-'));
  const saved = { token: process.env.AT_PET_API_TOKEN, path: process.env.AT_PET_CREDENTIALS,
    scoped: process.env.AT_PET_API_TOKEN_OPENAICODEX };
  delete process.env.AT_PET_API_TOKEN;
  delete process.env.AT_PET_API_TOKEN_OPENAICODEX;
  process.env.AT_PET_CREDENTIALS = join(directory, 'clients.json');
  const store = (token) => writeFileSync(process.env.AT_PET_CREDENTIALS, JSON.stringify({ version: 1,
    clients: [{ id: 'admin', administrator: true, token: 'administrator-fixture' },
      { id: 'openaicodex', agentKey: 'openaicodex', token }] }));
  const received = [];
  const api = createServer((req, res) => {
    received.push(req.headers.authorization);
    if (req.url === '/redirect') res.writeHead(302, { location: '/target' });
    res.end('{}');
  });
  api.listen(0, '127.0.0.1');
  await once(api, 'listening');
  const base = `http://127.0.0.1:${api.address().port}`;
  try {
    store('first-client-fixture');
    assert.equal(tokenFor('openai-codex'), 'first-client-fixture');
    assert.throws(() => tokenFor('other-client'));
    assert.throws(() => tokenFor('admin')); // Admin credentials are never auto-discovered.
    await authenticatedFetch(base, '/event', {}, 'openai-codex');
    store('rotated-client-fixture');
    await authenticatedFetch(base, '/event', {}, 'openai-codex');
    assert.deepEqual(received, ['Bearer first-client-fixture', 'Bearer rotated-client-fixture']);
    assert.throws(() => authenticatedFetch('http://example.com', '/event', {}, 'openai-codex'), /loopback/);
    await assert.rejects(authenticatedFetch(base, '/redirect', {}, 'openai-codex'));
    assert.equal(received.length, 3); // Redirect target was never contacted.
    writeFileSync(process.env.AT_PET_CREDENTIALS, 'invalid');
    assert.throws(() => tokenFor('openai-codex'));
  } finally {
    api.close();
    // directory is the absolute path returned by mkdtempSync under tmpdir().
    assert.ok(directory.startsWith(join(tmpdir(), 'atpet-auth-')));
    rmSync(directory, { recursive: true, force: true });
    for (const [name, value] of [['AT_PET_API_TOKEN', saved.token], ['AT_PET_CREDENTIALS', saved.path],
      ['AT_PET_API_TOKEN_OPENAICODEX', saved.scoped]]) {
      if (value === undefined) delete process.env[name]; else process.env[name] = value;
    }
  }
});
