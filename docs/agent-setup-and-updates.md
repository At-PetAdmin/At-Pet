# Agent 接入与获取更新

在设置的 **Agent 事件接入** 中勾选需要展示事件的来源，点击 **一键配置已选 Agent**。
搜索只筛选卡片，不改变勾选状态；批量配置包含搜索隐藏的已选来源。卡片内也可单独配置。

| Agent | 一键配置位置（用户主目录下） |
| --- | --- |
| Claude Code | `.claude/settings.json` |
| OpenAI Codex | `.codex/hooks.json`（尊重 `CODEX_HOME`） |
| WorkBuddy / CodeBuddy | `.codebuddy/settings.json` |
| Command Code | `.commandcode/settings.json` |
| Qwen Code | `.qwen/settings.json` |
| OpenCode V2 | `.config/opencode/plugins/at-pet.ts` 与 `client-auth.mjs`（尊重 `XDG_CONFIG_HOME`） |

Node hooks 需要 Node.js 20+，并且 Agent 进程能在 PATH 中找到 `node`。
桥接程序、profile 和认证助手内置在程序资源中，配置时释放到 At-Pet 的本地应用数据目录，
不依赖源码目录或发行 ZIP 中的额外文件。hook 引用固定的本地路径，配置时写入保存的 API 端口；
改变端口后请重新配置 hook，并重启 At-Pet 与 Agent。`AT_PET_PORT` 环境变量优先。
OpenCode 的 `AT_PET_URL` 环境变量仍优先于生成文件内的默认端口。

合并保留原有 JSON 设置、第三方 hooks 和 matcher；重复配置不重复添加相同 hook。
修改现有配置前，会在旁边生成带时间与随机标识的 `.at-pet-*.bak` 备份，
结果窗口的详细信息展示配置与备份路径。无效 JSON 或不兼容的 hooks 结构会报错并保留原文件。
JSON 注释不受支持，需先整理为合法 JSON。OpenCode 文件变更同样先备份。
不修改 Agent 的 hook 信任记录，不写入 API token；内置发送端自动读取对应 Agent 的本地凭证。

配置后重启 Agent 并审核 hooks。只有真实事件会使卡片显示“已连接”，勾选或安装不会伪造连接。
如果使用了同一个 Agent 的插件，请选择插件或直接 hooks 其中一种，避免重复上报。
事件筛选、桌宠绑定、子 Agent 桌宠和权限转发选项仍需通过 **保存事件筛选设置** 保存。

## 组织插件

每张卡片提供安装指引和组织仓库入口，独立插件优先使用对应仓库 README 的安装方法：

- [Codex](https://github.com/At-PetAdmin/codex-at-pet#安装)：
  `codex plugin marketplace add At-PetAdmin/codex-at-pet`，
  然后 `codex plugin add codex-at-pet@at-pet-local`。
  重启后在 `/hooks` 或桌面端 Settings → Hooks 中审核并信任。
- [DeepSeek Harness](https://github.com/At-PetAdmin/dsh_at-pet#安装)：
  插件页安装 `dsh-atpet`，或在 Harness 会话中使用
  `plugin_manager({ action: 'install_bundle', target: 'dsh-atpet' })`。
  CLI 的 web profile 使用 `dsh plugin --profile web add dsh-atpet`，不用于 desktop profile。
- [GitHub Copilot](https://github.com/At-PetAdmin/github-copilot_at-pet)：
  克隆后在仓库根目录运行 `node scripts/install.mjs app --repo <项目路径>`，
  CLI 使用 `node scripts/install.mjs cli --repo <项目路径>`。
- 其他已提供插件的来源见 [Agent 插件集合](https://github.com/At-PetAdmin/At-Pet_Agent-plugin)。

本地 API 默认启用密钥鉴权。可在「常规设置 → 安全选项」关闭密钥鉴权，应用并重启后即可免密接入。
内置 Node 集成需将 `client-auth.mjs` 放在入口脚本旁。已安装的旧版发送端如果仍要求读取密钥，请更新发送端。
详见 [本地 API 认证与迁移](local-api-security.md)。

## 获取更新

设置新增 **获取更新** 标签页，选择 **正式版** 或 **测试版／预发布** 后点击 **检查更新**。
程序只在点击检查时查询 `At-PetAdmin/At-Pet` 的 GitHub Releases，不上传本地设置。
按版本号（包括 beta 数字）选择对应通道的最新发布，忽略草稿，展示发布说明。
支持 `v1.3.4-beta1` 和 `v1.3.0beta1` 等标签格式。

默认选择系统架构，也可手动选择 Windows AMD64 / ARM64。
**下载此版本** 在浏览器打开该发布的对应 ZIP；缺少对应架构包时禁用下载按钮，仍可查看发布页面。
网络失败或 GitHub 请求限流时显示可重试提示。当前版本高于所选通道时明确提示，也允许主动获取旧版。

下载完成后退出 At-Pet，解压替换程序再启动。设置与宠物库保存在用户数据目录中。
此入口不自动替换正在运行的程序，不创建 GitHub Release。

## 验证

`agent-setup` Qt 回归测试使用临时配置，覆盖合并保留、备份、重复配置去重、端口更新、无效配置保护、
释放的 Node 入口、组织指引、通道选择、预发布版本比较、架构包匹配和搜索状态保留。
