# 主流 Agent 市场全景与 At-Pet 适配判定（2026-10）

> 调研子代理 #1 · 欧美线 · 重跑版
> 判定基准：At-Pet 的四条接入路径与四问（Q1 会话开始 / Q2 工具前后 / Q3 回合结束 / Q4 权限截获并可回传）。
> 本文把「文档写了」「我推断」「待验证」分开标注，避免把厂商宣传当成可编程能力。

## 0. 先说结论

1. **按 90 天交付排序，最大杠杆是「hook 归一化 + 安装器」；按协议赌注排序，才是 ACP。** 已经有一批主流 CLI/IDE 提供结构化 hook，事件形状高度相似（JSON stdin、JSON stdout、退出码），用一份 bridge 加多份厂商 manifest 就能覆盖 Cursor、Gemini CLI、Copilot、Cline、Roo、Kilo、Droid 等。落地成本以「天」计。两者不矛盾：hook 是近期现金流，ACP 是长期通用层。
2. **ACP 是唯一的「一次接入、多家复用」协议级机会，但它不是广播总线。** ACP 是客户端与 agent 之间的点对点 JSON-RPC；At-Pet 不坐在连接上就看不到任何事件。要用 ACP，必须做 ACP 代理或 ACP 客户端，改变用户的启动路径。这是「P1 战略赌注」，不是「本周就能上」的 P0。
3. **真正能回传权限的机制只有四类：hook 返回 decision、MCP 阻塞式工具、ACP `session/request_permission`、PTY 写回。** 其余（JSONL、OTel、MITM、窗口嗅探、IDE 扩展、网关）只能看，不能批。任何声称「全支持」但走这些路径的方案，都是假权限。
4. **当前仓库最大的体验风险是「幽灵支持」：13 个 registry key 里有 4 个没有任何发送端。** Cursor、Windsurf、Gemini CLI、Antigravity 在设置里可选，但不会产生事件。新增产品前，先把「无发送端」状态显式化。
5. **桌面聊天 app 与云端 agent 不值得做深适配。** 它们没有本地生命周期 hook；MCP 只能靠模型主动调用；云端的回合以分钟计，桌宠应显示「远程任务」而不是长时间 thinking。

## 1. 核验边界与置信度

本轮在线检索额度只覆盖了协议层（ACP / AG-UI / A2A）。产品层的事实来自三类材料：本仓库源码与既有适配文档、厂商官方文档入口、我对 2025-2026 公开文档的了解。**没有逐页复核的产品细节一律标 D，不把记忆写成事实。**

| 标记 | 含义 | 使用方式 |
| --- | --- | --- |
| A | 本仓库源码或本轮联网结果直接支持 | 可以直接据此排期 |
| B | 官方文档曾明确写过，本文附官方入口 | 排期前用文末清单逐页复核 |
| C | 由扩展机制、协议形状或产品架构推断 | 只能作为假设，需做 spike |
| D | 待验证，公开资料不足或近一年改版未确认 | 不许写进用户可见的「已支持」 |

工作量口径：S = 0.5-1 人日；M = 2-4 人日；L = 1-2 周；XL = 2 周以上。工作量只算 At-Pet 侧，不含厂商改版导致的返工。

## 2. 四问速查

| 代号 | 问题 | 对应事件词 | 缺失后果 |
| --- | --- | --- | --- |
| Q1 | 会话开始能否发信号 | `started` / `session_start` / `turn_start` | 桌宠不知道新会话，只能被动等第一个工具 |
| Q2 | 工具调用前后能否发信号 | `tool_start` / `pre_tool_use` / `post_tool_use` | 无法显示「正在干活」，也无法区分读文件与跑命令 |
| Q3 | 回合结束能否发信号 | `done` / `stop` / `turn_complete` | 桌宠永远卡在 thinking/tool，这是最常见的「接了但没用」 |
| Q4 | 权限询问能否截获并回传 | `permission_requested` + 裁决 | 只能通知，不能替用户点「允许/拒绝」 |

档位：4/4 = 原生直连；1-3 = 桥接可做；只有 Q1 或靠猜 = 强行兼容；0 = 不可行。

表中的符号：`✓` = 有明确机制；`?` = 待验证；`✗` = 无；`△` = 部分（只能拒绝，或只能看）。

## 3. 总表

### 3.1 CLI / 终端 agent

| 产品 | 厂商 | 形态 | 主要扩展面 | Q1-Q4 | 适配结论 | 工作量 | 权限 | 证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Claude Code | Anthropic | CLI + IDE 插件 + Web | hooks（含 PermissionRequest）、Agent SDK、MCP、OTel、JSONL | ✓✓✓✓ | 已接入，原生直连 | 维护 | 全回传 | A 仓库 + B `code.claude.com/docs/en/hooks` |
| OpenAI Codex CLI | OpenAI | CLI + 桌面 app + 云 | hooks、plugin、app-server JSON-RPC、MCP、OTel | ✓✓✓✓ | 已接入，原生直连 | 维护 | 全回传 | A 仓库 hooks.json |
| GitHub Copilot CLI | GitHub | CLI | hooks（userPromptSubmitted / preToolUse / postToolUse / agentStop / sessionEnd） | ✓✓✓? | 已接入事件；权限回传当前无 | 维护 | 无（待官方） | A 仓库 hooks.json + B docs.github.com |
| OpenCode | Anomaly | CLI + TUI | V2 plugin（tool.execute.before/after、event、chat.message） | ✓✓✓? | 已接入；权限按无宣传 | 维护 | 无（待验证） | A 仓库 plugin |
| Gemini CLI | Google | CLI | hooks、extensions、policy engine、MCP、OTel、stream-json | ✓✓✓? | 桥接可做，P0 | S-M | 待验证（阻断点存在，allow 未确认） | B `github.com/google-gemini/gemini-cli/blob/main/docs/hooks/index.md` |
| Aider | Aider | CLI / Python 库 | `--notify`、Python API、日志；无生命周期 hook | ✓?✓✗ | 强行兼容，不建议主路径 | M | 无 | B `aider.chat/docs/` |
| Goose | Block | CLI + 桌面 | MCP 扩展、recipes、会话库；hook 待验证 | ✓?✓✗ | 桥接可做（日志/包装），P2 | M | 无 | B `block.github.io/goose/docs/` |
| OpenHands | All Hands | CLI + Web + 云 | 事件流 WebSocket/REST、SDK、MCP、confirmation | ✓✓✓? | 桥接可做（事件流客户端），P1 | L | 待验证 | B `docs.all-hands.dev/` |
| Crush | Charm | CLI/TUI | MCP、配置、日志；无公开 hook | ✓?✓✗ | 强行兼容（日志 tail） | M | 无 | B `github.com/charmbracelet/crush` |
| Amp | Sourcegraph | CLI + VS Code | CLI 流式输出、threads、MCP；hook 待验证 | ✓?✓✗ | 桥接可做/待验证，P1 | M | 无/待验证 | B `ampcode.com/manual` |
| Droid | Factory | CLI | 类 Claude hooks、MCP、exec JSON | ✓✓✓? | 桥接可做，P1 | S-M | 待验证 | B `docs.factory.ai/` |
| Cursor CLI / cursor-agent | Anysphere | CLI | 与 Cursor 同一套 hooks、stream-json | ✓✓✓✓ | 桥接可做，P0 | S-M | 全回传（若 hook allow/deny 生效） | B `cursor.com/docs/agent/hooks` |
| Warp Agent | Warp | 终端 + Agent | 无公开 hook；Warp Drive workflows；日志 | ✓?✓✗ | 强行兼容/不可行 | L | 无 | B `docs.warp.dev/` |
| Zed Agent | Zed | 编辑器 + ACP 客户端 | ACP、MCP；自身 agent 无 hook | 经 ACP ✓✓✓✓ | 通过 ACP 代理可做，P1 战略 | L | 可回传（ACP） | B `zed.dev/docs/agents` |
| Kilo Code CLI | Kilo | CLI（Cline/Roo 系） | 类 Cline hooks、MCP | ✓✓✓? | 桥接可做，P1 | S-M | 部分 | B `kilocode.ai/docs/` |
| Claude Agent SDK | Anthropic | SDK / 自建 harness | hooks + `canUseTool` + PermissionResult | ✓✓✓✓ | 原生直连（仅自建 harness） | S | 全回传 | B `docs.claude.com/en/api/agent-sdk/overview` |
| Mistral Vibe CLI | Mistral | CLI | 配置、MCP 待验证；无公开 hook | ✓?✓✗ | 强行兼容/待验证 | M | 无 | B `github.com/mistralai/mistral-vibe` |
| Grok CLI | xAI / 社区（待确认） | CLI | 官方形态与 hook 均待验证 | ? | 待验证，不排期 | ? | 待验证 | D 官方入口未确认；社区实现另计 |

