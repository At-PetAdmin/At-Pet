# At-Pet

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white&style=flat-square)](https://isocpp.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?logo=qt&logoColor=white&style=flat-square)](https://www.qt.io/)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-0078D6?logo=windows&logoColor=white&style=flat-square)](https://www.microsoft.com/windows)
[![CMake 3.21+](https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white&style=flat-square)](https://cmake.org/)

English | [简体中文](README_CN.md)

A C++17 desktop companion for Windows, built on Qt 6 so the UI and core can be extended to macOS and Linux. The first implementation targets Windows; Copilot-window tracking currently uses the Windows API.

## Features

- Transparent, frameless, always-on-top pet windows. Drag to move; click for a greeting; double-click to enter text for the pet to say; right-click for controls.
- Built-in animated fallback pet and custom sprite-sheet animations for idle, walk, sleep, thinking, work, review, tool execution, completion, cancellation, and errors.
- Import, preview, switch, delete, and summon every valid pet in the library.
- Watches imported `pet.json` and its folder for changes and reloads configuration/sprites without restarting.
- Local HTTP API and Server-Sent Events (SSE) for state updates and external agent events. Initial AgentApp sources: ZCode, GitHub Copilot, DeepSeek Harness, OpenCode, Antigravity, and Command Code.
- Windows-only option to look toward a visible window whose title contains “Copilot”. AgentApp lifecycle state is received through the API; these apps are not automatically instrumented.
- System-tray controls for showing/hiding pets, importing pets, summoning the whole library, and switching between Simplified Chinese and English. The default follows the system locale; your selection is remembered.（托盘菜单支持简体中文/英语切换，默认跟随系统语言并记住选择。）

## Build (Windows / Qt MinGW)

Requires Qt 6.4+ (Widgets and Network), CMake 3.21+, Ninja, and a MinGW compiler compatible with the installed Qt kit. For the Qt installation in `E:\Qt\6.11.2`:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="E:/Qt/6.11.2/mingw_64" `
  -DCMAKE_MAKE_PROGRAM="E:/Qt/Tools/Ninja/ninja.exe" `
  -DCMAKE_C_COMPILER="E:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="E:/Qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build
```

Run `build\at-pet.exe` with the Qt kit's `bin` directory on `PATH`, or deploy Qt runtime files with `windeployqt`.

## Pet library and format

Use **Import config + spritesheet…** from the pet’s right-click menu or tray menu to select a `pet.json` and image separately. The app copies both into a new library entry, rewrites the sprite filename, and adds a numeric suffix for duplicate names. **Import pet folder…** remains available for complete folders containing `pet.json` and its referenced assets. Imported pets can be previewed for 12 seconds or switched into the main position. “Summon all pets” opens every valid library entry at once.

Example `pet.json` in the reference project's format:

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

`sprite` (also `spriteSheet`), `frameWidth`/`frameHeight` or `cell`, `scale`, `fps`, and `defaultAnimation` are supported. `cell` may be a `[width, height]` pair or an object with `width` and `height`. For reference-style animations, `row` is zero-based and `frames` is the number of consecutive frames on that row. Legacy inclusive `start`/`end` ranges and explicit frame-index arrays are also accepted; indices are zero-based in row-major order. Configurations that provide `row_counts` instead of `animations` are supported too: rows map in order to idle, walk, sleep, thinking, work, review, error, done, cancelled, tool, and interact. The JSON file, sprite sheet, and pet folder are watched for edits. If no valid sprite is available, the built-in vector pet remains usable.

On Windows, the library is stored under `%APPDATA%\Ni-ShuWu\At-Pet\pets`.

## Local API

The service binds only to `127.0.0.1:8765` by default. Set `AT_PET_PORT` to choose another port. The API is unauthenticated and intended only for trusted programs on the same machine; do not expose or forward this port to a network.

- `GET /health` — health check.
- `GET /state` — current pets and known agent sessions.
- `GET /events` — SSE stream; sends a JSON state snapshot on connect and whenever state changes.
- `POST /animation` — `{"name":"sleep"}`.
- `POST /say` — `{"text":"Hello!"}`.
- `POST /summon` — `{"all":true}` or `{"petId":"sample-cat"}`.
- `POST /event` — `{"sessionId":"session-1","agentApp":"github-copilot","event":"thinking","model":"...","message":"..."}`. Supported `agentApp` values: `zcode`, `github-copilot`, `deepseek-harness`, `opencode`, `antigravity`, and `command-code`. The normalized app label is included with each session in `/state` and SSE; the field is optional for generic event sources.

Agent `event` values map as follows: `thinking`/`model_thinking` → thinking, `tool_start`/`tool_running` → tool, `tool_end` → thinking, `work` → work, `review`/`reflection` → review, `completed`/`done` → done, `cancelled`/`aborted` → cancelled, and `error`/`failed` → error. Multiple `sessionId` values are tracked independently; the most recently updated session drives the pets. Terminal animations are shown briefly before the next active session or idle state takes over. Send `idle` or `session_end` to remove a session.

Example PowerShell request:

```powershell
Invoke-RestMethod http://127.0.0.1:8765/event -Method Post `
  -ContentType 'application/json' `
  -Body '{"sessionId":"session-1","event":"tool_start","message":"Running tests"}'
```

The first release provides the event bridge rather than reading Copilot session internals directly. A Copilot extension or other local integration can POST lifecycle events to it.

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, coding conventions, and the pull-request process. The Chinese guide is available at [CONTRIBUTING_CN.md](CONTRIBUTING_CN.md).

## License

At-Pet is released under the [MIT License](LICENSE).
