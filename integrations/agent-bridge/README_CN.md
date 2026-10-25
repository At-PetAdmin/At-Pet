# At-Pet Agent Bridge（通用桥接）

把任意 agent 的 hook payload 归一化成 At-Pet 事件：一个引擎，每个 agent 一份 JSON。

## 为什么要有它

`integrations/agent-plugins/` 下每个插件都自带一份 `bridge.mjs`。其中七份几乎是同一个文件：同样的 hook 表、同样的权限流程、同样的 stdin 处理。接入一个新 agent 意味着复制 5 KB 再改一个常量，修一个 bug 要改七遍。

这个目录把这件事换成「一个引擎 + 声明式 profile」。agent 从此是数据：各家真正的差异只有三点 —— 会话 id 放在哪个字段、hook 名放在哪个字段、hook 如何映射成事件。

## 环境要求

- Node.js 20 或更新（引擎用到 `node:test` 与 `AbortSignal.timeout`）。
- At-Pet 正在运行且本地 HTTP API 可用，默认 `http://127.0.0.1:8765`。

## 用法

```powershell
'{"session_id":"s1","hook_event_name":"PreToolUse","tool_name":"Read"}' | node integrations/agent-bridge/bridge.mjs --profile claude-code
```

| 参数 | 含义 |
| --- | --- |
| `--profile <id>` | profile 名，对应 `profiles/` 下的文件。 |
| `--hook <name>` | payload 里没有 hook 名时手动指定。 |
| `--print` | 打印归一化后的 body，不发请求。 |
| `--capture <file>` | 把原始 payload 追加写入 JSONL，用于校准。 |
| `--url <base>` | 覆盖 At-Pet 地址（也可用环境变量 `AT_PET_URL`）。 |
| `--strict` | 失败时返回非零。不加则始终返回 0。 |

位置参数写法 `node bridge.mjs <profile> [HookName]` 等价，方便改现有 hook 配置时少改几个字符。

## 已核实的 profile

| profile | agentApp | 字段契约对照来源 | 权限回传 |
| --- | --- | --- | --- |
| `claude-code` | `claude-code` | `app/agentadapters/claude-code/adapter.cpp` | 有 |
| `opencode-v2` | `opencode` | `app/agentadapters/opencode-v2/adapter.cpp` | 无 |
| `openai-codex` | `openai-codex` | `integrations/agent-plugins/openai-codex/bridge.mjs`、`hooks/hooks.json` | 有 |
| `github-copilot` | `github-copilot` | `integrations/agent-plugins/github-copilot/bridge.mjs` | 有 |
| `workbuddy` | `workbuddy` | `integrations/agent-plugins/workbuddy/bridge.mjs` | 有 |
| `command-code` | `command-code` | `integrations/agent-plugins/command-code/bridge.mjs` | 有 |
| `zcode` | `zcode` | `integrations/agent-plugins/zcode/bridge.mjs` | 有 |
| `deepseek-harness` | `deepseek-harness` | `integrations/agent-plugins/deepseek-harness/bridge.mjs` | 有 |
| `penguin-harness` | `penguin-harness` | `integrations/agent-plugins/penguin-harness/bridge.mjs` | 有 |
| `kimi-code` | `kimi-code` | Kimi Code CLI 官方 hooks 文档（`~/.kimi-code/config.toml`） | 无 |
| `trae` | `trae` | Trae IDE 官方 hooks 文档；仅观察，授权留在 Trae 内 | 无 |
| `qoder` | `qoder` | Qoder 官方 hooks 文档（`PermissionRequest`） | 有 |
| `qoder-cn` | `qoder-cn` | Qoder CN（原通义灵码） | 有 |
| `qwen-code` | `qwen-code` | Qwen Code 官方 hooks 文档（`.qwen/settings.json`） | 有 |
| `_claude-hook-family` | — | 公共基座，不直接运行 | — |

表里没有的 agent 一律视为未核实。**不要给没有观察过 payload 形状的 agent 写 profile**：字段路径写错不会报错，只会静默不出事件，那比不支持更糟。

## profile 字段（以下就是引擎实际读取的全部内容）

