import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { readFileSync, unlinkSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  buildEvent,
  buildPermissionResponse,
  getPath,
  loadProfile,
  normalizeToken,
  parseArgs,
  pickFirst,
  resolveHookToken,
  toText,
} from '../bridge.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const BRIDGE = join(HERE, '..', 'bridge.mjs');

function runBridge(args, payload, env = {}) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [BRIDGE, ...args], {
      env: { ...process.env, AT_PET_TIMEOUT_MS: '8000', ...env },
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    let stdout = '';
    let stderr = '';
    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');
    child.stdout.on('data', (chunk) => { stdout += chunk; });
    child.stderr.on('data', (chunk) => { stderr += chunk; });
    child.on('close', (code) => resolve({ code, stdout: stdout.trim(), stderr: stderr.trim() }));
    child.stdin.end(payload === undefined ? '' : JSON.stringify(payload));
  });
}

function startFakeAtPet(handler) {
  return new Promise((resolve) => {
    const server = createServer((req, res) => {
      let raw = '';
      req.setEncoding('utf8');
      req.on('data', (chunk) => { raw += chunk; });
      req.on('end', () => {
        const body = raw ? JSON.parse(raw) : {};
        const reply = handler(req.method, req.url, body);
        res.writeHead(reply.status || 200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(reply.body || {}));
      });
    });
    server.listen(0, '127.0.0.1', () => resolve({ server, url: 'http://127.0.0.1:' + server.address().port }));
  });
}

test('normalizeToken strips punctuation and casing', () => {
  assert.equal(normalizeToken('PreToolUse'), 'pretooluse');
  assert.equal(normalizeToken('tool.execute.after'), 'toolexecuteafter');
  assert.equal(normalizeToken(undefined), '');
});

test('toText keeps strings, numbers and compact objects', () => {
  assert.equal(toText('  hi '), 'hi');
  assert.equal(toText(7), '7');
  assert.equal(toText(true), 'true');
  assert.equal(toText({ a: 1 }), '{"a":1}');
  assert.equal(toText(null), '');
});

test('getPath walks objects and arrays and blocks prototype keys', () => {
  const payload = { a: { b: [{ c: 'x' }] }, tool_input: { command: 'ls' } };
  assert.equal(getPath(payload, 'a.b[0].c'), 'x');
  assert.equal(getPath(payload, 'tool_input.command'), 'ls');
  assert.equal(getPath(payload, 'a.b[9].c'), undefined);
  assert.equal(getPath(payload, '__proto__.x'), undefined);
  assert.equal(getPath(payload, 'constructor.x'), undefined);
});

test('pickFirst returns the first non-empty candidate', () => {
  const payload = { sessionId: '', other: { id: 'deep' } };
  assert.equal(pickFirst(payload, ['sessionId', 'other.id']), 'deep');
  assert.equal(pickFirst(payload, ['missing']), '');
});

test('loadProfile resolves extends and keeps the child id', () => {
  const profile = loadProfile('openai-codex');
  assert.equal(profile.id, 'openai-codex');
  assert.equal(profile.agentApp, 'openai-codex');
  assert.ok(profile.events.pretooluse);
  assert.equal(profile.limits.toolName, 160);
});

test('claude-code PreToolUse maps to tool_start with the tool name as message', () => {
  const profile = loadProfile('claude-code');
  const payload = { session_id: 'abc', hook_event_name: 'PreToolUse', tool_name: 'Read' };
  const token = resolveHookToken(profile, payload, '');
  assert.deepEqual(buildEvent(profile, payload, token), {
    sessionId: 'abc',
    agentApp: 'claude-code',
    event: 'tool_start',
    source: 'claude-code',
    toolName: 'Read',
    message: 'Read',
  });
});

test('subagent hooks get a derived session id and a parent', () => {
  const profile = loadProfile('claude-code');
  const payload = { session_id: 'abc', hook_event_name: 'SubagentStart', agent_id: 'def' };
  const event = buildEvent(profile, payload, resolveHookToken(profile, payload, ''));
  assert.equal(event.sessionId, 'abc:def');
  assert.equal(event.parentSessionId, 'abc');
  assert.equal(event.isSubagent, true);
});

test('opencode parent sessions are flagged as subagents', () => {
  const profile = loadProfile('opencode-v2');
  const payload = { sourceEvent: 'tool.execute.before', sessionID: 'child', parentID: 'root', toolName: 'bash' };
  const event = buildEvent(profile, payload, resolveHookToken(profile, payload, ''));
  assert.equal(event.sessionId, 'child');
  assert.equal(event.parentSessionId, 'root');
  assert.equal(event.isSubagent, true);
});

