# atpet-zcode-plugin

将 ZCode 桌面客户端的会话、提示、工具调用和回合结束事件接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认 `127.0.0.1:8765`）。
- 已安装 Node.js（运行 `bridge.mjs`，无第三方依赖）。
- ZCode 桌面客户端，并支持插件与 hooks。

## 安装

ZCode 的插件支持个人来源（Git、URL、本地目录）。

1. 打开 ZCode，进入 **设置 → 插件 → 创建 → 添加个人来源**，选择本地目录，指向本目录（`Agent-plugin/zcode`）。
2. 在个人插件中安装 **atpet-zcode-plugin** 并启用。
3. 完全退出 ZCode 后重新启动，让 hooks 生效。

## 事件映射

| Hook | At-Pet 事件 |
| --- | --- |
| `SessionStart` | 无 |
| `UserPromptSubmit` | `thinking` |
| `PreToolUse` | `tool_start`（消息为工具名） |
| `PostToolUse` | `tool_end` |
| `Stop` | `done` |
| `SessionEnd` | `session_end` |

## 说明

- 本插件只观察，不做权限决定。
- `hooks/hooks.json` 中的事件名按 Claude Code 风格命名，目前只有 `SessionStart` 在官方样例中得到确认，其余事件请在 ZCode 中用真实会话验证。
- `${CLAUDE_PLUGIN_ROOT}` 指向插件目录，由 ZCode 注入。
