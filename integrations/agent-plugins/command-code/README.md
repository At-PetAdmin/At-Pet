# atpet-command-code-plugin

将 Command Code 的会话、工具调用和回合结束事件接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

Command Code 没有独立的插件清单，接入方式是在 `settings.json` 中配置 hooks。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认 `127.0.0.1:8765`）。
- 已安装 Node.js（运行 `bridge.mjs`，无第三方依赖）。

## 安装

1. 打开 `hooks/settings.example.json`，把其中所有 `ABSOLUTE_PATH` 替换为本目录的绝对路径。
2. 将其中的 `hooks` 合并到以下任一位置：
   - 用户级（所有项目生效）：`%USERPROFILE%\.commandcode\settings.json`
   - 项目级：`<项目>\.commandcode\settings.json`
3. 重启 Command Code。

## 事件映射

| Hook | At-Pet 事件 |
| --- | --- |
| `SessionStart` | 无 |
| `PreToolUse` | `tool_start`（消息为工具名） |
| `PostToolUse` | `tool_end` |
| `Stop` | `done` |

## 说明

- Command Code 的 hook 只做观察，不会弹出 At-Pet 的权限确认。`PreToolUse` 脚本输出为空，表示允许工具执行，不会阻止任何操作。
- 桥接脚本会忽略 `PreToolUse` 和 `PostToolUse` 之外的 matcher 设置，始终对所有工具发出事件。
