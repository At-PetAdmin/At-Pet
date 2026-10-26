# At-Pet Agent 适配清单

> 初稿 2026-10-25。第一、二、四节已完成核对（源码 / 官方文档为准）；第五节市场全景仍在调研中。

## 一、先把「兼容」的定义钉死

At-Pet 不是某个 agent 的插件，而是一个只吃**归一化事件**的动画状态机。所以「能不能兼容」不取决于这个 agent 有多火，而取决于它能不能在四个时机把信号递出来。接入路径有四条，按侵入性从低到高：

| 路径 | 机制 | 现有实例 | 代价 |
| --- | --- | --- | --- |
| 原生 HTTP | agent 侧直接 POST 127.0.0.1:8765/event | 任何能发 HTTP 的脚本 | 需自写发送器 |
| 原生适配器端点 | POST /agent-event/<key>，由 C++ AgentMonitorStrategy 归化该 agent 的私有 payload | Claude Code、OpenCode V2 | 需新增 C++ 策略 |
| 桥接脚本 | 零依赖 bridge.mjs 把 agent 的 hook / settings 事件翻译成 /event | Codex、Copilot CLI、WorkBuddy、Command Code、ZCode、DeepSeek Harness、PenguinHarness | 只需配置 |
| MCP stdio 桥 | agent 主动调用 report_agent_event 工具 | 任意 MCP 客户端 | 模型不保证调用 |

另有 POST /permission：把权限询问弹到桌宠上，用户在桌宠里批准 / 拒绝后回传给 agent。这是 At-Pet 相对普通通知工具的核心差异，也是判断「深适配」的唯一硬指标。

### 事件词表（app/main.cpp 已实现，大小写与下划线不敏感）

| 桌宠动画 | 可识别的 event token |
| --- | --- |
| thinking | started / session_start / turn_start / user_prompt_submit / assistant_message / model_thinking / tool_end / post_tool_use |
| tool | tool_start / pre_tool_use / tool_call / tool_use / tool_running |
| permission | permission_requested / permission_approved / permission_denied |
| work | work |
| review | review / reflection |
| done | completed / done / stop / turn_complete / agent_turn_complete |
| cancelled | cancelled / canceled / aborted / interrupt / interrupted |
| error | error / failed / exception / api_error |
| 移除会话 | idle / session_end / session_ended |

子代理分窗靠 parentSessionId / parent_session_id / isSubagent / subagent，命中任一即视为子会话，单独开一只桌宠。

### 判定档位的四问

对任意 agent，问这四个问题就能定档，不需要看它有多流行：

1. **会话开始**能不能发信号？（映射到 thinking）
2. **工具调用前后**能不能发信号？（映射到 tool）
3. **回合结束**能不能发信号？（映射到 done。缺这一条，桌宠会永远卡在 thinking 或 tool 上，这是最常见的「接了但没用」）
4. **权限询问**能不能被截获并把裁决回传？（映射到 permission）

四问全中 = 原生直连。拿到 1–3 = 桥接可做。只拿到 1 或全靠猜 = 强行兼容。一个都拿不到 = 不可行。

## 二、现状矩阵

app/agentregistry.h 注册了 18 个 agent 标识，只有 3 种监控器类型，但每条记录还带一个交付状态（BuiltInAdapter / ProfileShipped / ExternalRequired），设置界面会把它直接写出来。

