# At-Pet MCP Server

这是一个供兼容 MCP 的 Agent 选择使用的本地 stdio MCP Server，提供连接检查和受限的 `report_agent_event` 工具，并将 Agent 主动上报的生命周期事件转发到 At-Pet 本机的 `POST /event`。它不会读取 Agent 内部状态或自动监听生命周期；需要自动上报时，请使用 Agent 原生 hook 或插件集成。

## 要求

- At-Pet 正在运行且本地 HTTP API 已启用（默认地址：`http://127.0.0.1:8765`）。
- Node.js 20 或更新版本。

## 安装

在此目录执行以下命令，安装锁定版本的依赖：

```powershell
npm ci
```

MCP 客户端会通过 stdio 启动 `server.mjs`。不要将其作为独立后台服务启动。

## OpenCode 配置

在 OpenCode 的 `opencode.json`（或 `opencode.jsonc`）中添加以下配置，并将示例路径替换为本机仓库的绝对路径：

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

重启 OpenCode 或重新加载 MCP Server，然后调用 `check_at_pet_connection` 验证本地 API。其他 MCP 客户端也可以使用 Node 启动同一个 `server.mjs`，并在其 stdio Server 配置中设置对应环境变量。

## 上报事件

调用 `report_agent_event` 时，为当前 Agent 对话传入稳定的 `sessionId`。支持的事件为 `started`、`thinking`、`work`、`review`、`tool_start`、`tool_end`、`completed`、`cancelled`、`error`、`idle` 和 `session_end`。`agentApp` 默认使用 `AT_PET_AGENT_APP`（未设置时为 `external`）；`toolName` 可选，且不要包含工具参数或结果。

例如，Agent 可在工作开始时上报 `started`，在工具操作前后上报 `tool_start`/`tool_end`，并在结束时上报 `completed` 或 `cancelled`。这只反映 Agent 主动选择上报的事件；不能保证每轮或每次工具调用都会自动上报，也不提供 At-Pet 原生权限弹窗流程。

`AT_PET_URL` 仅接受回环地址上的 HTTP URL（`127.0.0.1`、`localhost` 或 `::1`），并且上报字段受限。服务不会转发提示词、工具参数或工具结果。

## 测试

```powershell
npm test
```
