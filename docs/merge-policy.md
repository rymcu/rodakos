# PR 检查与合并纪律

> 2026-10-07 核验：`main` 的 `protected` 为 `false`，required status checks 为空，
> 仓库 ruleset 列表为空。以下是维护者执行的合并纪律，不表示 GitHub 已强制保护分支。

## 必须核验的检查

所有面向 `main` 的 PR，包括文档变更和 draft PR，都由现有 `pull_request` 工作流运行：

| 工作流 | PR 中的检查名称 | 验证范围 |
| --- | --- | --- |
| RodakOS host checks | `Linux host tests (ASan, UBSan)` | 锁定组件来源、主机服务/UI 回归及 sanitizer |
| RodakOS host checks | `PowerShell syntax` | 已跟踪 `.ps1` 文件的语法，不执行脚本 |
| RodakOS firmware build | `ESP-IDF 6.0.2 development package` | 主应用/Recovery 构建、依赖图及两个临时开发签名包 |

检查名称来自 [host workflow](../.github/workflows/host-checks.yml) 和
[firmware workflow](../.github/workflows/firmware-build.yml) 的 job `name`。变更名称时同步
更新本表，并核对管理员以后可能配置的 required checks。这里只记录现状，不修改仓库保护。

## 合并前

1. 作者说明最终变更、验证结果和仍开放的硬件/发布门禁；维护者审查 diff、未解决反馈及证据。
   Draft PR 保持待审状态，准备合并时再由维护者转为 ready。
2. 在 PR 的 Checks 页确认表中三个检查全部为 **success**。Pending、failure、cancelled、
   skipped、缺失检查和没有 required checks 都不能视为通过。文档 PR 也等待相同检查。
3. 核对检查属于当前 PR 候选及当前目标基线。新增提交后重新等待检查；如果 `main` 已变更，
   更新 PR 分支并重新运行检查，不把旧基线的绿灯当作最新合并结果。
4. PR 的 `pull_request` workflow 默认 checkout GitHub 的测试合并提交；`github.sha`、
   artifact 名称和证据文件中的源码 SHA 应对应这个 checkout。另行记录 PR head/base 和
   Actions API 的 `headSha`，不能无条件把它们当作同一个 SHA。比较测试合并提交的父提交
   与当前 PR head/base，确认本次检查覆盖当前候选，不能把旧 `main` push run 当作 PR 检查。
5. 核对 artifact 中的源码/工具身份与 run。Host 日志、固件 `identity.json` /
   `result.json` 及验包方法见 [host CI](host-ci.md) 和 [firmware CI](firmware-ci.md)。
   日志工件默认保留 14 天；无法取得必需证据时重新运行，不能据已过期链接宣称已复核。
6. 以上成立后，由有权限的维护者执行合并。不得利用当前未保护分支的状态绕过失败检查或
   直接推送尚未通过上述审查的工作。本文不授权自动合并。

只读核验示例：

```powershell
gh pr checks <PR编号> --repo rymcu/rodakos
gh pr view <PR编号> --repo rymcu/rodakos --json headRefOid,baseRefOid,statusCheckRollup
gh api repos/rymcu/rodakos/git/ref/pull/<PR编号>/merge
gh run view <运行编号> --repo rymcu/rodakos --json event,headSha,status,conclusion,url
gh api repos/rymcu/rodakos/branches/main
gh api repos/rymcu/rodakos/rulesets
```

`gh pr checks --required` 在当前未配置 required checks 时不能替代完整检查表。分支保护
和 ruleset 是独立的管理员配置；若以后启用，应选择上表三个检查，保留最新基线验证，
再用实际 PR 确认强制效果。管理员权限本身不等于这些配置已经启用。

## 证据边界

PR 绿灯证明该候选的软件检查与开发包校验通过。CI 使用 runner 临时信任根，不使用
设备已安装的 Recovery 根或生产密钥；两个 CI 包的 immutable 一致性只覆盖该次 run。
不能把 CI 包直接替换为设备兼容包或生产发布包。

这些检查不访问 COM/NVS，也不替代真人唤醒、触屏、声学、物理断电、资源压力或长稳。
对应事项继续按 [发布验收](ota-release-readiness.md) 和 [路线图](roadmap.md) 管理。
[#26](https://github.com/rymcu/rodakos/issues/26) 的实际 PR 检查证据应记录 PR、当前 head/base、
测试合并提交及两个 workflow run；不能仅凭本文件宣称 PR 展示已经验证。