| agent key | 显示名 | 接入物 | 交付状态 | 权限回传 |
| --- | --- | --- | --- | --- |
| claudecode | Claude Code | C++ 适配器 + HTTP hooks | 内置 | 有 |
| opencode | OpenCode | C++ 适配器 + TS 插件 | 内置 | 无 |
| openaicodex | OpenAI Codex | bridge.mjs，hooks.json 订阅了 PermissionRequest | 已附带 | 有 |
| githubcopilot | GitHub Copilot | bridge.mjs（CLI hooks） | 已附带 | 视配置 |
| workbuddy | WorkBuddy | bridge.mjs，随附配置未订阅 PermissionRequest | 已附带 | 需补配置 |
| commandcode | Command Code | bridge.mjs | 已附带 | 视配置 |
| zcode | ZCode | bridge.mjs，随附配置未订阅 PermissionRequest | 已附带 | 需补配置 |
| deepseekharness | DeepSeek Harness | bridge.mjs | 已附带 | 视配置 |
| penguinharness | PenguinHarness | bridge.mjs | 已附带 | 视配置 |
| kimicode | Kimi Code | agent-bridge profile + config.toml 模板 | 已附带 | 无（只能拒绝） |
| trae | Trae | agent-bridge profile（hooks.json） | 已附带 | 无（仅观察） |
| qoder | Qoder | agent-bridge profile（settings.json） | 已附带 | 有 |
| qodercn | Qoder CN | agent-bridge profile（原通义灵码） | 已附带 | 有 |
| qwencode | Qwen Code | agent-bridge profile（.qwen/settings.json） | 已附带 | 有 |
| cursor | Cursor | 无 | 无发送端 | 无 |
| windsurf | Windsurf | 无 | 无发送端 | 无 |
| geminicli | Gemini CLI | 无 | 无发送端 | 无 |
| antigravity | Antigravity | 无 | 无发送端 | 无 |

三个结论：

- **18 个标识里 14 个已有发送端**，接入物是 `integrations/agent-plugins/` 下 10 个目录加 `agent-bridge` 的 14 份 profile；C++ 原生适配器仍然只有 2 个。新增 agent 的成本已经被压到「写一份 JSON」。
- **四个无发送端项现在会自己说出来**。设置界面里 Cursor / Windsurf / Gemini CLI / Antigravity 的条目会显示「尚未内置发送端，配置发送端前不会有事件」，不再让人误以为已经接上。
- **权限回传要按实际订阅算，不能按能力算**。WorkBuddy 和 ZCode 的 bridge 里写着 `askPermission()`，但随附的 hooks.json 没订阅 `PermissionRequest`，所以现状是「能，但没接上」。这一格必须改配置才会变成「有」。

## 三、优先级清单

排序依据是「四问命中数 × 改动量」，不是流行度。

### P0 — 已有官方 hook，缺的只是一个 bridge 目录

| 目标 | 理由 | 做法 | 改动面 |
| --- | --- | --- | --- |
| Cursor | registry 里已有 key，官方有 agent hooks（shell / MCP 执行前后、回合结束等），且 hook 可返回 allow / deny，四问可全中 | 新增 integrations/agent-plugins/cursor/bridge.mjs，复用现有 bridge 模板 | 一个新目录 + README |
| Gemini CLI | registry 里已有 key，官方有 hooks 与 extensions 两层扩展面 | 同上；若 payload 结构差异大，可在 C++ 侧加策略 | 一个 bridge，或加一个 AgentMonitorType |
| VS Code Copilot agent | 复用现有 githubcopilot key，补齐 IDE 侧缺口；现有实现只覆盖 CLI | 新增一个 hook 发送器，与 CLI 共存 | 一个 hook 脚本 + 文档 |

这三项的共同点：**四问里的第 3 问（回合结束）由官方 hook 直接提供**，不需要猜。

注意第五节：如果走 ACP 路线，Cursor 与 Gemini CLI 这两项可以并入同一次实现，不必各写一个 bridge。两条路不冲突，先落 hook 见效快，ACP 决定长期形态。

### P1 — 有扩展面但需要额外推导

- 只有 MCP、没有 hook 的 agent：可以接，但模型不一定调用工具，只能做提示性上报。适合当兜底，不适合当主路径。
- 只有本地日志 / transcript 的 agent：需要轮询 + 偏移量，能拿到工具级事件，但回合结束要靠启发式判断。

### P2 / P3 — 见第四节技术矩阵与第五节市场全景

### 覆盖现状：别把「list 里有」当成「接上了」

这三件事必须分开说，否则一定会误判：

| 层次 | 含义 | 当前数量 |
| --- | --- | --- |
| registry 里有 key | 设置界面可识别、可按来源过滤 | 18 |
| 有发送端（bridge / 适配器 / profile） | 真能产生事件 | 14 |
| 有权限回传 | 事件能落到 /permission 且裁决能回传 | 2 家随附配置已订阅，9 家需自己在 agent 侧加 hook，3 家只能观察 |

