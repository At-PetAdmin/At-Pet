// Distributed with each standalone integration; keep copies identical.
import { readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';

export function credentialPath() {
  if (process.env.AT_PET_CREDENTIALS) return process.env.AT_PET_CREDENTIALS;
  const root = process.platform === 'win32'
    ? (process.env.LOCALAPPDATA || join(homedir(), 'AppData', 'Local'))
    : process.platform === 'darwin' ? join(homedir(), 'Library', 'Application Support')
      : (process.env.XDG_DATA_HOME || join(homedir(), '.local', 'share'));
  return join(root, 'Ni-ShuWu', 'At-Pet', 'api-security', 'clients.json');
}

export function tokenFor(agentApp) {
  const key = String(agentApp).toLowerCase().replace(/[^a-z0-9]/g, '');
  const explicit = process.env[`AT_PET_API_TOKEN_${key.toUpperCase()}`] || process.env.AT_PET_API_TOKEN;
  if (explicit) return explicit;
  const store = JSON.parse(readFileSync(credentialPath(), 'utf8'));
  const client = store.clients?.find((item) => item.id === key && item.agentKey === key && !item.administrator);
  if (store.version !== 1 || !client?.token) throw new Error('At-Pet client credential unavailable');
  return client.token;
}

export function authenticatedFetch(baseUrl, path, init, agentApp) {
  const url = new URL(baseUrl);
  if (url.protocol !== 'http:' || !['127.0.0.1', 'localhost', '[::1]', '::1'].includes(url.hostname) ||
      url.username || url.password || url.pathname !== '/' || url.search || url.hash) {
    throw new Error('At-Pet credentials may only be sent to a loopback HTTP origin');
  }
  const headers = new Headers(init?.headers);
  const request = { ...init, headers, redirect: 'error' };
  let token;
  try {
    token = tokenFor(agentApp);
  } catch {
    headers.delete('Authorization');
    return fetchWithoutCredential(url.origin, path, request);
  }
  headers.set('Authorization', `Bearer ${token}`);
  return fetch(url.origin + path, request);
}

async function fetchWithoutCredential(origin, path, request) {
  const health = await fetch(origin + '/health', { signal: request.signal, redirect: 'error' });
  const status = health.ok ? await health.json() : null;
  if (status?.ok !== true || status.authenticationRequired !== false) {
    throw new Error('At-Pet client credential unavailable');
  }
  return fetch(origin + path, request);
}
