# atpet-github-copilot-plugin

将 GitHub Copilot 的会话、提示、工具调用和错误事件接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认 `127.0.0.1:8765`）。
- 已安装 Node.js（用于运行 `bridge.mjs`，无第三方依赖）。
- GitHub Copilot CLI，或支持 hooks 的 Copilot 环境。

## 安装

1. 把 `hooks/atpet.json` 中所有 `ABSOLUTE_PATH` 替换为本目录的绝对路径，例如 `D:\code\At-Pet_Agent-plugin\Agent-plugin\github-copilot`。
2. 选一个位置放置修改后的文件：
   - 用户级（所有项目生效）：`%USERPROFILE%\.copilot\hooks\atpet.json`
   - 仓库级：`<仓库>\.github\hooks\atpet.json`
3. 重启 Copilot CLI。配置在启动时加载。

## 事件映射

| Copilot hook | At-Pet 事件 |
| --- | --- |
| `sessionStart` | 无（首次提交时才显示会话） |
| `userPromptSubmitted` | `thinking` |
| `preToolUse` | `tool_start`（消息为工具名） |
| `postToolUse` | `tool_end` |
| `errorOccurred` | `error` |
| `agentStop` | `done` |
| `sessionEnd` | `session_end` |

## 说明

- Copilot 的 hook 只负责观察，没有权限回调，因此 At-Pet 不会弹出权限确认。
- 桥接脚本从 hook 的 stdin 读取 `toolName`、`cwd` 等字段；若 payload 中没有会话 ID，则用工作目录生成一个稳定 ID。
- 如果需要排查，可在命令行手动测试：
  ```powershell
  '{"cwd":"D:\\demo","toolName":"bash"}' | node bridge.mjs github-copilot preToolUse
  ```
