# Agent 适配器架构

`agentmonitor.h` 定义共享的 `AgentMonitorStrategy` 适配接口。每种需要解析原生协议的集成都放在独立目录中实现：

- `claude-code/`：解析 Claude Code HTTP hooks，并处理原生权限响应。
- `opencode-v2/`：归一化 OpenCode V2 插件事件。

`agentmonitor.cpp` 只根据已注册的监控类型选择策略；HTTP API 负责传输与路由，`agentregistry.h` 声明工具标识、别名和监控类型。若集成已直接发送 At-Pet 规范化的 `/event` payload，则不需要原生适配器；例如 Copilot CLI hook 发送端位于 `integrations/copilot-cli/`。

新增原生适配器时，在对应工具目录实现 `AgentMonitorStrategy`，再将监控类型加入 `agentregistry.h`、在 `agentmonitor.cpp` 注册工厂，并把源文件加入 `CMakeLists.txt`。
