# At-Pet

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white&style=flat-square)](https://isocpp.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?logo=qt&logoColor=white&style=flat-square)](https://www.qt.io/)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-0078D6?logo=windows&logoColor=white&style=flat-square)](https://www.microsoft.com/windows)
[![CMake 3.21+](https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white&style=flat-square)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen?style=flat-square)](CONTRIBUTING_CN.md)

[English](README.md) | 简体中文 | [贡献指南](CONTRIBUTING_CN.md)

一个基于 C++17 和 Qt 6 的 Windows 桌面宠物应用。界面与核心逻辑基于 Qt 构建，便于扩展到 macOS 和 Linux；首个实现面向 Windows，Copilot 窗口跟踪目前使用 Windows API。

## 功能特性

- 透明、无边框、始终置顶的宠物窗口。拖动可移动；单击触发问候；双击输入想让宠物说的话；右键打开控制菜单。
- 内置矢量备用宠物，并支持自定义精灵表动画，覆盖 idle、walk、sleep、thinking、work、review、工具执行、完成、取消和错误等状态。
- 导入、预览、切换、删除和召唤宠物库中的每一只有效宠物。
- 监视已导入的 `pet.json` 及其所在文件夹，修改后无需重启即可重新加载配置和精灵图。
- 本地 HTTP API 与 Server-Sent Events（SSE），用于状态更新和外部 Agent 事件。初始支持的 AgentApp 来源：ZCode、GitHub Copilot、DeepSeek Harness、OpenCode、Antigravity 和 Command Code。
- Windows 专属选项：让宠物看向标题中包含 “Copilot” 的可见窗口。AgentApp 的生命周期状态通过 API 接收；这些应用不会自动被注入或监听。
- 托盘菜单支持显示/隐藏宠物、导入宠物、召唤宠物库中的全部宠物，以及在简体中文和英语之间切换。默认跟随系统语言，并记住您的选择。

## 构建（Windows / Qt MinGW）

需要 Qt 6.4+（Widgets、Network 和 LinguistTools）、CMake 3.21+、Ninja，以及与所安装 Qt 套件兼容的 MinGW 编译器。以 `E:\Qt\6.11.2` 下的 Qt 安装为例：

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="E:/Qt/6.11.2/mingw_64" `
  -DCMAKE_MAKE_PROGRAM="E:/Qt/Tools/Ninja/ninja.exe" `
  -DCMAKE_C_COMPILER="E:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="E:/Qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build
```

运行 `build\at-pet.exe` 时，需要把 Qt 套件的 `bin` 目录加入 `PATH`，或使用 `windeployqt` 部署 Qt 运行时文件。

在 macOS 和 Linux 上，使用对应平台的 Qt 6 套件和编译器即可；目前只有 Windows 平台的目标窗口跟踪依赖平台 API。

## 宠物库与格式

通过宠物右键菜单或托盘菜单中的 **导入配置与精灵表…**，可以分别选择 `pet.json` 和图片。应用会把两者复制为新的宠物库条目，重写精灵图文件名，并在名称重复时追加数字后缀。对于包含 `pet.json` 及其引用资源的完整文件夹，仍可使用 **导入宠物文件夹…**。导入的宠物可以预览 12 秒，或切换为主位置。“召唤全部宠物”会同时打开库中所有有效条目。

参考项目格式的 `pet.json` 示例：

```json
{
  "name": "Sample Cat",
  "sprite": "spritesheet.png",
  "cell": [128, 128],
  "scale": 1.5,
  "fps": 8,
  "defaultAnimation": "idle",
  "animations": {
    "idle": { "row": 0, "frames": 4 },
    "walk": { "row": 1, "frames": 4 },
    "thinking": { "row": 2, "frames": 4 },
    "tool": { "row": 3, "frames": 4 },
    "sleep": { "row": 4, "frames": 4 },
    "work": { "row": 5, "frames": 4 },
    "review": { "row": 6, "frames": 4 },
    "done": { "row": 7, "frames": 4 },
    "cancelled": { "row": 8, "frames": 4 },
    "error": { "row": 9, "frames": 4 }
  }
}
```

支持 `sprite`（也接受 `spriteSheet`）、`frameWidth`/`frameHeight` 或 `cell`、`scale`、`fps` 和 `defaultAnimation`。`cell` 可以是 `[width, height]` 数组，也可以是包含 `width` 和 `height` 的对象。对于参考风格的动画，`row` 从 0 开始，`frames` 表示该行上连续帧的数量。旧式包含首尾的 `start`/`end` 范围和显式帧索引数组也受支持；索引按行优先顺序从 0 开始。使用 `row_counts` 而不是 `animations` 的配置同样受支持：各行按顺序映射到 idle、walk、sleep、thinking、work、review、error、done、cancelled、tool 和 interact。JSON 文件、精灵表和宠物文件夹都会被监视是否有改动。如果没有可用的有效精灵图，内置矢量宠物仍然可以正常使用。

在 Windows 上，宠物库位于 `%APPDATA%\Ni-ShuWu\At-Pet\pets`。

## 本地 API

服务默认只绑定到 `127.0.0.1:8765`。可通过 `AT_PET_PORT` 选择其他端口。该 API 不进行身份验证，仅用于同一台机器上的可信程序；请勿将其暴露或转发到网络。

- `GET /health` — 健康检查。
- `GET /state` — 当前宠物和已知 Agent 会话。
- `GET /events` — SSE 流；连接时发送一次 JSON 状态快照，状态变化时再次发送。
- `POST /animation` — `{"name":"sleep"}`。
- `POST /say` — `{"text":"Hello!"}`。
- `POST /summon` — `{"all":true}` 或 `{"petId":"sample-cat"}`。
- `POST /event` — `{"sessionId":"session-1","agentApp":"github-copilot","event":"thinking","model":"...","message":"..."}`。支持的 `agentApp` 值：`zcode`、`github-copilot`、`deepseek-harness`、`opencode`、`antigravity` 和 `command-code`。规范化后的应用标签会随 `/state` 和 SSE 中每个会话一起返回；通用事件源可以省略该字段。

Agent `event` 值映射如下：`thinking`/`model_thinking` → thinking，`tool_start`/`tool_running` → tool，`tool_end` → thinking，`work` → work，`review`/`reflection` → review，`completed`/`done` → done，`cancelled`/`aborted` → cancelled，`error`/`failed` → error。多个 `sessionId` 会被独立跟踪；最近更新的会话会驱动宠物。终止状态动画会短暂显示，然后由下一个活动会话或空闲状态接管。发送 `idle` 或 `session_end` 可移除会话。

PowerShell 请求示例：

```powershell
Invoke-RestMethod http://127.0.0.1:8765/event -Method Post `
  -ContentType 'application/json' `
  -Body '{"sessionId":"session-1","event":"tool_start","message":"Running tests"}'
```

首个版本提供的是事件桥接，而不是直接读取 Copilot 会话内部状态。Copilot 扩展或其他本地集成可以向其 POST 生命周期事件。

## 参与贡献

欢迎参与贡献。构建方式、代码约定和提交流程请参阅 [CONTRIBUTING_CN.md](CONTRIBUTING_CN.md)（英文版：[CONTRIBUTING.md](CONTRIBUTING.md)）。

## 许可证

At-Pet 基于 [MIT License](LICENSE) 发布。
