# At-Pet Copilot integration (Windows)

This plugin forwards GitHub Copilot lifecycle events to At-Pet's local event API. It sends the Copilot session ID, fixed event names, and tool names for tool lifecycle events; prompts, tool arguments, and responses are never forwarded or logged. Requests are restricted to loopback HTTP.

## Install for Copilot CLI

From the At-Pet repository root, install the local plugin:

```powershell
copilot plugin install .\integrations\copilot-cli
```

Start At-Pet with its local HTTP API enabled (default: `http://127.0.0.1:8765/event`), then start or resume a Copilot CLI session. A submitted prompt maps to `thinking`; tool start, success, and failure map to `tool_start`, `tool_end`, and `error`; a completed agent turn maps to `completed`; and a closed session maps to `session_end`.

## Try it in GitHub Copilot app

The Copilot app supports plugins that supply hooks. Add the At-Pet repository as a custom marketplace in **Customize → Plugins → marketplace settings**, then install **at-pet-copilot**. The plugin uses the same Windows PowerShell hooks as the CLI.

App hook event availability and payloads can differ from the CLI. App compatibility is best-effort: confirm it by submitting a prompt and checking that the pet changes to thinking and then completed. If the app does not expose the expected hook events, this plugin cannot infer its state from the window alone.

## Notes

- Windows and PowerShell only; Linux/macOS are out of scope.
- At-Pet must be running and its local `/event` API must be reachable. The API can be overridden with `AT_PET_EVENT_URL`, but the hook ignores non-loopback URLs.
- Hook failures are intentionally swallowed so an unavailable At-Pet instance does not block Copilot.
