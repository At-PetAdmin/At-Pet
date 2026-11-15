# Local API authentication and event ordering

## Credentials and upgrade

All endpoints except `GET /health` require `Authorization: Bearer <token>`.
Loopback Host checks, browser Origin rejection and the absence of CORS headers
remain in effect. There is no unauthenticated compatibility mode.

At-Pet creates a 256-bit random token for each registered Agent, an `external`
client, and a separate `admin` client on first startup. The credential store is
`%LOCALAPPDATA%\Ni-ShuWu\At-Pet\api-security\clients.json` on Windows. The
directory has a protected DACL granting access to the current user and SYSTEM.
On other platforms the directory is owner-only. A corrupt/unreadable store
disables the API instead of silently replacing credentials or disabling auth.

These tokens distinguish integrations and prevent unauthenticated callers or
other Windows users from using the API. They do not isolate mutually hostile
programs already running as the same OS user: such a program can read that
user's credentials. Do not expose or forward the port to a network.

Bundled Node integrations and the Copilot PowerShell sender discover their own
Agent token. **Keep `client-auth.mjs` beside each distributed Node/TypeScript
entry point.** Upgrade installed integration copies along with the application.
The helpers refuse non-loopback URLs and redirects, and never auto-select the
administrator credential. Explicit overrides are `AT_PET_CREDENTIALS` (a store
path), `AT_PET_API_TOKEN_<AGENTKEY>` (uppercase registry key, no punctuation),
or `AT_PET_API_TOKEN` (one process's explicit token). Missing credentials make
hook bridges fall back to the Agent's own behavior, with no automatic approval.

Claude's native HTTP hooks need an environment variable in the process starting
Claude Code. After starting the updated At-Pet, run this in the shell that will
launch Claude Code; the token is not printed:

```powershell
$storePath = Join-Path $env:LOCALAPPDATA 'Ni-ShuWu/At-Pet/api-security/clients.json'
$store = Get-Content -LiteralPath $storePath -Raw | ConvertFrom-Json
$env:AT_PET_API_TOKEN_CLAUDECODE = ($store.clients | Where-Object id -eq 'claudecode').token
```

The bundled hook configuration includes the Authorization header and the
required `allowedEnvVars` entry, as described in the
[Claude HTTP hook reference](https://code.claude.com/docs/en/hooks#http-hook-fields).
Restart existing Claude processes after setting the variable.

For a custom caller, add an entry to the private store while At-Pet is stopped:
`id` (unique client name), `agentKey` (normalized Agent key), `token` (at least
32 characters), and `administrator: false`. Use a securely generated token.
Multiple clients may bind to the same Agent while retaining separate request
ownership and session state. Never put this file or tokens in a repository.
To revoke/rotate a client, replace/remove its entry and restart At-Pet; explicit
environment overrides must also be updated. Deleting the store while At-Pet is
stopped regenerates all tokens at the next startup and revokes all old clients.

## Authorization and limits

| Operation | Authorization |
| --- | --- |
| `/event`, `/agent-event/<agent>` | Authenticated client bound to that Agent, or admin |
| Create `/permission`, `/question` | Same Agent binding |
| Poll/delete `/permission/<id>`, `/question/<id>` | Creating client, or admin; others receive 404 |
| `/state`, `/events`, `/animation`, `/say`, `/summon` | Admin only |
| `/health` | Public, minimal response with `authenticationRequired: true` |

The server stamps `clientId`; callers cannot spoof it in JSON. Non-admin
credentials cannot read another client's tasks through the global state or SSE
endpoints. Permission decisions still require the local dialog; bearer tokens
do not provide a remote approval endpoint.

Limits: 64 connections, 8 SSE clients, 1 MiB request data, 10 seconds to finish
an HTTP request, 16 outstanding permission/question records per client and 128
globally. Records expire after 10 minutes, cancelling pending dialogs. Slow SSE
consumers are disconnected above 1 MiB of queued output. Missing/invalid auth
returns 401, unauthorized operations 403, and exhausted quotas 429.

## Event contract

An ordered event can be sent as:

```json
{
  "agentApp": "claude-code",
  "sessionId": "conversation-1",
  "runId": "prompt-or-turn-1",
  "sequence": 2,
  "eventId": "unique-source-event-2",
  "event": "tool_start",
  "toolCallId": "call-1"
}
```

Ordering is scoped to authenticated client + Agent + session + run. Sequence
numbers must be nonnegative safe integers and strictly increase within a run;
they require `runId`. Older lifecycle events are discarded. Identified tools
are tracked independently, so a delayed event for A can still update A without
rolling back the run watermark or losing concurrently running B.
Start a new run with `started`, `turn_start`, or
`turnStart: true`. Changing `runId` without a start marker is rejected. Retired
run IDs cannot reopen a session. Once a source supplies run IDs or sequences,
later events cannot bypass ordering by omitting them. Do not generate a fresh
run ID for every individual event.

`eventId` deduplicates retries. `toolCallId` tracks concurrent tools separately:
finishing A leaves B running; an end received before its matching start prevents
that late start from reviving the tool. Tool failures do not terminate unrelated
tools. Send `completed`/`cancelled`/`error` for the overall run result.

Completed/ended states retain ingestion tombstones for 30 minutes, independent
of the five-second visual cleanup. A completed turn rejects late work events;
the next explicit turn start may resume it. A closed session needs a new run or
a strictly newer source timestamp to reopen. Storage is bounded to 4096 session
records, 256 recent event IDs/tools and 64 retired runs per session.

Legacy senders may omit ordering metadata. They retain terminal barriers, but
cannot provide full ordering across multiple turns or independent tools.
`occurredAt` (ISO 8601 UTC) is an optional fallback for timestamp ordering;
once present it must remain present. Timestamps over five minutes in the future
are rejected. Bridge-generated emission timestamps detect transport reordering,
not the original order of hooks that were themselves started late. Strong
cross-turn guarantees require the source's stable run ID and sequence.

Claude native events preserve the documented `prompt_id` as `runId` and
`tool_use_id` as `toolCallId`; deterministic IDs deduplicate matching hooks.
See [common hook fields](https://code.claude.com/docs/en/hooks#common-input-fields).
The long-lived OpenCode sender assigns per-run sequences before starting HTTP
requests. The generic and standalone bridges preserve source run/sequence/tool
IDs when available. Missing source tool IDs retain session-level presentation.

Local permission-dialog controls are a separate channel and never consume or
invent lifecycle sequence numbers. Ingestion checks happen before connectivity
tracking, so discarded late events cannot reconnect an ended session.