### 3.2 IDE / 编辑器 agent

| 产品 | 厂商 | 形态 | 主要扩展面 | Q1-Q4 | 适配结论 | 工作量 | 权限 | 证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Cursor | Anysphere | 独立 IDE + CLI | `.cursor/hooks.json`、hooks allow/deny、MCP | ✓✓✓✓ | 桥接可做，P0 | S-M | 全回传 | A 仓库文档 + B `cursor.com/docs/agent/hooks` |
| Windsurf | Cognition | 独立 IDE | Cascade hooks、MCP | ✓✓✓? | 桥接可做，P1 | S-M | 待验证 | B `docs.windsurf.com/windsurf/cascade/hooks` |
| VS Code Copilot Agent Mode | Microsoft/GitHub | IDE 内置 agent | agent hooks（2026 新增）、MCP、LM API | ✓✓✓? | 桥接可做，P0（复用 githubcopilot） | S-M | 待验证 | B `code.visualstudio.com/docs/copilot/overview` |
| Cline | Cline | VS Code 扩展 | `.clinerules/hooks`、MCP | ✓✓✓部分 | 桥接可做，P1 | S-M | 仅拒绝/待验证 | B `docs.cline.bot/features/hooks` |
| Roo Code | Roo | VS Code 扩展 | `.roo/hooks`、MCP | ✓✓✓部分 | 桥接可做，P1 | S-M | 仅拒绝/待验证 | B `docs.roocode.com/features/hooks` |
| Kilo Code | Kilo | VS Code 扩展 | 类 Roo/Cline hooks、MCP | ✓✓✓部分 | 桥接可做，P1 | S-M | 部分 | B `kilocode.ai/docs/` |
| Continue | Continue | VS Code/JetBrains | config.yaml、MCP、rules；无生命周期 hook | ✓?✗✗ | 不可行/仅 MCP 兜底 | M | 无 | B `docs.continue.dev/` |
| Zed | Zed | 独立编辑器 | ACP 客户端、MCP | 经 ACP ✓✓✓✓ | 通过 ACP 代理可做 | L | 可回传（ACP） | B `zed.dev/docs/agents` |
| JetBrains AI Assistant / Junie | JetBrains | IDE + CLI | Junie CLI、MCP、guidelines；hook 待验证 | ✓?✓✗ | 强行兼容，P2 | M | 无 | B `jetbrains.com/help/junie/` |
| Kiro | AWS | 独立 IDE + CLI | `.kiro/hooks` 是文件事件，不是会话生命周期；MCP | ✗✗✗✗ | 不可行（易与生命周期 hook 混淆） | - | 无 | B `kiro.dev/docs/hooks/` |
| Augment Code | Augment | VS Code/JetBrains 扩展 | MCP、上下文引擎；无公开 hook | ✓?✓✗ | 强行兼容，P2 | M | 无 | B `docs.augmentcode.com/` |
| Sourcegraph Cody | Sourcegraph | IDE 扩展 | 已被 Amp 取代/收敛 | - | 不投入 | - | 无 | B `sourcegraph.com/docs/cody` |
| Google Antigravity | Google | 独立 IDE | Agent Manager、MCP；无公开 hook | ✓?✓✗ | 暂不可行，P3 | L | 无 | B `antigravity.google/docs` |
| Firebase Studio | Google | 云 IDE | Gemini、云工作区；无本地 hook | ✗✗✗✗ | 不可行 | - | 无 | B `firebase.google.com/docs/studio` |

### 3.3 桌面 app

| 产品 | 厂商 | 形态 | 主要扩展面 | Q1-Q4 | 适配结论 | 工作量 | 权限 | 证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ChatGPT Desktop（Work/Codex） | OpenAI | 桌面 app | Codex app、MCP、本地线程库；无公开生命周期 hook | ✓?✓✗ | 仅 MCP/日志兜底，P2 | M-L | 无 | B `developers.openai.com/codex/app` |
| Claude Desktop | Anthropic | 桌面 app | MCP、本地日志；无 hook | ✓?✗✗ | 不可行（仅 MCP 兜底） | M | 无 | B `support.anthropic.com/` |
| Cherry Studio | Cherry | 桌面 chat | MCP、provider 配置 | ✗✗✗✗ | 不可行 | - | 无 | B `docs.cherry-ai.com/` |
| LM Studio | LM Studio | 本地模型 app | OpenAI 兼容 server；无 agent 生命周期 | ✗✗✗✗ | 不可行 | - | 无 | B `lmstudio.ai/docs/` |
| Open WebUI | Open WebUI | 自托管 Web | pipelines/functions、tools、WebSocket | ✓?✓✗ | 强行兼容，P3 | M | 无 | B `docs.openwebui.com/` |
| Jan | Jan | 桌面 chat | MCP、本地 API | ✗✗✗✗ | 不可行 | - | 无 | B `jan.ai/docs/` |
| AnythingLLM | Mintplex | 桌面/自托管 | agent skills、API、MCP | ✓?✓✗ | 强行兼容，P3 | M | 无 | B `docs.anythingllm.com/` |
| Chatbox | Chatbox | 桌面 chat | MCP、provider | ✗✗✗✗ | 不可行 | - | 无 | B `docs.chatboxai.app/` |

### 3.4 云 / 异步 agent

| 产品 | 厂商 | 形态 | 主要扩展面 | Q1-Q4 | 适配结论 | 工作量 | 权限 | 证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Devin | Cognition | 云 | REST API、session/message、webhook 待验证 | ✓✓✓✗ | 桥接可做（通知级），P2 | M | 无 | B `docs.devin.ai/` |
| Google Jules | Google | 云 | Jules API、tasks/activities | ✓✓✓✗ | 桥接可做（通知级），P3 | M | 无 | B `jules.google/docs/` |
| Codex Cloud | OpenAI | 云 | GitHub PR/check、Codex 账号；无公开 hook | ✓?✓✗ | 仅 PR 状态，P3 | M | 无 | B `developers.openai.com/codex/cloud` |
| Claude Code on the web | Anthropic | 云 | 云会话、GitHub、teleport 到 CLI | ✓?✓✗ | 仅会话状态，P3 | M | 无 | B `code.claude.com/docs/en/claude-code-on-the-web` |
| Replit Agent | Replit | 云 IDE | 无公开 hook/事件 API | ✗✗✗✗ | 不可行 | - | 无 | B `docs.replit.com/` |

## 4. 协议层：ACP / AG-UI / A2A / MCP 的杠杆

### 4.1 一句话对比

| 协议 | 解决的问题 | 事件粒度 | 权限回传 | 对 At-Pet 的杠杆 | 结论 |
| --- | --- | --- | --- | --- | --- |
| ACP | 编辑器/客户端 ↔ coding agent | 会话、消息流、工具调用、计划、权限 | 原生 `session/request_permission` | 高，一次代理覆盖多家 | **值得押注，但要做代理** |
| AG-UI | agent 后端 ↔ 前端 UI | run、文本、工具、状态、interrupt | 有 human-in-the-loop | 中，但 coding agent 采用少 | 观察，不做主路径 |
| A2A | agent ↔ agent 任务协作 | task/artifact/status | 无 UI 级权限 | 低 | 不适用 |
| MCP | agent ↔ 工具/上下文 | tool/resource/prompt | 仅阻塞式工具可间接 | 低（已有） | 兜底，不做主路径 |

### 4.2 ACP：唯一能原生覆盖四问的通用层

ACP（Agent Client Protocol）是 Zed 推动的开放协议，JSON-RPC 2.0，通常跑在本地子进程的 stdio 上。它把「客户端」和「agent」分开：客户端负责 UI、文件系统、权限；agent 负责推理和工具编排。

**它天然覆盖四问：**