| 键 | 类型 | 含义 |
| --- | --- | --- |
| `id` | string | profile 名。 |
| `extends` | string | 父 profile，递归合并，子级优先。 |
| `agentApp` | string | 作为 `agentApp` 上报，At-Pet 会归一化成 registry key。缺省等于 `id`。 |
| `description` | string | 说明文字，引擎不读。 |
| `hook.paths` | string[] | hook 名的候选路径。 |
| `session` | string[] | 会话 id 的候选路径。 |
| `model` / `toolName` / `message` / `cwd` | string[] | 对应字段的候选路径。 |
| `subagent.flag` | string[] | 值为字符串 `true` 即视为子代理。 |
| `subagent.hooks` | string[] | 出现即代表子代理的 hook 名。 |
| `subagent.idFrom` | string[] | 子代理 id，拼成 `父会话:子id`。 |
| `subagent.parentFrom` | string[] | 显式父会话 id，存在即视为子代理。 |
| `events` | object | 归一化 hook 名 → 规则，或规则数组。 |
| `permission` | object | 存在即启用该 profile 的权限回传。 |
| `limits` | object | `sessionId` / `toolName` / `message` 截断长度，默认 256 / 160 / 180。 |

规则形如 `{ "event": "tool_start", "when": { ... }, "messageFrom": "toolName", "messagePaths": [ ... ], "enabled": false }`。给数组时按顺序取第一条 `when` 命中的规则；没有 `when` 的规则永远命中。

`when` 支持 `path`，外加 `equals`、`in`（数组）、`includes`（子串）、`exists`（布尔）。

**事件键是归一化的**：转小写并去掉所有非字母数字，`tool.execute.after` 要写成 `toolexecuteafter`。写错不会报错，只会静默跳过，所以务必用 `--print` 验证。

字段路径用点号和数组下标：`tool_input.command`、`tool_input.files[0].path`。原型链上的键会被拒绝。

对象与数组会转成紧凑 JSON，和 `app/agentadapters/*/adapter.cpp` 里 `compactValue` 的行为一致。

## 新增一个 agent

1. 先拿到真实 payload。厂商文档给样例最好，否则把 hook 临时指向一个本地监听跑一次。
2. 复制 `profiles/claude-code.json` 为 `profiles/<名字>.json`，改 `id` 与 `agentApp`；如果 payload 遵循 Claude hook 形状，保留 `extends`。
3. 补 `events` 映射，逐个 hook 用 `--print` 验证。
4. 把厂商 hook 指向 `node <绝对路径>/bridge.mjs --profile <名字>`。

payload 形状还不确定时，先用 `--capture payload.jsonl` 跑一次真实 hook，看看到底收到了什么。猜字段是十分钟的事，抓一次是两分钟的事。

提 PR 时请把「真实 payload + `--print` 输出」一起附上，这是证明字段路径正确最便宜的方式。

## 行为保证

- At-Pet 没开、payload 损坏、hook 未映射时，进程一律返回 0。桥接不能把 agent 卡住。测试里想看失败请加 `--strict`。
- HTTP 请求 2 秒超时，慢机器可用 `AT_PET_TIMEOUT_MS` 放宽（测试套件用 8000）；权限回传每 500 ms 轮询一次，最长等 590 秒，之后什么都不返回，让 agent 保留自己的询问。
- 读 stdin 超过 2 秒无数据或超过 1 MiB 就放弃。
- 只上报上面列出的归一化字段：会话、应用、事件、模型、工具名、短消息、子代理标记。prompt 与工具结果不会转发。权限请求会带上工具名和截断后的描述 —— 那是用户做判断必需的信息。
- 每条事件都带 source（等于 profile id），后续接入弱信号通道时，消费方能区分「hook 直报」和「推断」。
- 默认连本机。`AT_PET_URL` 可以指向别处，但 At-Pet 的 API 按设计不鉴权，应当只在本机使用。

## 测试

```powershell
cd integrations/agent-bridge
npm test
```

共 26 项：路径解析、profile 继承、事件映射、子代理处理、条件规则、对着本地假 API 的权限允许 / 拒绝、两种权限响应格式、各 profile 的权限策略、payload 抓取，以及「At-Pet 不在也不让进程失败」。

## 与现有插件的关系

`integrations/agent-plugins/*` 保持原样可用。把某一家迁过来只是改一行 hook 配置，不需要往对方软件里安装任何东西。