14 个有发送端的是：Claude Code、OpenCode、OpenAI Codex、GitHub Copilot、WorkBuddy、Command Code、ZCode、DeepSeek Harness、PenguinHarness、Kimi Code、Trae、Qoder、Qoder CN、Qwen Code。开箱就能弹权限并回传的是 Claude Code、Codex、Qoder、Qoder CN、Qwen Code；GitHub Copilot、WorkBuddy、Command Code、ZCode、DeepSeek Harness、PenguinHarness 的 bridge 具备能力，但随附配置没订阅对应 hook。

剩余四个（Cursor、Windsurf、Gemini CLI、Antigravity）有 key、无发送端。它们不是「漏了」，是**还没拿到真实 payload**：写 profile 需要观察一次实际 hook 输出，否则字段路径一错就是静默失效。

再往外一圈是市场全景里点名、但我们完全没接的：Factory Droid、Cline、Roo Code、Kilo Code、Amp、Goose、OpenHands、Devin、Jules、Trae、Qoder、通义灵码、Comate、CodeGeeX、CodeArts、Qwen Code、GLM、MiniMax、iFlow 等。

所以「是否遗漏」这个问题目前只能这样回答：**覆盖是动态的，不存在某个版本叫「已全部兼容」**。能做的是把新增成本压到最低——现在接一家是「抓一份 payload → 写一份 JSON → `--print` 验证」，`--capture` 可以把原始 payload 直接落盘，30 分钟级别。守住「没观察过就不写 profile」这条，比追一个完整名单更耐用。

### 已落地：通用桥接引擎

`integrations/agent-bridge/` 已经按「一个引擎 + 声明式 profile」建好，新增 agent 不再复制 bridge。

- 引擎 `bridge.mjs`：读 stdin payload → 按 profile 解析字段 → POST /event；命中权限 hook 时走 /permission 并把裁决按厂商格式写回 stdout。
- 10 份 profile：claude-code、opencode-v2、openai-codex、github-copilot、workbuddy、command-code、zcode、deepseek-harness、penguin-harness、kimi-code。前九份的字段路径对照 `app/agentadapters/*/adapter.cpp` 与各插件自己的 `bridge.mjs`；kimi-code 对照 Moonshot 官方 hooks 文档，没有推测。
- 23 项测试通过（连跑三轮稳定），其中包括「At-Pet 没开时进程仍返回 0」这条硬要求。
- **未核实的产品不写 profile**：字段路径写错不会报错，只会静默不出事件，那比不支持更糟。这条写进了 README，也请后续新增 profile 时守住。

迁移是渐进的：现有 `integrations/agent-plugins/*` 保持不变，把某一家切过来只是改一行 hook 命令。

## 四、「强行兼容」技术矩阵

先立一条硬约束，后面所有排序都由它决定：**只有三条路能把权限裁决真正回送给 agent** —— hook 返回 decision（PreToolUse / PermissionRequest）、MCP 的阻塞式工具调用（stdio 天然可以挂起等用户）、ConPTY 把按键写回伪终端。JSONL tail、OTel、MITM、窗口嗅探、IDE 扩展、统一网关、剪贴板，全部只能「看到」不能「回送」。

这条约束直接决定产品形态：**能弹权限气泡的 agent 和只能转圈的 agent，必须在 UI 上区分开**。如果桌宠显示了「是否允许执行」，用户点了同意却回不到 agent，agent 会一直挂着等 —— 这比根本不显示权限更糟。