- `session/new` / `session/load` → Q1 会话开始。
- `session/prompt` 之后持续推送 `session/update`，其中包含 agent message chunk、tool call、tool call update、plan → Q2 工具前后，而且比 hook 更细。
- 回合结束有明确的 stop reason / prompt 响应 → Q3。
- `session/request_permission` 由客户端返回 allow/deny（还可带选项）→ Q4，且是阻塞式、语义明确的权限通道。
- `session/cancel` → cancelled 事件。

**它的限制同样明确：**

1. ACP 是点对点协议，不是消息总线。At-Pet 不在客户端与 agent 之间，就收不到任何 `session/update`。
2. ACP 没有发现机制。它不会告诉局域网「这里有个 agent 在跑」；必须由客户端拉起 agent，或由代理转发。
3. 大多数用户是通过 Zed、JetBrains 等客户端使用 ACP agent，而不是直接运行 agent 的 ACP server 模式。At-Pet 要接，就得成为客户端，或成为客户端与 agent 之间的代理。
4. 代理必须忠实转发所有 JSON-RPC 消息，处理双向请求、取消、错误和版本协商。代理挂住会直接卡死编辑器。

**实现者名单（需要逐项核验版本与 Windows 可用性）：**

| 角色 | 已知实现 | 状态 |
| --- | --- | --- |
| 客户端 | Zed、JetBrains IDE、Neovim 生态插件 | B/C，Zed 为主 |
| agent | Gemini CLI（ACP 模式）、Claude Code（经 `claude-code-acp` 适配器）、Codex（经 `codex-acp` 适配器）、OpenCode、Goose、Qwen Code | B/C，需逐项验证 |
| 未确认 | Cursor、Copilot、Windsurf、Cline/Roo/Kilo、Droid、Amp | D，不能假设 |

> 结论：ACP 是「通用接入层」的最佳候选，但它的通用性来自**协议形状**，不是来自**现成流量**。要做成，需要交付一个 `at-pet-acp` 代理：对内把 `session/update` 翻译成 `/event`，把 `session/request_permission` 翻译成 `/permission`；对外保持完整的 ACP 语义。用户把编辑器里的自定义 agent 命令指向这个代理即可。这是一次性投入、多 agent 复用的正确形态。

### 4.3 AG-UI：事件模型很好，但生态不在 coding CLI

AG-UI 把 agent 运行过程抽象成一套前端事件：run 开始/结束、文本消息增量、工具调用开始/结束/结果、状态快照/增量、错误，以及 human-in-the-loop 的 interrupt/resume。它通过 SSE 或 HTTP 传输，适合「agent 后端 + 富前端」。

对 At-Pet 的价值：事件语义和桌宠状态机几乎一一对应，尤其是 tool call 与 run finished。但它的采用者主要是 CopilotKit、LangGraph、CrewAI、Mastra、PydanticAI 这类应用层框架，不是 Claude Code / Codex / Cursor 这类 coding agent 的默认输出协议。**除非 At-Pet 自己托管 agent，否则拿不到这些事件。** 结论：观察，不做主路径；若未来做「At-Pet 内置 agent」，可以优先采用 AG-UI 作为前端协议。

### 4.4 A2A：任务协作协议，不是 UI 事件协议

A2A 解决的是 agent 与 agent 之间的任务委派：Agent Card 发现、task 生命周期、artifact 交换、流式状态。它已经是 Linux Foundation 项目，2026 年发布 v1.0.x，采用者数量很大。但它的粒度是「任务」，不是「这次工具调用开始了」；它也没有面向桌宠的权限裁决语义。

对 At-Pet 的价值仅限于云端异步任务：把 A2A task 状态映射成 work/done/error。**不能覆盖 Q2 和 Q4**，也不适合本地 CLI。结论：不押注。

### 4.5 MCP：已有，但只能做兜底

MCP 是 agent 与工具/上下文之间的协议。At-Pet 已经有 `report_agent_event` stdio 工具，这是正确的兜底：任何 MCP 客户端都能接，零厂商适配。但它有三个硬限制：

1. **模型不保证调用。** 工具是否被调用取决于模型和系统提示，不能当生命周期信号。
2. **看不到 agent 自己的内置工具。** MCP 只暴露 MCP 工具调用，看不到 shell、文件编辑、内置搜索。
3. **权限回传只能靠阻塞式工具。** 如果 agent 把审批交给某个 MCP 工具，可以阻塞等待；但主流 agent 的内置权限弹窗不经过 MCP。

结论：MCP 保留为「任意客户端的最后兜底」，不作为任何 P0 的主路径。

### 4.6 押注顺序

| 顺序 | 层 | 理由 | 交付物 |
| --- | --- | --- | --- |
| 1 | hook 归一化 | 现成流量最大，改动最小，权限语义因厂商而异但可适配 | `hook-adapter` + 厂商 manifest + 安装器 |
| 2 | ACP 代理 | 唯一能一次覆盖多家的协议级路径，权限原生 | `at-pet-acp` 双向代理 |
| 3 | MCP | 零适配兜底，覆盖长尾 | 现有 `report_agent_event` |
| 4 | AG-UI | 自建 agent 时才有用 | 暂不交付 |
| 5 | A2A | 云端任务状态，粒度太粗 | 暂不交付 |

## 5. CLI 产品逐个判定

### 5.1 Gemini CLI（Google）——P0

- 形态：Node/TypeScript CLI，交互式 TUI，也可 `-p` 一次性执行；有 extensions 生态。
- 文档写了：hooks 覆盖会话、prompt、工具前后、通知、压缩、结束等生命周期；配置在用户级或项目级 `settings.json`；支持 MCP、OTel、`--output-format stream-json`；另有 policy engine 控制工具审批。
- 推断：`BeforeTool` 能阻断工具，是天然的权限截获点；如果 hook 能返回 allow 覆盖本地确认，则 Q4 全中。若只能 deny，则只能做「拒绝」不能做「批准」，权限档降为部分。
- 四问：Q1 ✓，Q2 ✓，Q3 ✓（AfterAgent/SessionEnd 组合），Q4 待验证。
- 适配动作：新增 `integrations/agent-plugins/gemini-cli/`，复用 bridge；在 `settings.json` 注册 hooks；把 `BeforeTool` 的阻塞请求转发到 `/permission`，按返回写 decision。
- 风险：Gemini CLI 的 hook 输出 schema 与 Claude Code 不完全相同，不能直接复用字段名；`settings.json` 可能被用户手工维护，安装器必须合并而不是覆盖。
- 证据：<https://github.com/google-gemini/gemini-cli/blob/main/docs/hooks/index.md>（B）。

### 5.2 Cursor CLI / cursor-agent（Anysphere）——P0

- 形态：独立 CLI，可脚本化；与 Cursor IDE 共享 agent 能力与 hooks 配置。
- 文档写了：Cursor agent hooks 支持提交 prompt 前、shell/MCP 执行前后、读文件前、编辑后、stop 等事件；hook 输出可以 allow/deny/ask；配置在项目 `.cursor/hooks.json` 或用户级 hooks 文件。
- 推断：同一份 bridge 可以同时服务 IDE 与 CLI；`stop` 对应 Q3，shell/MCP 前后对应 Q2，prompt 提交对应 Q1，allow/deny 对应 Q4。四问全中。
- 四问：✓✓✓✓。
- 适配动作：新增 `cursor` 插件目录；用 session_id + generation_id 做去重；对 `beforeShellExecution` 展示完整命令、cwd、sandbox 状态，再回传裁决。
- 风险：hooks 仍是较新能力，不同 Cursor 版本字段可能变；CLI 与 IDE 可能对同一会话各发一次，必须去重。
- 证据：<https://cursor.com/docs/agent/hooks>（B）；仓库 `docs/agent-compatibility.md` 已把它列为 P0（A）。

### 5.3 Droid（Factory）——P1

- 形态：CLI coding agent，支持交互与 `droid exec` 非交互。
- 公开资料显示：Factory 提供 hooks 机制，事件形状接近 Claude Code；支持 MCP 与权限模式。
- 待验证：hook 事件全集、配置路径、权限输出字段；是否支持 `permissionDecision` 或只能 block。
- 推断：如果 hooks 覆盖 PreToolUse/PostToolUse/Stop/SessionStart，Q1-Q3 可全中；Q4 取决于是否能在 PreToolUse 阻塞并返回 allow。
- 适配动作：先做 payload 抓取（脱敏）与 golden fixture，再决定是复用 Claude bridge 还是单独 manifest。
- 风险：Factory 文档更新快，不能按 Claude Code 的字段硬编码。
- 证据：<https://docs.factory.ai/>（B/D）。

