# Contributing to At-Pet

English | [简体中文](CONTRIBUTING_CN.md)

Thanks for your interest in At-Pet! This guide covers how to build the project, the conventions we follow, and how to get a change merged.

## Ways to contribute

- Report bugs and propose features through [GitHub Issues](https://github.com/Ni-ShuWu/At-Pet/issues).
- Improve the pet format, animations, and built-in assets.
- Add or refine AgentApp integrations through the local event API.
- Translate the UI and documentation (Simplified Chinese and English are currently maintained).
- Review pull requests and help other users.

## Before you start

- Search existing issues and pull requests before opening a new one.
- For large features, API changes, or new dependencies, open an issue first so the approach can be discussed.
- Keep each pull request focused on one change. Split unrelated refactors into separate PRs.

## Prerequisites

- Qt 6.4 or newer with the **Widgets**, **Network**, and **LinguistTools** components.
- CMake 3.21 or newer.
- Ninja (recommended generator).
- A C++17 compiler. On Windows, use the MinGW toolchain that matches your Qt kit. On macOS and Linux, use any compiler supported by your Qt 6 installation.

## Build and run

The project uses the standard CMake workflow:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<path-to-qt-kit>"
cmake --build build
```

For the Qt installation at `E:\Qt\6.11.2` on Windows:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="E:/Qt/6.11.2/mingw_64" `
  -DCMAKE_MAKE_PROGRAM="E:/Qt/Tools/Ninja/ninja.exe" `
  -DCMAKE_C_COMPILER="E:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="E:/Qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build
```

Run `build\at-pet.exe` with the Qt kit's `bin` directory on `PATH`, or deploy the runtime with `windeployqt`.

### Building without a local Qt installation

The `Build Windows amd64 package` workflow produces a self-contained Windows x86-64 package, so you can work on the code without installing Qt:

1. Push your branch and open a pull request against `main`. The workflow runs on every pull request and on pushes to `main`.
2. Open the run from the **Checks** tab of your pull request.
3. Download the `At-Pet-windows-amd64` artifact, unzip it, and start `at-pet.exe`. Qt and the MinGW runtime are bundled.

Before uploading, the job starts the packaged app and verifies that `GET /health` answers on a temporary port. When a `v*` tag is pushed, the same package and its `.sha256` file are attached to that GitHub release.

The project currently has no automated test suite. Please verify your change manually before opening a pull request.

## Manual verification checklist

Run the checks that apply to your change:

- The project builds without new warnings or errors.
- The app starts, the tray icon appears, and the pet window renders correctly.
- Import config + spritesheet works, folder import works, and duplicate names get a numeric suffix.
- Preview, **Set as Current Pet**, **Summon all pets**, and delete behave correctly.
- Editing an imported `pet.json` or its sprite sheet reloads without restarting.
- Language switching and the selected language persist across restarts.
- The local API responds on the configured port:
  - `GET /health`, `GET /state`, and `GET /events` (SSE).
  - `POST /animation`, `POST /say`, `POST /summon`, and `POST /event`.
  - `AT_PET_PORT` selects a non-default port.
- Windows-only behavior (including the Copilot look-at option) is still guarded by `Q_OS_WIN`.

## Project layout

| Path | Purpose |
| --- | --- |
| `app/main.cpp` | Application entry point, tray menu, pet controller, event routing, and shared state. |
| `app/petwindow.cpp`, `app/petwindow.h` | Pet window rendering, animation playback, dragging, and interaction. |
| `app/httpapi.cpp`, `app/httpapi.h` | Loopback HTTP and SSE server. |
| `app/settingsdialog.cpp`, `app/settingsdialog.h` | Settings and pet-library UI, including preview. |
| `translations/at-pet_zh_CN.ts` | Simplified Chinese Qt translation source. |
| `assets/` | Built-in assets. |
| `dist/` | Maintainer-published release archives. Do not add local build output here. |

## Coding guidelines

- Use C++17 and Qt 6 (Widgets and Network). Avoid adding third-party dependencies unless they are discussed and agreed in an issue first.
- Match the surrounding style: four-space indentation, braces on the same line, and the existing naming conventions.
- Wrap every user-visible string in `tr()` and add the corresponding translation.
- Keep platform-independent code portable. Guard Windows-specific code with `Q_OS_WIN` and keep it out of shared paths where possible.
- Keep the local API bound to loopback only. The API is intentionally unauthenticated, so never bind it to a public interface or forward the port.
- Do not commit build output, IDE metadata, or generated files. See `.gitignore`.
- Prefer small, readable changes over broad refactors in a bug-fix PR.

## Localization

- User-facing strings use Qt's translation system. Add new strings with `tr()` in the source, then update `translations/at-pet_zh_CN.ts` with Qt Linguist or the `update_translations` CMake target.
- `qt_add_translations` compiles the `.ts` files at build time, so the app always ships the current translations.
- Keep wording concise and consistent with the existing UI.
- When behavior or configuration changes, update both `README.md` and `README_CN.md`, and both `CONTRIBUTING.md` and `CONTRIBUTING_CN.md` where relevant.

## Changing the pet format or local API

- Preserve backward compatibility when possible. The parser already accepts several legacy forms (`spriteSheet`, `frameWidth`/`frameHeight`, `start`/`end` ranges, explicit frame arrays, and `row_counts`); keep those working.
- Document new fields and endpoints in both READMEs.
- Update the hot-reload path so `pet.json` changes continue to apply without a restart.
- Treat `/event` mappings as a public contract used by external integrations.

## Commit messages and pull requests

Please submit contribution text in the language of the guide you are following: English for this guide and Simplified Chinese for the Chinese guide.

1. Fork the repository and create a topic branch from `main`.
2. Make your change and verify it using the checklist above.
3. Write an imperative commit summary, for example `Fix row-count animation mapping`.
4. Open a pull request against `main` that explains **what** changed, **why**, and **how you tested it**. Link the related issue when one exists.
5. Include a screenshot or short recording for UI and animation changes.
6. Keep the branch rebased on `main` and respond to review feedback.

Maintainers may squash or rebase commits when merging. Draft pull requests are welcome for early feedback.

## Reporting bugs

Please include:

- Operating system and version.
- Qt version and compiler.
- Steps to reproduce, expected behavior, and actual behavior.
- The relevant `pet.json` (redact anything private) and, if possible, the sprite sheet.
- Any console output or error messages.

## Security

Do not open a public issue for a security vulnerability. Use [GitHub private security advisories](https://github.com/Ni-ShuWu/At-Pet/security/advisories/new) or contact the maintainers privately. Note that the local API is unauthenticated by design; reports about exposing it to an untrusted network are still welcome.

## License

At-Pet is released under the [MIT License](LICENSE). By submitting a contribution, you agree that it is licensed under the same terms.
