# atpet-opencode-plugin

将 OpenCode V2 的会话、工具事件与权限请求接入 [At-Pet](https://github.com/Ni-ShuWu/At-Pet) 桌宠。源码 `at-pet.ts` 与母项目 `integrations/opencode-v2/at-pet.ts` 保持一致。

## 前提

- Windows 10/11，且 At-Pet 已启动（默认监听 `127.0.0.1:8765`）。
- OpenCode V2。

## 安装

任选一种：

- 项目级：将 `at-pet.ts` 复制到项目的 `.opencode/plugins/` 目录。
- 全局：将 `at-pet.ts` 复制到 `%USERPROFILE%\.config\opencode\plugins\`。

然后重启 OpenCode。

## 说明

- 插件会把会话状态、工具执行前后事件发送到 `/agent-event/opencode`。
- 当 OpenCode 发出 `ask` 权限请求时，会在桌宠中弹出提示，你可选择「允许一次」或「拒绝」，结果会同步给 OpenCode。
- At-Pet 不可用时，插件不会改变 OpenCode 原有的 `ask` 决策。
- 若 At-Pet 使用了非默认端口，请设置环境变量 `AT_PET_URL`，例如 `http://127.0.0.1:9000`。
- 插件卸载时会取消未完成的 At-Pet 权限请求。
