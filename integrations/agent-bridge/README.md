# At-Pet Agent Bridge

A profile-driven bridge that turns any agent hook payload into At-Pet events. One engine, one JSON file per agent.

## Why this exists

Each plugin under `integrations/agent-plugins/` ships its own copy of `bridge.mjs`. Seven of those files are near-identical: same hook table, same permission flow, same stdin handling. Adding an agent meant copying ~5 KB and editing a constant, so a bug fix had to be applied seven times.

This directory replaces that with one engine plus declarative profiles. An agent is data now, not code: the only thing that differs between them is which field holds the session id, which field names the hook, and how a hook maps to an event.

## Requirements

- Node.js 20 or newer (the engine uses `node:test` and `AbortSignal.timeout`).
- At-Pet running with its local HTTP API enabled, by default `http://127.0.0.1:8765`.

## Usage

```powershell
'{"session_id":"s1","hook_event_name":"PreToolUse","tool_name":"Read"}' | node integrations/agent-bridge/bridge.mjs --profile claude-code
```

| Flag | Meaning |
| --- | --- |
| `--profile <id>` | Profile name, matching a file in `profiles/`. |
| `--hook <name>` | Hook name when the payload does not carry one. |
| `--print` | Print the normalized body instead of posting it. |
| `--capture <file>` | Append the raw payload to a JSONL file for calibration. |
| `--url <base>` | Override the At-Pet base URL (also `AT_PET_URL`). |
| `--strict` | Exit non-zero on failure. Without it the process always exits 0. |

The positional form `node bridge.mjs <profile> [HookName]` behaves the same way, so an existing hook command only needs its arguments changed.

## Profiles

| Profile | agentApp | Field contract verified against | Permission relay |
| --- | --- | --- | --- |
| `claude-code` | `claude-code` | `app/agentadapters/claude-code/adapter.cpp` | Yes |
| `opencode-v2` | `opencode` | `app/agentadapters/opencode-v2/adapter.cpp` | No |
| `openai-codex` | `openai-codex` | `integrations/agent-plugins/openai-codex/bridge.mjs`, `hooks/hooks.json` | Yes |
| `github-copilot` | `github-copilot` | `integrations/agent-plugins/github-copilot/bridge.mjs` | Yes |
| `workbuddy` | `workbuddy` | `integrations/agent-plugins/workbuddy/bridge.mjs` | Yes |
| `command-code` | `command-code` | `integrations/agent-plugins/command-code/bridge.mjs` | Yes |
| `zcode` | `zcode` | `integrations/agent-plugins/zcode/bridge.mjs` | Yes |
| `deepseek-harness` | `deepseek-harness` | `integrations/agent-plugins/deepseek-harness/bridge.mjs` | Yes |
| `penguin-harness` | `penguin-harness` | `integrations/agent-plugins/penguin-harness/bridge.mjs` | Yes |
| `kimi-code` | `kimi-code` | Kimi Code CLI hooks documentation (`~/.kimi-code/config.toml`) | No |
| `trae` | `trae` | Trae IDE hooks documentation; observation only, approvals stay in Trae | No |
| `qoder` | `qoder` | Qoder hooks documentation (`PermissionRequest`) | Yes |
| `qoder-cn` | `qoder-cn` | Qoder CN, formerly Tongyi Lingma | Yes |
| `qwen-code` | `qwen-code` | Qwen Code hooks documentation (`.qwen/settings.json`) | Yes |
| `_claude-hook-family` | — | base profile, not run directly | — |

Profiles not listed here are not verified. Do not add a profile for an agent whose payload shape has not been observed: a wrong field path fails silently, which is worse than no support at all.

## Profile schema

Everything below is what `bridge.mjs` actually reads.