### 5.4 OpenHands（All Hands）——P1

- 形态：Python 后端 + Web UI + CLI + 云端；本地可跑 Docker runtime。
- 文档写了：会话有事件流（action/observation），可通过 WebSocket/REST 获取；支持 MCP；有 confirmation/security analyzer 概念。
- 推断：事件流能覆盖 Q1-Q3，且粒度比 hook 细；Q4 需要确认是否有「等待用户确认」的 API 以及能否从外部写入裁决。若只能读事件，则权限仍为无。
- 适配动作：写一个轻量 sidecar，订阅会话事件，归一化为 `/event`；权限先只做通知，不承诺回传。
- 风险：事件流协议与版本绑定；Docker runtime 在 Windows 上的路径/权限问题会拖长工期；云端与本地事件源不同。
- 证据：<https://docs.all-hands.dev/>（B/C）。

### 5.5 Amp（Sourcegraph）——P1/待验证

- 形态：CLI + VS Code 扩展 + Web threads；Sourcegraph 已把 Cody 逐步收敛到 Amp。
- 文档写了：Amp 有 CLI、thread 分享、MCP、权限模式；是否有生命周期 hooks 待验证。
- 推断：若 CLI 提供流式 JSON 输出，可从 stdout 解析 Q1-Q3；Q4 若无 hook，只能靠 PTY，不建议。
- 适配动作：先验证 `amp` CLI 的非交互输出格式与事件字段；有 hook 再升级为 bridge。
- 风险：Amp 产品迭代快，CLI 参数可能变；不要基于 UI 抓屏做状态。
- 证据：<https://ampcode.com/manual>（B/D）。

### 5.6 Goose / Codename Goose（Block）——P2

- 形态：Rust CLI + 桌面 app；以 MCP 扩展为核心；有 recipes 和会话存储。
- 文档写了：扩展通过 MCP；支持多种 provider；有 `goose run` 非交互模式。
- 待验证：是否存在官方 lifecycle hooks；会话库 schema；是否输出结构化事件。
- 推断：最稳妥的是包装 `goose run`，从进程生命周期拿 Q1/Q3，从 MCP 代理拿 Q2（仅 MCP 工具），内置 shell 工具看不到。
- 适配动作：P2 观察；若社区已有 hooks，再按 bridge 接入。
- 权限：无明确回传通道。
- 证据：<https://block.github.io/goose/docs/>（B/D）。

### 5.7 Aider——强行兼容，不建议

- 形态：Python CLI / 库；以 git diff 和 repo map 为核心。
- 文档写了：有 `--notify` 之类的命令回调、lint/test 命令、Python API；没有 Claude Code 式的生命周期 hooks。
- 推断：Q1 可靠进程启动；Q3 可靠 `--notify` 或进程退出；Q2 只能靠 Python API/monkeypatch 或日志解析；Q4 完全没有结构化通道。
- 适配动作：如果一定要做，提供 `aider-at-pet` 包装器，在启动/结束发事件；工具级状态不做承诺。
- 风险：日志格式随版本变；PTY 解析在 Windows 上尤其脆。
- 证据：<https://aider.chat/docs/>（B/C）。

### 5.8 Crush（Charm）——强行兼容

- 形态：Go 编写的终端 TUI；配置驱动；MCP 支持。
- 文档写了：配置、provider、MCP、session；没有公开的 hook/事件 API。
- 推断：只能 tail 日志或包装进程；Q2/Q3 靠文本启发式，容易误判。
- 适配动作：不排期；若做，只做「启动/退出」两级状态。
- 证据：<https://github.com/charmbracelet/crush>（B/C）。

### 5.9 Warp Agent——不可行/强行兼容

- 形态：商业终端，Agent Mode 与 blocks 深度绑定；闭源。
- 文档写了：Warp Drive、workflows、AI 功能；没有公开的本地生命周期 hook 或事件 API。
- 推断：窗口/日志嗅探只能猜，不能回传权限；不符合 At-Pet 的「事件驱动」原则。
- 结论：不做。
- 证据：<https://docs.warp.dev/>（B/D）。

### 5.10 Zed Agent——ACP 路径

- 形态：Rust 编辑器；自身 agent + 外部 agent（ACP）。
- 文档写了：Zed 是 ACP 的主要客户端；外部 agent 通过 ACP 接入；支持 MCP。
- 推断：At-Pet 无法从 Zed 内部直接拿事件；唯一干净路径是让 Zed 启动 `at-pet-acp` 代理，再由代理拉起真实 agent。
- 适配动作：与 ACP 代理一起交付；在 Zed 的 custom agent 配置里填代理命令。
- 风险：Zed 的 agent 启动参数与 ACP 版本可能变；代理必须支持文件系统代理能力，否则 agent 无法读写。
- 证据：<https://zed.dev/docs/agents>（B）。

### 5.11 Kilo Code CLI——P1

- 形态：Cline/Roo 系的 CLI/IDE 共享内核。
- 公开资料显示：Kilo Code 有 hooks 与 MCP；CLI 是否复用同一套事件待核验。
- 推断：如果 CLI 复用同一套 hooks，Q1-Q3 可中，Q4 取决于 hook 是否能阻塞并返回 allow。
- 适配动作：与 Cline/Roo 共用 manifest，只做字段差异。
- 证据：<https://kilocode.ai/docs/>（B/C）。

### 5.12 Claude Agent SDK——自建 harness 的参考实现

- 形态：SDK（Python/TypeScript），不是终端产品；用于自建 agent。
- 文档写了：hooks + `canUseTool` 回调 + PermissionResult，可在工具执行前阻塞并返回 allow/deny；支持 MCP、子代理、会话。
- 推断：这是四问最完整的编程接口，适合做「At-Pet 官方示例 harness」或给企业客户做定制接入。
- 适配动作：提供一个 30 行的示例，展示 `canUseTool` 里 POST `/permission` 并等待裁决。
- 风险：只覆盖用 SDK 自建的应用，不覆盖现成 CLI。
- 证据：<https://docs.claude.com/en/api/agent-sdk/overview>（B）。

### 5.13 Mistral Vibe CLI——待验证

- 形态：开源 Python CLI（Mistral 的 coding agent）。
- 文档写了：安装、配置、provider、工具；hook/事件面待验证。
- 推断：最可能只有日志与进程生命周期；不建议在核实前排期。
- 证据：<https://github.com/mistralai/mistral-vibe>（B/D）。

### 5.14 Grok CLI——待验证，不排期

- 现状：xAI 的官方 CLI 形态与社区 `grok-cli` 容易混淆；官方是否提供 hooks、MCP、结构化输出均未确认。
- 处理：在核实官方仓库与文档前，不写进支持列表；若只有社区实现，应按「第三方包装器」对待。
- 证据：D（官方入口未确认）。

### 5.15 已接入产品的现状（一句话）

- **Claude Code**：hooks + PermissionRequest 全链路，四问全中，是当前最完整的参考实现。
- **OpenAI Codex CLI**：hooks 覆盖 SessionStart/UserPromptSubmit/PreToolUse/PostToolUse/PermissionRequest/Stop/SessionEnd，权限有 600 秒阻塞窗口，四问全中。
- **GitHub Copilot CLI**：hooks 覆盖 prompt、工具前后、失败、agentStop、sessionEnd；权限回传当前未实现，属于「三问半」。
- **OpenCode**：V2 插件能拿 tool 前后与 session 事件；权限是否可编程回传仍待验证，先按「无」宣传。

## 6. IDE 产品逐个判定

### 6.1 Cursor——P0，最值得先做

