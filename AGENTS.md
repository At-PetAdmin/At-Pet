# AGENTS.md

给 AI 编码代理的本仓库约定。

## 分支与发布

- **beta 阶段的改动一律合进 `beta` 分支**——分支名就叫 `beta`，不加任何后缀（不是 `beta1`、`v1.3.0-beta`、`release/beta`）。
- beta 阶段**不要**直接往 `main` 合。
- **只有用户指名道姓要求发布正式版**时，才把 `beta` 上的改动合进 `main`。
- 发版（包括 `v1.3.0beta1` 这类预发布）在 `beta` 分支上进行。

## Branching & releases

- Beta work merges into the **`beta`** branch — literally `beta`, with no suffix (`beta1`, `v1.3.0-beta`, `release/beta` are all wrong).
- Do **not** merge into `main` while in beta.
- Merge `beta` into `main` **only when the user explicitly asks for a formal release**.
- Cut releases (including `v1.3.0beta1`) on `beta`.