| 手段 | 难度 1-5 | 覆盖 | 保真度 | 权限回传 | 结论 |
| --- | --- | --- | --- | --- | --- |
| 官方 hook / plugin | 2 | 现有 9 家 + 有 hook 的新产品 | 权限级 | 可（唯一正规路径） | 基线，必做 |
| MCP 工具上报 | 2 | 最广（MCP 生态） | 工具级，模型驱动 | 可（阻塞式工具） | 广度底座，必做 |
| JSONL / transcript tail | 3 | 4–6 家 | 会话级 + 工具级（事后） | 否 | 兜底补漏 |
| ConPTY 包装 | 5 | 所有 CLI | 会话级可，工具级靠文本猜 | 可（写按键） | 受限做，标注「从桌宠启动」 |
| OpenTelemetry 旁路 | 3 | 3–6 家 | 工具级（开 tracing 时） | 否 | 做，用于交叉校验 |
| HTTPS MITM | 5 | 理论全覆盖 | 模型调用级 | 否 | 不做 |
| 进程 / 窗口嗅探 | 1 | 全部 | 忙 / 闲 | 否 | 做，仅做心跳 |
| IDE 薄扩展 | 3-4 | VS Code 系约 8 个 | 终端命令级 | 否 | 做，只做终端信号 |
| 剪贴板 / FS watcher | 1 | 全部 | 噪声 | 否 | 不做主通道 |
| 统一网关 | 3 | 自愿配置者 | 会话级 | 否 | 做，作为可选路径 |
| ETW / UIA / OCR | 4 | 视手段 | 弱 / 文本 | 仅 UIA 可点 | 仅实验 |

### 值得落地的，各自的真实工程细节

**JSONL tail** 是无 hook 产品最现实的兜底，但只做事后补录。Qt 侧有三个坑必须处理：监听目录而不是只监听文件（很多工具是写完 rename 或新建文件，只 watch 文件会在替换后失效）；每个文件维护 path + offset + 已见 uuid，最后一段不完整就丢弃并回退 offset，否则半行 JSON 会被当成解析失败；文件被写入方独占时退避重试。

回合结束的判定是最容易出错的地方。误判代价不对称：**早判「结束」会让桌宠提前庆祝，晚判只是多转两秒**。所以默认去抖偏长（约 800ms），并且要求「未闭合的工具调用栈为空」才发 completed，否则只发 thinking。压缩摘要那类条目必须跳过。同一 session 若 hook 通道已在报，tail 必须关闭，否则工具动画会重放两次。

**ConPTY** 在 Windows 上是官方能力，Qt6 + MSVC 直接调 Win32 API 即可，不需要第三方库。难的不是建伪终端，是**把字节流变成语义**：Ink / React 类 TUI 大量使用光标移动和区域重绘，原始流里 Thinking 字样会出现几十次且被覆盖，必须先维护字符网格渲染出「当前屏幕」再在屏幕文本上匹配 —— 等于自己写一个最小终端模拟器。另外 npm 全局安装的 CLI 在 Windows 上是 .cmd shim，要经 cmd.exe 启动；Node / Python 要靠 isatty 判断才会开彩色和 TUI；按键映射必须按产品分别配置，按错键等于替用户批准了工具执行。安全底线：只回传用户的明确点击，绝不自动批准，送键前再校验屏幕文本仍包含该提示。

**OTel 旁路** 的价值不是覆盖新 agent，是给 hook 做交叉校验 —— hook 漏报时补上工具 span，反之亦然。落地可以在进程内开 QTcpServer 监听 4318 走 OTLP/HTTP + JSON，用 QJsonDocument 解析，避免在 C++ 里引 protobuf；也可以旁挂 otelcol-contrib，代价是第二个进程要管生命周期。注意 gen_ai 语义约定改过属性名（gen_ai.system 改成 gen_ai.provider_name 一类），读取要兼容多版本。

**IDE 扩展** 有一个容易误判的前提：**一个 VS Code 扩展读不到别家 agent 的对话内部**。Chat / Language Model API 是给扩展自己实现 participant 用的，没有开放「监听 Copilot Chat 的 turn」。Cursor / Windsurf / Trae / Kiro / Qoder 是 VS Code 分支，同一个扩展装得上、公共 API 能用，但依然看不到它们私有的 chat 状态。真正能拿到的是**集成终端里命令的起止**（shell integration）—— 如果用户在 IDE 终端里跑 CLI，这就有工具级价值。所以薄扩展的定位应该是「终端命令 + 文件活动 + 心跳」，不承诺 chat 内部。