- 形态：独立 IDE（VS Code fork）+ CLI；Agent 面板与 Tab 补全并存。
- 文档写了：agent hooks 可在 prompt 提交、shell/MCP 执行前后、文件读取前、文件编辑后、stop 等节点触发；hook 可返回 allow/deny/ask；配置在项目或用户级 hooks 文件。
- 推断：这是「四问全中 + 现成用户量 + 已有 registry key」三者同时满足的少数产品。
- 权限细节：`beforeShellExecution` 必须展示完整命令、工作目录、是否 sandbox；不要只显示「Cursor 想运行命令」。
- 适配动作：一个 bridge 同时覆盖 IDE 与 `cursor-agent`；用 generation_id 去重；stop 事件做兜底清理。
- 风险：hooks 较新，beta 字段可能变；用户可能同时开多个窗口，sessionId 要带 workspace。
- 证据：<https://cursor.com/docs/agent/hooks>（B）。

### 6.2 Windsurf——P1

- 形态：独立 IDE（Codeium/Windsurf），Cascade agent。
- 文档写了：Cascade hooks 可在用户 prompt、工具前后、Cascade 响应后等节点触发；配置在 `.windsurf/hooks.json` 一类项目/用户文件。
- 待验证：hook 是否能返回 allow/deny，还是只能观察；事件字段与超时行为。
- 推断：如果只能观察，Q1-Q3 可做，Q4 降为无；仍值得做，因为 registry 已有 key 且用户量可观。
- 适配动作：先做只读版（Q1-Q3），权限作为第二阶段；文档里明确「当前仅状态同步」。
- 风险：Windsurf 被 Cognition 收购后产品线有调整，文档可能迁移；不要依赖旧 Codeium 文档。
- 证据：<https://docs.windsurf.com/windsurf/cascade/hooks>（B/D）。

### 6.3 VS Code Copilot Agent Mode——P0，复用 githubcopilot key

- 形态：VS Code 内置 Copilot Chat 的 agent 模式；与 Copilot CLI 共享账号与部分配置。
- 文档写了：VS Code 提供 Copilot agent hooks（2026 年新增的预览能力）、MCP、LM API、chat participant API。
- 待验证：agent hooks 的事件全集、配置文件位置、是否能拦截内置工具的权限确认。IDE 的审批 UI 与 CLI 不同，不能假设 CLI 的 `permissionDecision` 直接可用。
- 推断：事件层大概率可覆盖 Q1-Q3；Q4 若 hook 不能接管内置审批，就只能做「通知 + IDE 内批准」。
- 适配动作：与现有 `githubcopilot` bridge 共用归一化层，新增 IDE hook 安装器；不要新建重复 key。
- 风险：VS Code 与 Copilot CLI 的 hook schema 可能分叉；预览功能默认关闭，安装器要检测版本。
- 证据：<https://code.visualstudio.com/docs/copilot/overview>（B/D）。

### 6.4 Cline——P1

- 形态：VS Code 扩展；任务制 agent。
- 文档写了：hooks 目录（`.clinerules/hooks`）支持 TaskStart、TaskResume、TaskCancel、TaskComplete、PreToolUse、PostToolUse、UserPromptSubmit、PreCompact 等；通过 stdin 传 JSON、stdout 返回 JSON；可以取消工具。
- 推断：Q1-Q3 全中；Q4 若 PreToolUse 只能 `cancel` 不能 `allow`，则只是「拒绝权」，不是双向裁决。
- 适配动作：bridge 里把 cancel 映射为 permission_denied，但 UI 上不要声称可以批准。
- 风险：Cline 的 hook 是任务级，不是会话级；sessionId 要用 taskId。
- 证据：<https://docs.cline.bot/features/hooks>（B）。

### 6.5 Roo Code——P1

- 形态：VS Code 扩展（Cline 分支），多模式 agent。
- 文档写了：`.roo/hooks` 提供任务与工具生命周期 hooks；支持 MCP。
- 推断：与 Cline 高度同源，可共用 80% 适配层；Q4 同样受限于「能否 allow」。
- 适配动作：与 Cline 共用 manifest，字段差异用映射表。
- 风险：Roo 的模式（Architect/Code/Debug）会改变工具集，权限文案要带 mode。
- 证据：<https://docs.roocode.com/features/hooks>（B）。

### 6.6 Kilo Code——P1

- 形态：VS Code 扩展 + CLI，Cline/Roo 系。
- 文档写了：hooks 与 MCP；具体事件名待逐页核验。
- 推断：三兄弟（Cline/Roo/Kilo）做一套适配即可，分别出 manifest。
- 适配动作：先做 Cline，再复制到 Roo/Kilo；用 golden payload 回归。
- 证据：<https://kilocode.ai/docs/>（B/C）。

### 6.7 Continue——不可行/仅 MCP 兜底

- 形态：VS Code/JetBrains 扩展 + CLI；config.yaml 驱动。
- 文档写了：模型、上下文、rules、MCP；没有会话/工具生命周期的外部 hook。
- 推断：只能在 MCP 工具调用时上报，内置编辑与终端看不到；Q3 也没有可靠信号。
- 结论：不做深适配；如果用户强需求，提供 `report_agent_event` 的提示词模板。
- 证据：<https://docs.continue.dev/>（B/C）。

### 6.8 Zed——ACP 客户端，见 4.2

- 形态：Rust 编辑器；自身 agent 与外部 ACP agent 并存。
- 文档写了：ACP 是 Zed 的外部 agent 接入方式；支持 MCP。
- 推断：Zed 自身 agent 没有公开 hook；不要为其单独写 bridge。
- 适配动作：只做 ACP 代理，在 Zed 的自定义 agent 配置里指向代理。
- 风险：代理要同时实现文件系统与终端能力，否则 agent 在 Zed 里不可用。
- 证据：<https://zed.dev/docs/agents>（B）。

### 6.9 JetBrains AI Assistant / Junie——P2

- 形态：IDE 插件 + Junie CLI；JetBrains 也是 ACP 客户端之一。
- 文档写了：Junie 有 guidelines、MCP、CLI；AI Assistant 有 chat/edit。
- 待验证：Junie CLI 是否有结构化输出或 hooks；IDE 内置审批是否能被插件接管。
- 推断：最可行的是 ACP 路径（如果 Junie 暴露 ACP agent 模式），否则只能日志/包装。
- 证据：<https://www.jetbrains.com/help/junie/>（B/D）。

### 6.10 Kiro（AWS）——不可行，注意命名陷阱

- 形态：AWS 的 spec-driven IDE + CLI。
- 文档写了：Kiro 的「Agent Hooks」是**文件系统事件**（文件保存/创建/删除时触发自动化），不是会话、工具、回合的生命周期 hook。
- 推断：它无法回答 Q1-Q4 中的任何一个；把它当成 Claude Code hooks 是典型误判。
- 适配动作：不接；如果未来 Kiro 增加 lifecycle hooks，再重新评估。
- 证据：<https://kiro.dev/docs/hooks/>（B）。

### 6.11 Augment Code——P2

- 形态：VS Code/JetBrains 扩展；强调代码库上下文。
- 文档写了：MCP、上下文引擎、agent；没有公开的生命周期 hook。
- 推断：只能靠 MCP 工具或日志，Q3 不可靠；不建议投入。
- 证据：<https://docs.augmentcode.com/>（B/C）。

### 6.12 Sourcegraph Cody——不投入

- 现状：Cody 的产品线已收敛，Sourcegraph 主推 Amp；继续为 Cody 做适配的收益很低。
- 结论：从支持矩阵中移除或标「已停止维护」，不要保留幽灵 key。
- 证据：<https://sourcegraph.com/docs/cody>（B）。

### 6.13 Google Antigravity——P3

- 形态：agent-first IDE（VS Code 系），Agent Manager 可并行管理多个 agent。
- 文档写了：Artifacts、Agent Manager、MCP；没有公开的 lifecycle hooks。
- 推断：短期只能靠日志/窗口，违反 At-Pet 的事件原则；registry 里已有 key，但没有发送端，应标「未连接」。
- 证据：<https://antigravity.google/docs>（B/D）。

### 6.14 Firebase Studio——不可行

- 形态：云端 IDE + Gemini 原型生成。
- 文档写了：浏览器内工作区、App Prototyping agent；没有本地 hook。
- 推断：所有事件都在云端，本地桌宠拿不到；不做。
- 证据：<https://firebase.google.com/docs/studio>（B）。

## 7. 桌面 app 逐个判定

### 7.1 ChatGPT Desktop（Work / Codex）——仅 MCP/日志兜底