| Key | Type | Meaning |
| --- | --- | --- |
| `id` | string | Profile name. |
| `extends` | string | Parent profile; merged recursively, child wins. |
| `agentApp` | string | Sent as `agentApp`; At-Pet normalizes it to a registry key. Defaults to `id`. |
| `description` | string | Free text, not read by the engine. |
| `hook.paths` | string[] | Candidate paths for the hook name. |
| `session` | string[] | Candidate paths for the session id. |
| `model` / `toolName` / `message` / `cwd` | string[] | Candidate paths for those fields. |
| `subagent.flag` | string[] | Paths whose string value `true` marks a subagent. |
| `subagent.hooks` | string[] | Hook names that always mean a subagent. |
| `subagent.idFrom` | string[] | Subagent id appended to the session as `parent:id`. |
| `subagent.parentFrom` | string[] | Explicit parent id; presence flags a subagent. |
| `events` | object | Map of normalized hook name to a rule, or an array of rules. |
| `permission` | object | Presence enables the permission relay for that profile. |
| `limits` | object | `sessionId` / `toolName` / `message` truncation, default 256 / 160 / 180. |

A rule is `{ "event": "tool_start", "when": { ... }, "messageFrom": "toolName", "messagePaths": [ ... ], "enabled": false }`. When an array is given, the first rule whose `when` matches wins; a rule without `when` always matches.

`when` supports `path` plus any of `equals`, `in` (array), `includes` (substring), `exists` (boolean).

Event keys are normalized: lowercase, non-alphanumerics removed. `tool.execute.after` becomes `toolexecuteafter`. A wrong key is silently ignored, so check it with `--print`.

Field paths use dotted segments and array indexes: `tool_input.command`, `tool_input.files[0].path`. Prototype keys are rejected.

Objects and arrays resolve to compact JSON, matching how `app/agentadapters/*/adapter.cpp` renders structured values.

## Adding an agent

1. Capture a real payload. Either the vendor documents a hook payload sample, or the hook can be pointed at a local listener once.
2. Copy `profiles/claude-code.json` to `profiles/<name>.json`, set `id` and `agentApp`, keep `extends` if the payload follows the Claude hook shape.
3. Add the `events` map and verify with `--print` for one payload per hook you care about.
4. Point the vendor hook at `node <abs path>/bridge.mjs --profile <name>`.

When the payload shape is still unknown, run the hook once with `--capture payload.jsonl` and read what actually arrived. That turns guesswork into a two-minute calibration.

Keep the `--print` output with a captured payload in the pull request. It is the cheapest proof the field paths are right.

## Behaviour guarantees

- The process exits 0 even when At-Pet is down, the payload is malformed, or the hook is unmapped. A broken bridge must never block an agent. Use `--strict` in tests to see the failure instead.
- HTTP requests time out after 2 s; set `AT_PET_TIMEOUT_MS` to raise it on slow machines (the test suite uses 8000). Permission relay polls every 500 ms for up to 590 s, then returns nothing so the agent keeps its own prompt.
- Reading stdin gives up after 2 s of idleness or 1 MiB of input.
- Only the normalized fields listed above are sent: session, app, event, model, tool name, a short message, and the subagent markers. Prompts and tool results are not forwarded. Permission requests carry the tool name plus a truncated description, which is what the user needs to decide.
- Every event carries `source`, set to the profile id, so a consumer can tell a hook report from a weaker signal later.
- The default endpoint is loopback. `AT_PET_URL` can point elsewhere, but At-Pet's API is unauthenticated by design and should stay local.

## Tests

```powershell
cd integrations/agent-bridge
npm test
```

26 tests: path resolution, profile inheritance, event mapping, subagent handling, conditional rules, permission allow and deny against a fake local API, the two permission response styles, per-profile permission policy, payload capture, and the guarantee that a missing At-Pet does not fail the process.

## Relationship to the existing plugins

`integrations/agent-plugins/*` keep working unchanged. Moving a vendor over is a one-line change in its hook configuration; nothing in this directory has to be installed into the vendor.

