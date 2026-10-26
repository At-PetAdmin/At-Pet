#!/usr/bin/env node
// At-Pet generic agent bridge.
// Reads one hook payload from stdin, maps it through a declarative profile, and posts a
// single normalized event to At-Pet's local HTTP API.
//
// Usage:
//   node bridge.mjs --profile <id> [--hook <HookName>] [--print] [--url <base>] [--strict]
//   node bridge.mjs <id> [HookName]      positional form, kept for existing hook configs
//
// The process exits 0 unless --strict is passed: a failing bridge must never break the
// agent that invoked it.

import { createHash } from 'node:crypto';
import { appendFileSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const PROFILE_DIR = join(dirname(fileURLToPath(import.meta.url)), 'profiles');

const DEFAULT_LIMITS = { sessionId: 256, toolName: 160, message: 180 };
const DEFAULT_TIMING = {
  requestTimeoutMs: 2000,
  permissionPollMs: 500,
  permissionWaitMs: 590000,
  stdinIdleMs: 2000,
  maxStdinBytes: 1048576,
};
const BLOCKED_KEYS = new Set(['__proto__', 'prototype', 'constructor']);

const envTimeout = Number(process.env.AT_PET_TIMEOUT_MS);
if (Number.isFinite(envTimeout) && envTimeout > 0) DEFAULT_TIMING.requestTimeoutMs = envTimeout;

export function normalizeToken(value) {
  if (value === undefined || value === null) return '';
  return String(value).toLowerCase().replace(/[^a-z0-9]/g, '');
}

export function toText(value) {
  if (typeof value === 'string') return value.trim();
  if (typeof value === 'number' && Number.isFinite(value)) return String(value);
  if (typeof value === 'boolean') return String(value);
  if (value && typeof value === 'object') {
    try {
      return JSON.stringify(value);
    } catch {
      return '';
    }
  }
  return '';
}

// Path syntax: a.b.c, a.b[0].c, a[1]
export function getPath(root, path) {
  if (!root || typeof root !== 'object' || typeof path !== 'string' || !path) return undefined;
  let current = root;
  for (const segment of path.split('.')) {
    if (!segment || current === null || typeof current !== 'object') return undefined;
    const match = /^([^\[\]]+)((?:\[\d+\])*)$/.exec(segment);
    if (!match) return undefined;
    const key = match[1];
    if (BLOCKED_KEYS.has(key)) return undefined;
    current = current[key];
    const indexes = match[2].match(/\[\d+\]/g) || [];
    for (const raw of indexes) {
      if (!Array.isArray(current)) return undefined;
      current = current[Number(raw.slice(1, -1))];
    }
  }
  return current;
}

export function pickFirst(root, paths) {
  if (!Array.isArray(paths)) return '';
  for (const path of paths) {
    const text = toText(getPath(root, path));
    if (text) return text;
  }
  return '';
}

export function cut(value, limit) {
  const text = typeof value === 'string' ? value : toText(value);
  return limit > 0 ? text.slice(0, limit) : text;
}

export function matchesWhen(rule, payload) {
  if (!rule) return true;
  if (typeof rule !== 'object' || typeof rule.path !== 'string') return true;
  const value = toText(getPath(payload, rule.path));
  if (rule.exists === true && !value) return false;
  if (rule.exists === false && value) return false;
  if (rule.equals !== undefined && value !== String(rule.equals)) return false;
  if (Array.isArray(rule.in) && !rule.in.map((item) => String(item)).includes(value)) return false;
  if (typeof rule.includes === 'string' && !value.includes(rule.includes)) return false;
  return true;
}

function isPlainObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function deepMerge(base, override) {
  if (!isPlainObject(base)) return override;
  if (!isPlainObject(override)) return override;
  const result = { ...base };
  for (const [key, value] of Object.entries(override)) {
    result[key] = isPlainObject(value) && isPlainObject(base[key]) ? deepMerge(base[key], value) : value;
  }
  return result;
}

export function loadProfile(id, directory = PROFILE_DIR) {
  if (!id) throw new Error('profile id is required');
  const seen = new Set();
  const read = (name) => {
    if (seen.has(name)) throw new Error('circular extends: ' + name);
    seen.add(name);
    const raw = JSON.parse(readFileSync(join(directory, name + '.json'), 'utf8'));
    if (!isPlainObject(raw)) throw new Error('profile is not an object: ' + name);
    if (!raw.extends) return { ...raw, id: raw.id || name };
    const base = read(raw.extends);
    const merged = deepMerge(base, { ...raw, extends: undefined });
    return { ...merged, id: raw.id || name };
  };
  const profile = read(id);
  if (!profile.agentApp) profile.agentApp = profile.id;
  return profile;
}

export function resolveHookToken(profile, payload, hookArgument) {
  if (hookArgument) return normalizeToken(hookArgument);
  const paths = (profile.hook && profile.hook.paths) || ['hook_event_name'];
  for (const path of paths) {
    const token = normalizeToken(toText(getPath(payload, path)));
    if (token) return token;
  }
  return '';
}

export function resolveSession(profile, payload, hookToken) {
  const sub = profile.subagent || {};
  const parent = pickFirst(payload, profile.session || ['session_id', 'sessionId', 'sessionID']);
  let baseId = parent;
  if (!baseId) {
    const seed = pickFirst(payload, profile.cwd || ['cwd']) || process.cwd();
    baseId = (profile.agentApp || profile.id || 'agent') + '-' +
      createHash('sha1').update(String(seed)).digest('hex').slice(0, 12);
  }
  const declaredParent = pickFirst(payload, sub.parentFrom || []);
  const hookMatches = (sub.hooks || []).map((item) => normalizeToken(item)).includes(hookToken);
  const flag = pickFirst(payload, sub.flag || []);
  const isSubagent = Boolean(declaredParent) || hookMatches || flag === 'true';
  const subId = pickFirst(payload, sub.idFrom || []);
  let sessionId = baseId;
  if (hookMatches && subId) sessionId = baseId + ':' + subId;
  return {
    sessionId,
    isSubagent,
    parentSessionId: isSubagent ? (declaredParent || baseId) : '',
  };
}

export function buildEvent(profile, payload, hookToken) {
  const rules = profile.events ? profile.events[hookToken] : undefined;
  if (!rules) return null;
  const list = Array.isArray(rules) ? rules : [rules];
  const rule = list.find((item) => item && item.enabled !== false && matchesWhen(item.when, payload));
  if (!rule || !rule.event) return null;

  const limits = { ...DEFAULT_LIMITS, ...(profile.limits || {}) };
  const session = resolveSession(profile, payload, hookToken);
  if (!session.sessionId) return null;

  const body = {
    sessionId: cut(session.sessionId, limits.sessionId),
    agentApp: profile.agentApp,
    event: rule.event,
    source: profile.id,
  };
  if (session.isSubagent) {
    body.isSubagent = true;
    if (session.parentSessionId) body.parentSessionId = cut(session.parentSessionId, limits.sessionId);
  }
  const model = pickFirst(payload, profile.model || ['model']);
  if (model) body.model = cut(model, 120);
  const toolName = pickFirst(payload, profile.toolName || ['tool_name', 'toolName']);
  if (toolName) body.toolName = cut(toolName, limits.toolName);
  const messagePaths = rule.messageFrom
    ? [].concat(profile[rule.messageFrom] || []).concat(rule.messagePaths || [])
    : (rule.messagePaths || profile.message || []);
  const message = pickFirst(payload, messagePaths);
  if (message) body.message = cut(message, limits.message);
  return body;
}

export function buildPermissionResponse(profile, allow) {
  const response = (profile.permission && profile.permission.response) || {};
  const chosen = allow ? response.allow : response.deny;
  // null means the vendor treats silence as approval (Kimi Code blocks explicitly).
  if (!chosen) return null;
  const decision = { ...chosen };
  if (response.style === 'hookSpecificOutput') {
    return {
      hookSpecificOutput: {
        hookEventName: response.eventName || 'PermissionRequest',
        decision,
      },
    };
  }
  if (response.style === 'permissionDecision') {
    return { hookSpecificOutput: decision };
  }
  return decision;
}

async function requestJson(url, init, timeoutMs) {
  return fetch(url, { ...init, signal: AbortSignal.timeout(timeoutMs) });
}

export async function relayPermission(profile, payload, sessionId, options) {
  const permission = profile.permission;
  if (!permission) return null;
  const timing = { ...DEFAULT_TIMING, ...(options && options.timing) };
  const baseUrl = (options && options.baseUrl) || 'http://127.0.0.1:8765';
  const toolName = cut(pickFirst(payload, permission.toolName || ['tool_name', 'toolName']), DEFAULT_LIMITS.toolName);
  if (!sessionId || !toolName) return null;
  const title = pickFirst(payload, permission.titleFrom || []) || permission.title || (profile.agentApp + ' permission: ' + toolName);
  const description = cut(pickFirst(payload, permission.description || []), 2000);

  let requestId = '';
  try {
    const accepted = await requestJson(baseUrl + '/permission', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        sessionId,
        agentApp: profile.agentApp,
        toolName,
        title,
        description,
      }),
    }, timing.requestTimeoutMs);
    if (!accepted.ok) return null;
    requestId = String((await accepted.json()).requestId || '');
    if (!requestId) return null;

    const deadline = Date.now() + timing.permissionWaitMs;
    while (Date.now() < deadline) {
      await new Promise((resolve) => setTimeout(resolve, timing.permissionPollMs));
      const result = await requestJson(baseUrl + '/permission/' + encodeURIComponent(requestId), {}, timing.requestTimeoutMs);
      if (result.status === 404) return null;
      if (!result.ok) continue;
      const status = String((await result.json()).status || '');
      if (status === 'allowed_once') return 'allow';
      if (status === 'denied') return 'deny';
    }
  } catch {
    // At-Pet unreachable: fall back to the agent's own approval prompt.
  }
  if (requestId) {
    await requestJson(baseUrl + '/permission/' + encodeURIComponent(requestId), { method: 'DELETE' }, timing.requestTimeoutMs)
      .catch(() => {});
  }
  return null;
}

