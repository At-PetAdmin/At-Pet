# atpet-openai-codex-plugin

将 OpenAI Codex 的会话、提示、工具调用与权限请求接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认 `127.0.0.1:8765`）。
- 已安装 Node.js（运行 `bridge.mjs`，无第三方依赖）。
- 支持 Codex hooks 的版本。

## 安装

两种方式任选其一。

### 方式一：作为插件加载

把本目录作为 Codex 插件加载。插件会自动读取 `hooks/hooks.json`，其中用 `PLUGIN_ROOT` 指向本目录，无需修改路径。

### 方式二：写入 Codex 的 hooks.json

把 `hooks/hooks.json` 中的各个 `command` 改为绝对路径形式，写入 `~/.codex/hooks.json`（用户级）或 `<仓库>/.codex/hooks.json`（项目级）：

```
node "D:\code\At-Pet_Agent-plugin\Agent-plugin\openai-codex\bridge.mjs" openai-codex
```

完成后在 Codex 中输入 `/hooks`，审核并信任这些 hook。未信任的 hook 不会执行。

## 事件映射

| Codex hook | At-Pet 行为 |
| --- | --- |
| `SessionStart` | 无（首次提交时才显示会话） |
| `UserPromptSubmit` | `thinking` |
| `PreToolUse` | `tool_start`（消息为工具名） |
| `PostToolUse` | `tool_end` |
| `PermissionRequest` | 在桌宠上弹出「允许一次 / 拒绝」，结果回传给 Codex |
| `Stop` | `done` |
| `SessionEnd` | `session_end` |

## 权限

`PermissionRequest` 会向 At-Pet 的 `/permission` 提交请求并轮询结果。你在弹窗中选择后，决定会返回给 Codex。若 At-Pet 未运行，插件不做决定，Codex 继续显示它自己的审批提示。

## 注意

- 插件中 `commandWindows` 使用 PowerShell 语法，`$env:PLUGIN_ROOT` 为其环境变量写法。若你的 Codex 在 Windows 上使用其他 shell，请按 shell 语法修改。该写法尚未在真机上验证。
- 本插件默认把 ChatGPT 桌面端的 Codex 会话标记为 `openai-codex`。