test('conditional rules pick the first match', () => {
  const profile = loadProfile('opencode-v2');
  const busy = { sourceEvent: 'session.status', sessionID: 's', statusType: 'busy' };
  const idle = { sourceEvent: 'session.status', sessionID: 's', statusType: 'idle' };
  assert.equal(buildEvent(profile, busy, 'sessionstatus').event, 'thinking');
  assert.equal(buildEvent(profile, idle, 'sessionstatus').event, 'idle');
});

test('unknown hooks and unmatched conditions produce no event', () => {
  const profile = loadProfile('openai-codex');
  assert.equal(buildEvent(profile, { session_id: 's' }, 'somethingelse'), null);
  const idlePrompt = { session_id: 's', hook_event_name: 'Notification', notification_type: 'idle_prompt' };
  assert.equal(buildEvent(profile, idlePrompt, 'notification'), null);
});

test('permission responses follow the hook contract', () => {
  const profile = loadProfile('claude-code');
  assert.deepEqual(buildPermissionResponse(profile, true), {
    hookSpecificOutput: { hookEventName: 'PermissionRequest', decision: { behavior: 'allow' } },
  });
  assert.deepEqual(buildPermissionResponse(profile, false), {
    hookSpecificOutput: {
      hookEventName: 'PermissionRequest',
      decision: { behavior: 'deny', message: 'Denied in At-Pet.' },
    },
  });
});

test('parseArgs accepts flags and the positional form', () => {
  assert.deepEqual(parseArgs(['--profile', 'x', '--hook', 'Stop', '--print']), {
    positional: [], profile: 'x', hook: 'Stop', print: true,
  });
  assert.equal(parseArgs(['openai-codex', 'PreToolUse']).profile, 'openai-codex');
  assert.equal(parseArgs(['openai-codex', 'PreToolUse']).hook, 'PreToolUse');
});

test('CLI prints the normalized event with --print', async () => {
  const result = await runBridge(['--profile', 'claude-code', '--hook', 'Stop', '--print'], {
    session_id: 's1', hook_event_name: 'Stop',
  });
  assert.equal(result.code, 0);
  assert.deepEqual(JSON.parse(result.stdout), { sessionId: 's1', agentApp: 'claude-code', event: 'completed', source: 'claude-code' });
});

test('CLI stays silent and exits 0 for unmapped hooks', async () => {
  const result = await runBridge(['--profile', 'claude-code'], { session_id: 's1', hook_event_name: 'Mystery' });
  assert.equal(result.code, 0);
  assert.equal(result.stdout, '');
});

test('CLI exits 0 when At-Pet is unreachable', async () => {
  const result = await runBridge(
    ['--profile', 'claude-code'],
    { session_id: 's1', hook_event_name: 'Stop' },
    { AT_PET_URL: 'http://127.0.0.1:9' },
  );
  assert.equal(result.code, 0);
});

test('CLI posts the event body to /event', async () => {
  const seen = [];
  const fake = await startFakeAtPet((method, url, body) => {
    seen.push({ method, url, body });
    return { status: 200, body: { ok: true } };
  });
  try {
    const result = await runBridge(
      ['--profile', 'openai-codex'],
      { session_id: 's1', hook_event_name: 'PreToolUse', tool_name: 'shell', model: 'gpt-x' },
      { AT_PET_URL: fake.url },
    );
    assert.equal(result.code, 0);
    assert.equal(seen.length, 1);
    assert.equal(seen[0].method, 'POST');
    assert.equal(seen[0].url, '/event');
    assert.deepEqual(seen[0].body, {
      sessionId: 's1',
      agentApp: 'openai-codex',
      event: 'tool_start',
      source: 'openai-codex',
      model: 'gpt-x',
      toolName: 'shell',
      message: 'shell',
    });
  } finally {
    fake.server.close();
  }
});

test('permission hook relays an allow decision', async () => {
  const fake = await startFakeAtPet((method, url) => {
    if (method === 'POST' && url === '/permission') return { status: 200, body: { requestId: 'r1' } };
    if (method === 'GET' && url === '/permission/r1') return { status: 200, body: { status: 'allowed_once' } };
    return { status: 404, body: {} };
  });
  try {
    const result = await runBridge(
      ['--profile', 'claude-code'],
      { session_id: 's1', hook_event_name: 'PermissionRequest', tool_name: 'Bash', tool_input: { command: 'rm -rf x' } },
      { AT_PET_URL: fake.url },
    );
    assert.equal(result.code, 0);
    assert.deepEqual(JSON.parse(result.stdout), {
      hookSpecificOutput: { hookEventName: 'PermissionRequest', decision: { behavior: 'allow' } },
    });
  } finally {
    fake.server.close();
  }
});

