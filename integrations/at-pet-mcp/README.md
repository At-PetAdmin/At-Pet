# At-Pet MCP Server

An optional local stdio MCP server for MCP-compatible Agents. It exposes a connection check and a constrained `report_agent_event` tool that forwards explicitly requested lifecycle events to At-Pet's local `POST /event` endpoint. It does not read Agent internals or monitor lifecycle events automatically; use an Agent's native hook or plugin integration when automatic reporting is required.

## Requirements

- At-Pet running with its local HTTP API enabled (default: `http://127.0.0.1:8765`).
- Node.js 20 or newer.

## Install

From this directory, install the locked dependencies:

```powershell
npm ci
```

The MCP client launches `server.mjs` over stdio. Do not start it as a standalone background service.

## OpenCode configuration

Add an entry to OpenCode's `opencode.json` (or `opencode.jsonc`). Replace the example path with the absolute path to this repository on your machine:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "at-pet": {
      "type": "local",
      "command": [
        "node",
        "C:\\path\\to\\At-Pet\\integrations\\at-pet-mcp\\server.mjs"
      ],
      "environment": {
        "AT_PET_URL": "http://127.0.0.1:8765",
        "AT_PET_AGENT_APP": "opencode"
      },
      "enabled": true
    }
  }
}
```

Restart OpenCode or reload its MCP servers, then call `check_at_pet_connection` to verify the local API is reachable. Other MCP clients can launch the same `server.mjs` with Node and set the equivalent environment variables in their stdio-server configuration.

## Reporting events

Call `report_agent_event` with a stable `sessionId` for the current Agent conversation. The supported events are `started`, `thinking`, `work`, `review`, `tool_start`, `tool_end`, `completed`, `cancelled`, `error`, `idle`, and `session_end`. `agentApp` defaults to `AT_PET_AGENT_APP` (or `external`); `toolName` is optional and must not contain tool arguments or results.

For example, an Agent can report `started` when work begins, `tool_start`/`tool_end` around a tool operation, and `completed` or `cancelled` at the end. These calls only reflect events the Agent chooses to report. They are not guaranteed to run for every turn or tool call, and they do not provide At-Pet's native permission prompt workflow.

The server accepts only HTTP URLs on loopback (`127.0.0.1`, `localhost`, or `::1`) for `AT_PET_URL`, and limits reports to the listed event fields. It does not forward prompts, tool arguments, or tool results.

## Test

```powershell
npm test
```
