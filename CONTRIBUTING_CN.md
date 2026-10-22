# 为 At-Pet 贡献代码

[English](CONTRIBUTING.md) | 简体中文

感谢您对 At-Pet 的关注！本指南介绍如何构建项目、遵循哪些约定，以及如何让您的改动顺利合并。

## 贡献方式

- 通过 [GitHub Issues](https://github.com/Ni-ShuWu/At-Pet/issues) 报告问题或提出功能建议。
- 改进宠物格式、动画和内置资源。
- 通过本地事件 API 新增或完善 AgentApp 集成。
- 翻译界面和文档（目前维护简体中文和英文）。
- 审阅 Pull Request，帮助其他用户。

## 开始之前

- 在提交新 issue 或 PR 之前，请先搜索已有的 issue 和 Pull Request。
- 对于较大的功能、API 变更或新增依赖，请先开 issue 讨论方案。
- 每个 Pull Request 只做一件事。无关的重构请拆分为单独的 PR。

## 环境要求

- Qt 6.4 或更高版本，包含 **Widgets**、**Network** 和 **LinguistTools** 组件。
- CMake 3.21 或更高版本。
- Ninja（推荐生成器）。
- 支持 C++17 的编译器。Windows 上请使用与您的 Qt 套件匹配的 MinGW 工具链；macOS 和 Linux 上使用 Qt 6 支持的任何编译器即可。

## 构建与运行

项目使用标准 CMake 工作流：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<Qt 套件路径>"
cmake --build build
```

以 Windows 上 `E:\Qt\6.11.2` 的 Qt 安装为例：

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="E:/Qt/6.11.2/mingw_64" `
  -DCMAKE_MAKE_PROGRAM="E:/Qt/Tools/Ninja/ninja.exe" `
  -DCMAKE_C_COMPILER="E:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="E:/Qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build
```

运行 `build\at-pet.exe` 时，需要把 Qt 套件的 `bin` 目录加入 `PATH`，或使用 `windeployqt` 部署运行时。macOS 和 Linux 使用对应平台的构建产物即可。

### 没有本地 Qt 环境时构建

`Build Windows amd64 package` workflow 会产出免安装 Qt 的 Windows x86-64 压缩包，因此不安装 Qt 也可以参与开发：

1. 推送分支并向 `main` 提交 Pull Request。该 workflow 会在每个 Pull Request 以及推送到 `main` 时运行。
2. 在 Pull Request 的 **Checks** 标签页中打开对应的运行记录。
3. 下载 `At-Pet-windows-amd64` 产物，解压后直接运行 `at-pet.exe`；Qt 与 MinGW 运行时已一并打包。

上传产物前，该任务会启动打包后的应用，并在临时端口上验证 `GET /health` 可正常响应。推送 `v*` 标签时，同一个压缩包及其 `.sha256` 校验文件会自动附加到对应的 GitHub Release。

项目目前没有自动化测试套件。提交 Pull Request 前，请手动验证您的改动。

## 手动验证清单

请执行与您的改动相关的检查：

- 项目可以构建，且没有新增警告或错误。
- 应用可以启动，托盘图标正常显示，宠物窗口渲染正确。
- 导入配置与精灵表、导入文件夹均正常，重名条目会追加数字后缀。
- 预览、**设为当前宠物**、**召唤全部宠物**和删除功能行为正确。
- 修改已导入的 `pet.json` 或其精灵表后，无需重启即可重新加载。
- 语言切换正常，所选语言在重启后仍然保留。
- 本地 API 在配置的端口上正常响应：
  - `GET /health`、`GET /state` 和 `GET /events`（SSE）。
  - `POST /animation`、`POST /say`、`POST /summon` 和 `POST /event`。
  - `AT_PET_PORT` 可以指定非默认端口。
- Windows 专属行为（包括 Copilot 视线跟踪选项）仍由 `Q_OS_WIN` 保护。

## 项目结构

| 路径 | 用途 |
| --- | --- |
| `app/main.cpp` | 应用入口、托盘菜单、宠物控制器、事件路由和共享状态。 |
| `app/petwindow.cpp`、`app/petwindow.h` | 宠物窗口渲染、动画播放、拖动和交互。 |
| `app/httpapi.cpp`、`app/httpapi.h` | 仅绑定回环地址的 HTTP 与 SSE 服务器。 |
| `app/settingsdialog.cpp`、`app/settingsdialog.h` | 设置与宠物库界面，包括预览。 |
| `translations/at-pet_zh_CN.ts` | 简体中文 Qt 翻译源文件。 |
| `assets/` | 内置资源。 |
| `dist/` | 维护者发布的发行包。请勿在此放置本地构建产物。 |

## 代码约定

- 使用 C++17 和 Qt 6（Widgets 与 Network）。除非先在 issue 中讨论并达成一致，否则不要新增第三方依赖。
- 与周围代码风格保持一致：四空格缩进、左大括号不换行、沿用现有命名方式。
- 所有面向用户的字符串都要用 `tr()` 包裹，并补充对应翻译。
- 保持平台无关代码的可移植性。Windows 专属代码使用 `Q_OS_WIN` 保护，并尽量与共享路径隔离。
- 本地 API 只能绑定回环地址。该 API 有意不进行身份验证，切勿绑定到公网接口或转发该端口。
- 不要提交构建产物、IDE 元数据或生成文件，参见 `.gitignore`。
- 在缺陷修复 PR 中，优先选择小而清晰的改动，避免大范围重构。

## 本地化

- 面向用户的字符串使用 Qt 翻译系统。在源码中用 `tr()` 添加新字符串，然后使用 Qt Linguist 或 `update_translations` CMake 目标更新 `translations/at-pet_zh_CN.ts`。
- `qt_add_translations` 会在构建时编译 `.ts` 文件，因此应用始终包含最新翻译。
- 文案应简洁，并与现有界面保持一致。
- 当行为或配置发生变化时，请同步更新 `README.md` 和 `README_CN.md`；如涉及贡献流程，也请同步更新 `CONTRIBUTING.md` 和 `CONTRIBUTING_CN.md`。

## 修改宠物格式或本地 API

- 尽可能保持向后兼容。解析器已支持多种旧格式（`spriteSheet`、`frameWidth`/`frameHeight`、`start`/`end` 范围、显式帧数组和 `row_counts`），请确保它们继续可用。
- 在两种语言的 README 中记录新增字段和接口。
- 更新热重载路径，确保修改 `pet.json` 后仍可无需重启生效。
- 将 `/event` 的映射关系视为外部集成依赖的公开契约。

## 提交信息与 Pull Request

请使用所遵循指南对应的语言提交贡献内容：阅读本指南时使用简体中文，阅读英文版指南时使用英文。

1. Fork 仓库，并从 `main` 创建主题分支。
2. 完成改动，并按上面的清单进行验证。
3. 使用祈使句编写提交摘要，例如 `Fix row-count animation mapping`。
4. 向 `main` 提交 Pull Request，说明**改了什么**、**为什么改**以及**如何测试**；如有相关 issue，请一并关联。
5. UI 和动画改动请附上截图或简短录屏。
6. 保持分支基于最新的 `main`，并及时回应审阅意见。

维护者在合并时可能会压缩或变基提交。欢迎使用 Draft Pull Request 获取早期反馈。

## 报告问题

请提供以下信息：

- 操作系统及版本。
- Qt 版本和编译器。
- 复现步骤、期望行为和实际行为。
- 相关的 `pet.json`（请隐去隐私信息），以及尽可能提供精灵表。
- 控制台输出或错误信息。

## 安全

请不要为安全漏洞创建公开 issue。请使用 [GitHub 私有安全公告](https://github.com/Ni-ShuWu/At-Pet/security/advisories/new) 或私下联系维护者。请注意，本地 API 有意不进行身份验证；关于将其暴露到不可信网络的报告仍然欢迎。

## 许可证

At-Pet 基于 [MIT License](LICENSE) 发布。提交贡献即表示您同意您的贡献以相同条款授权。
