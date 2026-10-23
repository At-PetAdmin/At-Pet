# atpet-claude-code-plugin

将 Claude Code 的会话与工具事件、权限请求接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认监听 `127.0.0.1:8765`）。
- Claude Code 版本需支持 HTTP hooks。

## 安装方式

### 方式一：作为插件加载（推荐）

将本目录（`Agent-plugin/claude-code`）作为 Claude Code 插件加载，插件会自动注册 `hooks/hooks.json` 中的 hooks。

### 方式二：写入 Claude Code 设置

把 `hooks/hooks.json` 中的 `hooks` 内容合并到 Claude Code 的 `settings.json`（用户级 `%USERPROFILE%\.claude\settings.json`，或项目级 `.claude\settings.json`），然后重启 Claude Code。

## 说明

- 所有 hook 都 POST 到 `http://127.0.0.1:8765/agent-event/claudecode`，原样传递 Claude Code 的 hook 载荷，由 At-Pet 解析。
- `PermissionRequest` 会等待你在桌宠弹窗中选择「允许一次」或「拒绝」，超时时间为 600 秒。
- 若 At-Pet 使用了其他端口，请通过 `AT_PET_PORT` 设置，并同步修改 `hooks/hooks.json` 中的 URL。
- At-Pet 只监听本机回环地址，请勿将端口暴露到网络。
