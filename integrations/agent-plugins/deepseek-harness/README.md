# atpet-deepseek-harness-plugin

将 DeepSeek Harness（`dsh`）的会话、提示、工具调用和回合结束事件接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

DSH 是插件化的 Agent 框架，本插件不写代码，而是通过 DSH 官方的 `@deepseek-ai/dsh-hooks-codex` 桥接包，直接读取 Codex 格式的 `hooks.json`。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认 `127.0.0.1:8765`）。
- 已安装 Node.js（运行 `bridge.mjs`，无第三方依赖）。
- 已安装 DeepSeek Harness（`npx @deepseek-ai/dsh web` 可启动）。

## 安装

1. 把 `hooks/hooks.json` 中所有 `ABSOLUTE_PATH` 替换为本目录的绝对路径（正斜杠，例如 `D:/code/At-Pet_Agent-plugin/Agent-plugin/deepseek-harness`）。
2. 把 `dsh-hooks.yml` 中的 `ABSOLUTE_PATH` 同样替换，然后把其中的一行挂载配置加入 DSH 的 profile 补丁（`cordis.patch.yml`，或通过 `dsh --patch <文件>` 叠加）。
   - DSH 的 profile 补丁格式以官方文档为准，请先确认补丁文件的位置和写法。
3. 重新启动 DSH。

## 事件映射

| Hook | At-Pet 事件 |
| --- | --- |
| `SessionStart` | 无 |
| `UserPromptSubmit` | `thinking` |
| `PreToolUse` | `tool_start`（消息为工具名） |
| `PostToolUse` | `tool_end` |
| `Stop` | `done` |

## 说明

- DSH 的 Codex 桥接包只支持 5 个事件（`SessionStart`、`UserPromptSubmit`、`PreToolUse`、`PostToolUse`、`Stop`），因此本插件不包含权限请求，也不包含 `SessionEnd`。
- 桥接包不支持 `async` 的 hook。本插件所有 hook 都是同步执行，并且很快返回。
- 本插件只观察，不做权限决定。
