# At-Pet

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white&style=flat-square)](https://isocpp.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?logo=qt&logoColor=white&style=flat-square)](https://www.qt.io/)
[![Platform: Windows 10/11](https://img.shields.io/badge/Platform-Windows_10%2F11-0078D6?logo=windows&logoColor=white&style=flat-square)](https://www.microsoft.com/windows)
[![CMake 3.21+](https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white&style=flat-square)](https://cmake.org/)

English | [简体中文](README_CN.md)

A C++17 desktop companion for Windows 10/11 on amd64 (x86-64) and ARM64, built on Qt 6. Windows is the only supported platform; Linux and macOS are outside this project's scope.

## Features

- Transparent, frameless, always-on-top pet windows. Drag to move; click for a greeting; double-click to enter text for the pet to say; right-click for controls. Pets can freely wander up, down, left, and right within the screen; toggle this behavior from the context menu (enabled by default and remembered). Optional gravity makes a pet fall after being dragged, carrying the momentum of the drag when it is flung; with gravity on, a pet only wanders along the floor and falls back down whenever it is lifted. A flung pet bounces off the screen edges instead of stopping dead, and it springs back up off the floor like a basketball, squashing on impact and losing height with each bounce until it settles; **Gravity strength** in General Settings scales how fast it falls (1.00 by default). Dragging, flinging, and gravity all keep the pet fully on screen.
- Bundled Codex Pets sprite pet as the built-in default, plus custom sprite-sheet animations for idle, walk, sleep, thinking, work, review, tool execution, permission requests, completion, cancellation, and errors.
- Import, preview, switch, delete, and summon every valid pet in the library; optionally show a separate pet for each detected subagent session.
- Watches imported `pet.json` and its folder for changes and reloads configuration/sprites without restarting.
- Local HTTP API and Server-Sent Events (SSE) for state updates and agent events. Native adapters are provided for Claude Code HTTP hooks and the OpenCode V2 plugin; MCP-compatible clients can use the optional stdio bridge for explicit event reporting, and other sources can use the generic HTTP API.
- Agent Monitoring settings show each source's integration strategy and can filter animation events. At-Pet does not scan agent processes or read machine-specific log paths. Native integrations must be installed in the agent application.
- Optional completion chime (muted via **Play a sound when an Agent task completes** in General Settings). Permission prompts visually highlight potentially dangerous actions as a heuristic; the warning does not change the Allow once / Deny choices. Agents can also ask the user a question through the local HTTP API.
- Windows-only option to make the pet's eyes follow the mouse pointer (**Look at the mouse cursor** in the right-click menu, or **Eyes follow the mouse cursor (Windows)** in General Settings). This only changes the pet's gaze; it does not detect or synchronize Copilot session status.
- Perch (widget) mode: drop a pet on the title bar of another window — VS Code, the Copilot app, Explorer, and similar framed windows — and it walks onto the edge and sits there. A perched pet rides along when you move or resize the host window, can slide down the frame on its own or via **Slide down the window frame**, and hops off when you drag it away.
- Do not disturb: when a full-screen game or editor is in the foreground and **Do not disturb automatically in full-screen apps (shrink to the screen edge)** is enabled, every pet shrinks into a small bubble that hugs the screen edge. Summon it again from the tray icon (or untick **Do not disturb**), and it returns to where it was. The tray menu also offers the mode manually.
- Poking a pet (single click) shows a random line, read from `quotes`/`pokeQuotes` in `pet.json`; without them the built-in greeting is used.
- Animation groups: `animationGroups.rest` (for example `["idle", "sleep"]`) periodically switches a resting pet between those animations instead of always showing `idle`.
- **Edit pet.json…** in the pet library tab opens the selected pet's configuration in a text editor, validates the JSON before saving, and hot-reloads the pet immediately.
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

### Build artifacts from CI

The **Build Windows amd64 package** workflow builds a self-contained Windows x86-64 package on every pull request and every push to `main`, so contributors without a Qt installation can still test their changes. Download `At-Pet-windows-amd64` from the run's **Artifacts** section, unzip it, and start `at-pet.exe`. Pushing a `v*` tag attaches the package and its `.sha256` checksum to that GitHub release. The Windows ARM64 workflow can be run manually for testing and publishes a package when triggered by a `v*` tag. Release archives and SHA-256 checksum files are also checked in under `dist/` for tagged releases.

### Windows version metadata and code signing

`assets/branding/app-icon.rc.in` embeds the window icon and a version resource, so `at-pet.exe` reports `At-Pet`, `At-PetAdmin`, and the CMake project version (`PROJECT_VERSION`) under **Properties > Details** and through the version API. Windows still labels unsigned downloads as **Unknown publisher**; only an Authenticode signature changes that. Both Windows workflows sign `at-pet.exe` when these repository secrets are configured and skip the step when they are not:

| Secret | Value |
| --- | --- |
| `WINDOWS_CERT_PFX_BASE64` | Base64 of the code-signing `.pfx` file, for example `[Convert]::ToBase64String([IO.File]::ReadAllBytes('at-pet.pfx'))` |
| `WINDOWS_CERT_PASSWORD` | Password for the `.pfx` file |

Local builds and pull requests from forks without the secrets stay unsigned, so they keep working for contributors.

## Pet library and format

Use **Import config + spritesheet…** from the pet’s right-click menu or tray menu to select a `pet.json` and image separately. The app copies both into a new library entry, rewrites the sprite filename, and adds a numeric suffix for duplicate names. **Import pet folder…** remains available for complete folders containing `pet.json` and its referenced assets. Imported pets can be previewed for 12 seconds or switched into the main position. “Summon all pets” opens every valid library entry at once. **Edit pet.json…** opens the selected library entry's configuration in a text editor; saving validates the JSON and hot-reloads the pet without restarting.
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
  },
  "animationGroups": {
    "rest": ["idle", "sleep"]
  },
  "quotes": [
    "Need a hand with that refactor?",
    "Remember to stretch!"
  ]
}
```

`sprite` (also `spriteSheet`), `frameWidth`/`frameHeight` or `cell`, `scale`, global default `fps`, and `defaultAnimation` are supported. Each `animations` entry may also define its own `fps` (1–30), falling back to the global `fps` when omitted. The Settings window lets you tune each animation for each pet in its preview; these preferences are saved in application settings without modifying imported `pet.json` files. Changing the rate updates matching pets immediately when that animation is playing; otherwise, the new rate takes effect the next time it plays.
 For distinct horizontal walking animations, optionally define `walkLeft` and/or `walkRight` (also accepted: `walk_left`/`walk_right` or `walk-left`/`walk-right`); At-Pet selects the direction-specific animation while moving and falls back to `walk` when it is missing. `cell` may be a `[width, height]` pair or an object with `width` and `height`. For reference-style animations, `row` is zero-based and `frames` is the number of consecutive frames on that row. Legacy inclusive `start`/`end` ranges and explicit frame-index arrays are also accepted; indices are zero-based in row-major order.  Configurations that provide `row_counts` instead of `animations` are supported too: rows map in order to idle, walk, sleep, thinking, work, review, error, done, cancelled, tool, and interact. `animationGroups` names reusable sets of animations; the `rest` group (a comma-separated string, an array, or `[{"name": ...}]`) makes an idle pet switch to one of those animations every 8–20 seconds and hold it for 3.5–9 seconds, so a "resting" pet alternates between e.g. `idle` and `sleep` instead of freezing on one pose. Entries that do not exist in `animations` are ignored. `quotes` (also `pokeQuotes`) adds random lines — a string, an array of strings, or objects with a `text` field — shown when the pet is poked. The JSON file, sprite sheet, and pet folder are watched for edits. The built-in default is embedded from `assets/default`, so it needs no import; if that config or sheet is unavailable, the built-in vector pet is used as a fallback.On Windows, the library is stored under `%APPDATA%\Ni-ShuWu\At-Pet\pets`.

 ### Quote pack

 Every pet can share one quote pack: a standalone JSON file at `%APPDATA%\Ni-ShuWu\At-Pet\quotes.json` (`QStandardPaths::AppDataLocation/quotes.json`). It is never written into `pet.json`, so switching, updating or re-importing pets keeps it intact.

 Complete schema — all five categories, both supported languages:

 ```json
 {
   "zh_CN": {
     "poke": ["别戳我啦～", "痒痒的！"],
     "done": ["完工啦，检查一下？", "这单收工！"],
     "error": ["出错了，我来看看日志。", "别慌，报错而已。"],
     "cancel": ["已取消，随时再叫我。", "好，这次先不做。"],
     "idle": ["要不要休息一下？", "我在这儿陪你。"]
   },
   "en": {
     "poke": ["Hey, that tickles!"],
     "done": ["All done! Want to review?"],
     "error": ["Something broke — check the logs?"],
     "cancel": ["Cancelled. Ping me anytime."],
     "idle": ["Take a short break?", "Right here with you."]
   }
 }
 ```

 * **Categories**: `poke`, `done`, `error`, `cancel`, `idle`. Accepted aliases: `poked`/`interact`/`click` → poke, `completed`/`completion`/`finished` → done, `failed`/`failure` → error, `cancelled`/`canceled`/`aborted` → cancel, `waiting`/`random` → idle.
 * **Entry formats**: a category may be a single string, an array of strings, or an array of objects carrying a `text` field. Blank lines and non-string entries are ignored; a file with no usable line at all is rejected.
 * **Languages**: keys are normalised for case and `-`/`_` (`zh-CN` and `zh_CN` are the same). Resolution order is exact interface language → base language (`zh_CN` → `zh`, `en_US` → `en`) → `en` → the first language in the file that has that category.
 * **Importing**: **Settings → General Settings → Quote Pack → Import quotes.json…**. The candidate file is validated first; an invalid pack shows a specific error and never overwrites the installed one. A valid pack is copied atomically to the path above and every running pet picks it up immediately.
 * **Triggers**: poking the pet, terminal agent events (`done`, `error`, `cancelled`), and one idle line every 2–5 minutes while no agent session is active, the pet is visible and it is not already speaking. The same idle line is not repeated back to back when the pack offers alternatives, and a language switch is honoured on the next line.
 * **Missing animations**: a pet whose `animations` table lacks the requested entry (for example no `done` row) skips that animation and still speaks — the last frame stays on screen instead of turning blank.

 A bilingual sample lives at `assets/default/quotes.example.json`; it is not loaded automatically. With no pack installed every behaviour above falls back to the pet's own `quotes` from `pet.json` and then to the built-in prompt.

 ## Local API

The service binds only to `127.0.0.1:8765` by default. Set `AT_PET_PORT` to choose another port. The API is unauthenticated and intended only for trusted programs on the same machine; do not expose or forward this port to a network.

- `GET /health` — health check.
- `GET /state` — current pets and known agent sessions; `agentSources` lists each Agent key, monitor type, and native endpoint when available.
- `GET /events` — SSE stream; sends a JSON state snapshot on connect and whenever state changes.
- `POST /animation` — `{"name":"sleep"}`.
- `POST /say` — `{"text":"Hello!"}`.
- `POST /summon` — `{"all":true}` or `{"petId":"sample-cat"}`.
- `POST /event` — `{"sessionId":"session-1","agentApp":"github-copilot","event":"thinking","model":"...","message":"...","isSubagent":false,"parentSessionId":"parent-1"}`. Recognized `agentApp` labels are `github-copilot`, `zcode`, `deepseek-harness`, `opencode`, `antigravity`, `command-code`, `claude-code`, `openai-codex`, `gemini-cli`, `cursor`, `windsurf`, `workbuddy`, and `penguin-harness`; unknown labels are also accepted for generic sources. Agent events are not collected automatically: an integration must POST them to this local API.
- `POST /permission` — `{"sessionId":"session-1","agentApp":"claude-code","toolName":"Bash","description":"Run tests"}` to show a permission prompt. The response includes a `requestId`.
- `GET /permission/{requestId}` — poll for `pending`, `allowed_once`, or `denied`. The first poll after a decision consumes that result; later polls return 404.
- `DELETE /permission/{requestId}` — cancel a generic pending permission request and dismiss its prompt; Claude HTTP hook requests cannot be cancelled this way.
- `POST /question` — open a question dialog: `{"agentApp":"my-agent","sessionId":"session-1","question":"Which option should I use?","choices":["A","B"]}`. `agentApp` and `question` are required; `header` and `choices` are optional, and the user can type a custom answer.
- `GET /question/{requestId}` — poll for `pending`, `answered` (includes `answer`), or `cancelled`; the first poll after a decision consumes the result.
- `DELETE /question/{requestId}` — cancel the request and dismiss its dialog.
- `POST /agent-event/claudecode` — accepts Claude Code HTTP hook payloads, including `PermissionRequest`; the permission hook stays open until the user chooses Allow once or Deny.
- `POST /agent-event/opencode` — accepts events normalized by the included OpenCode V2 plugin.

Agent `event` values map as follows: `thinking`/`model_thinking` → thinking, `tool_start`/`tool_running` → tool, `tool_end` → thinking, permission or approval requests → permission, `work` → work, `review`/`reflection` → review, `completed`/`done` → done, `cancelled`/`aborted` → cancelled, and `error`/`failed` → error. Multiple `sessionId` values are tracked independently; the most recently updated non-subagent session drives the main pets. Set `isSubagent` or `parentSessionId` to mark child sessions; when enabled in Agent Monitoring settings, each gets a separate pet. Terminal animations are shown briefly before the next active session or idle state takes over. Send `idle` or `session_end` to remove a session.

Permission prompts offer only **Allow once** or **Deny**; closing the dialog also denies. Potentially dangerous actions receive a visual warning based on heuristic matching; it is advisory only and does not alter the decision options. Pending requests do not time out or auto-deny. Generic integrations poll the result and enforce it for that tool request. Claude's HTTP hook receives a native allow/deny response after the prompt; the OpenCode plugin waits for the same decision and maps it to OpenCode's permission effect. At-Pet never executes tools, and the Agent Monitoring animation filters do not affect permission prompts.

### Native integrations

**Claude Code:** Use a Claude Code version that supports HTTP hooks. Add the following entries to your Claude Code settings and restart Claude Code. The same endpoint accepts each hook's native payload:

```json
{
  "hooks": {
    "UserPromptSubmit": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PreToolUse": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PostToolUse": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PostToolUseFailure": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "PermissionRequest": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "Stop": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "SessionEnd": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "SubagentStart": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }],
    "SubagentStop": [{ "hooks": [{ "type": "http", "url": "http://127.0.0.1:8765/agent-event/claudecode", "timeout": 600 }] }]
  }
}
```

**OpenCode V2:** copy [`integrations/opencode-v2/at-pet.ts`](integrations/opencode-v2/at-pet.ts) into the project's `.opencode/plugins/` folder or `%USERPROFILE%\.config\opencode\plugins\` for a global install, then restart OpenCode. Set `AT_PET_URL` only if At-Pet uses a non-default local port. The plugin forwards session/tool events and sends `ask` permission decisions to At-Pet; unavailable At-Pet requests leave OpenCode's existing `ask` decision unchanged. If the plugin is unloaded while a prompt is open, it cancels the pending At-Pet request and closes the prompt without changing OpenCode's `ask` decision.

**Optional MCP server:** MCP-compatible clients can use the local stdio bridge described in [`integrations/at-pet-mcp/README.md`](integrations/at-pet-mcp/README.md). Its tools report events only when explicitly called; unlike hooks and plugins, MCP does not automatically observe lifecycle events.

For an additional agent integration tutorial, see the [At-Pet Agent integration plugin](https://github.com/Ni-ShuWu/At-Pet_Agent-plugin).

Example PowerShell request:

```powershell
Invoke-RestMethod http://127.0.0.1:8765/event -Method Post `
  -ContentType 'application/json' `
  -Body '{"sessionId":"session-1","event":"tool_start","message":"Running tests"}'
```

At-Pet provides a local event bridge rather than reading agent session internals directly. A compatible extension, hook, or other trusted local integration can POST lifecycle events and permission requests to it.

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, coding conventions, and the pull-request process. The Chinese guide is available at [CONTRIBUTING_CN.md](CONTRIBUTING_CN.md).

## License

At-Pet is released under the [MIT License](LICENSE).
