# 仓库安全 CI

`Repository security` 在推送到 `beta` / `main`、发起目标为这两个分支的 PR、每周一以及手动触发时运行三个独立检查。无需提供扫描服务的私有令牌。

1. **Local security and CodeQL**：在 Windows 构建真实应用，运行 `http-api-guard`，验证仅监听本机、Host / Origin 限制、宠物 ID 路径校验和权限转发开关；使用 CodeQL 的 C/C++ `security-extended` 查询检查代码漏洞。SARIF 中的安全发现会使检查失败，并保留报告。
2. **Secrets (email permitted)**：使用固定版本 Gitleaks 默认规则扫描完整 Git 历史，发现凭据即失败。日志与报告脱敏。普通 E-mail 地址不属于凭据，不配置“含邮箱整行放行”，因此邮箱同一行的真实密钥仍会被检测。每次运行先用临时构造的假令牌验证这一约定。
3. **Malware signatures**：更新 ClamAV 病毒库，扫描当前提交的完整受跟踪文件快照，包括归档文件。感染、超出扫描限制或扫描错误都会失败；不会执行被扫描文件。

CodeQL 覆盖可静态识别的漏洞，ClamAV 覆盖已知恶意代码签名；通过检查不代表能证明不存在未知后门。扫描工具下载、病毒库更新和编译失败也会使对应检查失败，不会被当成“无问题”。

报告保留 14 天。完整 Git 历史检查可能发现旧提交中的凭据；应先撤销或轮换，再处理历史，不能只删除最新文件。要把这些检查设为 PR 必须通过的条件，需要在 GitHub 分支保护或规则集中选中上述三个检查。

## 检查速度

- API 实现编译为共用对象库，应用与 HTTP 回归测试共享；Qt UI 测试依赖也只编译一次，避免八个测试目标重复编译同一份界面实现。
- Windows 打包使用固定版本的 sccache 与 GitHub Actions 缓存；CodeQL 构建显式禁用编译缓存，保持所有生产源文件都在分析跟踪中重新编译。
- amd64 的全部回归测试并行度为 4；安全回归并行度为 2。各测试使用独立设置和临时目录。
- 相同分支的新推送或 PR 更新会取消过时的同分支任务；标签发布构建保持运行。
- CodeQL 继续使用完整 `security-extended` 查询；Gitleaks 继续扫描完整历史，ClamAV 继续扫描完整受跟踪快照及归档文件。

优化前基线（提交 `ca34b29`）：[amd64 构建](https://github.com/At-PetAdmin/At-Pet/actions/runs/43677101844)约 4 分 36 秒，其中编译 2 分 40 秒、回归 15 秒；[安全检查](https://github.com/At-PetAdmin/At-Pet/actions/runs/43677101853)约 5 分 30 秒。耗时会随缓存命中与 runner 负载变化，首次填充缓存不代表后续构建速度。

缓存配置参照 [sccache Action](https://github.com/Mozilla-Actions/sccache-action)，固定 Action 提交和 sccache 版本。缓存仅用于编译对象，不缓存签名结果或跳过回归、安全检查。

本地密钥预检（先安装 Gitleaks 8.30.1）：

```sh
python scripts/test-secret-scanner.py
gitleaks dir . --config .gitleaks.toml --redact
gitleaks git . --config .gitleaks.toml --redact --log-opts="--all"
```