test('permission hook relays a deny decision', async () => {
  const fake = await startFakeAtPet((method, url) => {
    if (method === 'POST' && url === '/permission') return { status: 200, body: { requestId: 'r2' } };
    if (method === 'GET' && url === '/permission/r2') return { status: 200, body: { status: 'denied' } };
    return { status: 404, body: {} };
  });
  try {
    const result = await runBridge(
      ['--profile', 'claude-code'],
      { session_id: 's1', hook_event_name: 'PermissionRequest', tool_name: 'Bash' },
      { AT_PET_URL: fake.url },
    );
    const output = JSON.parse(result.stdout);
    assert.equal(output.hookSpecificOutput.decision.behavior, 'deny');
  } finally {
    fake.server.close();
  }
});

test('the new domestic profiles resolve to distinct agent apps', () => {
  for (const [id, app] of [['trae', 'trae'], ['qoder', 'qoder'], ['qoder-cn', 'qoder-cn'], ['qwen-code', 'qwen-code']]) {
    const profile = loadProfile(id);
    assert.equal(profile.agentApp, app);
    assert.ok(profile.events.pretooluse);
  }
});

test('qoder and qwen-code relay permissions while trae stays observational', () => {
  const qoder = loadProfile('qoder');
  assert.equal(qoder.permission.hook, 'permissionrequest');
  assert.equal(buildPermissionResponse(qoder, true).hookSpecificOutput.decision.behavior, 'allow');
  assert.equal(loadProfile('qwen-code').permission.hook, 'permissionrequest');
  assert.equal(loadProfile('trae').permission, null);
  assert.equal(buildPermissionResponse(loadProfile('trae'), true), null);
});

test('qwen-code maps tool and permission hooks', () => {
  const profile = loadProfile('qwen-code');
  assert.equal(buildEvent(profile, { hook_event_name: 'PreToolUse', session_id: 'q1', tool_name: 'Bash' }, 'pretooluse').event, 'tool_start');
  assert.equal(buildEvent(profile, { hook_event_name: 'PermissionRequest', session_id: 'q1', tool_name: 'Bash' }, 'permissionrequest').event, 'permission');
});

test('kimi-code maps the documented hook set', () => {
  const profile = loadProfile('kimi-code');
  const use = { hook_event_name: 'PreToolUse', session_id: 'k1', tool_name: 'Shell' };
  assert.deepEqual(buildEvent(profile, use, resolveHookToken(profile, use, '')), {
    sessionId: 'k1', agentApp: 'kimi-code', event: 'tool_start', toolName: 'Shell', message: 'Shell',
    source: 'kimi-code',
  });
  assert.deepEqual(buildEvent(profile, { hook_event_name: 'Stop', session_id: 'k1' }, 'stop'), {
    sessionId: 'k1', agentApp: 'kimi-code', event: 'completed',
    source: 'kimi-code',
  });
  assert.equal(buildEvent(profile, { hook_event_name: 'SessionEnd', session_id: 'k1' }, 'sessionend').event, 'session_end');
});

test('kimi-code stays silent instead of faking a permission prompt', () => {
  const profile = loadProfile('kimi-code');
  assert.equal(profile.permission, undefined);
  assert.equal(buildPermissionResponse(profile, true), null);
  assert.equal(buildPermissionResponse(profile, false), null);
});

test('permissionDecision style renders hookSpecificOutput', () => {
  const profile = {
    permission: {
      response: {
        style: 'permissionDecision',
        allow: null,
        deny: { permissionDecision: 'deny', permissionDecisionReason: 'Denied in At-Pet.' },
      },
    },
  };
  assert.equal(buildPermissionResponse(profile, true), null);
  assert.deepEqual(buildPermissionResponse(profile, false), {
    hookSpecificOutput: { permissionDecision: 'deny', permissionDecisionReason: 'Denied in At-Pet.' },
  });
});

test('--capture appends the raw payload for calibration', async () => {
  const target = join(HERE, 'capture.test.jsonl');
  try {
    const result = await runBridge(['--profile', 'kimi-code', '--capture', target, '--print'], {
      hook_event_name: 'Stop', session_id: 'k1',
    });
    assert.equal(result.code, 0);
    const record = JSON.parse(readFileSync(target, 'utf8').trim().split('\n')[0]);
    assert.equal(record.profile, 'kimi-code');
    assert.equal(record.hook, 'stop');
    assert.equal(record.payload.session_id, 'k1');
  } finally {
    try { unlinkSync(target); } catch { /* the file may not exist when the run failed */ }
  }
});

test('permission hook prints nothing when At-Pet is unreachable', async () => {
  const result = await runBridge(
    ['--profile', 'claude-code'],
    { session_id: 's1', hook_event_name: 'PermissionRequest', tool_name: 'Bash' },
    { AT_PET_URL: 'http://127.0.0.1:9' },
  );
  assert.equal(result.code, 0);
  assert.equal(result.stdout, '');
});