**进程 / 窗口嗅探** 难度最低，覆盖全部，但只有忙 / 闲。它最好的用途不是当状态源，而是**发现「某 agent 正在跑却没有任何通道上报」**，然后让 UI 降级成弱感知模式，而不是装作一切正常。这也正好补上第二节那四个静默项。

### 明确不做的

MITM：能拿到模型调用级信息，但证书链在各运行时表现不一（有些用 rustls + 内置根证书的客户端装了证书也连不上），企业 EDR 会拦，且能看到 prompt 全文，合规成本高于收益。OCR：每帧几百毫秒 CPU，主题缩放字体全影响准确率。Named Pipe 探测：协议私有，投入产出比极差。剪贴板当状态源：噪声压倒信号。

## 五、最大杠杆：ACP（Agent Client Protocol）

这是这轮调研最值得单独拎出来的发现。

Zed 维护的 ACP 生态页（2026-10-25 抓取）列出的实现者里，有 Cursor、Gemini CLI、GitHub Copilot、Google Antigravity、Codex CLI、Claude Agent、Cline、Goose、Factory Droid、OpenHands、Qoder CLI、Qwen Code、Kimi CLI、GLM Agent、MiniMax Code、Kilo、JetBrains Junie、Kiro CLI、Augment Code、Mistral Vibe、Amp、Devin、Poolside、CodeBuddy、OpenCode（V1 / V2）等约 50 个。注意：第二节里那四个「占了名字但静默」的项，**Cursor、Gemini CLI、Antigravity 三个都在这份名单里**。

ACP 是 JSON-RPC 2.0 over newline-delimited stdio，有三处直接对上我们的需求：

- **session/update**（agent → client 的单向通知）：sessionUpdate 里有 agent_message_chunk、tool_call_update，后者带 toolCallId 和 status（in_progress / completed / failed）。这正是我们缺的 tool_start / tool_end，而且是结构化的，不用猜。
- **session/request_permission**（agent → client 的请求，必须应答）：带 title、description、subject.toolCall、options（例如 allow_once / deny）。**这是真正的权限回传**，不是「看到」而是「裁决」。
- **initialize / session/new / session/prompt** 构成会话骨架，session/prompt 的响应即一个回合的结束边界。

代价必须说清楚：ACP 里 client 是**发起方**，由 client 拉起 agent 进程。所以 At-Pet 只有两种用法 —— 当 client（从桌宠启动 agent），或者插在 Zed 与 agent 之间做 stdio 代理。它没法变成「用户在 Cursor 里自己用、桌宠在旁边看」的透明旁路。

但这个限制和 ConPTY 封装是同一类，而 ACP 的保真度高出整整一档：不用解析 ANSI、不用重建屏幕、权限是协议原生的。如果只押一个「一次接很多家」的方案，我押 ACP。

两种落法：

1. **client 模式**：桌宠负责 spawn agent、跑 session/new → session/prompt，把 session/update 映射成 thinking / tool / done，request_permission 弹到桌宠上。等于把桌宠变成 agent 宿主。
2. **proxy 模式**：at-pet-proxy -- <agent 命令>，在 Zed 里把 agent 命令换成它，透明转发 JSON-RPC 并旁路上报。用户仍留在 Zed 里，桌宠只做观察和小型裁决。

风险：ACP 已经出过 v1 → v2 的 schema 迁移（官方有 migration 文档），要按 protocolVersion 协商；不是每个实现都完整支持 request_permission，需要逐家实测。
## 六、市场全景

分两路在做：国产 / 区域产品（Trae、Qoder、通义灵码、Comate、CodeGeeX、CodeArts、Kimi CLI、iFlow、Qwen Code、GLM、MiniMax 等），以及欧美与协议层（Gemini CLI、Cursor、Windsurf、Cline / Roo、Amp、Droid、Goose、Devin、Jules，外加 ACP / AG-UI / A2A / MCP 这几个通用协议里哪个最值得押注）。

