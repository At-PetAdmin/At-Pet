# atpet-workbuddy-plugin

将 WorkBuddy（腾讯 CodeBuddy 内核）的会话、提示、工具调用和权限提示接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认 `127.0.0.1:8765`）。
- 已安装 Node.js，并且 Git Bash 中可以直接运行 `node`。WorkBuddy 在 Windows 上通过 Git Bash 执行 hook 命令。
- 已安装 WorkBuddy 或 CodeBuddy Code 的支持 hooks 的版本。

## 安装

### 方式一：作为插件加载（推荐）

把本目录作为插件加载。插件的 `hooks/hooks.json` 会通过 `${CODEBUDDY_PLUGIN_ROOT}` 找到 `bridge.mjs`。

### 方式二：写入 settings.json

把 `hooks/hooks.json` 中的 `hooks` 内容合并到 `~/.codebuddy/settings.json`，并将 `${CODEBUDDY_PLUGIN_ROOT}` 替换为本目录的绝对路径（使用正斜杠，例如 `D:/code/At-Pet_Agent-plugin/Agent-plugin/workbuddy`）。

修改后在 CodeBuddy 中运行 `/hooks` 检查配置。

## 事件映射

| Hook | At-Pet 事件 |
| --- | --- |
| `SessionStart` | 无 |
| `UserPromptSubmit` | `thinking` |
| `PreToolUse` | `tool_start`（消息为工具名） |
| `PostToolUse` | `tool_end` |
| `Notification`（`permission_prompt`） | `permission` |
| `Stop` | `done` |
| `SubagentStop` | `done`（作为子代理） |
| `SessionEnd` | `session_end` |

## 说明

- 本插件只观察，不替你做权限决定。WorkBuddy 的权限提示会触发桌宠的 `permission` 动画，但允许或拒绝仍在 WorkBuddy 中完成。
- 插件清单位置 `.codebuddy-plugin/plugin.json` 和 `${CODEBUDDY_PLUGIN_ROOT}` 变量名参照 CodeBuddy 的插件规范，尚未在 WorkBuddy 中确认，安装前请以 WorkBuddy 的插件文档为准。
