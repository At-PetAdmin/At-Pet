# Kimi Code

Connects the Moonshot Kimi Code CLI to At-Pet through its `config.toml` hooks.

## Why this one differs from the other plugins

This directory has no `bridge.mjs` copy. It drives `integrations/agent-bridge/`, the profile-driven engine, so the only thing specific to Kimi Code is `profiles/kimi-code.json`.

## Install

1. Keep At-Pet running with its local API on `127.0.0.1:8765`.
2. Copy the `[[hooks]]` blocks from `hooks/config.toml.example` into your Kimi Code config. The current location is `~/.kimi-code/config.toml`; older builds used `~/.kimi/config.toml`. If both exist, the newer one wins.
3. Replace `C:/path/to/At-Pet` with the absolute path to this repository.
4. Start a session. At-Pet should switch to thinking on `SessionStart` and settle on `completed` at `Stop`.

## What At-Pet receives

| Kimi Code hook | At-Pet event | Animation |
| --- | --- | --- |
| `SessionStart` | `thinking` | thinking |
| `PreToolUse` | `tool_start` | tool (with the tool name) |
| `PostToolUse` | `tool_end` | thinking |
| `Stop` | `completed` | done |
| `SessionEnd` | `session_end` | idle |
| `UserPromptSubmit` | `thinking` | thinking (if the CLI emits it) |

`PostToolUse` and `UserPromptSubmit` are candidate rules: Kimi Code documents `SessionStart`, `PreToolUse`, `Stop` and `SessionEnd`, so the other two stay inert unless the CLI starts emitting them.

## No permission prompts, on purpose

Kimi Code can block a tool call from `PreToolUse`, but it has no separate permission-request event. Wiring that hook to `/permission` would pop a dialog on every single tool call, including read-only ones. At-Pet therefore observes only. If you want strict confirmation, add a `matcher` to the `PreToolUse` block in your own config.

## Reference

- Hooks documentation: https://www.kimi.com/code/docs/en/kimi-code-cli/customization/hooks.html
- Engine and profile schema: `integrations/agent-bridge/README.md`