欧美与协议层已补成完整总表：[agent-market-survey-2026-10.md](agent-market-survey-2026-10.md)。判定标准仍是第一节的四问与第四节的技术矩阵；国产 / 区域产品线待补。

### 已采纳的契约变更：source 字段

已落地：agent-bridge 在每条事件里带 source（等于 profile id，例如 qwen-code、kimi-code），main.cpp 把它存进会话并在 /state 里原样输出；旧发送器不带这个字段也不会出问题。这样前端能区分「hook 直报」与将来的「弱信号推断」，接 JSONL tail 或窗口嗅探时不必再改协议。

### 国产 / 区域产品要点（2026-10 核对）

这轮单独把中文区产品过了一遍，结论按「值不值得做」排序：

| 产品 | 结论 | 关键事实 |
| --- | --- | --- |
| Qwen Code | 已接 | hooks 事件完整，`PermissionRequest` 返回 `decision.behavior`，配置在 `.qwen/settings.json` |
| Qoder / Qoder CN | 已接 | 20+ 事件，含 `PermissionRequest`；通义灵码已于 2026-05 更名 Qoder CN，旧 Lingma v0.11 没有权限事件 |
| Trae | 已接，仅观察 | `hooks.json` 支持 6 类事件，`PreToolUse` 可返回 allow/deny/ask；但每次工具调用都弹窗不现实，授权仍留在 Trae |
| Kimi Code | 已接，仅观察 | hooks 18 类事件，但文档只公开 `permissionDecision: deny`，没有允许路径；完整裁决要走 `kimi web` 的 REST/WS 或 ACP |
| 华为 CodeArts Agent | 待做（S） | OpenCode 风格插件，`tool.execute.*`、`turn.end`、`permission.ask` 齐全；没有独立 SessionStart，用首条 `chat.message` 代替 |
| CodeBuddy / WorkBuddy | 待补配置 | `PermissionRequest` 存在，但随附 `hooks.json` 没订阅，所以现在只有动画没有回传 |
| ZCode | 待补配置 | 同上；另外项目级 `.zcode` 配置目前会被整体忽略，只能写用户级 |
| 文心快码 / CodeGeeX / 讯飞 iFlyCode | 暂不做 | 公开文档里没有 hooks；只有 MCP 和补全，四问只能命中半个 |
| MarsCode | 不做 | 文档站已指向 TRAE Plugin，产品并入 Trae 生态，单独适配只会重复维护 |
| iFlow CLI | 不做 | 2026-04-17 已停止服务 |
| GLM Coding Plan | 不做适配器 | 它是模型订阅不是 agent，按宿主（Claude Code / OpenCode / Cline）复用已有接入 |
| Coze / Dify | 不做深适配 | 不是 coding agent，没有 turn/tool 生命周期，只能当 MCP 演示 |

两条通用规律，写下来免得下次再踩：

- **同一家产品的 IDE 版和 CLI 版往往不是一套事件**。Qoder 的 Hooks 页写 `~/.qoder/settings.json`，权限页写 `~/.qoder-cn/settings.json`，很可能是两个运行时。接入时优先用项目级配置，别猜用户级路径。
- **「支持 hooks」不等于「能回传权限」**。Kimi、CodeBuddy、ZCode、Trae 都有阻断能力，但公开的允许路径各不相同。凡是只能看不能批的，UI 上就不要给可点的允许按钮 —— 这条已经写进第五节。
## 七、风险与反模式

- **接了 thinking 但没接 done** 是最坏的情况：桌宠会一直转圈，比不接更让人烦躁。任何新接入必须先验证回合结束能落在 done 上。
- **靠窗口标题嗅探**只能做朝向这类弱信号，不能当状态源。
- **每个 agent 复制一份 bridge** 会让修 bug 变成 9 次重复劳动；新目录应尽量由同一份模板生成。
- 端口 8765 无鉴权，只允许本机；任何新增发送器都要遵守「只连 loopback」这条约束。
- **订阅制提供商随时可能 403**：接入文档要写清「桌宠没开 / 端口不通」时的降级行为，hook 侧不能因为发不出事件就阻塞 agent。