export function readStdin(timing) {
  const config = { ...DEFAULT_TIMING, ...(timing || {}) };
  return new Promise((resolve) => {
    let data = '';
    let settled = false;
    const finish = (value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      resolve(value);
    };
    const timer = setTimeout(() => finish(data), config.stdinIdleMs);
    if (timer.unref) timer.unref();
    if (process.stdin.isTTY) {
      finish('');
      return;
    }
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (chunk) => {
      data += chunk;
      if (Buffer.byteLength(data) > config.maxStdinBytes) {
        finish(data.slice(0, config.maxStdinBytes));
        process.stdin.destroy();
      }
    });
    process.stdin.on('end', () => finish(data));
    process.stdin.on('error', () => finish(data));
  });
}

export function parsePayload(text) {
  const trimmed = String(text || '').trim();
  if (!trimmed) return {};
  const value = JSON.parse(trimmed);
  return isPlainObject(value) ? value : {};
}

export function parseArgs(argv) {
  const args = { positional: [] };
  for (let index = 0; index < argv.length; index += 1) {
    const item = argv[index];
    if (item === '--profile' || item === '--hook' || item === '--url') {
      args[item.slice(2)] = argv[index + 1] || '';
      index += 1;
    } else if (item === '--capture') {
      args.capture = argv[index + 1] || '';
      index += 1;
    } else if (item === '--print') {
      args.print = true;
    } else if (item === '--strict') {
      args.strict = true;
    } else if (item.startsWith('--')) {
      continue;
    } else {
      args.positional.push(item);
    }
  }
  if (!args.profile) args.profile = args.positional[0] || '';
  if (!args.hook) args.hook = args.positional[1] || '';
  return args;
}