- 形态：OpenAI 桌面客户端；Work 与 Codex 共用 app 壳；本地有线程/任务状态。
- 文档写了：Codex app 有任务、审阅、终端；支持 MCP；Codex CLI 的 hooks 不自动覆盖桌面 app。
- 推断：桌面 app 的 agent 生命周期没有公开 hook；如果 app-server 或本地线程库可订阅，能拿 Q1-Q3，但属于非公开接口，版本一变就断。
- 结论：不作为独立适配对象；已有 Codex CLI 接入即可。若用户把桌面 app 当主入口，应在 UI 里说明「仅 CLI 事件」。
- 证据：<https://developers.openai.com/codex/app>（B/D）。

### 7.2 Claude Desktop——不可行/仅 MCP

- 形态：桌面 chat + MCP 客户端；可连接本地 MCP server。
- 文档写了：MCP、connectors、本地文件；没有 agent 生命周期 hook。
- 推断：只能看到 MCP 工具调用，看不到 Claude 自己的推理与回合结束；Q3 无信号。
- 结论：不做深适配；保留 MCP `report_agent_event` 作为可选。
- 证据：<https://support.anthropic.com/>（B/C）。

### 7.3 Cherry Studio——不可行

- 形态：桌面多模型 chat 客户端；MCP、知识库。
- 文档写了：provider、assistant、MCP；没有生命周期事件。
- 结论：不做；最多在 MCP 工具调用时上报，价值低。
- 证据：<https://docs.cherry-ai.com/>（B/C）。

### 7.4 LM Studio——不可行

- 形态：本地模型运行器 + OpenAI 兼容 server。
- 文档写了：local server、模型加载、chat；没有 agent 工具循环的生命周期。
- 推断：只能看到 LLM 请求，不能区分「模型在思考」与「工具在执行」；Q2/Q3 无。
- 结论：不做。
- 证据：<https://lmstudio.ai/docs/>（B/C）。

### 7.5 Open WebUI——P3/强行兼容

- 形态：自托管 Web UI；pipelines/functions 可扩展；支持 tools。
- 文档写了：pipelines、functions、WebSocket、OpenAI 兼容后端。
- 推断：可以写 filter/outlet 在请求前后发事件，但那是 chat 请求级，不是 agent 工具级；Q3 可做，Q2 只能看 MCP 工具。
- 结论：不做主路径；若做，明确「chat 级状态」。
- 证据：<https://docs.openwebui.com/>（B/C）。

### 7.6 Jan——不可行

- 形态：桌面本地模型 chat；MCP、模型管理。
- 文档写了：模型、assistant、MCP；没有 agent 生命周期 hook。
- 结论：不做。
- 证据：<https://jan.ai/docs/>（B/C）。

### 7.7 AnythingLLM——P3

- 形态：桌面/自托管 RAG + agent；有 API 与 agent skills。
- 文档写了：workspace、agent、API；没有标准 lifecycle hook。
- 推断：可通过 API 轮询会话，但工具级事件不完整；Q3 可能靠消息结束推断。
- 结论：不做；除非有企业客户明确要求。
- 证据：<https://docs.anythingllm.com/>（B/C）。

### 7.8 Chatbox——不可行

- 形态：桌面 chat 客户端；MCP、provider。
- 文档写了：chat、MCP；没有 agent 生命周期。
- 结论：不做。
- 证据：<https://docs.chatboxai.app/>（B/C）。

## 8. 云 / 异步 agent 逐个判定

### 8.1 Devin（Cognition）——通知级

- 形态：云端软件工程 agent；有 API、会话、消息、PR。
- 文档写了：Devin API 可创建/查询 session、发消息、取状态；webhook 是否存在待验证。
- 推断：轮询 session 状态可覆盖 Q1/Q3，消息/步骤可部分覆盖 Q2；Q4 无外部裁决通道。
- 适配动作：只做「远程任务」状态卡：working / waiting / done / error；不要映射成细粒度工具动画。
- 风险：轮询延迟与配额；云端 agent 可能几十分钟不动，桌宠不能一直 thinking。
- 证据：<https://docs.devin.ai/>（B/D）。

### 8.2 Google Jules——通知级

- 形态：云端异步 coding agent；Jules API 提供 tasks/activities。
- 文档写了：任务创建、状态、活动列表；没有本地 hook。
- 推断：Q1/Q3 可靠，Q2 取决于 activities 粒度；Q4 无。
- 结论：P3，只做任务状态。
- 证据：<https://jules.google/docs/>（B/D）。

### 8.3 Codex Cloud——仅 PR 状态

- 形态：云端 Codex 任务；通过 GitHub 交付 PR。
- 文档写了：云任务、环境、PR；没有公开的本地 hook。
- 推断：只能从 GitHub PR/check 事件推断开始与结束；Q2 不可见；Q4 无。
- 结论：不单独做；如果做，走 GitHub webhook 的「远程任务」状态。
- 证据：<https://developers.openai.com/codex/cloud>（B/D）。

### 8.4 Claude Code on the web——仅会话状态

- 形态：云端 Claude Code 会话；可与本地 CLI teleport 接力。
- 文档写了：云会话、GitHub 集成、teleport；没有对外 lifecycle hook。
- 推断：云端状态可轮询，细粒度工具事件不可得；Q4 无。
- 结论：P3。
- 证据：<https://code.claude.com/docs/en/claude-code-on-the-web>（B/D）。

### 8.5 Replit Agent——不可行

- 形态：云端 IDE + Agent；闭源；没有公开 hook/事件 API。
- 结论：不做。
- 证据：<https://docs.replit.com/>（B/C）。

## 9. P0 / P1 / P2 / P3 清单

排序依据：四问命中数 × 现成用户量 × 改动量，不按厂商热度。

### P0：两周内可交付，且能明显改善「接了但不动」

| 序号 | 目标 | 为什么是 P0 | 交付物 | 验收标准 | 估时 |
| --- | --- | --- | --- | --- | --- |
| P0-1 | Cursor（IDE + CLI） | 四问全中；registry 已有 key；hooks 可 allow/deny | `integrations/agent-plugins/cursor/` + README + 安装脚本 | 新建会话 2 秒内出现 thinking；每次 shell 前后有 tool；stop 后 2 秒内 done；权限可从桌宠批准/拒绝 | S-M |
| P0-2 | Gemini CLI | 四问中三问明确；Q4 可验证；registry 已有 key | `gemini-cli/` bridge + settings 合并安装器 | 同上；若 allow 不可用，UI 明确「仅拒绝」 | S-M |
| P0-3 | VS Code Copilot Agent Mode | 复用 `githubcopilot` key，补齐 IDE 侧空白；2026 新增 hooks | IDE hook 发送器 + 与 CLI 共用的归一化层 | 不新建重复 key；IDE 与 CLI 事件可区分 | S-M |
| P0-4 | 能力诚实化 | 4 个 key 没有发送端，是当前最大信任风险 | 设置页显示「未安装 / 已安装未连接 / 已连接」+ 最近事件时间 | 任何无发送端的 key 不再显示为「已支持」 | S |

**P0 的统一定义：** 「支持」= 安装后有事件、断开有提示、权限有明确档位。只完成 bridge 不算完成，必须在设置页可见。

### P1：一个月内，覆盖第二梯队

| 序号 | 目标 | 判断 | 交付物 | 估时 |
| --- | --- | --- | --- | --- |
| P1-1 | Windsurf | hooks 存在；权限能力待验证；先做只读状态 | `windsurf/` bridge，权限标「待验证」 | S-M |
| P1-2 | Cline / Roo / Kilo | 同源，三份 manifest 共用一套 bridge | `cline/`、`roo/`、`kilo/` 或一个多 manifest 包 | M |
| P1-3 | Factory Droid | 类 Claude hooks，接入成本低 | `droid/` bridge | S-M |
| P1-4 | ACP 代理 spike | 战略杠杆，但先验证可行性，不直接产品化 | `at-pet-acp` PoC：Zed → 代理 → Gemini CLI | M |
| P1-5 | OpenHands | 事件流完整，但要做 sidecar；权限待验证 | 事件流订阅器，只做状态 | L |
| P1-6 | Amp | CLI 流式输出可用则做；hook 待验证 | `amp/` 包装器或 bridge | M |
| P1-7 | Junie CLI | 只在有结构化输出时做 | 包装器 | M |

### P2：季度级，战略或长尾

