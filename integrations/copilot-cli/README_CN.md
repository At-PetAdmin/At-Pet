# At-Pet Copilot 集成（Windows）

此插件会将 GitHub Copilot 生命周期事件转发至 At-Pet 本机事件 API。只发送 Copilot 会话 ID、固定事件名，以及工具生命周期事件中的工具名称；不会转发或记录提示词、工具参数或回复内容。请求仅允许发送到本机回环地址。

## 安装到 Copilot CLI

在 At-Pet 仓库根目录运行：

```powershell
copilot plugin install .\integrations\copilot-cli
```

启动 At-Pet 并启用本地 HTTP API（默认地址：`http://127.0.0.1:8765/event`），然后启动或恢复 Copilot CLI 会话。提交提示词时发送 `thinking`；工具开始、成功和失败时分别发送 `tool_start`、`tool_end`、`error`；Agent 回合结束时发送 `completed`；会话关闭时发送 `session_end`。

## 在 GitHub Copilot 桌面 App 中尝试

Copilot App 支持提供 hooks 的插件。进入 **Customize → Plugins → Marketplace 设置**，将 At-Pet 仓库添加为自定义 Marketplace，然后安装 **at-pet-copilot**。该插件在 CLI 和桌面 App 中使用同一套 Windows PowerShell hooks。

桌面 App 的 hook 事件和载荷可能与 CLI 不同，因此目前属于尽力兼容。可提交一条提示词，确认桌宠是否先切换到思考状态、再切换到完成状态。如果 App 没有提供预期的 hook 事件，本插件无法仅凭窗口状态推断 Agent 是否正在工作。

## 注意事项

- 仅支持 Windows 和 PowerShell；Linux/macOS 不在本集成范围内。
- At-Pet 必须正在运行且本地 `/event` API 可访问。可通过 `AT_PET_EVENT_URL` 覆盖 API 地址，但 hook 会忽略非回环地址。
- hook 错误会被忽略，At-Pet 未运行时不会阻止 Copilot 工作。