export async function main(argv = process.argv.slice(2)) {
  const args = parseArgs(argv);
  if (!args.profile) {
    process.stderr.write('agent-bridge: --profile is required\n');
    return 2;
  }
  const profile = loadProfile(args.profile);
  const payload = parsePayload(await readStdin());
  const hookToken = resolveHookToken(profile, payload, args.hook);
  if (args.capture) {
    try {
      appendFileSync(args.capture, JSON.stringify({
        at: new Date().toISOString(),
        profile: args.profile,
        hook: hookToken,
        payload,
      }) + '\n');
    } catch {
      // Capture is diagnostic only; never let it break the run.
    }
  }
  if (!hookToken) return 0;

  const baseUrl = (args.url || process.env.AT_PET_URL || 'http://127.0.0.1:8765').replace(/\/+$/, '');

  const permissionHook = normalizeToken(profile.permission && profile.permission.hook);
  if (permissionHook && hookToken === permissionHook) {
    const session = resolveSession(profile, payload, hookToken);
    if (args.print) {
      process.stdout.write(JSON.stringify({ sessionId: session.sessionId, hookToken }) + '\n');
      return 0;
    }
    const decision = await relayPermission(profile, payload, session.sessionId, { baseUrl });
    if (!decision) return 0;
    const answer = buildPermissionResponse(profile, decision === 'allow');
    if (answer) process.stdout.write(JSON.stringify(answer));
    return 0;
  }

  const body = buildEvent(profile, payload, hookToken);
  if (!body) return 0;
  if (args.print) {
    process.stdout.write(JSON.stringify(body) + '\n');
    return 0;
  }
  await requestJson(baseUrl + '/event', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  }, DEFAULT_TIMING.requestTimeoutMs);
  return 0;
}

const entry = process.argv[1] ? pathToFileURL(process.argv[1]).href : '';
if (entry && import.meta.url === entry) {
  const strict = process.argv.includes('--strict');
  main()
    .then((code) => {
      process.exitCode = strict ? code : 0;
    })
    .catch((error) => {
      if (strict) {
        process.stderr.write('agent-bridge: ' + (error && error.message ? error.message : error) + '\n');
        process.exitCode = 1;
      } else {
        process.exitCode = 0;
      }
    });
}