| 序号 | 目标 | 判断 | 估时 |
| --- | --- | --- | --- |
| P2-1 | ACP 代理产品化 | spike 成功后，做成可配置的双向代理，覆盖 Zed/JetBrains/Goose/OpenCode 等 | L-XL |
| P2-2 | Goose | 只有日志/包装；等官方 hooks | M |
| P2-3 | Crush | 只能日志 tail，价值低 | M |
| P2-4 | Warp | 闭源且未发现公开接口，不建议 | - |
| P2-5 | Mistral Vibe / Grok CLI | 先核实官方形态与事件面 | M/? |
| P2-6 | ChatGPT Desktop / Claude Desktop | 仅 MCP 兜底，不做生命周期承诺 | M |
| P2-7 | Devin / Jules | 云端任务状态卡，非细粒度动画 | M |

### P3：不投入或只保留 key

| 目标 | 处理 |
| --- | --- |
| Google Antigravity | registry 保留 key，标「未连接」；等官方 hooks |
| Firebase Studio | 不做，纯云端 |
| Replit Agent | 不做，无公开接口 |
| Sourcegraph Cody | 从矩阵移除，产品已收敛 |
| LM Studio / Jan / Chatbox / Cherry Studio | 不做，没有 agent 生命周期 |
| AnythingLLM / Open WebUI | 只做 chat 级状态，不做工具级 |
| Codex Cloud / Claude Code web | 只做远程任务状态，走轮询或 PR 事件 |

### 不建议做清单（明确写进文档，避免反复讨论）

1. **窗口标题/截图嗅探。** 只能猜，不能回传权限；Windows 上还会受 DPI、遮挡、多显示器影响。
2. **MITM 代理 Claude/Cursor 的 HTTPS。** 证书固定、更新频繁、法律与安全风险都高。
3. **解析 JSONL 当主路径。** 延迟、轮转、编码、半行写入都会造成误判；只能做 P2 兜底。
4. **用 OTel 做权限。** OTel 是单向遥测，没有裁决通道；采样还会丢事件。
5. **给每个 IDE 写一个 VS Code 扩展。** 扩展只能看到自己参与的工具，接管不了内置审批；维护成本远高于 hook。
6. **把 MCP 工具当生命周期。** 模型不调用就没有事件；不能作为「支持」的证据。
7. **把 Kiro 的文件 hooks 当生命周期。** 命名相同，语义完全不同。

## 10. 适配层的工程蓝图

### 10.1 一份 bridge，多份 manifest

当前每个插件目录复制一份 `bridge.mjs`，优点是独立安装，缺点是修 bug 要改 N 份。建议收敛为：

| 层 | 职责 | 是否共享 |
| --- | --- | --- |
| `core.mjs` | HTTP 发送、重试、超时、session 去重、权限轮询 | 共享 |
| `manifest/<vendor>.json` | 事件名映射、字段路径、权限输出 schema、超时 | 每厂商一份 |
| `install.mjs` | 检测版本、合并配置、备份、卸载 | 共享 |
| `fixtures/<vendor>/*.json` | golden payload，回归测试 | 每厂商一份 |

manifest 至少包含这些字段：

```json
{
  "vendor": "cursor",
  "events": {
    "beforeSubmitPrompt": "thinking",
    "beforeShellExecution": "tool",
    "afterShellExecution": "thinking",
    "stop": "done"
  },
  "sessionIdPaths": ["conversation_id", "session_id"],
  "toolNamePaths": ["tool_name", "command"],
  "permission": {
    "event": "beforeShellExecution",
    "requestPath": "/permission",
    "decisionField": "permission",
    "allowValues": ["allow"],
    "denyValues": ["deny"]
  }
}
```

> 上面是**建议 schema**，不是厂商文档。真实字段必须以各厂商 hook 文档与抓包为准。

### 10.2 权限请求的最小协议

At-Pet 的 `/permission` 至少要携带：agentKey、sessionId、toolName、完整命令/参数、cwd、风险等级、可选决策（allow once / allow session / deny）、超时时间。返回必须带 requestId，防止旧请求覆盖新请求。

桌宠 UI 必须显示：谁在请求（agent + 子代理）、要做什么（完整命令，不只 tool 名）、在哪里做（cwd）、允许的后果。**不要做「一键总是允许」。** 默认按 session 记忆，不跨会话。

建议 `/event` 增加可选 `source`（`hook` / `jsonl-tail` / `window-sniff` / `acp` / `mcp`），前端据此决定动画强度。现有实现会忽略未知字段，这是向后兼容的无损变更。

### 10.3 失败策略

| 场景 | 策略 |
| --- | --- |
| At-Pet 未启动 | hook 立即放行（fail-open），退出码 0，不阻塞 agent |
| 权限请求超时 | 返回「未裁决」，让 agent 走自己的默认 UI；不要静默拒绝 |
| 事件发送失败 | 本地环形缓冲，最多重试 2 次；不阻塞 hook 超过 200ms |
| 重复事件 | sessionId + toolId + event + 2 秒窗口去重 |
| 子代理过多 | 每父会话最多 N 只；超出折叠为计数 |
| 版本不兼容 | manifest 带 `minVersion`/`maxVersion`，不匹配则禁用并提示 |

### 10.4 安装器

Windows 上最容易失败的不是协议，是路径与引号。安装器必须：

1. 检测 agent 版本与配置目录，找不到就明说，不写半套配置。
2. 合并已有 hooks，保留用户条目；写之前备份，卸载时只删自己的条目。
3. 用绝对路径或稳定的环境变量；PowerShell 的 `$env:` 与 cmd 的 `%VAR%` 不能混。
4. 处理空格、中文路径、OneDrive 重定向。
5. 安装后发一条 `self_test` 事件，设置页显示「已连接」。

## 11. 风险与反模式

| 风险 | 表现 | 后果 | 缓解 |
| --- | --- | --- | --- |
| 幽灵支持 | 设置里可选，实际无事件 | 用户认为桌宠坏了 | 能力状态 + 最近事件时间 + 一键自检 |
| 权限死锁 | hook 等桌宠，桌宠没开/用户在忙 | agent 卡住，用户丢失工作 | fail-open + 超时 + 明确默认 |
| 权限伪造 | 本地任意进程 POST `/permission` | 误导用户批准危险命令 | 本地 token、来源校验、展示完整命令 |
| 重复事件 | IDE 与 CLI 同时发 | 动画闪烁、会话重复 | 去重键 + 单会话单宠物 |
| 子代理爆炸 | 每个子代理开一只 | 屏幕被占满 | 父子聚合 + 上限 |
| 日志 tail 误判 | 半行、编码、轮转 | 假 tool / 假 done | 只做 P2，带 offset 与校验 |
| OTel 采样 | 关键事件丢失 | 状态卡住 | 不用 OTel 做主路径 |
| 云端延迟 | 几分钟才更新 | 长时间 thinking | 单独的「远程任务」状态 + TTL |
| hook schema 变更 | 厂商升级 | 静默失效 | manifest 版本约束 + 启动自检 |
| Node 依赖 | 用户没装 Node | bridge 不执行 | 安装器检测；长期考虑单文件二进制 |
| 阻塞过久 | hook 超时被厂商杀 | 权限请求丢失 | 权限超时 < 厂商超时；记录审计 |
| 命令展示不全 | 只显示 tool 名 | 用户误批 | 展示完整命令、cwd、参数 |
| 会话串台 | sessionId 复用 | 事件跑到别的宠物 | 用厂商原生 session id + workspace |
| 产品改名/收敛 | Cody、Windsurf | 文档与 key 过期 | 季度复核，保留「已停止维护」状态 |
| ACP 代理挂起 | 未转发 cancel/error | 编辑器卡死 | 全双工转发 + 超时 + 日志 |
| 版本协商失败 | agent 与代理 ACP 版本不匹配 | 无法启动 | 启动握手 + 明确报错 |
| 安全边界扩大 | 代理暴露到网络 | 远程可调用本地 agent | 只绑 127.0.0.1 + token |
| 过度承诺 | 文档写「支持」但只有 MCP | 信任受损 | 支持矩阵分「事件级 / 通知级 / 无」 |

### 反模式

