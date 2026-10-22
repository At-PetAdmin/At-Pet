# Agent adapter architecture

`agentmonitor.h` defines the shared `AgentMonitorStrategy` contract. Each native integration implements that contract in its own directory:

- `claude-code/` parses Claude Code HTTP hooks and handles native permission responses.
- `opencode-v2/` normalizes events from the OpenCode V2 plugin.

`agentmonitor.cpp` only selects the strategy for a registered monitor type. The HTTP API owns transport and routing, while `agentregistry.h` declares tool identities, aliases, and monitor types. Integrations that already send At-Pet's normalized `/event` payload do not need a native adapter; for example, the Copilot CLI hook sender lives under `integrations/copilot-cli/`.

To add a native adapter, implement `AgentMonitorStrategy` in a new tool-specific directory, add its type to `agentregistry.h`, register the factory in `agentmonitor.cpp`, and list its source files in `CMakeLists.txt`.
