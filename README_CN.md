# At-Pet

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white&style=flat-square)](https://isocpp.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?logo=qt&logoColor=white&style=flat-square)](https://www.qt.io/)
[![Platform: Windows 10/11](https://img.shields.io/badge/Platform-Windows_10%2F11-0078D6?logo=windows&logoColor=white&style=flat-square)](https://www.microsoft.com/windows)
[![CMake 3.21+](https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white&style=flat-square)](https://cmake.org/)

[English](README.md) | 简体中文 | [贡献指南](CONTRIBUTING_CN.md)

一个基于 C++17 和 Qt 6 的 Windows 10/11 桌面宠物应用，支持 amd64（x86-64）和 ARM64。Windows 是本项目唯一支持的平台；Linux 和 macOS 不在项目范围内。

## 功能特性

- 透明、无边框、始终置顶的宠物窗口。拖动可移动；单击触发问候；双击输入想让宠物说的话；右键打开控制菜单。
- 内置 Codex Pets 精灵桌宠作为默认宠物，并支持自定义精灵表动画，覆盖 idle、walk、sleep、thinking、work、review、工具执行、权限请求、完成、取消和错误等状态。
- 导入、预览、切换、删除和召唤宠物库中的每一只有效宠物；也可选择为检测到的每个子代理会话显示独立桌宠。
- 监视已导入的 `pet.json` 及其所在文件夹，修改后无需重启即可重新加载配置和精灵图。
- 本地 HTTP API 与 Server-Sent Events（SSE），用于状态更新和 Agent 事件。Claude Code HTTP hooks 与 OpenCode V2 插件提供原生适配器；兼容 MCP 的客户端可选择使用 stdio 桥接主动上报事件，其他来源也可使用通用 HTTP API。
- Agent Monitoring 设置会显示每个来源的接入策略，并可过滤用于控制动画的事件。At-Pet 不会扫描 Agent 进程或读取与机器相关的日志路径；原生集成需在对应 Agent 应用中安装。
- Windows 专属选项：让宠物看向标题中包含 “Copilot” 的可见窗口。这只会改变桌宠朝向，不会检测或同步 Copilot 会话状态。公开的 `github/app` 仓库不包含桌面客户端实现，也未公开状态事件 API，因此 GitHub Copilot 需要外部事件发送端才能同步状态。
- 托盘菜单支持显示/隐藏宠物、导入宠物、召唤宠物库中的全部宠物，以及在简体中文和英语之间切换。默认跟随系统语言，并记住您的选择。

## 构建（Windows / Qt MinGW）

需要 Qt 6.4+（Widgets、Network 和 LinguistTools）、CMake 3.21+、Ninja，以及与所安装 Qt 套件兼容的 MinGW 编译器。以 `E:\Qt\6.11.2` 下的 Qt 安装为例：

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="E:/Qt/6.11.2/mingw_64" `
  -DCMAKE_MAKE_PROGRAM="E:/Qt/Tools/Ninja/ninja.exe" `
  -DCMAKE_C_COMPILER="E:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="E:/Qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build
```

运行 `build\at-pet.exe` 时，需要把 Qt 套件的 `bin` 目录加入 `PATH`，或使用 `windeployqt` 部署 Qt 运行时文件。

### 从 CI 获取构建产物

**Build Windows amd64 package** workflow 会在每个 Pull Request 和每次推送到 `main` 时构建免安装 Qt 的 Windows x86-64 压缩包，因此没有本地 Qt 环境的贡献者也能测试自己的改动。在运行记录的 **Artifacts** 区域下载 `At-Pet-windows-amd64`，解压后直接运行 `at-pet.exe`。推送 `v*` 标签时，压缩包和 `.sha256` 校验文件会一并附加到对应 GitHub Release。Windows ARM64 workflow 可手动运行用于测试，使用 `v*` 标签触发时会发布对应架构的包。

## 宠物库与格式

通过宠物右键菜单或托盘菜单中的 **导入配置与精灵表…**，可以分别选择 `pet.json` 和图片。应用会把两者复制为新的宠物库条目，重写精灵图文件名，并在名称重复时追加数字后缀。对于包含 `pet.json` 及其引用资源的完整文件夹，仍可使用 **导入宠物文件夹…**。导入的宠物可以预览 12 秒，或切换为主位置。“召唤全部宠物”会同时打开库中所有有效条目。

参考项目格式的 `pet.json` 示例：

```json
{
  "name": "Sample Cat",
  "sprite": "spritesheet.png",
  "cell": [128, 128],
  "scale": 1.5,
  "fps": 8,
  "defaultAnimation": "idle",
  "animations": {
    "idle": { "row": 0, "frames": 4 },
    "walk": { "row": 1, "frames": 4 },
    "thinking": { "row": 2, "frames": 4 },
    "tool": { "row": 3, "frames": 4 },
    "sleep": { "row": 4, "frames": 4 },
    "work": { "row": 5, "frames": 4 },
    "review": { "row": 6, "frames": 4 },
    "done": { "row": 7, "frames": 4 },
    "cancelled": { "row": 8, "frames": 4 },
    "error": { "row": 9, "frames": 4 }
  }
}
```

支持 `sprite`（也接受 `spriteSheet`）、`frameWidth`/`frameHeight` 或 `cell`、`scale`、全局默认 `fps` 和 `defaultAnimation`。每个 `animations` 条目也可单独设置 `fps`（1–30），未设置时继承全局 `fps`。设置窗口的桌宠预览中可为每个宠物的每个动画分别调速；该偏好保存在应用设置中，不会修改导入的 `pet.json`。如果对应动画正在播放，修改后会立即应用到现有桌宠；否则会在该动画下次播放时生效。`cell` 可以是 `[width, height]` 数组，也可以是包含 `width` 和 `height` 的对象。对于参考风格的动画，`row` 从 0 开始，`frames` 表示该行上连续帧的数量。旧式包含首尾的 `start`/`end` 范围和显式帧索引数组也受支持；索引按行优先顺序从 0 开始。使用 `row_counts` 而不是 `animations` 的配置同样受支持：各行按顺序映射到 idle、walk、sleep、thinking、work、review、error、done、cancelled、tool 和 interact。JSON 文件、精灵表和宠物文件夹都会被监视是否有改动。内置默认宠物通过 `assets/default` 嵌入程序，无需导入；如果该配置或精灵表不可用，则使用内置矢量宠物作为后备。
在 Windows 上，宠物库位于 `%APPDATA%\Ni-ShuWu\At-Pet\pets`。

## 本地 API

服务默认只绑定到 `127.0.0.1:8765`。可通过 `AT_PET_PORT` 选择其他端口。该 API 不进行身份验证，仅用于同一台机器上的可信程序；请勿将其暴露或转发到网络。

- `GET /health` — 健康检查。
- `GET /state` — 当前宠物和已知 Agent 会话；`agentSources` 列出各 Agent 的 key、监控类型以及可用时的原生端点。
- `GET /events` — SSE 流；连接时发送一次 JSON 状态快照，状态变化时再次发送。
- `POST /animation` — `{"name":"sleep"}`。
- `POST /say` — `{"text":"Hello!"}`。
- `POST /summon` — `{"all":true}` 或 `{"petId":"sample-cat"}`。
- `POST /event` — `{"sessionId":"session-1","agentApp":"github-copilot","event":"thinking","model":"...","message":"...","isSubagent":false,"parentSessionId":"parent-1"}`。可识别的 `agentApp` 标签：`github-copilot`、`zcode`、`deepseek-harness`、`opencode`、`antigravity`、`command-code`、`claude-code`、`openai-codex`、`gemini-cli`、`cursor`、`windsurf`、`workbuddy` 和 `penguin-harness`；通用事件源也可以使用其他标签。Agent 事件不会自动采集，需由集成程序 POST 到本地 API。
- `POST /permission` — 例如 `{"sessionId":"session-1","agentApp":"claude-code","toolName":"Bash","description":"Run tests"}`，用于显示权限请求弹窗。响应中包含 `requestId`。
- `GET /permission/{requestId}` — 轮询 `pending`、`allowed_once` 或 `denied`。决策后的首次轮询会消费该结果，后续请求返回 404。
- `DELETE /permission/{requestId}` — 取消通用待处理权限请求并关闭对应弹窗；Claude HTTP hook 请求不能通过此端点取消。
- `POST /agent-event/claudecode` — 接收 Claude Code HTTP hook 原生 payload，包括 `PermissionRequest`；权限 hook 会保持连接，直到用户选择仅本次允许或拒绝。
- `POST /agent-event/opencode` — 接收随附 OpenCode V2 插件归一化后的事件。

Agent `event` 值映射如下：`thinking`/`model_thinking` → thinking，`tool_start`/`tool_running` → tool，`tool_end` → thinking，权限/审批请求 → permission，`work` → work，`review`/`reflection` → review，`completed`/`done` → done，`cancelled`/`aborted` → cancelled，`error`/`failed` → error。多个 `sessionId` 会被独立跟踪；最近更新的非子代理会话驱动主桌宠。设置 `isSubagent` 或 `parentSessionId` 标识子会话；在 Agent Monitoring 设置中启用后，每个子会话会拥有独立桌宠。终止状态动画会短暂显示，然后由下一个活动会话或空闲状态接管。发送 `idle` 或 `session_end` 可移除会话。

权限弹窗仅提供 **Allow once（仅本次允许）** 和 **Deny（拒绝）**；关闭弹窗也会按拒绝处理。请求不会自动超时或自动拒绝。通用集成需轮询结果并将决定落实到对应工具请求；Claude 原生 HTTP hook 会在用户决定后收到原生 allow/deny 响应，OpenCode 插件则等待同一决定并映射为其权限 effect。At-Pet 不会执行工具，Agent Monitoring 中的动画过滤开关也不会影响权限请求。

### 原生集成

**Claude Code：** 请使用支持 HTTP hooks 的 Claude Code 版本。将以下配置项加入 Claude Code 设置并重启 Claude Code。各 hook 都使用同一端点接收原生 payload：

```json
{
  "hooks": {
    "UserPromptSubmit": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PreToolUse": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PostToolUse": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PostToolUseFailure": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PermissionRequest": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "Stop": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "SessionEnd": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "SubagentStart": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "SubagentStop": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }]
  }
}
```

**OpenCode V2：** 将 [`integrations/opencode-v2/at-pet.ts`](integrations/opencode-v2/at-pet.ts) 复制到项目的 `.opencode/plugins/` 文件夹，或全局目录 `%USERPROFILE%\.config\opencode\plugins\`，然后重启 OpenCode。仅当 At-Pet 使用非默认本地端口时才需设置 `AT_PET_URL`。插件会上报会话/工具事件，并将 `ask` 权限决定交给 At-Pet；若 At-Pet 不可用，则保留 OpenCode 原有的 `ask` 决定。插件卸载时，如果仍有权限弹窗，插件会取消待处理请求并关闭弹窗，不会更改 OpenCode 原有的 `ask` 决定。

**可选 MCP Server：** 兼容 MCP 的客户端可按 [`integrations/at-pet-mcp/README_CN.md`](integrations/at-pet-mcp/README_CN.md) 配置本地 stdio 桥接。工具仅在被明确调用时上报事件；与 hooks 和插件不同，MCP 不会自动监听生命周期。

PowerShell 请求示例：

```powershell
Invoke-RestMethod http://127.0.0.1:8765/event -Method Post `
  -ContentType 'application/json' `
  -Body '{"sessionId":"session-1","event":"tool_start","message":"Running tests"}'
```

At-Pet 提供的是本地事件桥接，不会直接读取 Agent 会话内部状态。兼容的扩展、hook 或其他可信本地集成可以向 API POST 生命周期事件和权限请求。

## 参与贡献

欢迎参与贡献。构建方式、代码约定和提交流程请参阅 [CONTRIBUTING_CN.md](CONTRIBUTING_CN.md)（英文版：[CONTRIBUTING.md](CONTRIBUTING.md)）。

## 许可证

At-Pet 基于 [MIT License](LICENSE) 发布。
