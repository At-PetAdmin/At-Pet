# At-Pet Agent Plugins

Agent plugins that connect external agent apps to At-Pet. Each plugin turns the agent's hook payloads into At-Pet events (`/event`) and relays permission prompts (`/permission`) to the local HTTP API at `127.0.0.1:8765`.

Source: [At-Pet_Agent-plugin](https://github.com/Ni-ShuWu/At-Pet_Agent-plugin) (`Agent-plugin/`, MIT).

## Plugins

| Agent | Directory | Integration |
| --- | --- | --- |
| Claude Code | `claude-code/` | HTTP hooks |
| OpenCode | `opencode/` | OpenCode V2 plugin |
| GitHub Copilot | `github-copilot/` | Copilot hooks (`bridge.mjs`) |
| OpenAI Codex | `openai-codex/` | Codex hooks, with permission requests |
| WorkBuddy | `workbuddy/` | CodeBuddy-compatible hooks |
| Command Code | `command-code/` | `settings.json` hooks |
| ZCode | `zcode/` | Plugin hooks |
| DeepSeek Harness | `deepseek-harness/` | `dsh-hooks-codex` bridge |
| PenguinHarness | `penguin-harness/` | Plugin hooks (`user_prompt`, `stop`) |
| Kimi Code | `kimi-code/` | `config.toml` hooks through `agent-bridge` |
| Trae | `agent-bridge` profile only | `hooks.json`; observation only |
| Qoder / Qoder CN | `agent-bridge` profile only | `settings.json`; `PermissionRequest` |
| Qwen Code | `agent-bridge` profile only | `.qwen/settings.json`; `PermissionRequest` |

Every plugin except Claude Code and OpenCode uses its own copy of the zero-dependency `bridge.mjs`, so each directory can be installed on its own. Each directory has a README with install steps.

`../agent-bridge/` holds a profile-driven replacement for those copies: one engine plus a JSON profile per agent, with the field contract for each profile traced back to the adapter or bridge it replaces. The plugins here are unaffected; switching one over is a one-line change in its hook command.

## Requirements

- Windows 10/11.
- At-Pet is running, with the local API on `127.0.0.1:8765` (or set `AT_PET_URL`).
- Node.js for the `bridge.mjs` based plugins.