1. **先占 key，后找发送端。** 这正是当前 4 个静默 key 的来源。
2. **把「能看到」当成「能批准」。** JSONL、OTel、日志、窗口都属于只看。
3. **用模型提示词保证调用 MCP 工具。** 提示词不是契约。
4. **为每个产品复制一份 bridge。** 修一个 bug 要改十份。
5. **在 hook 里做长时间网络等待。** 厂商会超时杀进程，且阻塞用户。
6. **把子代理当独立产品。** 子代理是会话属性，不是新 agent。
7. **用「最近事件时间」代替健康检查。** 空闲会话本来就没有事件；要区分 idle 与 broken。
8. **在权限弹窗里隐藏命令。** 这是安全产品，不是通知产品。
9. **把云端任务映射成细粒度工具动画。** 数据不够，必然是假的。
10. **把 ACP 当广播总线。** 不在连接上就没有事件。

## 12. 待验证清单（按排期顺序）

以下每一条都对应一个会影响档位的具体问题。核实前不要写进用户文档的「支持」列。

| 优先级 | 对象 | 要核实的问题 | 官方入口 | 影响 |
| --- | --- | --- | --- | --- |
| P0 | Cursor | hooks 的完整事件名、配置优先级、allow/deny 是否阻塞、CLI 与 IDE 是否共用 | <https://cursor.com/docs/agent/hooks> | 决定 Q4 是否全中 |
| P0 | Gemini CLI | `BeforeTool` 能否返回 allow；hook 超时；settings 合并规则 | <https://github.com/google-gemini/gemini-cli/blob/main/docs/hooks/index.md> | 决定 Q4 是「全回传」还是「仅拒绝」 |
| P0 | VS Code Copilot | agent hooks 是否已 GA/预览、配置位置、能否接管内置审批 | <https://code.visualstudio.com/docs/copilot/overview> | 决定 IDE 侧是否值得做 |
| P0 | Copilot CLI | 是否支持 `permissionDecision`；与 IDE 的事件是否重复 | <https://docs.github.com/en/copilot/how-tos/copilot-cli> | 现有接入的权限档位 |
| P0 | OpenCode | `permission.ask` 是否可由插件裁决；V2 插件生命周期 | <https://opencode.ai/docs/plugins/> | 现有接入的权限档位 |
| P1 | Windsurf | hooks 是否能 deny/allow；配置文件路径与版本 | <https://docs.windsurf.com/windsurf/cascade/hooks> | 是否升到 P0 |
| P1 | Cline | PreToolUse 的 `cancel` 之外是否有 allow；hook 超时 | <https://docs.cline.bot/features/hooks> | 权限是双向还是单向 |
| P1 | Roo Code | 与 Cline 的字段差异；模式对工具集的影响 | <https://docs.roocode.com/features/hooks> | 能否共用 manifest |
| P1 | Droid | hooks 事件全集、配置路径、权限输出 | <https://docs.factory.ai/> | 能否复用 Claude bridge |
| P1 | ACP | 哪些 agent 已发布 ACP server 模式；Windows 支持；版本 | <https://agentclientprotocol.com/> | 代理的产品化范围 |
| P1 | OpenHands | 事件流协议、确认 API、Windows Docker 可用性 | <https://docs.all-hands.dev/> | 是否值得 L 级投入 |
| P1 | Amp | CLI 是否有 stream-json / hooks | <https://ampcode.com/manual> | 桥接还是包装 |
| P2 | Goose | 是否有 lifecycle hooks；会话库 schema | <https://block.github.io/goose/docs/> | 日志方案是否可行 |
| P2 | Junie | CLI 结构化输出；IDE 审批能否被插件接管 | <https://www.jetbrains.com/help/junie/> | 是否只能包装 |
| P2 | Devin | webhook 是否存在；session 状态粒度 | <https://docs.devin.ai/> | 轮询还是推送 |
| P2 | Jules | activities 粒度；API 配额 | <https://jules.google/docs/> | 通知级是否值得 |
| P2 | ChatGPT Desktop | 是否有公开 app-server/事件接口 | <https://developers.openai.com/codex/app> | 桌面 app 是否可做 |
| P3 | Antigravity | 是否发布 lifecycle hooks | <https://antigravity.google/docs> | 是否保留 key |
| P3 | Kiro | 是否有会话级 hooks（区别于文件 hooks） | <https://kiro.dev/docs/hooks/> | 是否永远不可行 |
| P3 | Mistral Vibe / Grok CLI | 官方形态、hook、MCP、结构化输出 | 官方仓库/文档待定位 | 是否进入矩阵 |

### 12.1 核实方法

1. 只信官方文档与官方仓库，不信聚合站、博客、二手截图。
2. 每一条结论记录「文档原文关键词 + 抓取日期 + 版本号」。
3. 能用本地安装验证的，优先跑一个最小 payload：发一条 prompt、跑一个 shell、触发一次权限，保存原始 JSON。
4. 厂商升级后，golden payload 回归失败即视为不兼容，不要让 bridge 静默降级。

## 13. 支持等级怎么展示

建议把设置页从「支持/不支持」改成四档，直接对应本文的判定：

| 等级 | 文案 | 含义 | 允许的动画 |
| --- | --- | --- | --- |
| L0 | 未连接 | 没有发送端，或未安装 | 无 |
| L1 | 状态同步 | 只有会话开始/结束，或只有 MCP | thinking / done / idle |
| L2 | 工具级 | 有工具前后与回合结束，但权限只能看 | 全部状态，权限只通知 |
| L3 | 权限接管 | 四问全中，可批准/拒绝 | 全部 + 权限弹窗 |

当前仓库的映射建议：Claude Code = L3；Codex = L3；Cursor = L3（核实后）；Gemini CLI = L2 或 L3（取决于 allow）；Copilot CLI = L2；OpenCode = L2；Cursor/Windsurf/Gemini/Antigravity 中未安装发送端的显示 L0，而不是「已支持」。

## 14. 90 天路线

| 周 | 目标 | 产出 |
| --- | --- | --- |
| 1-2 | P0-4 能力诚实化 + Cursor | 设置页四档、最近事件、自检；Cursor bridge |
| 3-4 | Gemini CLI + VS Code Copilot | 两个 bridge，权限档位按核实结果标注 |
| 5-6 | Cline/Roo/Kilo 共用适配 | 一套 core + 三份 manifest + golden fixtures |
| 7-8 | Droid + Windsurf | 只读状态优先，权限第二阶段 |
| 9-10 | ACP 代理 spike | Zed → 代理 → Gemini CLI 跑通；录屏与失败案例 |
| 11-12 | 稳定性与文档 | 安装器、卸载、超时、去重、支持矩阵、用户文档 |

## 15. 证据与版本说明

- 本仓库事实：`docs/agent-compatibility.md`、`app/agentregistry.h`、`integrations/agent-plugins/*`、`integrations/copilot-cli/*`（A）。
- 协议事实：ACP / AG-UI / A2A 的本轮检索结果（A）；具体实现者名单以各项目官方仓库为准（B/C）。
- 产品事实：官方文档入口（B）；未逐页复核的细节（C/D）。
- 本文日期：2026-10-25。厂商 hooks 多为近一年新增，建议每季度复核一次，并在文档里保留「最后核验日期」。

### 15.1 官方入口汇总

| 类别 | 入口 |
| --- | --- |
| Claude Code hooks | <https://code.claude.com/docs/en/hooks> |
| Codex | <https://developers.openai.com/codex/> |
| Copilot CLI | <https://docs.github.com/en/copilot/how-tos/copilot-cli> |
| OpenCode plugins | <https://opencode.ai/docs/plugins/> |
| Gemini CLI hooks | <https://github.com/google-gemini/gemini-cli/blob/main/docs/hooks/index.md> |
| Cursor hooks | <https://cursor.com/docs/agent/hooks> |
| Windsurf hooks | <https://docs.windsurf.com/windsurf/cascade/hooks> |
| Cline hooks | <https://docs.cline.bot/features/hooks> |
| Roo hooks | <https://docs.roocode.com/features/hooks> |
| ACP | <https://agentclientprotocol.com/> |
| AG-UI | <https://docs.ag-ui.com/> |
| A2A | <https://a2a-protocol.org/> |
| MCP | <https://modelcontextprotocol.io/> |

> 说明：上表是入口，不等于每个子页面都已核验。引用具体事件名时，请以对应版本的官方页面为准，并在 PR 里附抓取日期。
