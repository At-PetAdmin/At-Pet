# atpet-penguin-harness-plugin

将 PenguinHarness 的会话活动（提交的消息、结束的回合）接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认 `127.0.0.1:8765`）。
- 已安装 Node.js（运行 `bridge.mjs`，无第三方依赖）。
- 已安装 PenguinHarness 桌面端或 CLI。

## 结构

这是一个 PenguinHarness 插件（npm 包格式）：

- `package.json` 的 `penguin.hooks` 声明两个 hook：`user_prompt`（每次提交消息）和 `stop`（每次回合结束）。
- `bridge.mjs` 从 stdin 读取 `{ hook, session_id, trace_path }`，将事件转发给 At-Pet。

## 安装

PenguinHarness 的插件以 `plugins/<name>` 形式组织，并通过插件库安装。把本目录作为插件包安装到 Agent 上，安装后 PenguinHarness 会生成对应的 hooks 配置。

本地目录安装的具体步骤，请以 PenguinHarness 文档为准（`penguin.ooo/docs`）。目前尚未在真机上验证。

## 事件映射

| Hook | At-Pet 事件 |
| --- | --- |
| `user_prompt` | `thinking` |
| `stop` | `done` |

## 说明

- 只观察，不做权限决定，也不输出任何内容（空输出表示不干预）。
- PenguinHarness 的其他 hook 点（如工具调用前后）未在本插件中声明，因为尚未确认其 stdin 格式。
